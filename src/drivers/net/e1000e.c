/* src/drivers/net/e1000e.c - Intel e1000e Network Driver
 *
 * Driver pour les cartes réseau Intel 82574L et compatibles e1000e.
 * Supporte également les anciennes cartes e1000 (82540EM utilisée par QEMU).
 */

#include "e1000e.h"
#include "../../arch/x86/idt.h"
#include "../../arch/x86/io.h"
#include "../../mm/kheap.h"
#include "../../mm/vmm.h"
#include "../../net/core/netdev.h"
#include "../../net/l2/ethernet.h"
#include "../../kernel/klog.h"

/* ============================================ */
/*           Variables globales                 */
/* ============================================ */

static E1000Device *g_e1000_dev = NULL;
static NetInterface *g_e1000_netif = NULL;

/* ============================================ */
/*           MMIO Access Functions              */
/* ============================================ */

static inline uint32_t e1000_read_reg(E1000Device *dev, uint32_t reg) {
    return *(volatile uint32_t *)((uint8_t *)dev->mmio_base + reg);
}

static inline void e1000_write_reg(E1000Device *dev, uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)((uint8_t *)dev->mmio_base + reg) = val;
}

/* ============================================ */
/*           EEPROM Functions                   */
/* ============================================ */

/**
 * Read a word from the EEPROM.
 */
static uint16_t e1000_eeprom_read(E1000Device *dev, uint8_t addr) {
    uint32_t val;
    
    /* Start the read */
    e1000_write_reg(dev, E1000_EERD, 
                    E1000_EERD_START | ((uint32_t)addr << E1000_EERD_ADDR_SHIFT));
    
    /* Wait for completion */
    int timeout = 10000;
    do {
        val = e1000_read_reg(dev, E1000_EERD);
        if (val & E1000_EERD_DONE) {
            return (uint16_t)(val >> E1000_EERD_DATA_SHIFT);
        }
    } while (--timeout > 0);
    
    KLOG_ERROR("E1000E", "EEPROM read timeout");
    return 0xFFFF;
}

/**
 * Read MAC address from EEPROM.
 */
static bool e1000_read_mac_eeprom(E1000Device *dev) {
    uint16_t word;
    
    /* MAC address is stored in EEPROM words 0, 1, 2 */
    word = e1000_eeprom_read(dev, 0);
    if (word == 0xFFFF) {
        return false;
    }
    dev->mac_addr[0] = word & 0xFF;
    dev->mac_addr[1] = (word >> 8) & 0xFF;
    
    word = e1000_eeprom_read(dev, 1);
    dev->mac_addr[2] = word & 0xFF;
    dev->mac_addr[3] = (word >> 8) & 0xFF;
    
    word = e1000_eeprom_read(dev, 2);
    dev->mac_addr[4] = word & 0xFF;
    dev->mac_addr[5] = (word >> 8) & 0xFF;
    
    return true;
}

/**
 * Read MAC address from RAL/RAH registers (fallback).
 */
static void e1000_read_mac_ral(E1000Device *dev) {
    uint32_t ral = e1000_read_reg(dev, E1000_RAL0);
    uint32_t rah = e1000_read_reg(dev, E1000_RAH0);
    
    dev->mac_addr[0] = ral & 0xFF;
    dev->mac_addr[1] = (ral >> 8) & 0xFF;
    dev->mac_addr[2] = (ral >> 16) & 0xFF;
    dev->mac_addr[3] = (ral >> 24) & 0xFF;
    dev->mac_addr[4] = rah & 0xFF;
    dev->mac_addr[5] = (rah >> 8) & 0xFF;
}

/* ============================================ */
/*           Initialization Functions           */
/* ============================================ */

/**
 * Reset the device.
 */
static void e1000_reset(E1000Device *dev) {
    uint32_t ctrl;
    
    /* Disable interrupts */
    e1000_write_reg(dev, E1000_IMC, 0xFFFFFFFF);
    
    /* Global reset */
    ctrl = e1000_read_reg(dev, E1000_CTRL);
    ctrl |= E1000_CTRL_RST;
    e1000_write_reg(dev, E1000_CTRL, ctrl);
    
    /* Wait for reset to complete */
    for (volatile int i = 0; i < 100000; i++);
    
    /* Disable interrupts again after reset */
    e1000_write_reg(dev, E1000_IMC, 0xFFFFFFFF);
    
    /* Clear pending interrupts */
    e1000_read_reg(dev, E1000_ICR);
}

/**
 * Initialize RX descriptors and buffers.
 */
static bool e1000_init_rx(E1000Device *dev) {
    /* Allocate descriptor ring (16-byte aligned) */
    size_t desc_size = sizeof(E1000RxDesc) * E1000_NUM_RX_DESC;
    dev->rx_descs = (E1000RxDesc *)kmalloc(desc_size + 16);
    if (dev->rx_descs == NULL) {
        KLOG_ERROR("E1000E", "Failed to allocate RX descriptors");
        return false;
    }
    
    /* Align to 16 bytes */
    uint32_t addr = (uint32_t)(uintptr_t)dev->rx_descs;
    if (addr & 0xF) {
        addr = (addr + 15) & ~0xF;
        dev->rx_descs = (E1000RxDesc *)(uintptr_t)addr;
    }
    
    /* Allocate buffers and initialize descriptors */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        dev->rx_buffers[i] = (uint8_t *)kmalloc(E1000_RX_BUFFER_SIZE + 16);
        if (dev->rx_buffers[i] == NULL) {
            KLOG_ERROR("E1000E", "Failed to allocate RX buffer");
            return false;
        }
        
        /* Align buffer */
        uint32_t buf_addr = (uint32_t)(uintptr_t)dev->rx_buffers[i];
        if (buf_addr & 0xF) {
            buf_addr = (buf_addr + 15) & ~0xF;
            dev->rx_buffers[i] = (uint8_t *)(uintptr_t)buf_addr;
        }
        
        dev->rx_descs[i].buffer_addr = (uint64_t)(uintptr_t)dev->rx_buffers[i];
        dev->rx_descs[i].status = 0;
    }
    
    /* Program the descriptor ring address */
    uint32_t rx_ring_addr = (uint32_t)(uintptr_t)dev->rx_descs;
    e1000_write_reg(dev, E1000_RDBAL, rx_ring_addr);
    e1000_write_reg(dev, E1000_RDBAH, 0);  /* 32-bit only */
    
    /* Program the descriptor ring length */
    e1000_write_reg(dev, E1000_RDLEN, desc_size);
    
    /* Set head and tail pointers */
    e1000_write_reg(dev, E1000_RDH, 0);
    e1000_write_reg(dev, E1000_RDT, E1000_NUM_RX_DESC - 1);
    
    dev->rx_cur = 0;
    
    KLOG_INFO_HEX("E1000E", "RX ring at: ", rx_ring_addr);
    
    return true;
}

/**
 * Initialize TX descriptors and buffers.
 */
static bool e1000_init_tx(E1000Device *dev) {
    /* Allocate descriptor ring (16-byte aligned) */
    size_t desc_size = sizeof(E1000TxDesc) * E1000_NUM_TX_DESC;
    dev->tx_descs = (E1000TxDesc *)kmalloc(desc_size + 16);
    if (dev->tx_descs == NULL) {
        KLOG_ERROR("E1000E", "Failed to allocate TX descriptors");
        return false;
    }
    
    /* Align to 16 bytes */
    uint32_t addr = (uint32_t)(uintptr_t)dev->tx_descs;
    if (addr & 0xF) {
        addr = (addr + 15) & ~0xF;
        dev->tx_descs = (E1000TxDesc *)(uintptr_t)addr;
    }
    
    /* Initialize descriptors */
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = NULL;  /* Allocated on demand */
        dev->tx_descs[i].buffer_addr = 0;
        dev->tx_descs[i].cmd = 0;
        dev->tx_descs[i].status = E1000_TXD_STAT_DD;  /* Mark as done */
    }
    
    /* Program the descriptor ring address */
    uint32_t tx_ring_addr = (uint32_t)(uintptr_t)dev->tx_descs;
    e1000_write_reg(dev, E1000_TDBAL, tx_ring_addr);
    e1000_write_reg(dev, E1000_TDBAH, 0);  /* 32-bit only */
    
    /* Program the descriptor ring length */
    e1000_write_reg(dev, E1000_TDLEN, desc_size);
    
    /* Set head and tail pointers */
    e1000_write_reg(dev, E1000_TDH, 0);
    e1000_write_reg(dev, E1000_TDT, 0);
    
    dev->tx_cur = 0;
    
    KLOG_INFO_HEX("E1000E", "TX ring at: ", tx_ring_addr);
    
    return true;
}

/**
 * Enable RX.
 */
static void e1000_enable_rx(E1000Device *dev) {
    uint32_t rctl = 0;
    
    rctl |= E1000_RCTL_EN;          /* Enable receiver */
    rctl |= E1000_RCTL_BAM;         /* Accept broadcast */
    rctl |= E1000_RCTL_BSIZE_2048;  /* Buffer size 2048 */
    rctl |= E1000_RCTL_SECRC;       /* Strip CRC */
    
    e1000_write_reg(dev, E1000_RCTL, rctl);
    
    KLOG_INFO_HEX("E1000E", "RX enabled, RCTL: ", rctl);
}

/**
 * Enable TX.
 */
static void e1000_enable_tx(E1000Device *dev) {
    uint32_t tctl = 0;
    
    tctl |= E1000_TCTL_EN;          /* Enable transmitter */
    tctl |= E1000_TCTL_PSP;         /* Pad short packets */
    tctl |= (15 << E1000_TCTL_CT_SHIFT);    /* Collision threshold */
    tctl |= (64 << E1000_TCTL_COLD_SHIFT);  /* Collision distance (full duplex) */
    
    e1000_write_reg(dev, E1000_TCTL, tctl);
    
    /* Set Inter-Packet Gap */
    e1000_write_reg(dev, E1000_TIPG, 0x0060200A);
    
    KLOG_INFO_HEX("E1000E", "TX enabled, TCTL: ", tctl);
}

/**
 * Setup link.
 */
static void e1000_setup_link(E1000Device *dev) {
    uint32_t ctrl;
    
    ctrl = e1000_read_reg(dev, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;     /* Set Link Up */
    ctrl |= E1000_CTRL_ASDE;    /* Auto-Speed Detection */
    ctrl &= ~E1000_CTRL_LRST;   /* Clear Link Reset */
    ctrl &= ~E1000_CTRL_FRCSPD; /* Don't force speed */
    ctrl &= ~E1000_CTRL_FRCDPX; /* Don't force duplex */
    
    e1000_write_reg(dev, E1000_CTRL, ctrl);
    
    /* Wait for link */
    for (volatile int i = 0; i < 100000; i++);
    
    uint32_t status = e1000_read_reg(dev, E1000_STATUS);
    dev->link_up = (status & E1000_STATUS_LU) != 0;
    
    if (dev->link_up) {
        KLOG_INFO("E1000E", "Link status: UP");
    } else {
        KLOG_WARN("E1000E", "Link status: DOWN");
    }
}

/**
 * Enable interrupts.
 */
static void e1000_enable_interrupts(E1000Device *dev) {
    /* Enable RX and link status change interrupts */
    uint32_t ims = E1000_ICR_RXT0 | E1000_ICR_LSC | E1000_ICR_RXDMT0;
    e1000_write_reg(dev, E1000_IMS, ims);
    
    /* Clear any pending interrupts */
    e1000_read_reg(dev, E1000_ICR);
}

/* ============================================ */
/*           Packet Handling                    */
/* ============================================ */

/**
 * Process received packets.
 */
static void e1000_receive(E1000Device *dev) {
    while (dev->rx_descs[dev->rx_cur].status & E1000_RXD_STAT_DD) {
        E1000RxDesc *desc = &dev->rx_descs[dev->rx_cur];
        uint8_t *buf = dev->rx_buffers[dev->rx_cur];
        uint16_t len = desc->length;
        
        /* Check for errors */
        if (desc->errors) {
            dev->errors++;
            /* RX error occurred */
        } else if (len > 0 && (desc->status & E1000_RXD_STAT_EOP)) {
            /* Valid packet received */
            dev->packets_rx++;
            
            /* Pass to network stack */
            if (g_e1000_netif != NULL) {
                ethernet_handle_packet_netif(g_e1000_netif, buf, len);
                g_e1000_netif->packets_rx++;
                g_e1000_netif->bytes_rx += len;
            }
        }
        
        /* Reset descriptor for reuse */
        desc->status = 0;
        
        /* Update tail pointer */
        uint16_t old_cur = dev->rx_cur;
        dev->rx_cur = (dev->rx_cur + 1) % E1000_NUM_RX_DESC;
        e1000_write_reg(dev, E1000_RDT, old_cur);
    }
}

/**
 * IRQ handler internal.
 */
static void e1000_irq_handler_internal(void) {
    if (g_e1000_dev == NULL || !g_e1000_dev->initialized) {
        return;
    }
    
    /* Read and clear interrupt cause */
    uint32_t icr = e1000_read_reg(g_e1000_dev, E1000_ICR);
    
    if (icr & E1000_ICR_RXT0) {
        /* RX interrupt */
        e1000_receive(g_e1000_dev);
    }
    
    if (icr & E1000_ICR_LSC) {
        /* Link status change */
        uint32_t status = e1000_read_reg(g_e1000_dev, E1000_STATUS);
        g_e1000_dev->link_up = (status & E1000_STATUS_LU) != 0;
        if (g_e1000_dev->link_up) {
            KLOG_INFO("E1000E", "Link status changed: UP");
        } else {
            KLOG_INFO("E1000E", "Link status changed: DOWN");
        }
    }
    
    if (icr & E1000_ICR_TXDW) {
        /* TX done - could free buffers here */
    }
}

/* ============================================ */
/*           NetInterface Send Function         */
/* ============================================ */

static int e1000_netif_send(NetInterface *netif, uint8_t *data, int len) {
    if (netif == NULL || netif->driver_data == NULL) {
        return -1;
    }
    
    E1000Device *dev = (E1000Device *)netif->driver_data;
    
    if (!dev->initialized || !dev->link_up) {
        return -1;
    }
    
    if (len > E1000_TX_BUFFER_SIZE || len < 14) {
        return -1;
    }
    
    /* Get current TX descriptor */
    E1000TxDesc *desc = &dev->tx_descs[dev->tx_cur];
    
    /* Wait for descriptor to be available */
    int timeout = 10000;
    while (!(desc->status & E1000_TXD_STAT_DD) && --timeout > 0) {
        /* Busy wait */
    }
    
    if (timeout == 0) {
        dev->errors++;
        return -1;
    }
    
    /* Allocate buffer if needed */
    if (dev->tx_buffers[dev->tx_cur] == NULL) {
        dev->tx_buffers[dev->tx_cur] = (uint8_t *)kmalloc(E1000_TX_BUFFER_SIZE);
        if (dev->tx_buffers[dev->tx_cur] == NULL) {
            return -1;
        }
    }
    
    /* Copy data to buffer */
    uint8_t *buf = dev->tx_buffers[dev->tx_cur];
    for (int i = 0; i < len; i++) {
        buf[i] = data[i];
    }
    
    /* Setup descriptor */
    desc->buffer_addr = (uint64_t)(uintptr_t)buf;
    desc->length = len;
    desc->cso = 0;
    desc->css = 0;
    desc->special = 0;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    
    /* Update tail pointer to trigger transmission */
    dev->tx_cur = (dev->tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(dev, E1000_TDT, dev->tx_cur);
    
    dev->packets_tx++;
    netif->packets_tx++;
    netif->bytes_tx += len;
    
    return len;
}

/* ============================================ */
/*           Public API                         */
/* ============================================ */

bool e1000e_is_supported(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != E1000E_VENDOR_ID) {
        return false;
    }
    
    switch (device_id) {
        case E1000E_DEV_82540EM:
        case E1000E_DEV_82545EM:
        case E1000E_DEV_82574L:
        case E1000E_DEV_82579LM:
        case E1000E_DEV_82579V:
        case E1000E_DEV_I217LM:
        case E1000E_DEV_I217V:
        case E1000E_DEV_I218LM:
        case E1000E_DEV_I218V:
        case E1000E_DEV_I219LM:
        case E1000E_DEV_I219V:
            return true;
        default:
            return false;
    }
}

E1000Device *e1000e_init(PCIDevice *pci_dev) {
    KLOG_INFO("E1000E", "=== Intel e1000e Network Driver ===");
    
    if (pci_dev == NULL) {
        KLOG_ERROR("E1000E", "No PCI device provided");
        return NULL;
    }
    
    /* Allocate device structure */
    E1000Device *dev = (E1000Device *)kmalloc(sizeof(E1000Device));
    if (dev == NULL) {
        KLOG_ERROR("E1000E", "Failed to allocate device structure");
        return NULL;
    }
    
    /* Initialize structure */
    dev->pci_dev = pci_dev;
    dev->initialized = false;
    dev->link_up = false;
    dev->packets_rx = 0;
    dev->packets_tx = 0;
    dev->errors = 0;
    dev->rx_descs = NULL;
    dev->tx_descs = NULL;
    dev->rx_cur = 0;
    dev->tx_cur = 0;
    
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        dev->rx_buffers[i] = NULL;
    }
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = NULL;
    }
    
    /* Get MMIO base from BAR0 */
    uint32_t bar0 = pci_dev->bar0;
    if (bar0 & 1) {
        /* I/O space - not supported, we need MMIO */
        KLOG_ERROR("E1000E", "BAR0 is I/O space, MMIO required");
        kfree(dev);
        return NULL;
    }
    
    dev->mmio_phys = bar0 & ~0xF;
    dev->mmio_size = 128 * 1024;  /* 128KB typical for e1000 */
    
    KLOG_INFO_HEX("E1000E", "MMIO Physical: ", dev->mmio_phys);
    
    /* Map MMIO region */
    dev->mmio_base = vmm_map_mmio(dev->mmio_phys, dev->mmio_size);
    if (dev->mmio_base == NULL) {
        KLOG_ERROR("E1000E", "Failed to map MMIO region");
        kfree(dev);
        return NULL;
    }
    
    KLOG_INFO_HEX("E1000E", "MMIO Virtual: ", (uint32_t)(uintptr_t)dev->mmio_base);
    
    /* Enable Bus Mastering */
    pci_enable_bus_mastering(pci_dev);
    
    /* Reset the device */
    KLOG_INFO("E1000E", "Resetting device...");
    e1000_reset(dev);
    
    /* Read MAC address */
    KLOG_INFO("E1000E", "Reading MAC address...");
    if (!e1000_read_mac_eeprom(dev)) {
        /* Fallback to RAL/RAH registers */
        e1000_read_mac_ral(dev);
    }
    
    /* Check for valid MAC */
    bool valid_mac = false;
    for (int i = 0; i < 6; i++) {
        if (dev->mac_addr[i] != 0xFF && dev->mac_addr[i] != 0x00) {
            valid_mac = true;
            break;
        }
    }
    
    if (!valid_mac) {
        KLOG_WARN("E1000E", "Invalid MAC address, using default");
        dev->mac_addr[0] = 0x52;
        dev->mac_addr[1] = 0x54;
        dev->mac_addr[2] = 0x00;
        dev->mac_addr[3] = 0x12;
        dev->mac_addr[4] = 0x34;
        dev->mac_addr[5] = 0x56;
    }
    
    KLOG_INFO("E1000E", "MAC Address read from device");
    
    /* Program MAC address into RAL/RAH */
    uint32_t ral = dev->mac_addr[0] | 
                   ((uint32_t)dev->mac_addr[1] << 8) |
                   ((uint32_t)dev->mac_addr[2] << 16) |
                   ((uint32_t)dev->mac_addr[3] << 24);
    uint32_t rah = dev->mac_addr[4] | 
                   ((uint32_t)dev->mac_addr[5] << 8) |
                   (1 << 31);  /* Address Valid bit */
    
    e1000_write_reg(dev, E1000_RAL0, ral);
    e1000_write_reg(dev, E1000_RAH0, rah);
    
    /* Clear Multicast Table Array */
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(dev, E1000_MTA + (i * 4), 0);
    }
    
    /* Initialize RX */
    if (!e1000_init_rx(dev)) {
        kfree(dev);
        return NULL;
    }
    
    /* Initialize TX */
    if (!e1000_init_tx(dev)) {
        kfree(dev);
        return NULL;
    }
    
    /* Setup IRQ */
    dev->irq = pci_dev->interrupt_line;
    KLOG_INFO_DEC("E1000E", "IRQ: ", dev->irq);
    
    /* Patch IDT if needed */
    if (dev->irq != 11) {
        extern void irq11_handler(void);
        idt_set_gate(32 + dev->irq, (uint32_t)(uintptr_t)irq11_handler, 0x08, 0x8E);
    }
    
    /* Store global instance */
    g_e1000_dev = dev;
    
    /* Create and register NetInterface */
    g_e1000_netif = (NetInterface *)kmalloc(sizeof(NetInterface));
    if (g_e1000_netif != NULL) {
        g_e1000_netif->name[0] = 'e';
        g_e1000_netif->name[1] = 't';
        g_e1000_netif->name[2] = 'h';
        g_e1000_netif->name[3] = '0';
        g_e1000_netif->name[4] = '\0';
        
        for (int i = 0; i < 6; i++) {
            g_e1000_netif->mac_addr[i] = dev->mac_addr[i];
        }
        
        g_e1000_netif->ip_addr = 0;
        g_e1000_netif->netmask = 0;
        g_e1000_netif->gateway = 0;
        g_e1000_netif->dns_server = 0;
        g_e1000_netif->flags = NETIF_FLAG_DOWN;
        g_e1000_netif->send = e1000_netif_send;
        g_e1000_netif->driver_data = dev;
        g_e1000_netif->packets_rx = 0;
        g_e1000_netif->packets_tx = 0;
        g_e1000_netif->bytes_rx = 0;
        g_e1000_netif->bytes_tx = 0;
        g_e1000_netif->errors = 0;
        g_e1000_netif->next = NULL;
        
        netdev_register(g_e1000_netif);
    }
    
    KLOG_INFO("E1000E", "Driver initialized successfully!");
    
    return dev;
}

bool e1000e_start(E1000Device *dev) {
    if (dev == NULL) {
        return false;
    }
    
    KLOG_INFO("E1000E", "Starting device...");
    
    /* Setup link */
    e1000_setup_link(dev);
    
    /* Enable RX */
    e1000_enable_rx(dev);
    
    /* Enable TX */
    e1000_enable_tx(dev);
    
    /* Enable interrupts */
    e1000_enable_interrupts(dev);
    
    dev->initialized = true;
    
    /* Update NetInterface flags */
    if (g_e1000_netif != NULL) {
        g_e1000_netif->flags &= ~NETIF_FLAG_DOWN;
        g_e1000_netif->flags |= NETIF_FLAG_UP | NETIF_FLAG_RUNNING;
    }
    
    KLOG_INFO("E1000E", "Device started!");
    
    return true;
}

bool e1000e_send(E1000Device *dev, const uint8_t *data, uint16_t len) {
    if (dev == NULL || g_e1000_netif == NULL) {
        return false;
    }
    return e1000_netif_send(g_e1000_netif, (uint8_t *)data, len) > 0;
}

void e1000e_irq_handler(void) {
    e1000_irq_handler_internal();
    
    /* Send EOI */
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

void e1000e_poll(void) {
    if (g_e1000_dev != NULL && g_e1000_dev->initialized) {
        e1000_irq_handler_internal();
    }
}

E1000Device *e1000e_get_device(void) {
    return g_e1000_dev;
}

void e1000e_get_mac(E1000Device *dev, uint8_t *buf) {
    if (dev == NULL || buf == NULL) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        buf[i] = dev->mac_addr[i];
    }
}
