/* src/drivers/virtio/virtio_pci_modern.h - VirtIO PCI Modern (MMIO) Definitions
 *
 * VirtIO PCI Modern utilise des capabilities PCI pour exposer plusieurs
 * structures de configuration via des BARs MMIO :
 *
 * - Common Configuration : Registres communs (features, status, queues)
 * - Notifications : Zone pour notifier le device
 * - ISR Status : Status des interruptions
 * - Device Configuration : Configuration spécifique au device
 *
 * Référence : VirtIO 1.0 Specification, Section 4.1
 */

#ifndef VIRTIO_PCI_MODERN_H
#define VIRTIO_PCI_MODERN_H

#include <stdint.h>
#include <stdbool.h>
#include "../pci.h"

/* ============================================ */
/*           PCI Capability Types               */
/* ============================================ */

#define VIRTIO_PCI_CAP_COMMON_CFG   1   /* Common configuration */
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2   /* Notifications */
#define VIRTIO_PCI_CAP_ISR_CFG      3   /* ISR access */
#define VIRTIO_PCI_CAP_DEVICE_CFG   4   /* Device specific configuration */
#define VIRTIO_PCI_CAP_PCI_CFG      5   /* PCI configuration access */

/* ============================================ */
/*           PCI Capability Structure           */
/* ============================================ */

/**
 * Structure de capability VirtIO PCI.
 * Trouvée dans la liste des capabilities PCI (cap_id = 0x09).
 */
typedef struct {
    uint8_t cap_vndr;       /* Generic PCI field: PCI_CAP_ID_VNDR (0x09) */
    uint8_t cap_next;       /* Generic PCI field: next ptr */
    uint8_t cap_len;        /* Generic PCI field: capability length */
    uint8_t cfg_type;       /* Identifies the structure (VIRTIO_PCI_CAP_*) */
    uint8_t bar;            /* BAR index where the structure is located */
    uint8_t padding[3];     /* Pad to full dword */
    uint32_t offset;        /* Offset within the BAR */
    uint32_t length;        /* Length of the structure in bytes */
} __attribute__((packed)) virtio_pci_cap_t;

/**
 * Extended capability for notifications (cfg_type = 2).
 */
typedef struct {
    virtio_pci_cap_t cap;
    uint32_t notify_off_multiplier;  /* Multiplier for queue notification offset */
} __attribute__((packed)) virtio_pci_notify_cap_t;

/* ============================================ */
/*           Common Configuration Layout        */
/* ============================================ */

/**
 * VirtIO PCI Common Configuration structure.
 * Accessible via MMIO at the location specified by VIRTIO_PCI_CAP_COMMON_CFG.
 */
typedef struct {
    /* About the whole device */
    uint32_t device_feature_select;     /* 0x00: RW - Feature bits selector */
    uint32_t device_feature;            /* 0x04: RO - Device features (32 bits) */
    uint32_t driver_feature_select;     /* 0x08: RW - Driver feature selector */
    uint32_t driver_feature;            /* 0x0C: RW - Driver features (32 bits) */
    uint16_t msix_config;               /* 0x10: RW - MSI-X config vector */
    uint16_t num_queues;                /* 0x12: RO - Number of queues */
    uint8_t device_status;              /* 0x14: RW - Device status */
    uint8_t config_generation;          /* 0x15: RO - Config generation counter */
    
    /* About a specific virtqueue */
    uint16_t queue_select;              /* 0x16: RW - Queue selector */
    uint16_t queue_size;                /* 0x18: RW - Queue size (max) */
    uint16_t queue_msix_vector;         /* 0x1A: RW - Queue MSI-X vector */
    uint16_t queue_enable;              /* 0x1C: RW - Queue enable */
    uint16_t queue_notify_off;          /* 0x1E: RO - Queue notify offset */
    uint64_t queue_desc;                /* 0x20: RW - Descriptor table address */
    uint64_t queue_avail;               /* 0x28: RW - Available ring address */
    uint64_t queue_used;                /* 0x30: RW - Used ring address */
} __attribute__((packed)) virtio_pci_common_cfg_t;

/* Offsets dans common_cfg */
#define VIRTIO_PCI_COMMON_DFSELECT      0x00
#define VIRTIO_PCI_COMMON_DF            0x04
#define VIRTIO_PCI_COMMON_GFSELECT      0x08
#define VIRTIO_PCI_COMMON_GF            0x0C
#define VIRTIO_PCI_COMMON_MSIX          0x10
#define VIRTIO_PCI_COMMON_NUMQ          0x12
#define VIRTIO_PCI_COMMON_STATUS        0x14
#define VIRTIO_PCI_COMMON_CFGGENERATION 0x15
#define VIRTIO_PCI_COMMON_Q_SELECT      0x16
#define VIRTIO_PCI_COMMON_Q_SIZE        0x18
#define VIRTIO_PCI_COMMON_Q_MSIX        0x1A
#define VIRTIO_PCI_COMMON_Q_ENABLE      0x1C
#define VIRTIO_PCI_COMMON_Q_NOFF        0x1E
#define VIRTIO_PCI_COMMON_Q_DESCLO      0x20
#define VIRTIO_PCI_COMMON_Q_DESCHI      0x24
#define VIRTIO_PCI_COMMON_Q_AVAILLO     0x28
#define VIRTIO_PCI_COMMON_Q_AVAILHI     0x2C
#define VIRTIO_PCI_COMMON_Q_USEDLO      0x30
#define VIRTIO_PCI_COMMON_Q_USEDHI      0x34

/* ============================================ */
/*           Device Status Bits                 */
/* ============================================ */

#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
#define VIRTIO_CONFIG_S_DRIVER          2
#define VIRTIO_CONFIG_S_DRIVER_OK       4
#define VIRTIO_CONFIG_S_FEATURES_OK     8
#define VIRTIO_CONFIG_S_NEEDS_RESET     64
#define VIRTIO_CONFIG_S_FAILED          128

/* ============================================ */
/*           ISR Status Bits                    */
/* ============================================ */

#define VIRTIO_PCI_ISR_QUEUE            0x01
#define VIRTIO_PCI_ISR_CONFIG           0x02

/* ============================================ */
/*           Modern Device Structure            */
/* ============================================ */

/**
 * Structure contenant les pointeurs vers les différentes zones MMIO.
 */
typedef struct {
    /* Capabilities trouvées */
    bool has_common_cfg;
    bool has_notify_cfg;
    bool has_isr_cfg;
    bool has_device_cfg;
    
    /* Informations des capabilities */
    uint8_t common_bar;
    uint32_t common_offset;
    uint32_t common_length;
    
    uint8_t notify_bar;
    uint32_t notify_offset;
    uint32_t notify_length;
    uint32_t notify_off_multiplier;
    
    uint8_t isr_bar;
    uint32_t isr_offset;
    uint32_t isr_length;
    
    uint8_t device_bar;
    uint32_t device_offset;
    uint32_t device_length;
    
    /* Pointeurs MMIO mappés */
    volatile void *common_cfg;      /* Common configuration */
    volatile void *notify_base;     /* Notification area */
    volatile void *isr;             /* ISR status */
    volatile void *device_cfg;      /* Device-specific config */
    
    /* BARs mappés (pour éviter de mapper plusieurs fois le même BAR) */
    volatile void *bar_mapped[6];
    uint32_t bar_size[6];
} virtio_pci_modern_t;

/* ============================================ */
/*           API                                */
/* ============================================ */

/**
 * Détecte si un device PCI supporte VirtIO Modern.
 * Parse les capabilities et remplit la structure.
 *
 * @param pci_dev  Device PCI
 * @param modern   Structure à remplir
 * @return true si Modern est supporté, false sinon
 */
bool virtio_pci_modern_detect(PCIDevice *pci_dev, virtio_pci_modern_t *modern);

/**
 * Mappe les BARs MMIO nécessaires pour VirtIO Modern.
 *
 * @param pci_dev  Device PCI
 * @param modern   Structure avec les infos des capabilities
 * @return 0 si succès, -1 si échec
 */
int virtio_pci_modern_map(PCIDevice *pci_dev, virtio_pci_modern_t *modern);

/**
 * Libère les mappings MMIO.
 *
 * @param modern  Structure à libérer
 */
void virtio_pci_modern_unmap(virtio_pci_modern_t *modern);

#endif /* VIRTIO_PCI_MODERN_H */
