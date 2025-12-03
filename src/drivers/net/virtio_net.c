/* src/drivers/net/virtio_net.c - Virtio Network Driver (Legacy) */
#include "virtio_net.h"
#include "../../arch/x86/idt.h"
#include "../../arch/x86/io.h"
#include "../../mm/kheap.h"
#include "../../net/core/netdev.h"
#include "../../net/l2/ethernet.h"
#include "../../net/netlog.h"

/* Global instance */
static VirtIONetDevice *g_virtio_dev = NULL;
static NetInterface *g_virtio_netif = NULL;

/* ============================================ */
/*           Helper Functions                   */
/* ============================================ */

static uint8_t virtio_read8(VirtIONetDevice *dev, uint16_t offset) {
  return inb(dev->io_base + offset);
}

static void virtio_write8(VirtIONetDevice *dev, uint16_t offset,
                          uint8_t value) {
  outb(dev->io_base + offset, value);
}

static uint16_t virtio_read16(VirtIONetDevice *dev, uint16_t offset) {
  return inw(dev->io_base + offset);
}

static void virtio_write16(VirtIONetDevice *dev, uint16_t offset,
                           uint16_t value) {
  outw(dev->io_base + offset, value);
}

static uint32_t virtio_read32(VirtIONetDevice *dev, uint16_t offset) {
  return inl(dev->io_base + offset);
}

static void virtio_write32(VirtIONetDevice *dev, uint16_t offset,
                           uint32_t value) {
  outl(dev->io_base + offset, value);
}

/* ============================================ */
/*           Queue Management                   */
/* ============================================ */

static void virtio_queue_init(VirtIONetDevice *dev, VirtioQueue *vq,
                              uint16_t index) {
  /* Select the queue */
  virtio_write16(dev, VIRTIO_REG_QUEUE_SELECT, index);

  /* Get queue size */
  uint16_t size = virtio_read16(dev, VIRTIO_REG_QUEUE_SIZE);
  if (size == 0) {
    net_puts("[Virtio] Queue size is 0!\n");
    return;
  }

  vq->queue_index = index;
  vq->size = size;
  vq->free_head = 0;
  vq->num_free = size;
  vq->last_used_idx = 0;

  /* Calculate required size */
  /* Descriptors: 16 bytes * size */
  uint32_t desc_size = sizeof(VirtQDesc) * size;

  /* Available Ring: 2 + 2 + 2 * size + 2 */
  uint32_t avail_size = 2 + 2 + 2 * size + 2;

  /* Used Ring: 2 + 2 + 8 * size + 2 */
  /* Must be aligned to 4096 bytes boundary */
  uint32_t used_size = 2 + 2 + sizeof(VirtQUsedElem) * size + 2;

  /* Allocate memory (must be physically contiguous and aligned) */
  /* For simplicity, we allocate separate chunks but in a real OS
     we would need physically contiguous pages. kmalloc here is simple heap. */

  /* Align to 4096 for the used ring requirement */
  uint32_t total_size = desc_size + avail_size;
  /* Padding for alignment */
  if (total_size % 4096 != 0) {
    total_size = (total_size + 4096) & ~0xFFF;
  }
  total_size += used_size;

  /* Allocate with alignment */
  uint8_t *mem = (uint8_t *)kmalloc(total_size + 4096);
  if (mem == NULL) {
    net_puts("[Virtio] Failed to allocate queue memory!\n");
    return;
  }

  /* Align to 4K */
  uint32_t mem_addr = (uint32_t)(uintptr_t)mem;
  if (mem_addr & 0xFFF) {
    mem_addr = (mem_addr + 4096) & ~0xFFF;
  }

  vq->desc = (VirtQDesc *)(uintptr_t)mem_addr;
  vq->avail = (VirtQAvail *)(uintptr_t)(mem_addr + desc_size);
  vq->used =
      (VirtQUsed *)(uintptr_t)(mem_addr +
                               ((desc_size + avail_size + 4095) & ~0xFFF));

  /* Initialize descriptors */
  for (int i = 0; i < size; i++) {
    vq->desc[i].next = (i + 1) % size;
    vq->desc[i].flags = 0;
    vq->desc[i].len = 0;
    vq->desc[i].addr = 0;
  }

  /* Allocate buffer tracking array */
  vq->buffers = (void **)kmalloc(sizeof(void *) * size);
  for (int i = 0; i < size; i++)
    vq->buffers[i] = NULL;

  /* Write physical address to device (page number) */
  /* Legacy Virtio expects PFN (Physical Frame Number) of the start of the
   * region */
  uint32_t pfn = mem_addr >> 12;
  virtio_write32(dev, VIRTIO_REG_QUEUE_ADDRESS, pfn);

  net_puts("[Virtio] Queue ");
  net_put_dec(index);
  net_puts(" initialized at PFN ");
  net_put_hex(pfn);
  net_puts("\n");
}

static int virtio_queue_add_buf(VirtioQueue *vq, void *buf, uint32_t len,
                                bool write, bool next) {
  if (vq->num_free == 0)
    return -1;

  uint16_t desc_idx = vq->free_head;
  VirtQDesc *desc = &vq->desc[desc_idx];

  desc->addr = (uint64_t)(uintptr_t)buf;
  desc->len = len;
  desc->flags = 0;

  if (write)
    desc->flags |= VIRTQ_DESC_F_WRITE;
  if (next)
    desc->flags |= VIRTQ_DESC_F_NEXT;

  vq->free_head = desc->next;
  vq->num_free--;

  vq->buffers[desc_idx] = buf;

  return desc_idx;
}

static void virtio_queue_notify(VirtIONetDevice *dev, VirtioQueue *vq,
                                uint16_t desc_idx) {
  /* Add to available ring */
  vq->avail->ring[vq->avail->idx % vq->size] = desc_idx;

  /* Memory barrier would go here */
  asm volatile("" ::: "memory");

  vq->avail->idx++;

  /* Memory barrier */
  asm volatile("" ::: "memory");

  /* Notify device */
  virtio_write16(dev, VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
}

/* ============================================ */
/*           Packet Reception                   */
/* ============================================ */

static void virtio_refill_rx(VirtIONetDevice *dev) {
  VirtioQueue *vq = &dev->rx_queue;

  while (vq->num_free >= 2) { /* Need 2 descriptors: Header + Packet */
    /* Allocate buffer */
    /* Note: In a real driver we would reuse buffers or use a pool */
    /* Here we allocate a new buffer for simplicity */
    uint8_t *buf =
        (uint8_t *)kmalloc(VIRTIO_NET_PKT_SIZE + sizeof(VirtioNetHeader));
    if (buf == NULL)
      break;

    VirtioNetHeader *header = (VirtioNetHeader *)buf;
    uint8_t *packet = buf + sizeof(VirtioNetHeader);

    /* Add header descriptor (writeable) */
    int head_idx =
        virtio_queue_add_buf(vq, header, sizeof(VirtioNetHeader), true, true);

    /* Add packet descriptor (writeable) */
    int pkt_idx =
        virtio_queue_add_buf(vq, packet, VIRTIO_NET_PKT_SIZE, true, false);

    /* Link them manually if needed, but add_buf handles next flag */
    /* We just need to notify the head */

    virtio_queue_notify(dev, vq, head_idx);
  }
}

static void virtio_receive(VirtIONetDevice *dev) {
  VirtioQueue *vq = &dev->rx_queue;

  while (vq->last_used_idx != vq->used->idx) {
    uint16_t used_idx = vq->last_used_idx % vq->size;
    VirtQUsedElem *elem = &vq->used->ring[used_idx];

    uint16_t desc_idx = (uint16_t)elem->id;
    uint32_t len = elem->len;

    /* Process the chain */
    /* First descriptor is header */
    VirtQDesc *header_desc = &vq->desc[desc_idx];
    VirtioNetHeader *header = (VirtioNetHeader *)(uintptr_t)header_desc->addr;

    /* Second descriptor is packet */
    /* We assume strict 2-descriptor chain as we set it up */
    if (header_desc->flags & VIRTQ_DESC_F_NEXT) {
      uint16_t pkt_desc_idx = header_desc->next;
      VirtQDesc *pkt_desc = &vq->desc[pkt_desc_idx];
      uint8_t *packet = (uint8_t *)(uintptr_t)pkt_desc->addr;

      /* Actual packet length is total length minus header size */
      /* But Virtio legacy says len in used ring is total bytes written */
      uint32_t pkt_len = len - sizeof(VirtioNetHeader);

      /* Pass to Ethernet layer */
      ethernet_handle_packet(packet, pkt_len);

      /* Free the buffer (or recycle) */
      /* For now, we just free and refill will alloc new ones */
      /* In production, we should recycle the buffer to avoid kmalloc overhead
       */
      kfree(header); /* header points to start of alloc block */

      /* Return descriptors to free pool */
      /* This is a bit hacky, we should have a proper free list management */
      /* But since we just increment free_head circularly, we can't easily
       * "return" random indices */
      /* Wait, the free list is linked via 'next'. We can push these back. */

      vq->desc[pkt_desc_idx].next = vq->free_head;
      vq->free_head = desc_idx;
      vq->num_free += 2;
    }

    vq->last_used_idx++;
    dev->packets_rx++;
  }

  /* Refill RX ring */
  virtio_refill_rx(dev);
}

/* ============================================ */
/*           Interrupt Handler                  */
/* ============================================ */

void virtio_net_poll(void) {
  VirtIONetDevice *dev =
      g_virtio_dev; // Changed from &g_virtio_device to g_virtio_dev
  if (dev == NULL)
    return; // Added null check

  /* Read ISR status to check if it's for us and clear interrupt */
  uint8_t isr = virtio_read8(dev, VIRTIO_REG_ISR_STATUS);

  if (isr & 1) {
    /* Queue Interrupt */
    // net_puts("[Virtio] Queue Interrupt\n");

    /* Check RX Queue */
    /* Note: In a real driver we should check used ring index */
    virtio_receive(dev);

    /* Check TX Queue (reclaim buffers) */
    /* TODO: Implement TX reclamation */
  }

  if (isr & 2) {
    /* Configuration Change Interrupt */
    net_puts("[Virtio] Config Change Interrupt\n");
  }
}

void virtio_net_irq_handler(void) {
  virtio_net_poll();

  /* Acknowledge interrupt (EOI) */
  outb(0x20, 0x20);
  outb(0xA0, 0x20);
}

/* ============================================ */
/*           NetInterface Implementation        */
/* ============================================ */

static int virtio_netif_send(NetInterface *netif, uint8_t *data, int len) {
  if (netif == NULL || netif->driver_data == NULL)
    return -1;

  VirtIONetDevice *dev = (VirtIONetDevice *)netif->driver_data;

  bool result = virtio_net_send(dev, data, (uint16_t)len);

  if (result) {
    netif->packets_tx++;
    netif->bytes_tx += len;
    return len;
  } else {
    netif->errors++;
    return -1;
  }
}

/* ============================================ */
/*           Public API                         */
/* ============================================ */

VirtIONetDevice *virtio_net_init(PCIDevice *pci_dev) {
  net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
  net_puts("\n=== Virtio Network Driver Initialization ===\n");
  net_reset_color();

  if (pci_dev == NULL)
    return NULL;

  VirtIONetDevice *dev = (VirtIONetDevice *)kmalloc(sizeof(VirtIONetDevice));
  if (dev == NULL)
    return NULL;

  dev->pci_dev = pci_dev;
  dev->io_base = pci_dev->bar0 & 0xFFFFFFFC;
  dev->initialized = false;
  dev->packets_rx = 0;
  dev->packets_tx = 0;
  dev->errors = 0;

  /* Enable Bus Mastering */
  pci_enable_bus_mastering(pci_dev);

  /* Reset Device */
  virtio_write8(dev, VIRTIO_REG_DEVICE_STATUS, 0);

  /* Acknowledge Device */
  virtio_write8(dev, VIRTIO_REG_DEVICE_STATUS,
                VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  /* Negotiate Features */
  uint32_t features = virtio_read32(dev, VIRTIO_REG_DEVICE_FEATURES);
  /* We only support basic features for now */
  features &= (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
  virtio_write32(dev, VIRTIO_REG_GUEST_FEATURES, features);

  /* Set FEATURES_OK */
  virtio_write8(dev, VIRTIO_REG_DEVICE_STATUS,
                VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                    VIRTIO_STATUS_FEATURES_OK);

  /* Check if device accepted features */
  uint8_t status = virtio_read8(dev, VIRTIO_REG_DEVICE_STATUS);
  if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
    net_puts("[Virtio] Feature negotiation failed!\n");
    kfree(dev);
    return NULL;
  }

  /* Initialize Queues */
  virtio_queue_init(dev, &dev->rx_queue, VIRTIO_NET_RX_QUEUE_IDX);
  virtio_queue_init(dev, &dev->tx_queue, VIRTIO_NET_TX_QUEUE_IDX);

  /* Read MAC Address */
  /* If VIRTIO_NET_F_MAC is negotiated, MAC is in config space at offset 0 */
  /* Config space starts at 20 (0x14) for Legacy PCI */
  /* But wait, IO space layout:
     0x00: Device Features
     ...
     0x14: Device Specific Config
  */
  for (int i = 0; i < 6; i++) {
    dev->mac_addr[i] = virtio_read8(dev, 0x14 + i);
  }

  net_puts("[Virtio] MAC Address: ");
  for (int i = 0; i < 6; i++) {
    net_put_hex_byte(dev->mac_addr[i]);
    if (i < 5)
      net_putc(':');
  }
  net_puts("\n");

  /* Setup Interrupts */
  if (pci_dev->interrupt_line != 11) {
    net_puts("[Virtio] Warning: IRQ is ");
    net_put_dec(pci_dev->interrupt_line);
    net_puts(", expected 11. Patching IDT...\n");

    extern void irq11_handler(void);
    idt_set_gate(32 + pci_dev->interrupt_line,
                 (uint32_t)(uintptr_t)irq11_handler, 0x08, 0x8E);
  }

  /* Populate RX Ring */
  virtio_refill_rx(dev);

  /* Set DRIVER_OK */
  virtio_write8(dev, VIRTIO_REG_DEVICE_STATUS,
                status | VIRTIO_STATUS_DRIVER_OK);

  dev->initialized = true;
  g_virtio_dev = dev;

  /* Register NetInterface */
  g_virtio_netif = (NetInterface *)kmalloc(sizeof(NetInterface));
  if (g_virtio_netif != NULL) {
    /* Initialize netif */
    /* ... same as pcnet ... */
    g_virtio_netif->name[0] = 'e';
    g_virtio_netif->name[1] = 't';
    g_virtio_netif->name[2] = 'h';
    g_virtio_netif->name[3] =
        '1'; /* Assuming eth1 if eth0 is pcnet, logic should be dynamic */
    g_virtio_netif->name[4] = '\0';

    for (int i = 0; i < 6; i++)
      g_virtio_netif->mac_addr[i] = dev->mac_addr[i];

    g_virtio_netif->ip_addr = 0;
    g_virtio_netif->netmask = 0;
    g_virtio_netif->gateway = 0;
    g_virtio_netif->dns_server = 0;
    g_virtio_netif->flags = NETIF_FLAG_UP | NETIF_FLAG_RUNNING;
    g_virtio_netif->send = virtio_netif_send;
    g_virtio_netif->driver_data = dev;

    netdev_register(g_virtio_netif);
  }

  return dev;
}

bool virtio_net_send(VirtIONetDevice *dev, const uint8_t *data, uint16_t len) {
  if (dev == NULL || !dev->initialized)
    return false;

  VirtioQueue *vq = &dev->tx_queue;

  if (vq->num_free < 2) {
    /* Try to reclaim used buffers */
    while (vq->last_used_idx != vq->used->idx) {
      uint16_t used_idx = vq->last_used_idx % vq->size;
      VirtQUsedElem *elem = &vq->used->ring[used_idx];
      uint16_t desc_idx = (uint16_t)elem->id;

      /* Free the buffer */
      /* In our simple implementation, we assume 2 descriptors per packet
       * (Header + Data) */
      /* We need to find the start of the chain */

      /* For now, just increment free count. Real implementation needs better
       * tracking */
      vq->num_free += 2; /* Rough approximation */
      vq->last_used_idx++;
    }

    if (vq->num_free < 2)
      return false;
  }

  /* Create Header */
  VirtioNetHeader *header = (VirtioNetHeader *)kmalloc(sizeof(VirtioNetHeader));
  header->flags = 0;
  header->gso_type = 0;
  header->hdr_len = 0;
  header->gso_size = 0;
  header->csum_start = 0;
  header->csum_offset = 0;

  /* Create Data Buffer */
  uint8_t *buf = (uint8_t *)kmalloc(len);
  for (int i = 0; i < len; i++)
    buf[i] = data[i];

  /* Add Header Descriptor */
  int head_idx =
      virtio_queue_add_buf(vq, header, sizeof(VirtioNetHeader), false, true);

  /* Add Data Descriptor */
  int data_idx = virtio_queue_add_buf(vq, buf, len, false, false);

  /* Notify */
  virtio_queue_notify(dev, vq, head_idx);

  return true;
}

VirtIONetDevice *virtio_net_get_device(void) { return g_virtio_dev; }
