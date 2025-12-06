/* src/drivers/virtio/virtio_transport.c - Implémentation de l'abstraction de transport
 *
 * Ce fichier implémente les fonctions communes et les opérations
 * spécifiques à chaque transport (PCI Legacy et MMIO).
 */

#include "virtio_transport.h"
#include "virtio_mmio.h"
#include "virtio_pci_modern.h"
#include "../../kernel/mmio/mmio.h"
#include "../../kernel/mmio/pci_mmio.h"
#include "../../mm/kheap.h"
#include "../../mm/pmm.h"
#include "../../kernel/klog.h"
#include "../../kernel/console.h"
#include "../../arch/x86_64/io.h"

/* ============================================ */
/*           PCI Legacy Transport               */
/* ============================================ */

/* Offsets pour PCI Legacy (mode WIO 16-bit) */
#define PCI_LEGACY_DEVICE_FEATURES  0x00
#define PCI_LEGACY_GUEST_FEATURES   0x04
#define PCI_LEGACY_QUEUE_ADDRESS    0x08
#define PCI_LEGACY_QUEUE_SIZE       0x0C
#define PCI_LEGACY_QUEUE_SELECT     0x0E
#define PCI_LEGACY_QUEUE_NOTIFY     0x10
#define PCI_LEGACY_DEVICE_STATUS    0x12
#define PCI_LEGACY_ISR_STATUS       0x13
#define PCI_LEGACY_CONFIG_START     0x14

/* VirtIO PCI Capability Types */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_PCI_CAP_PCI_CFG      5

/* Lecture PIO */
static uint8_t pci_pio_read8(VirtioDevice *dev, uint16_t offset) {
    return inb(dev->transport.pci.io_base + offset);
}

static uint16_t pci_pio_read16(VirtioDevice *dev, uint16_t offset) {
    return inw(dev->transport.pci.io_base + offset);
}

static uint32_t pci_pio_read32(VirtioDevice *dev, uint16_t offset) {
    return inl(dev->transport.pci.io_base + offset);
}

static void pci_pio_write8(VirtioDevice *dev, uint16_t offset, uint8_t val) {
    outb(dev->transport.pci.io_base + offset, val);
}

static void pci_pio_write16(VirtioDevice *dev, uint16_t offset, uint16_t val) {
    outw(dev->transport.pci.io_base + offset, val);
}

static void pci_pio_write32(VirtioDevice *dev, uint16_t offset, uint32_t val) {
    outl(dev->transport.pci.io_base + offset, val);
}

/* Opérations PCI Legacy */
static uint32_t pci_get_features(VirtioDevice *dev) {
    return dev->ops->read32(dev, PCI_LEGACY_DEVICE_FEATURES);
}

static void pci_set_features(VirtioDevice *dev, uint32_t features) {
    dev->ops->write32(dev, PCI_LEGACY_GUEST_FEATURES, features);
}

static uint8_t pci_get_status(VirtioDevice *dev) {
    return dev->ops->read8(dev, PCI_LEGACY_DEVICE_STATUS);
}

static void pci_set_status(VirtioDevice *dev, uint8_t status) {
    dev->ops->write8(dev, PCI_LEGACY_DEVICE_STATUS, status);
}

static void pci_reset(VirtioDevice *dev) {
    dev->ops->write8(dev, PCI_LEGACY_DEVICE_STATUS, 0);
}

static int pci_setup_queue(VirtioDevice *dev, VirtQueue *vq, uint16_t index) {
    /* Sélectionner la queue */
    dev->ops->write16(dev, PCI_LEGACY_QUEUE_SELECT, index);
    
    /* Lire la taille max */
    uint16_t max_size = dev->ops->read16(dev, PCI_LEGACY_QUEUE_SIZE);
    if (max_size == 0) {
        KLOG_ERROR("VIRTIO_PCI", "Queue not available");
        return -1;
    }
    
    uint16_t queue_size = (max_size < 256) ? max_size : 256;
    
    /* Initialiser la structure */
    vq->index = index;
    vq->size = queue_size;
    vq->free_head = 0;
    vq->num_free = queue_size;
    vq->last_used_idx = 0;
    vq->notify_offset = 0;  /* Non utilisé en mode Legacy */
    
    /* Calculer les tailles */
    uint32_t desc_size = queue_size * sizeof(VirtqDesc);
    uint32_t avail_size = sizeof(VirtqAvail) + queue_size * sizeof(uint16_t) + sizeof(uint16_t);
    uint32_t used_size = sizeof(VirtqUsed) + queue_size * sizeof(VirtqUsedElem) + sizeof(uint16_t);
    
    /* Aligner avail sur 2 bytes, used sur 4 bytes */
    uint32_t avail_offset = desc_size;
    uint32_t used_offset = (avail_offset + avail_size + 4095) & ~4095; /* Page align */
    uint32_t total_size = used_offset + used_size;
    total_size = (total_size + 4095) & ~4095;
    
    /* Allouer la mémoire */
    void *queue_mem = pmm_alloc_blocks((total_size + 4095) / 4096);
    if (queue_mem == NULL) {
        KLOG_ERROR("VIRTIO_PCI", "Failed to allocate queue memory");
        return -1;
    }
    
    /* Mettre à zéro */
    uint8_t *ptr = (uint8_t *)queue_mem;
    for (uint32_t i = 0; i < total_size; i++) {
        ptr[i] = 0;
    }
    
    /* Assigner les pointeurs */
    vq->desc = (VirtqDesc *)queue_mem;
    vq->desc_phys = (uint32_t)(uintptr_t)queue_mem;
    
    vq->avail = (VirtqAvail *)((uint8_t *)queue_mem + avail_offset);
    vq->avail_phys = vq->desc_phys + avail_offset;
    
    vq->used = (VirtqUsed *)((uint8_t *)queue_mem + used_offset);
    vq->used_phys = vq->desc_phys + used_offset;
    
    /* Initialiser la chaîne de descriptors libres */
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0;
    
    /* Allouer le tableau de buffers */
    vq->buffers = (void **)kmalloc(queue_size * sizeof(void *));
    if (vq->buffers == NULL) {
        KLOG_ERROR("VIRTIO_PCI", "Failed to allocate buffer array");
        return -1;
    }
    for (int i = 0; i < queue_size; i++) {
        vq->buffers[i] = NULL;
    }
    
    /* Configurer la queue dans le device (PFN = Page Frame Number) */
    uint32_t pfn = vq->desc_phys / 4096;
    dev->ops->write32(dev, PCI_LEGACY_QUEUE_ADDRESS, pfn);
    
    KLOG_INFO_HEX("VIRTIO_PCI", "Queue configured, PFN: ", pfn);
    
    return 0;
}

static void pci_notify_queue(VirtioDevice *dev, VirtQueue *vq) {
    dev->ops->write16(dev, PCI_LEGACY_QUEUE_NOTIFY, vq->index);
}

static uint8_t pci_read_config8(VirtioDevice *dev, uint16_t offset) {
    return dev->ops->read8(dev, PCI_LEGACY_CONFIG_START + offset);
}

static uint16_t pci_read_config16(VirtioDevice *dev, uint16_t offset) {
    return dev->ops->read16(dev, PCI_LEGACY_CONFIG_START + offset);
}

static uint32_t pci_read_config32(VirtioDevice *dev, uint16_t offset) {
    return dev->ops->read32(dev, PCI_LEGACY_CONFIG_START + offset);
}

static uint32_t pci_ack_interrupt(VirtioDevice *dev) {
    return dev->ops->read8(dev, PCI_LEGACY_ISR_STATUS);
}

/* Table d'opérations PCI PIO */
static const VirtioTransportOps pci_pio_ops = {
    .read8 = pci_pio_read8,
    .read16 = pci_pio_read16,
    .read32 = pci_pio_read32,
    .write8 = pci_pio_write8,
    .write16 = pci_pio_write16,
    .write32 = pci_pio_write32,
    .get_features = pci_get_features,
    .set_features = pci_set_features,
    .get_status = pci_get_status,
    .set_status = pci_set_status,
    .reset = pci_reset,
    .setup_queue = pci_setup_queue,
    .notify_queue = pci_notify_queue,
    .read_config8 = pci_read_config8,
    .read_config16 = pci_read_config16,
    .read_config32 = pci_read_config32,
    .ack_interrupt = pci_ack_interrupt,
};

/* ============================================ */
/*           PCI MMIO Transport                 */
/* ============================================ */
/* 
 * Le transport PCI MMIO utilise le même layout de registres que PCI PIO,
 * mais accède via Memory-Mapped I/O au lieu de Port I/O.
 * Cela offre de meilleures performances sur les systèmes modernes.
 */

static uint8_t pci_mmio_read8(VirtioDevice *dev, uint16_t offset) {
    return mmio_read8_off(dev->transport.pci.mmio_base, offset);
}

static uint16_t pci_mmio_read16(VirtioDevice *dev, uint16_t offset) {
    return mmio_read16_off(dev->transport.pci.mmio_base, offset);
}

static uint32_t pci_mmio_read32(VirtioDevice *dev, uint16_t offset) {
    return mmio_read32_off(dev->transport.pci.mmio_base, offset);
}

static void pci_mmio_write8(VirtioDevice *dev, uint16_t offset, uint8_t val) {
    mmio_write8_off(dev->transport.pci.mmio_base, offset, val);
    mmiowb();
}

static void pci_mmio_write16(VirtioDevice *dev, uint16_t offset, uint16_t val) {
    mmio_write16_off(dev->transport.pci.mmio_base, offset, val);
    mmiowb();
}

static void pci_mmio_write32(VirtioDevice *dev, uint16_t offset, uint32_t val) {
    mmio_write32_off(dev->transport.pci.mmio_base, offset, val);
    mmiowb();
}

/* Table d'opérations PCI MMIO */
static const VirtioTransportOps pci_mmio_ops = {
    .read8 = pci_mmio_read8,
    .read16 = pci_mmio_read16,
    .read32 = pci_mmio_read32,
    .write8 = pci_mmio_write8,
    .write16 = pci_mmio_write16,
    .write32 = pci_mmio_write32,
    .get_features = pci_get_features,
    .set_features = pci_set_features,
    .get_status = pci_get_status,
    .set_status = pci_set_status,
    .reset = pci_reset,
    .setup_queue = pci_setup_queue,
    .notify_queue = pci_notify_queue,
    .read_config8 = pci_read_config8,
    .read_config16 = pci_read_config16,
    .read_config32 = pci_read_config32,
    .ack_interrupt = pci_ack_interrupt,
};

/* ============================================ */
/*           PCI Modern Transport (MMIO)        */
/* ============================================ */

/* Accès aux registres via common_cfg MMIO */
static uint8_t pci_modern_read8(VirtioDevice *dev, uint16_t offset) {
    return mmio_read8_off(dev->transport.pci.common_cfg, offset);
}

static uint16_t pci_modern_read16(VirtioDevice *dev, uint16_t offset) {
    return mmio_read16_off(dev->transport.pci.common_cfg, offset);
}

static uint32_t pci_modern_read32(VirtioDevice *dev, uint16_t offset) {
    return mmio_read32_off(dev->transport.pci.common_cfg, offset);
}

static void pci_modern_write8(VirtioDevice *dev, uint16_t offset, uint8_t val) {
    mmio_write8_off(dev->transport.pci.common_cfg, offset, val);
    mmiowb();
}

static void pci_modern_write16(VirtioDevice *dev, uint16_t offset, uint16_t val) {
    mmio_write16_off(dev->transport.pci.common_cfg, offset, val);
    mmiowb();
}

static void pci_modern_write32(VirtioDevice *dev, uint16_t offset, uint32_t val) {
    mmio_write32_off(dev->transport.pci.common_cfg, offset, val);
    mmiowb();
}

/* Opérations de haut niveau pour Modern */
static uint32_t pci_modern_get_features(VirtioDevice *dev) {
    volatile void *cfg = dev->transport.pci.common_cfg;
    /* Sélectionner le mot 0 des features */
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_DFSELECT, 0);
    mmiowb();
    return mmio_read32_off(cfg, VIRTIO_PCI_COMMON_DF);
}

static void pci_modern_set_features(VirtioDevice *dev, uint32_t features) {
    volatile void *cfg = dev->transport.pci.common_cfg;
    /* Sélectionner le mot 0 des features */
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_GFSELECT, 0);
    mmiowb();
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_GF, features);
    mmiowb();
}

static uint8_t pci_modern_get_status(VirtioDevice *dev) {
    return mmio_read8_off(dev->transport.pci.common_cfg, VIRTIO_PCI_COMMON_STATUS);
}

static void pci_modern_set_status(VirtioDevice *dev, uint8_t status) {
    mmio_write8_off(dev->transport.pci.common_cfg, VIRTIO_PCI_COMMON_STATUS, status);
    mmiowb();
}

static void pci_modern_reset(VirtioDevice *dev) {
    mmio_write8_off(dev->transport.pci.common_cfg, VIRTIO_PCI_COMMON_STATUS, 0);
    mmiowb();
    /* Attendre que le reset soit effectif */
    while (mmio_read8_off(dev->transport.pci.common_cfg, VIRTIO_PCI_COMMON_STATUS) != 0) {
        /* Busy wait */
    }
}

static int pci_modern_setup_queue(VirtioDevice *dev, VirtQueue *vq, uint16_t index) {
    volatile void *cfg = dev->transport.pci.common_cfg;
    
    /* Sélectionner la queue */
    mmio_write16_off(cfg, VIRTIO_PCI_COMMON_Q_SELECT, index);
    mmiowb();
    
    /* Lire la taille max */
    uint16_t max_size = mmio_read16_off(cfg, VIRTIO_PCI_COMMON_Q_SIZE);
    if (max_size == 0) {
        KLOG_ERROR("VIRTIO_MODERN", "Queue not available");
        return -1;
    }
    
    uint16_t queue_size = (max_size < 256) ? max_size : 256;
    
    /* Initialiser la structure */
    vq->index = index;
    vq->size = queue_size;
    vq->free_head = 0;
    vq->num_free = queue_size;
    vq->last_used_idx = 0;
    
    /* Calculer les tailles */
    uint32_t desc_size = queue_size * sizeof(VirtqDesc);
    uint32_t avail_size = sizeof(VirtqAvail) + queue_size * sizeof(uint16_t) + sizeof(uint16_t);
    uint32_t used_size = sizeof(VirtqUsed) + queue_size * sizeof(VirtqUsedElem) + sizeof(uint16_t);
    
    uint32_t avail_offset = desc_size;
    uint32_t used_offset = (avail_offset + avail_size + 4095) & ~4095;
    uint32_t total_size = used_offset + used_size;
    total_size = (total_size + 4095) & ~4095;
    
    /* Allouer la mémoire */
    void *queue_mem = pmm_alloc_blocks((total_size + 4095) / 4096);
    if (queue_mem == NULL) {
        KLOG_ERROR("VIRTIO_MODERN", "Failed to allocate queue memory");
        return -1;
    }
    
    /* Mettre à zéro */
    uint8_t *ptr = (uint8_t *)queue_mem;
    for (uint32_t i = 0; i < total_size; i++) {
        ptr[i] = 0;
    }
    
    /* Assigner les pointeurs */
    vq->desc = (VirtqDesc *)queue_mem;
    vq->desc_phys = (uint32_t)(uintptr_t)queue_mem;
    
    vq->avail = (VirtqAvail *)((uint8_t *)queue_mem + avail_offset);
    vq->avail_phys = vq->desc_phys + avail_offset;
    
    vq->used = (VirtqUsed *)((uint8_t *)queue_mem + used_offset);
    vq->used_phys = vq->desc_phys + used_offset;
    
    /* Initialiser la chaîne de descriptors libres */
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0;
    
    /* Allouer le tableau de buffers */
    vq->buffers = (void **)kmalloc(queue_size * sizeof(void *));
    if (vq->buffers == NULL) {
        return -1;
    }
    for (int i = 0; i < queue_size; i++) {
        vq->buffers[i] = NULL;
    }
    
    KLOG_INFO_HEX("VIRTIO_MODERN", "Queue setup:", index);
    KLOG_INFO_HEX("VIRTIO_MODERN", "  Desc phys: ", vq->desc_phys);
    KLOG_INFO_HEX("VIRTIO_MODERN", "  Avail phys: ", vq->avail_phys);
    KLOG_INFO_HEX("VIRTIO_MODERN", "  Used phys: ", vq->used_phys);
    
    /* Configurer la queue dans le device (adresses 64-bit) */
    mmio_write16_off(cfg, VIRTIO_PCI_COMMON_Q_SIZE, queue_size);
    mmiowb();
    
    /* Descriptor table address */
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_Q_DESCLO, vq->desc_phys);
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_Q_DESCHI, 0);
    mmiowb();
    
    /* Available ring address */
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_Q_AVAILLO, vq->avail_phys);
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_Q_AVAILHI, 0);
    mmiowb();
    
    /* Used ring address */
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_Q_USEDLO, vq->used_phys);
    mmio_write32_off(cfg, VIRTIO_PCI_COMMON_Q_USEDHI, 0);
    mmiowb();
    
    /* Lire et cacher le notify_offset pour cette queue */
    vq->notify_offset = mmio_read16_off(cfg, VIRTIO_PCI_COMMON_Q_NOFF);
    KLOG_INFO_HEX("VIRTIO_MODERN", "  Notify offset: ", vq->notify_offset);
    
    /* Activer la queue */
    mmio_write16_off(cfg, VIRTIO_PCI_COMMON_Q_ENABLE, 1);
    mmiowb();
    
    KLOG_INFO("VIRTIO_MODERN", "Queue enabled!");
    
    return 0;
}

static void pci_modern_notify_queue(VirtioDevice *dev, VirtQueue *vq) {
    /* Utiliser le notify_offset caché lors du setup (pas d'accès MMIO ici) */
    uint32_t notify_offset = vq->notify_offset * dev->transport.pci.notify_off_multiplier;
    volatile void *notify_addr = (volatile void *)((uint8_t *)dev->transport.pci.notify_base + notify_offset);
    
    /* Écrire l'index de la queue pour notifier */
    mmio_write16(notify_addr, vq->index);
    mmiowb();
}

static uint8_t pci_modern_read_config8(VirtioDevice *dev, uint16_t offset) {
    if (dev->transport.pci.device_cfg == NULL) {
        return 0;
    }
    return mmio_read8_off(dev->transport.pci.device_cfg, offset);
}

static uint16_t pci_modern_read_config16(VirtioDevice *dev, uint16_t offset) {
    if (dev->transport.pci.device_cfg == NULL) {
        return 0;
    }
    return mmio_read16_off(dev->transport.pci.device_cfg, offset);
}

static uint32_t pci_modern_read_config32(VirtioDevice *dev, uint16_t offset) {
    if (dev->transport.pci.device_cfg == NULL) {
        return 0;
    }
    return mmio_read32_off(dev->transport.pci.device_cfg, offset);
}

static uint32_t pci_modern_ack_interrupt(VirtioDevice *dev) {
    if (dev == NULL || dev->transport.pci.isr == NULL) {
        return 0;
    }
    /* Lire l'ISR status (lecture efface automatiquement) */
    uint8_t isr = mmio_read8(dev->transport.pci.isr);
    return isr;
}

/* Table d'opérations PCI Modern */
static const VirtioTransportOps pci_modern_ops = {
    .read8 = pci_modern_read8,
    .read16 = pci_modern_read16,
    .read32 = pci_modern_read32,
    .write8 = pci_modern_write8,
    .write16 = pci_modern_write16,
    .write32 = pci_modern_write32,
    .get_features = pci_modern_get_features,
    .set_features = pci_modern_set_features,
    .get_status = pci_modern_get_status,
    .set_status = pci_modern_set_status,
    .reset = pci_modern_reset,
    .setup_queue = pci_modern_setup_queue,
    .notify_queue = pci_modern_notify_queue,
    .read_config8 = pci_modern_read_config8,
    .read_config16 = pci_modern_read_config16,
    .read_config32 = pci_modern_read_config32,
    .ack_interrupt = pci_modern_ack_interrupt,
};

/* ============================================ */
/*           MMIO Native Transport              */
/* ============================================ */

static uint8_t virtio_mmio_rd8(VirtioDevice *dev, uint16_t offset) {
    return mmio_read8_off(dev->transport.mmio.base, offset);
}

static uint16_t virtio_mmio_rd16(VirtioDevice *dev, uint16_t offset) {
    return mmio_read16_off(dev->transport.mmio.base, offset);
}

static uint32_t virtio_mmio_rd32(VirtioDevice *dev, uint16_t offset) {
    return mmio_read32_off(dev->transport.mmio.base, offset);
}

static void virtio_mmio_wr8(VirtioDevice *dev, uint16_t offset, uint8_t val) {
    mmio_write8_off(dev->transport.mmio.base, offset, val);
}

static void virtio_mmio_wr16(VirtioDevice *dev, uint16_t offset, uint16_t val) {
    mmio_write16_off(dev->transport.mmio.base, offset, val);
}

static void virtio_mmio_wr32(VirtioDevice *dev, uint16_t offset, uint32_t val) {
    mmio_write32_off(dev->transport.mmio.base, offset, val);
}

static uint32_t virtio_mmio_get_features(VirtioDevice *dev) {
    mmio_write32_off(dev->transport.mmio.base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    return mmio_read32_off(dev->transport.mmio.base, VIRTIO_MMIO_DEVICE_FEATURES);
}

static void virtio_mmio_set_features(VirtioDevice *dev, uint32_t features) {
    mmio_write32_off(dev->transport.mmio.base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write32_off(dev->transport.mmio.base, VIRTIO_MMIO_DRIVER_FEATURES, features);
}

static uint8_t virtio_mmio_get_status(VirtioDevice *dev) {
    return (uint8_t)mmio_read32_off(dev->transport.mmio.base, VIRTIO_MMIO_STATUS);
}

static void virtio_mmio_set_status(VirtioDevice *dev, uint8_t status) {
    mmio_write32_off(dev->transport.mmio.base, VIRTIO_MMIO_STATUS, status);
}

static void virtio_mmio_do_reset(VirtioDevice *dev) {
    mmio_write32_off(dev->transport.mmio.base, VIRTIO_MMIO_STATUS, 0);
    /* Attendre que le reset soit effectif */
    while (mmio_read32_off(dev->transport.mmio.base, VIRTIO_MMIO_STATUS) != 0) {
        /* Busy wait */
    }
}

static int virtio_mmio_setup_vq(VirtioDevice *dev, VirtQueue *vq, uint16_t index) {
    volatile void *base = dev->transport.mmio.base;
    
    /* Sélectionner la queue */
    mmio_write32_off(base, VIRTIO_MMIO_QUEUE_SEL, index);
    
    /* Vérifier que la queue n'est pas déjà utilisée (VirtIO 1.0+) */
    if (dev->transport.mmio.version == 2) {
        if (mmio_read32_off(base, VIRTIO_MMIO_QUEUE_READY) != 0) {
            KLOG_ERROR("VIRTIO_MMIO", "Queue already in use");
            return -1;
        }
    }
    
    /* Lire la taille max */
    uint32_t max_size = mmio_read32_off(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max_size == 0) {
        KLOG_ERROR("VIRTIO_MMIO", "Queue not available");
        return -1;
    }
    
    uint16_t queue_size = (max_size < 256) ? max_size : 256;
    
    /* Initialiser la structure */
    vq->index = index;
    vq->size = queue_size;
    vq->free_head = 0;
    vq->num_free = queue_size;
    vq->last_used_idx = 0;
    
    /* Calculer les tailles */
    uint32_t desc_size = queue_size * sizeof(VirtqDesc);
    uint32_t avail_size = sizeof(VirtqAvail) + queue_size * sizeof(uint16_t) + sizeof(uint16_t);
    uint32_t used_size = sizeof(VirtqUsed) + queue_size * sizeof(VirtqUsedElem) + sizeof(uint16_t);
    
    uint32_t avail_offset = desc_size;
    uint32_t used_offset = (avail_offset + avail_size + 4095) & ~4095;
    uint32_t total_size = used_offset + used_size;
    total_size = (total_size + 4095) & ~4095;
    
    /* Allouer la mémoire */
    void *queue_mem = pmm_alloc_blocks((total_size + 4095) / 4096);
    if (queue_mem == NULL) {
        KLOG_ERROR("VIRTIO_MMIO", "Failed to allocate queue memory");
        return -1;
    }
    
    /* Mettre à zéro */
    uint8_t *ptr = (uint8_t *)queue_mem;
    for (uint32_t i = 0; i < total_size; i++) {
        ptr[i] = 0;
    }
    
    /* Assigner les pointeurs */
    vq->desc = (VirtqDesc *)queue_mem;
    vq->desc_phys = (uint32_t)(uintptr_t)queue_mem;
    
    vq->avail = (VirtqAvail *)((uint8_t *)queue_mem + avail_offset);
    vq->avail_phys = vq->desc_phys + avail_offset;
    
    vq->used = (VirtqUsed *)((uint8_t *)queue_mem + used_offset);
    vq->used_phys = vq->desc_phys + used_offset;
    
    /* Initialiser la chaîne de descriptors libres */
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0;
    
    /* Allouer le tableau de buffers */
    vq->buffers = (void **)kmalloc(queue_size * sizeof(void *));
    if (vq->buffers == NULL) {
        return -1;
    }
    for (int i = 0; i < queue_size; i++) {
        vq->buffers[i] = NULL;
    }
    
    /* Configurer la queue */
    if (dev->transport.mmio.version == 2) {
        /* VirtIO 1.0+ */
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_NUM, queue_size);
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_DESC_LOW, vq->desc_phys);
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_DESC_HIGH, 0);
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, vq->avail_phys);
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, 0);
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_USED_LOW, vq->used_phys);
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_USED_HIGH, 0);
        mmio_write32_off(base, VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        /* Legacy */
        mmio_write32_off(base, VIRTIO_MMIO_LEGACY_GUEST_PAGE_SIZE, 4096);
        mmio_write32_off(base, VIRTIO_MMIO_LEGACY_QUEUE_NUM, queue_size);
        mmio_write32_off(base, VIRTIO_MMIO_LEGACY_QUEUE_ALIGN, 4096);
        mmio_write32_off(base, VIRTIO_MMIO_LEGACY_QUEUE_PFN, vq->desc_phys / 4096);
    }
    
    return 0;
}

static void virtio_mmio_notify_vq(VirtioDevice *dev, VirtQueue *vq) {
    mmio_write32_off(dev->transport.mmio.base, VIRTIO_MMIO_QUEUE_NOTIFY, vq->index);
}

static uint8_t virtio_mmio_read_cfg8(VirtioDevice *dev, uint16_t offset) {
    return mmio_read8_off(dev->transport.mmio.base, VIRTIO_MMIO_CONFIG + offset);
}

static uint16_t virtio_mmio_read_cfg16(VirtioDevice *dev, uint16_t offset) {
    return mmio_read16_off(dev->transport.mmio.base, VIRTIO_MMIO_CONFIG + offset);
}

static uint32_t virtio_mmio_read_cfg32(VirtioDevice *dev, uint16_t offset) {
    return mmio_read32_off(dev->transport.mmio.base, VIRTIO_MMIO_CONFIG + offset);
}

static uint32_t virtio_mmio_ack_int(VirtioDevice *dev) {
    uint32_t status = mmio_read32_off(dev->transport.mmio.base, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (status) {
        mmio_write32_off(dev->transport.mmio.base, VIRTIO_MMIO_INTERRUPT_ACK, status);
    }
    return status;
}

/* Table d'opérations MMIO */
static const VirtioTransportOps mmio_ops = {
    .read8 = virtio_mmio_rd8,
    .read16 = virtio_mmio_rd16,
    .read32 = virtio_mmio_rd32,
    .write8 = virtio_mmio_wr8,
    .write16 = virtio_mmio_wr16,
    .write32 = virtio_mmio_wr32,
    .get_features = virtio_mmio_get_features,
    .set_features = virtio_mmio_set_features,
    .get_status = virtio_mmio_get_status,
    .set_status = virtio_mmio_set_status,
    .reset = virtio_mmio_do_reset,
    .setup_queue = virtio_mmio_setup_vq,
    .notify_queue = virtio_mmio_notify_vq,
    .read_config8 = virtio_mmio_read_cfg8,
    .read_config16 = virtio_mmio_read_cfg16,
    .read_config32 = virtio_mmio_read_cfg32,
    .ack_interrupt = virtio_mmio_ack_int,
};

/* ============================================ */
/*           PCI Capability Scanning            */
/* ============================================ */

static void pci_scan_virtio_caps(PCIDevice *dev) {
    uint16_t status = pci_config_read_word(dev->bus, dev->slot, dev->func, PCI_STATUS);
    
    if (!(status & (1 << 4))) {
        KLOG_INFO("VIRTIO", "  No Capabilities list found (Legacy Device)");
        return;
    }
    
    uint8_t cap_ptr = pci_config_read_byte(dev->bus, dev->slot, dev->func, PCI_CAPABILITIES_PTR);
    KLOG_INFO_HEX("VIRTIO", "  Capabilities list at offset: ", cap_ptr);
    
    while (cap_ptr != 0) {
        uint8_t cap_id = pci_config_read_byte(dev->bus, dev->slot, dev->func, cap_ptr);
        uint8_t cap_len = pci_config_read_byte(dev->bus, dev->slot, dev->func, cap_ptr + 2);
        uint8_t next_ptr = pci_config_read_byte(dev->bus, dev->slot, dev->func, cap_ptr + 1);
        
        if (cap_id == PCI_CAP_ID_VNDR) {
            uint8_t cfg_type = pci_config_read_byte(dev->bus, dev->slot, dev->func, cap_ptr + 3);
            uint8_t bar = pci_config_read_byte(dev->bus, dev->slot, dev->func, cap_ptr + 4);
            
            KLOG_INFO("VIRTIO", "  Found VirtIO Capability:");
            KLOG_INFO_HEX("VIRTIO", "    Type: ", cfg_type);
            KLOG_INFO_HEX("VIRTIO", "    BAR: ", bar);
            
            switch (cfg_type) {
                case VIRTIO_PCI_CAP_COMMON_CFG: KLOG_INFO("VIRTIO", "    (Common Config)"); break;
                case VIRTIO_PCI_CAP_NOTIFY_CFG: KLOG_INFO("VIRTIO", "    (Notify Config)"); break;
                case VIRTIO_PCI_CAP_ISR_CFG:    KLOG_INFO("VIRTIO", "    (ISR Config)"); break;
                case VIRTIO_PCI_CAP_DEVICE_CFG: KLOG_INFO("VIRTIO", "    (Device Config)"); break;
                case VIRTIO_PCI_CAP_PCI_CFG:    KLOG_INFO("VIRTIO", "    (PCI Config)"); break;
            }
        }
        
        cap_ptr = next_ptr;
    }
}

/* ============================================ */
/*           API publique                       */
/* ============================================ */

VirtioDevice *virtio_create_from_pci(PCIDevice *pci_dev) {
    if (pci_dev == NULL) {
        return NULL;
    }
    
    KLOG_INFO("VIRTIO", "Creating device from PCI");
    
    VirtioDevice *dev = (VirtioDevice *)kmalloc(sizeof(VirtioDevice));
    if (dev == NULL) {
        return NULL;
    }
    
    /* Initialiser avec PIO par défaut */
    dev->transport_type = VIRTIO_TRANSPORT_PCI_LEGACY;
    dev->ops = &pci_pio_ops;
    dev->transport.pci.pci_dev = pci_dev;
    dev->transport.pci.io_base = pci_dev->bar0 & 0xFFFFFFFC;
    dev->transport.pci.mmio_base = NULL;
    dev->transport.pci.mmio_phys = 0;
    dev->transport.pci.mmio_size = 0;
    dev->transport.pci.use_mmio = false;
    dev->transport.pci.common_cfg = NULL;
    dev->transport.pci.notify_base = NULL;
    dev->transport.pci.isr = NULL;
    dev->transport.pci.device_cfg = NULL;
    dev->transport.pci.notify_off_multiplier = 0;
    for (int i = 0; i < 6; i++) {
        dev->transport.pci.bar_mapped[i] = NULL;
        dev->transport.pci.bar_size[i] = 0;
    }
    dev->device_id = VIRTIO_DEVICE_NET; /* Sera mis à jour */
    dev->vendor_id = pci_dev->vendor_id;
    dev->irq = pci_dev->interrupt_line;
    dev->initialized = false;
    dev->device_data = NULL;
    
    KLOG_INFO_HEX("VIRTIO", "  I/O Base (PIO): ", dev->transport.pci.io_base);
    
    /* ============================================ */
    /* Tenter de détecter VirtIO PCI Modern (MMIO) */
    /* ============================================ */
    virtio_pci_modern_t modern;
    if (virtio_pci_modern_detect(pci_dev, &modern)) {
        KLOG_INFO("VIRTIO", "  VirtIO Modern detected, attempting MMIO setup...");
        
        if (virtio_pci_modern_map(pci_dev, &modern) == 0) {
            /* Copier les pointeurs dans la structure du device */
            dev->transport_type = VIRTIO_TRANSPORT_PCI_MODERN;
            dev->ops = &pci_modern_ops;
            dev->transport.pci.use_mmio = true;
            dev->transport.pci.common_cfg = modern.common_cfg;
            dev->transport.pci.notify_base = modern.notify_base;
            dev->transport.pci.isr = modern.isr;
            dev->transport.pci.device_cfg = modern.device_cfg;
            dev->transport.pci.notify_off_multiplier = modern.notify_off_multiplier;
            
            /* Copier les mappings de BAR */
            for (int i = 0; i < 6; i++) {
                dev->transport.pci.bar_mapped[i] = modern.bar_mapped[i];
                dev->transport.pci.bar_size[i] = modern.bar_size[i];
            }
            
            KLOG_INFO("VIRTIO", "  *** Using PCI Modern MMIO transport ***");
            KLOG_INFO_HEX("VIRTIO", "  IRQ: ", dev->irq);
            
            return dev;
        } else {
            KLOG_INFO("VIRTIO", "  MMIO mapping failed, falling back to Legacy PIO");
        }
    }
    
    /* Fallback sur PCI Legacy (PIO) */
    KLOG_INFO("VIRTIO", "  Using PIO transport (VirtIO PCI Legacy)");
    KLOG_INFO_HEX("VIRTIO", "  IRQ: ", dev->irq);
    
    return dev;
}

VirtioDevice *virtio_create_from_mmio(uint64_t phys_addr, uint64_t size, uint8_t irq) {
    KLOG_INFO("VIRTIO", "Creating device from MMIO");
    KLOG_INFO_HEX("VIRTIO", "  Phys addr: ", phys_addr);
    
    VirtioDevice *dev = (VirtioDevice *)kmalloc(sizeof(VirtioDevice));
    if (dev == NULL) {
        return NULL;
    }
    
    /* Mapper la région MMIO */
    volatile void *base = ioremap(phys_addr, size);
    if (base == NULL) {
        KLOG_ERROR("VIRTIO", "Failed to map MMIO region");
        kfree(dev);
        return NULL;
    }
    
    /* Vérifier le magic value */
    uint32_t magic = mmio_read32_off(base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MMIO_MAGIC) {
        KLOG_ERROR("VIRTIO", "Invalid magic value!");
        KLOG_INFO_HEX("VIRTIO", "  Expected: ", VIRTIO_MMIO_MAGIC);
        KLOG_INFO_HEX("VIRTIO", "  Got: ", magic);
        iounmap((mmio_addr_t)base, size);
        kfree(dev);
        return NULL;
    }
    
    /* Lire la version */
    uint32_t version = mmio_read32_off(base, VIRTIO_MMIO_VERSION);
    if (version != 1 && version != 2) {
        KLOG_ERROR("VIRTIO", "Unsupported version");
        iounmap((mmio_addr_t)base, size);
        kfree(dev);
        return NULL;
    }
    
    /* Lire le device ID */
    uint32_t device_id = mmio_read32_off(base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id == 0) {
        KLOG_INFO("VIRTIO", "Device ID is 0 (placeholder)");
        iounmap((mmio_addr_t)base, size);
        kfree(dev);
        return NULL;
    }
    
    /* Initialiser */
    dev->transport_type = VIRTIO_TRANSPORT_MMIO;
    dev->ops = &mmio_ops;
    dev->transport.mmio.base = base;
    dev->transport.mmio.phys_addr = phys_addr;
    dev->transport.mmio.size = size;
    dev->transport.mmio.version = version;
    dev->device_id = device_id;
    dev->vendor_id = mmio_read32_off(base, VIRTIO_MMIO_VENDOR_ID);
    dev->irq = irq;
    dev->initialized = false;
    dev->device_data = NULL;
    
    KLOG_INFO_HEX("VIRTIO", "  Version: ", version);
    KLOG_INFO_HEX("VIRTIO", "  Device ID: ", device_id);
    KLOG_INFO_HEX("VIRTIO", "  Vendor ID: ", dev->vendor_id);
    
    return dev;
}

int virtio_init_device(VirtioDevice *dev, uint32_t required_features) {
    if (dev == NULL || dev->ops == NULL) {
        return -1;
    }
    
    KLOG_INFO("VIRTIO", "Initializing device...");
    
    /* Reset */
    dev->ops->reset(dev);
    
    /* Set ACKNOWLEDGE */
    uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE;
    dev->ops->set_status(dev, status);
    
    /* Set DRIVER */
    status |= VIRTIO_STATUS_DRIVER;
    dev->ops->set_status(dev, status);
    
    /* Lire et négocier les features */
    uint32_t device_features = dev->ops->get_features(dev);
    KLOG_INFO_HEX("VIRTIO", "  Device features: ", device_features);
    
    uint32_t accepted = device_features & required_features;
    dev->ops->set_features(dev, accepted);
    KLOG_INFO_HEX("VIRTIO", "  Accepted features: ", accepted);
    
    /* Set FEATURES_OK (pour MMIO moderne) */
    if (dev->transport_type == VIRTIO_TRANSPORT_MMIO && 
        dev->transport.mmio.version == 2) {
        status |= VIRTIO_STATUS_FEATURES_OK;
        dev->ops->set_status(dev, status);
        
        /* Vérifier */
        if (!(dev->ops->get_status(dev) & VIRTIO_STATUS_FEATURES_OK)) {
            KLOG_ERROR("VIRTIO", "Device did not accept features!");
            dev->ops->set_status(dev, VIRTIO_STATUS_FAILED);
            return -1;
        }
    }
    
    return 0;
}

int virtio_finalize_init(VirtioDevice *dev) {
    if (dev == NULL) {
        return -1;
    }
    
    /* Set DRIVER_OK */
    uint8_t status = dev->ops->get_status(dev);
    status |= VIRTIO_STATUS_DRIVER_OK;
    dev->ops->set_status(dev, status);
    
    dev->initialized = true;
    KLOG_INFO("VIRTIO", "Device initialization complete");
    
    return 0;
}

int virtio_setup_queue(VirtioDevice *dev, VirtQueue *vq, uint16_t index) {
    if (dev == NULL || vq == NULL || dev->ops == NULL) {
        return -1;
    }
    return dev->ops->setup_queue(dev, vq, index);
}

int virtio_queue_add_buf(VirtQueue *vq, void *buf, uint32_t len, 
                         bool device_writable, bool has_next) {
    if (vq == NULL || buf == NULL || vq->num_free == 0) {
        return -1;
    }
    
    uint16_t idx = vq->free_head;
    VirtqDesc *desc = &vq->desc[idx];
    
    /* Configurer le descriptor */
    desc->addr = (uint64_t)(uint32_t)(uintptr_t)buf;
    desc->len = len;
    desc->flags = 0;
    if (device_writable) {
        desc->flags |= VIRTQ_DESC_F_WRITE;
    }
    if (has_next && vq->num_free > 1) {
        desc->flags |= VIRTQ_DESC_F_NEXT;
    }
    
    /* Sauvegarder le buffer */
    vq->buffers[idx] = buf;
    
    /* Avancer le free_head */
    vq->free_head = desc->next;
    vq->num_free--;
    
    /* Ajouter à l'available ring */
    uint16_t avail_idx = vq->avail->idx % vq->size;
    vq->avail->ring[avail_idx] = idx;
    
    /* Memory barrier avant d'incrémenter idx */
    asm volatile("" ::: "memory");
    
    vq->avail->idx++;
    
    return idx;
}

void virtio_notify(VirtioDevice *dev, VirtQueue *vq) {
    if (dev == NULL || vq == NULL || dev->ops == NULL) {
        return;
    }
    dev->ops->notify_queue(dev, vq);
}

bool virtio_queue_has_used(VirtQueue *vq) {
    if (vq == NULL) {
        return false;
    }
    return vq->last_used_idx != vq->used->idx;
}

void *virtio_queue_get_used(VirtQueue *vq, uint32_t *len) {
    if (vq == NULL || !virtio_queue_has_used(vq)) {
        return NULL;
    }
    
    /* Memory barrier avant de lire */
    asm volatile("" ::: "memory");
    
    uint16_t used_idx = vq->last_used_idx % vq->size;
    VirtqUsedElem *elem = &vq->used->ring[used_idx];
    
    uint16_t desc_idx = (uint16_t)elem->id;
    if (len) {
        *len = elem->len;
    }
    
    void *buf = vq->buffers[desc_idx];
    vq->buffers[desc_idx] = NULL;
    
    /* Remettre le descriptor dans la liste libre */
    vq->desc[desc_idx].next = vq->free_head;
    vq->free_head = desc_idx;
    vq->num_free++;
    
    vq->last_used_idx++;
    
    return buf;
}

void virtio_reset(VirtioDevice *dev) {
    if (dev == NULL || dev->ops == NULL) {
        return;
    }
    dev->ops->reset(dev);
    dev->initialized = false;
}

void virtio_destroy(VirtioDevice *dev) {
    if (dev == NULL) {
        return;
    }
    
    virtio_reset(dev);
    
    /* Libérer les mappings MMIO */
    if (dev->transport_type == VIRTIO_TRANSPORT_MMIO) {
        if (dev->transport.mmio.base != NULL) {
            iounmap((mmio_addr_t)dev->transport.mmio.base, dev->transport.mmio.size);
        }
    } else if (dev->transport_type == VIRTIO_TRANSPORT_PCI_LEGACY) {
        if (dev->transport.pci.use_mmio && dev->transport.pci.mmio_base != NULL) {
            iounmap((mmio_addr_t)dev->transport.pci.mmio_base, dev->transport.pci.mmio_size);
        }
    }
    
    kfree(dev);
}

void virtio_dump_info(VirtioDevice *dev) {
    if (dev == NULL) {
        return;
    }
    KLOG_INFO("VIRTIO", "=== VirtIO Device ===");
    switch (dev->transport_type) {
        case VIRTIO_TRANSPORT_PCI_LEGACY:
            if (dev->transport.pci.use_mmio) {
                KLOG_INFO("VIRTIO", "  Transport: PCI MMIO");
                KLOG_INFO_HEX("VIRTIO", "    MMIO Base: ", (uint32_t)(uintptr_t)dev->transport.pci.mmio_base);
                KLOG_INFO_HEX("VIRTIO", "    MMIO Phys: ", dev->transport.pci.mmio_phys);
            } else {
                KLOG_INFO("VIRTIO", "  Transport: PCI PIO");
                KLOG_INFO_HEX("VIRTIO", "    I/O Base: ", dev->transport.pci.io_base);
            }
            break;
        case VIRTIO_TRANSPORT_MMIO:
            KLOG_INFO("VIRTIO", "  Transport: MMIO Native");
            KLOG_INFO_HEX("VIRTIO", "    Phys addr: ", dev->transport.mmio.phys_addr);
            KLOG_INFO_HEX("VIRTIO", "    Version: ", dev->transport.mmio.version);
            break;
        default:
            KLOG_INFO("VIRTIO", "  Transport: Unknown");
            break;
    }

    switch (dev->device_id) {
        case VIRTIO_DEVICE_NET:
            KLOG_INFO_HEX("VIRTIO", "  Device ID: (network) ", dev->device_id);
            break;
        case VIRTIO_DEVICE_BLOCK:
            KLOG_INFO_HEX("VIRTIO", "  Device ID: (block) ", dev->device_id);
            break;
        case VIRTIO_DEVICE_CONSOLE:
            KLOG_INFO_HEX("VIRTIO", "  Device ID: (console) ", dev->device_id);
            break;
        default:
            KLOG_INFO_HEX("VIRTIO", "  Device ID: (unknown) ", dev->device_id);
            break;
    }

    KLOG_INFO_HEX("VIRTIO", "  Vendor ID: ", dev->vendor_id);
    KLOG_INFO_HEX("VIRTIO", "  IRQ: ", dev->irq);
    KLOG_INFO_HEX("VIRTIO", "  Status: ", dev->ops->get_status(dev));
    KLOG_INFO("VIRTIO", dev->initialized ? "  Initialized: yes" : "  Initialized: no");
}
