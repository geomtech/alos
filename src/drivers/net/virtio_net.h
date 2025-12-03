/* src/drivers/net/virtio_net.h - Virtio Network Driver (Legacy) */
#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "../pci.h"
#include <stdbool.h>
#include <stdint.h>

/* ============================================ */
/*           Virtio Constants                   */
/* ============================================ */

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_DEVICE_ID_NET 0x1000
#define VIRTIO_SUBSYSTEM_NET 1

/* Legacy Virtio Header Offsets (IO Space) */
#define VIRTIO_REG_DEVICE_FEATURES 0x00
#define VIRTIO_REG_GUEST_FEATURES 0x04
#define VIRTIO_REG_QUEUE_ADDRESS 0x08
#define VIRTIO_REG_QUEUE_SIZE 0x0C
#define VIRTIO_REG_QUEUE_SELECT 0x0E
#define VIRTIO_REG_QUEUE_NOTIFY 0x10
#define VIRTIO_REG_DEVICE_STATUS 0x12
#define VIRTIO_REG_ISR_STATUS 0x13

/* Device Status Bits */
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED 128

/* Feature Bits */
#define VIRTIO_NET_F_CSUM (1 << 0)
#define VIRTIO_NET_F_GUEST_CSUM (1 << 1)
#define VIRTIO_NET_F_MAC (1 << 5)
#define VIRTIO_NET_F_GSO (1 << 6)
#define VIRTIO_NET_F_GUEST_TSO4 (1 << 7)
#define VIRTIO_NET_F_GUEST_TSO6 (1 << 8)
#define VIRTIO_NET_F_GUEST_ECN (1 << 9)
#define VIRTIO_NET_F_GUEST_UFO (1 << 10)
#define VIRTIO_NET_F_HOST_TSO4 (1 << 11)
#define VIRTIO_NET_F_HOST_TSO6 (1 << 12)
#define VIRTIO_NET_F_HOST_ECN (1 << 13)
#define VIRTIO_NET_F_HOST_UFO (1 << 14)
#define VIRTIO_NET_F_MRG_RXBUF (1 << 15)
#define VIRTIO_NET_F_STATUS (1 << 16)
#define VIRTIO_NET_F_CTRL_VQ (1 << 17)
#define VIRTIO_NET_F_CTRL_RX (1 << 18)
#define VIRTIO_NET_F_CTRL_VLAN (1 << 19)
#define VIRTIO_NET_F_GUEST_ANNOUNCE (1 << 21)

/* Queue Indices */
#define VIRTIO_NET_RX_QUEUE_IDX 0
#define VIRTIO_NET_TX_QUEUE_IDX 1
#define VIRTIO_NET_CTRL_QUEUE_IDX 2

/* Buffer Size */
#define VIRTIO_NET_PKT_SIZE 1514
#define VIRTIO_NET_HDR_SIZE 10

/* ============================================ */
/*           Virtio Structures                  */
/* ============================================ */

/* Virtio Queue Descriptor */
typedef struct __attribute__((packed)) {
  uint64_t addr;  /* Address (guest-physical) */
  uint32_t len;   /* Length */
  uint16_t flags; /* The flags as indicated above */
  uint16_t next;  /* Next field if flags & NEXT */
} VirtQDesc;

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2
#define VIRTQ_DESC_F_INDIRECT 4

/* Virtio Available Ring */
typedef struct __attribute__((packed)) {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[];           /* Queue Size elements */
  /* uint16_t used_event; */ /* Only if VIRTIO_F_EVENT_IDX */
} VirtQAvail;

#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

/* Virtio Used Ring Element */
typedef struct __attribute__((packed)) {
  uint32_t id;  /* Index of start of used descriptor chain */
  uint32_t len; /* Total length of the descriptor chain which was used (written
                   to) */
} VirtQUsedElem;

/* Virtio Used Ring */
typedef struct __attribute__((packed)) {
  uint16_t flags;
  uint16_t idx;
  VirtQUsedElem ring[];       /* Queue Size elements */
  /* uint16_t avail_event; */ /* Only if VIRTIO_F_EVENT_IDX */
} VirtQUsed;

#define VIRTQ_USED_F_NO_NOTIFY 1

/* Virtio Net Header */
typedef struct __attribute__((packed)) {
  uint8_t flags;
  uint8_t gso_type;
  uint16_t hdr_len;
  uint16_t gso_size;
  uint16_t csum_start;
  uint16_t csum_offset;
  /* uint16_t num_buffers; */ /* Only if VIRTIO_NET_F_MRG_RXBUF */
} VirtioNetHeader;

#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1

/* ============================================ */
/*           Driver Structures                  */
/* ============================================ */

typedef struct {
  uint16_t queue_index;
  uint16_t size;          /* Number of descriptors */
  uint16_t free_head;     /* Head of free descriptor list */
  uint16_t num_free;      /* Number of free descriptors */
  uint16_t last_used_idx; /* Last checked used index */

  /* DMA Memory */
  VirtQDesc *desc;   /* Descriptor Table */
  VirtQAvail *avail; /* Available Ring */
  VirtQUsed *used;   /* Used Ring */

  /* Helper to track buffers */
  void **buffers; /* Array of pointers to buffers associated with descriptors */
} VirtioQueue;

typedef struct {
  PCIDevice *pci_dev;
  uint32_t io_base;
  uint8_t mac_addr[6];
  uint8_t irq;

  VirtioQueue rx_queue;
  VirtioQueue tx_queue;

  bool initialized;

  /* Statistics */
  uint32_t packets_rx;
  uint32_t packets_tx;
  uint32_t errors;
} VirtIONetDevice;

/* ============================================ */
/*           Public Functions                   */
/* ============================================ */

/**
 * Initialize the Virtio Network Driver.
 */
VirtIONetDevice *virtio_net_init(PCIDevice *pci_dev);

/**
 * Start the device (enable interrupts and queues).
 */
bool virtio_net_start(VirtIONetDevice *dev);

/**
 * Send a packet.
 */
bool virtio_net_send(VirtIONetDevice *dev, const uint8_t *data, uint16_t len);

/**
 * Interrupt Handler.
 */
void virtio_net_irq_handler(void);

/**
 * Get the global device instance.
 */
VirtIONetDevice *virtio_net_get_device(void);

/**
 * Fonction de polling pour le driver Virtio.
 * Vérifie l'état des interruptions et traite les paquets si nécessaire.
 * Ne pas appeler depuis une ISR (n'envoie pas d'EOI).
 */
void virtio_net_poll(void);

#endif /* VIRTIO_NET_H */
