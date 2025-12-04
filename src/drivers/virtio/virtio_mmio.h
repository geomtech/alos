/* src/drivers/virtio/virtio_mmio.h - VirtIO MMIO Transport
 *
 * Ce fichier définit le transport VirtIO MMIO selon la spécification
 * OASIS VirtIO 1.0 (section 4.2).
 *
 * Le transport MMIO est différent du transport PCI Legacy :
 * - Layout de registres complètement différent
 * - Adresses de queues sur 64 bits (split en Low/High)
 * - Pas de BAR PCI, adresse MMIO fixe (device tree ou hardcodée)
 * - Magic value 0x74726976 ("virt" en little-endian)
 *
 * Utilisé principalement sur les systèmes embarqués sans PCI.
 */

#ifndef VIRTIO_MMIO_H
#define VIRTIO_MMIO_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================ */
/*           VirtIO MMIO Register Offsets       */
/* ============================================ */

/* Version 2 (moderne) - VirtIO 1.0+ */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000  /* R   - Magic "virt" = 0x74726976 */
#define VIRTIO_MMIO_VERSION             0x004  /* R   - Version (2 = moderne, 1 = legacy) */
#define VIRTIO_MMIO_DEVICE_ID           0x008  /* R   - Device type (1=net, 2=block, etc.) */
#define VIRTIO_MMIO_VENDOR_ID           0x00C  /* R   - Vendor ID */
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010  /* R   - Device features (32 bits) */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014  /* W   - Sélection du mot de features (0 ou 1) */
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020  /* W   - Driver features acceptées */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024  /* W   - Sélection du mot de features driver */
#define VIRTIO_MMIO_QUEUE_SEL           0x030  /* W   - Sélection de la queue */
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034  /* R   - Taille max de la queue */
#define VIRTIO_MMIO_QUEUE_NUM           0x038  /* W   - Taille de la queue utilisée */
#define VIRTIO_MMIO_QUEUE_READY         0x044  /* RW  - Queue prête (1) ou non (0) */
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050  /* W   - Notification de queue */
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060  /* R   - Status d'interruption */
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064  /* W   - Acquittement d'interruption */
#define VIRTIO_MMIO_STATUS              0x070  /* RW  - Device status */
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080  /* W   - Descriptor Table addr (low 32) */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084  /* W   - Descriptor Table addr (high 32) */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090  /* W   - Available Ring addr (low 32) */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094  /* W   - Available Ring addr (high 32) */
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0A0  /* W   - Used Ring addr (low 32) */
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0A4  /* W   - Used Ring addr (high 32) */
#define VIRTIO_MMIO_CONFIG_GENERATION   0x0FC  /* R   - Config generation counter */
#define VIRTIO_MMIO_CONFIG              0x100  /* RW  - Device-specific config (variable) */

/* Legacy (Version 1) - registres différents */
#define VIRTIO_MMIO_LEGACY_HOST_FEATURES     0x010
#define VIRTIO_MMIO_LEGACY_HOST_FEATURES_SEL 0x014
#define VIRTIO_MMIO_LEGACY_GUEST_FEATURES    0x020
#define VIRTIO_MMIO_LEGACY_GUEST_FEATURES_SEL 0x024
#define VIRTIO_MMIO_LEGACY_GUEST_PAGE_SIZE   0x028
#define VIRTIO_MMIO_LEGACY_QUEUE_SEL         0x030
#define VIRTIO_MMIO_LEGACY_QUEUE_NUM_MAX     0x034
#define VIRTIO_MMIO_LEGACY_QUEUE_NUM         0x038
#define VIRTIO_MMIO_LEGACY_QUEUE_ALIGN       0x03C
#define VIRTIO_MMIO_LEGACY_QUEUE_PFN         0x040
#define VIRTIO_MMIO_LEGACY_QUEUE_NOTIFY      0x050
#define VIRTIO_MMIO_LEGACY_INTERRUPT_STATUS  0x060
#define VIRTIO_MMIO_LEGACY_INTERRUPT_ACK     0x064
#define VIRTIO_MMIO_LEGACY_STATUS            0x070

/* ============================================ */
/*           Constantes VirtIO MMIO             */
/* ============================================ */

/* Magic value "virt" en little-endian */
#define VIRTIO_MMIO_MAGIC               0x74726976

/* Versions supportées */
#define VIRTIO_MMIO_VERSION_LEGACY      1
#define VIRTIO_MMIO_VERSION_MODERN      2

/* Device IDs */
#define VIRTIO_DEVICE_ID_NET            1
#define VIRTIO_DEVICE_ID_BLOCK          2
#define VIRTIO_DEVICE_ID_CONSOLE        3
#define VIRTIO_DEVICE_ID_ENTROPY        4
#define VIRTIO_DEVICE_ID_BALLOON        5
#define VIRTIO_DEVICE_ID_SCSI           8
#define VIRTIO_DEVICE_ID_9P             9
#define VIRTIO_DEVICE_ID_GPU            16
#define VIRTIO_DEVICE_ID_INPUT          18
#define VIRTIO_DEVICE_ID_SOCKET         19

/* Device Status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE       (1 << 0)
#define VIRTIO_STATUS_DRIVER            (1 << 1)
#define VIRTIO_STATUS_DRIVER_OK         (1 << 2)
#define VIRTIO_STATUS_FEATURES_OK       (1 << 3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET (1 << 6)
#define VIRTIO_STATUS_FAILED            (1 << 7)

/* Interrupt Status bits */
#define VIRTIO_MMIO_INT_VRING           (1 << 0)  /* Queue notification */
#define VIRTIO_MMIO_INT_CONFIG          (1 << 1)  /* Config change */

/* Feature bits communs */
#define VIRTIO_F_VERSION_1              (1ULL << 32)  /* VirtIO 1.0 moderne */
#define VIRTIO_F_RING_INDIRECT_DESC     (1 << 28)
#define VIRTIO_F_RING_EVENT_IDX         (1 << 29)

/* ============================================ */
/*           Structures VirtIO MMIO             */
/* ============================================ */

/**
 * Structure représentant un device VirtIO MMIO.
 */
typedef struct {
    volatile void *base;        /* Adresse de base MMIO mappée */
    uint32_t phys_addr;         /* Adresse physique */
    uint32_t size;              /* Taille de la région MMIO */
    uint32_t irq;               /* Numéro d'IRQ */
    
    uint32_t device_id;         /* Type de device (net, block, etc.) */
    uint32_t vendor_id;         /* Vendor ID */
    uint32_t version;           /* Version (1=legacy, 2=moderne) */
    
    bool initialized;           /* Device initialisé ? */
} VirtioMmioDevice;

/**
 * Structure pour une virtqueue MMIO.
 */
typedef struct {
    uint16_t index;             /* Index de la queue */
    uint16_t size;              /* Nombre d'entrées */
    
    /* Adresses physiques des structures */
    uint32_t desc_phys;         /* Descriptor Table */
    uint32_t avail_phys;        /* Available Ring */
    uint32_t used_phys;         /* Used Ring */
    
    /* Pointeurs virtuels */
    void *desc;
    void *avail;
    void *used;
    
    /* État */
    uint16_t last_used_idx;
    uint16_t free_head;
    uint16_t num_free;
    
    /* Buffers associés */
    void **buffers;
} VirtioMmioQueue;

/* ============================================ */
/*           Fonctions d'accès MMIO             */
/* ============================================ */

/**
 * Lit un registre 32-bit MMIO.
 */
static inline uint32_t virtio_mmio_read32(VirtioMmioDevice *dev, uint32_t offset)
{
    return *(volatile uint32_t *)((uint8_t *)dev->base + offset);
}

/**
 * Écrit un registre 32-bit MMIO.
 */
static inline void virtio_mmio_write32(VirtioMmioDevice *dev, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)((uint8_t *)dev->base + offset) = value;
}

/**
 * Lit un registre 8-bit de la config space.
 */
static inline uint8_t virtio_mmio_read_config8(VirtioMmioDevice *dev, uint32_t offset)
{
    return *(volatile uint8_t *)((uint8_t *)dev->base + VIRTIO_MMIO_CONFIG + offset);
}

/**
 * Lit un registre 16-bit de la config space.
 */
static inline uint16_t virtio_mmio_read_config16(VirtioMmioDevice *dev, uint32_t offset)
{
    return *(volatile uint16_t *)((uint8_t *)dev->base + VIRTIO_MMIO_CONFIG + offset);
}

/**
 * Lit un registre 32-bit de la config space.
 */
static inline uint32_t virtio_mmio_read_config32(VirtioMmioDevice *dev, uint32_t offset)
{
    return *(volatile uint32_t *)((uint8_t *)dev->base + VIRTIO_MMIO_CONFIG + offset);
}

/* ============================================ */
/*           API publique                       */
/* ============================================ */

/**
 * Probe un device VirtIO MMIO à une adresse donnée.
 * Vérifie le magic value et lit les informations de base.
 *
 * @param phys_addr  Adresse physique du device
 * @param size       Taille de la région MMIO
 * @param irq        Numéro d'IRQ
 * @return Device initialisé ou NULL si échec
 */
VirtioMmioDevice *virtio_mmio_probe(uint32_t phys_addr, uint32_t size, uint32_t irq);

/**
 * Initialise un device VirtIO MMIO.
 * Effectue la négociation de features et prépare le device.
 *
 * @param dev             Device à initialiser
 * @param required_features Features requises par le driver
 * @return 0 si succès, -1 si échec
 */
int virtio_mmio_init_device(VirtioMmioDevice *dev, uint64_t required_features);

/**
 * Configure une virtqueue.
 *
 * @param dev    Device VirtIO
 * @param queue  Structure de queue à initialiser
 * @param index  Index de la queue (0, 1, ...)
 * @return 0 si succès, -1 si échec
 */
int virtio_mmio_setup_queue(VirtioMmioDevice *dev, VirtioMmioQueue *queue, uint16_t index);

/**
 * Notifie le device qu'une queue a de nouveaux buffers.
 *
 * @param dev    Device VirtIO
 * @param queue  Queue à notifier
 */
void virtio_mmio_notify_queue(VirtioMmioDevice *dev, VirtioMmioQueue *queue);

/**
 * Lit et acquitte les interruptions.
 *
 * @param dev  Device VirtIO
 * @return Bitmask des interruptions (VIRTIO_MMIO_INT_*)
 */
uint32_t virtio_mmio_ack_interrupt(VirtioMmioDevice *dev);

/**
 * Reset le device.
 *
 * @param dev  Device VirtIO
 */
void virtio_mmio_reset(VirtioMmioDevice *dev);

/**
 * Libère les ressources d'un device.
 *
 * @param dev  Device à libérer
 */
void virtio_mmio_destroy(VirtioMmioDevice *dev);

/**
 * Affiche les informations d'un device (debug).
 *
 * @param dev  Device VirtIO
 */
void virtio_mmio_dump_info(VirtioMmioDevice *dev);

#endif /* VIRTIO_MMIO_H */
