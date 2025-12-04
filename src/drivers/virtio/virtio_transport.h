/* src/drivers/virtio/virtio_transport.h - Abstraction de transport VirtIO
 *
 * Ce fichier définit une interface commune pour les différents transports VirtIO :
 * - PCI Legacy (via PIO ou MMIO du BAR PCI)
 * - MMIO natif (VirtIO 1.0 MMIO transport)
 *
 * Cela permet au driver réseau (et autres) d'être indépendant du transport.
 */

#ifndef VIRTIO_TRANSPORT_H
#define VIRTIO_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "../pci.h"

/* ============================================ */
/*           Types de transport                 */
/* ============================================ */

typedef enum {
    VIRTIO_TRANSPORT_NONE = 0,
    VIRTIO_TRANSPORT_PCI_LEGACY,    /* VirtIO sur PCI (legacy, via PIO) */
    VIRTIO_TRANSPORT_PCI_MODERN,    /* VirtIO sur PCI (modern, via MMIO) */
    VIRTIO_TRANSPORT_MMIO           /* VirtIO MMIO natif (VirtIO 1.0) */
} virtio_transport_type_t;

/* ============================================ */
/*           Constantes communes                */
/* ============================================ */

/* Device Status bits (communs à tous les transports) */
#define VIRTIO_STATUS_ACKNOWLEDGE       (1 << 0)
#define VIRTIO_STATUS_DRIVER            (1 << 1)
#define VIRTIO_STATUS_DRIVER_OK         (1 << 2)
#define VIRTIO_STATUS_FEATURES_OK       (1 << 3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET (1 << 6)
#define VIRTIO_STATUS_FAILED            (1 << 7)

/* Device IDs */
#define VIRTIO_DEVICE_NET               1
#define VIRTIO_DEVICE_BLOCK             2
#define VIRTIO_DEVICE_CONSOLE           3
#define VIRTIO_DEVICE_ENTROPY           4

/* Network device features */
#define VIRTIO_NET_F_CSUM               (1 << 0)
#define VIRTIO_NET_F_GUEST_CSUM         (1 << 1)
#define VIRTIO_NET_F_MAC                (1 << 5)
#define VIRTIO_NET_F_STATUS             (1 << 16)
#define VIRTIO_NET_F_MRG_RXBUF          (1 << 15)

/* ============================================ */
/*           Structures de virtqueue           */
/* ============================================ */

/* Descriptor flags */
#define VIRTQ_DESC_F_NEXT               1   /* Buffer continues via next field */
#define VIRTQ_DESC_F_WRITE              2   /* Buffer is write-only (device writes) */
#define VIRTQ_DESC_F_INDIRECT           4   /* Buffer contains list of descriptors */

/* Virtqueue Descriptor */
typedef struct {
    uint64_t addr;      /* Adresse physique du buffer */
    uint32_t len;       /* Longueur du buffer */
    uint16_t flags;     /* Flags (NEXT, WRITE, INDIRECT) */
    uint16_t next;      /* Index du prochain descriptor (si NEXT) */
} __attribute__((packed)) VirtqDesc;

/* Available Ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];    /* Array de queue_size entrées */
} __attribute__((packed)) VirtqAvail;

/* Used Ring Element */
typedef struct {
    uint32_t id;        /* Index du descriptor head */
    uint32_t len;       /* Nombre d'octets écrits */
} __attribute__((packed)) VirtqUsedElem;

/* Used Ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[];  /* Array de queue_size entrées */
} __attribute__((packed)) VirtqUsed;

/* ============================================ */
/*           Structure de virtqueue unifiée     */
/* ============================================ */

typedef struct {
    uint16_t index;             /* Index de la queue */
    uint16_t size;              /* Nombre d'entrées */
    
    /* Pointeurs vers les structures */
    VirtqDesc *desc;            /* Descriptor Table */
    VirtqAvail *avail;          /* Available Ring */
    VirtqUsed *used;            /* Used Ring */
    
    /* Adresses physiques */
    uint32_t desc_phys;
    uint32_t avail_phys;
    uint32_t used_phys;
    
    /* État de la queue */
    uint16_t free_head;         /* Premier descriptor libre */
    uint16_t num_free;          /* Nombre de descriptors libres */
    uint16_t last_used_idx;     /* Dernier index used traité */
    
    /* Pour PCI Modern: offset de notification (caché lors du setup) */
    uint16_t notify_offset;
    
    /* Buffers associés aux descriptors */
    void **buffers;
} VirtQueue;

/* ============================================ */
/*           Structure de transport unifiée     */
/* ============================================ */

struct virtio_device;  /* Forward declaration */

/* Opérations de transport (vtable) */
typedef struct {
    /* Lecture/écriture de registres */
    uint8_t  (*read8)(struct virtio_device *dev, uint16_t offset);
    uint16_t (*read16)(struct virtio_device *dev, uint16_t offset);
    uint32_t (*read32)(struct virtio_device *dev, uint16_t offset);
    void     (*write8)(struct virtio_device *dev, uint16_t offset, uint8_t val);
    void     (*write16)(struct virtio_device *dev, uint16_t offset, uint16_t val);
    void     (*write32)(struct virtio_device *dev, uint16_t offset, uint32_t val);
    
    /* Opérations de haut niveau */
    uint32_t (*get_features)(struct virtio_device *dev);
    void     (*set_features)(struct virtio_device *dev, uint32_t features);
    uint8_t  (*get_status)(struct virtio_device *dev);
    void     (*set_status)(struct virtio_device *dev, uint8_t status);
    void     (*reset)(struct virtio_device *dev);
    
    /* Configuration de queue */
    int      (*setup_queue)(struct virtio_device *dev, VirtQueue *vq, uint16_t index);
    void     (*notify_queue)(struct virtio_device *dev, VirtQueue *vq);
    
    /* Configuration device-specific */
    uint8_t  (*read_config8)(struct virtio_device *dev, uint16_t offset);
    uint16_t (*read_config16)(struct virtio_device *dev, uint16_t offset);
    uint32_t (*read_config32)(struct virtio_device *dev, uint16_t offset);
    
    /* Interruptions */
    uint32_t (*ack_interrupt)(struct virtio_device *dev);
} VirtioTransportOps;

/* Structure de device VirtIO unifiée */
typedef struct virtio_device {
    virtio_transport_type_t transport_type;
    const VirtioTransportOps *ops;
    
    /* Informations communes */
    uint32_t device_id;
    uint32_t vendor_id;
    uint8_t irq;
    bool initialized;
    
    /* Données spécifiques au transport */
    union {
        struct {
            PCIDevice *pci_dev;
            uint32_t io_base;           /* Pour PIO (Legacy) */
            volatile void *mmio_base;   /* Pour MMIO du BAR (unused in Legacy) */
            uint32_t mmio_phys;
            uint32_t mmio_size;
            bool use_mmio;              /* true si on utilise MMIO (Modern) */
            
            /* Modern MMIO pointers */
            volatile void *common_cfg;  /* Common configuration */
            volatile void *notify_base; /* Notification area */
            volatile void *isr;         /* ISR status */
            volatile void *device_cfg;  /* Device-specific config */
            uint32_t notify_off_multiplier;
            
            /* BAR mappings for cleanup */
            volatile void *bar_mapped[6];
            uint32_t bar_size[6];
        } pci;
        
        struct {
            volatile void *base;
            uint32_t phys_addr;
            uint32_t size;
            uint32_t version;           /* 1=legacy, 2=modern */
        } mmio;
    } transport;
    
    /* Données spécifiques au type de device (net, block, etc.) */
    void *device_data;
} VirtioDevice;

/* ============================================ */
/*           API publique                       */
/* ============================================ */

/**
 * Crée un device VirtIO depuis un device PCI.
 * Détecte automatiquement si on peut utiliser MMIO ou PIO.
 */
VirtioDevice *virtio_create_from_pci(PCIDevice *pci_dev);

/**
 * Crée un device VirtIO depuis une adresse MMIO.
 * Pour les systèmes sans PCI (embedded).
 */
VirtioDevice *virtio_create_from_mmio(uint32_t phys_addr, uint32_t size, uint8_t irq);

/**
 * Initialise un device VirtIO (négociation de features, etc.).
 */
int virtio_init_device(VirtioDevice *dev, uint32_t required_features);

/**
 * Finalise l'initialisation (set DRIVER_OK).
 */
int virtio_finalize_init(VirtioDevice *dev);

/**
 * Configure une virtqueue.
 */
int virtio_setup_queue(VirtioDevice *dev, VirtQueue *vq, uint16_t index);

/**
 * Ajoute un buffer à une virtqueue.
 * @return Index du descriptor, ou -1 si échec
 */
int virtio_queue_add_buf(VirtQueue *vq, void *buf, uint32_t len, 
                         bool device_writable, bool has_next);

/**
 * Notifie le device qu'une queue a de nouveaux buffers.
 */
void virtio_notify(VirtioDevice *dev, VirtQueue *vq);

/**
 * Vérifie si des buffers ont été consommés par le device.
 */
bool virtio_queue_has_used(VirtQueue *vq);

/**
 * Récupère un buffer consommé.
 * @return Pointeur vers le buffer, ou NULL si aucun
 */
void *virtio_queue_get_used(VirtQueue *vq, uint32_t *len);

/**
 * Reset le device.
 */
void virtio_reset(VirtioDevice *dev);

/**
 * Libère les ressources.
 */
void virtio_destroy(VirtioDevice *dev);

/**
 * Affiche les infos du device (debug).
 */
void virtio_dump_info(VirtioDevice *dev);

#endif /* VIRTIO_TRANSPORT_H */
