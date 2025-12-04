/* src/drivers/virtio/virtio_mmio.c - VirtIO MMIO Transport Implementation
 *
 * Implémentation du transport VirtIO MMIO selon la spécification
 * OASIS VirtIO 1.0 (section 4.2).
 */

#include "virtio_mmio.h"
#include "../../kernel/mmio/mmio.h"
#include "../../mm/kheap.h"
#include "../../mm/pmm.h"
#include "../../kernel/klog.h"
#include "../../kernel/console.h"

/* ============================================ */
/*           Fonctions internes                 */
/* ============================================ */

/**
 * Vérifie si le device est valide (magic value correct).
 */
static bool virtio_mmio_check_magic(VirtioMmioDevice *dev)
{
    uint32_t magic = virtio_mmio_read32(dev, VIRTIO_MMIO_MAGIC_VALUE);
    return magic == VIRTIO_MMIO_MAGIC;
}

/**
 * Lit les features du device (64 bits via 2 lectures 32 bits).
 */
static uint64_t virtio_mmio_get_device_features(VirtioMmioDevice *dev)
{
    uint64_t features;
    
    /* Lire les 32 bits bas (features 0-31) */
    virtio_mmio_write32(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    features = virtio_mmio_read32(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    
    /* Lire les 32 bits hauts (features 32-63) */
    virtio_mmio_write32(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    features |= ((uint64_t)virtio_mmio_read32(dev, VIRTIO_MMIO_DEVICE_FEATURES)) << 32;
    
    return features;
}

/**
 * Écrit les features acceptées par le driver.
 */
static void virtio_mmio_set_driver_features(VirtioMmioDevice *dev, uint64_t features)
{
    /* Écrire les 32 bits bas */
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)(features & 0xFFFFFFFF));
    
    /* Écrire les 32 bits hauts */
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)(features >> 32));
}

/**
 * Lit le status du device.
 */
static uint32_t virtio_mmio_get_status(VirtioMmioDevice *dev)
{
    return virtio_mmio_read32(dev, VIRTIO_MMIO_STATUS);
}

/**
 * Écrit le status du device.
 */
static void virtio_mmio_set_status(VirtioMmioDevice *dev, uint32_t status)
{
    virtio_mmio_write32(dev, VIRTIO_MMIO_STATUS, status);
}

/**
 * Ajoute un bit au status.
 */
static void virtio_mmio_add_status(VirtioMmioDevice *dev, uint32_t status_bit)
{
    uint32_t status = virtio_mmio_get_status(dev);
    virtio_mmio_set_status(dev, status | status_bit);
}

/* ============================================ */
/*           API publique                       */
/* ============================================ */

VirtioMmioDevice *virtio_mmio_probe(uint32_t phys_addr, uint32_t size, uint32_t irq)
{
    KLOG_INFO("VIRTIO_MMIO", "Probing device at phys addr:");
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Address: ", phys_addr);
    
    /* Allouer la structure du device */
    VirtioMmioDevice *dev = (VirtioMmioDevice *)kmalloc(sizeof(VirtioMmioDevice));
    if (dev == NULL) {
        KLOG_ERROR("VIRTIO_MMIO", "Failed to allocate device structure");
        return NULL;
    }
    
    /* Initialiser les champs de base */
    dev->phys_addr = phys_addr;
    dev->size = size;
    dev->irq = irq;
    dev->initialized = false;
    
    /* Mapper la région MMIO */
    dev->base = ioremap(phys_addr, size);
    if (dev->base == NULL) {
        KLOG_ERROR("VIRTIO_MMIO", "Failed to map MMIO region");
        kfree(dev);
        return NULL;
    }
    
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Mapped at: ", (uint32_t)(uintptr_t)dev->base);
    
    /* Vérifier le magic value */
    if (!virtio_mmio_check_magic(dev)) {
        uint32_t magic = virtio_mmio_read32(dev, VIRTIO_MMIO_MAGIC_VALUE);
        KLOG_ERROR("VIRTIO_MMIO", "Invalid magic value!");
        KLOG_INFO_HEX("VIRTIO_MMIO", "  Expected: ", VIRTIO_MMIO_MAGIC);
        KLOG_INFO_HEX("VIRTIO_MMIO", "  Got: ", magic);
        iounmap(dev->base, size);
        kfree(dev);
        return NULL;
    }
    
    /* Lire la version */
    dev->version = virtio_mmio_read32(dev, VIRTIO_MMIO_VERSION);
    if (dev->version != VIRTIO_MMIO_VERSION_LEGACY && 
        dev->version != VIRTIO_MMIO_VERSION_MODERN) {
        KLOG_ERROR("VIRTIO_MMIO", "Unsupported version!");
        KLOG_INFO_HEX("VIRTIO_MMIO", "  Version: ", dev->version);
        iounmap(dev->base, size);
        kfree(dev);
        return NULL;
    }
    
    /* Lire le device ID */
    dev->device_id = virtio_mmio_read32(dev, VIRTIO_MMIO_DEVICE_ID);
    if (dev->device_id == 0) {
        /* Device ID 0 = placeholder, pas un vrai device */
        KLOG_INFO("VIRTIO_MMIO", "Device ID is 0 (placeholder), skipping");
        iounmap(dev->base, size);
        kfree(dev);
        return NULL;
    }
    
    /* Lire le vendor ID */
    dev->vendor_id = virtio_mmio_read32(dev, VIRTIO_MMIO_VENDOR_ID);
    
    KLOG_INFO("VIRTIO_MMIO", "Device found:");
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Version: ", dev->version);
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Device ID: ", dev->device_id);
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Vendor ID: ", dev->vendor_id);
    KLOG_INFO_HEX("VIRTIO_MMIO", "  IRQ: ", dev->irq);
    
    return dev;
}

int virtio_mmio_init_device(VirtioMmioDevice *dev, uint64_t required_features)
{
    if (dev == NULL) {
        return -1;
    }
    
    KLOG_INFO("VIRTIO_MMIO", "Initializing device...");
    
    /* Étape 1: Reset le device */
    virtio_mmio_reset(dev);
    
    /* Étape 2: Set ACKNOWLEDGE status bit */
    virtio_mmio_add_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);
    
    /* Étape 3: Set DRIVER status bit */
    virtio_mmio_add_status(dev, VIRTIO_STATUS_DRIVER);
    
    /* Étape 4: Lire les features du device */
    uint64_t device_features = virtio_mmio_get_device_features(dev);
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Device features (low): ", (uint32_t)(device_features & 0xFFFFFFFF));
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Device features (high): ", (uint32_t)(device_features >> 32));
    
    /* Étape 5: Négocier les features */
    uint64_t accepted_features = device_features & required_features;
    
    /* Pour VirtIO 1.0+, on doit accepter VIRTIO_F_VERSION_1 si disponible */
    if (dev->version == VIRTIO_MMIO_VERSION_MODERN) {
        if (device_features & VIRTIO_F_VERSION_1) {
            accepted_features |= VIRTIO_F_VERSION_1;
        }
    }
    
    virtio_mmio_set_driver_features(dev, accepted_features);
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Accepted features (low): ", (uint32_t)(accepted_features & 0xFFFFFFFF));
    
    /* Étape 6: Set FEATURES_OK (VirtIO 1.0+) */
    if (dev->version == VIRTIO_MMIO_VERSION_MODERN) {
        virtio_mmio_add_status(dev, VIRTIO_STATUS_FEATURES_OK);
        
        /* Vérifier que le device a accepté les features */
        uint32_t status = virtio_mmio_get_status(dev);
        if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
            KLOG_ERROR("VIRTIO_MMIO", "Device did not accept features!");
            virtio_mmio_add_status(dev, VIRTIO_STATUS_FAILED);
            return -1;
        }
    }
    
    KLOG_INFO("VIRTIO_MMIO", "Device initialized successfully");
    dev->initialized = true;
    
    return 0;
}

int virtio_mmio_setup_queue(VirtioMmioDevice *dev, VirtioMmioQueue *queue, uint16_t index)
{
    if (dev == NULL || queue == NULL) {
        return -1;
    }
    
    KLOG_INFO_HEX("VIRTIO_MMIO", "Setting up queue: ", index);
    
    /* Sélectionner la queue */
    virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_SEL, index);
    
    /* Vérifier que la queue n'est pas déjà utilisée */
    if (dev->version == VIRTIO_MMIO_VERSION_MODERN) {
        uint32_t ready = virtio_mmio_read32(dev, VIRTIO_MMIO_QUEUE_READY);
        if (ready != 0) {
            KLOG_ERROR("VIRTIO_MMIO", "Queue already in use!");
            return -1;
        }
    }
    
    /* Lire la taille max de la queue */
    uint32_t max_size = virtio_mmio_read32(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max_size == 0) {
        KLOG_ERROR("VIRTIO_MMIO", "Queue not available (max_size = 0)");
        return -1;
    }
    
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Max queue size: ", max_size);
    
    /* Utiliser une taille raisonnable (min de max_size et 256) */
    uint16_t queue_size = (max_size < 256) ? max_size : 256;
    
    /* Initialiser la structure de queue */
    queue->index = index;
    queue->size = queue_size;
    queue->last_used_idx = 0;
    queue->free_head = 0;
    queue->num_free = queue_size;
    
    /* Calculer les tailles des structures */
    /* Descriptor Table: 16 bytes par entrée */
    uint32_t desc_size = queue_size * 16;
    /* Available Ring: 2 + 2 + 2*queue_size + 2 */
    uint32_t avail_size = 6 + 2 * queue_size;
    /* Used Ring: 2 + 2 + 8*queue_size + 2 */
    uint32_t used_size = 6 + 8 * queue_size;
    
    /* Allouer la mémoire pour les structures (doit être physiquement contiguë) */
    /* Pour simplifier, on alloue un bloc pour tout */
    uint32_t total_size = desc_size + avail_size + used_size;
    total_size = (total_size + 4095) & ~4095; /* Aligner sur page */
    
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
    queue->desc = queue_mem;
    queue->desc_phys = (uint32_t)(uintptr_t)queue_mem;
    
    queue->avail = (uint8_t *)queue_mem + desc_size;
    queue->avail_phys = queue->desc_phys + desc_size;
    
    queue->used = (uint8_t *)queue_mem + desc_size + avail_size;
    queue->used_phys = queue->desc_phys + desc_size + avail_size;
    
    /* Allouer le tableau de buffers */
    queue->buffers = (void **)kmalloc(queue_size * sizeof(void *));
    if (queue->buffers == NULL) {
        KLOG_ERROR("VIRTIO_MMIO", "Failed to allocate buffer array");
        return -1;
    }
    for (int i = 0; i < queue_size; i++) {
        queue->buffers[i] = NULL;
    }
    
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Desc phys: ", queue->desc_phys);
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Avail phys: ", queue->avail_phys);
    KLOG_INFO_HEX("VIRTIO_MMIO", "  Used phys: ", queue->used_phys);
    
    /* Configurer la queue dans le device */
    if (dev->version == VIRTIO_MMIO_VERSION_MODERN) {
        /* VirtIO 1.0+ : adresses 64-bit séparées */
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_NUM, queue_size);
        
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, queue->desc_phys);
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_DESC_HIGH, 0);
        
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_AVAIL_LOW, queue->avail_phys);
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, 0);
        
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_USED_LOW, queue->used_phys);
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_USED_HIGH, 0);
        
        /* Activer la queue */
        virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        /* Legacy : utilise page frame number */
        virtio_mmio_write32(dev, VIRTIO_MMIO_LEGACY_GUEST_PAGE_SIZE, 4096);
        virtio_mmio_write32(dev, VIRTIO_MMIO_LEGACY_QUEUE_NUM, queue_size);
        virtio_mmio_write32(dev, VIRTIO_MMIO_LEGACY_QUEUE_ALIGN, 4096);
        virtio_mmio_write32(dev, VIRTIO_MMIO_LEGACY_QUEUE_PFN, queue->desc_phys / 4096);
    }
    
    KLOG_INFO("VIRTIO_MMIO", "Queue setup complete");
    
    return 0;
}

void virtio_mmio_notify_queue(VirtioMmioDevice *dev, VirtioMmioQueue *queue)
{
    if (dev == NULL || queue == NULL) {
        return;
    }
    
    /* Écrire l'index de la queue pour notifier le device */
    virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_NOTIFY, queue->index);
}

uint32_t virtio_mmio_ack_interrupt(VirtioMmioDevice *dev)
{
    if (dev == NULL) {
        return 0;
    }
    
    /* Lire le status d'interruption */
    uint32_t status = virtio_mmio_read32(dev, VIRTIO_MMIO_INTERRUPT_STATUS);
    
    /* Acquitter les interruptions */
    if (status != 0) {
        virtio_mmio_write32(dev, VIRTIO_MMIO_INTERRUPT_ACK, status);
    }
    
    return status;
}

void virtio_mmio_reset(VirtioMmioDevice *dev)
{
    if (dev == NULL) {
        return;
    }
    
    /* Écrire 0 au status pour reset */
    virtio_mmio_set_status(dev, 0);
    
    /* Attendre que le reset soit effectif */
    while (virtio_mmio_get_status(dev) != 0) {
        /* Busy wait */
    }
    
    dev->initialized = false;
}

void virtio_mmio_destroy(VirtioMmioDevice *dev)
{
    if (dev == NULL) {
        return;
    }
    
    /* Reset le device */
    virtio_mmio_reset(dev);
    
    /* Unmapper la région MMIO */
    if (dev->base != NULL) {
        iounmap(dev->base, dev->size);
    }
    
    /* Libérer la structure */
    kfree(dev);
}

void virtio_mmio_dump_info(VirtioMmioDevice *dev)
{
    if (dev == NULL) {
        return;
    }
    
    console_puts("\n=== VirtIO MMIO Device ===\n");
    console_puts("Physical addr: 0x");
    console_put_hex(dev->phys_addr);
    console_puts("\nVirtual addr:  0x");
    console_put_hex((uint32_t)(uintptr_t)dev->base);
    console_puts("\nVersion:       ");
    console_put_dec(dev->version);
    console_puts(dev->version == 2 ? " (modern)\n" : " (legacy)\n");
    console_puts("Device ID:     ");
    console_put_dec(dev->device_id);
    
    switch (dev->device_id) {
        case VIRTIO_DEVICE_ID_NET:     console_puts(" (network)\n"); break;
        case VIRTIO_DEVICE_ID_BLOCK:   console_puts(" (block)\n"); break;
        case VIRTIO_DEVICE_ID_CONSOLE: console_puts(" (console)\n"); break;
        case VIRTIO_DEVICE_ID_ENTROPY: console_puts(" (entropy)\n"); break;
        default:                       console_puts(" (unknown)\n"); break;
    }
    
    console_puts("Vendor ID:     0x");
    console_put_hex(dev->vendor_id);
    console_puts("\nIRQ:           ");
    console_put_dec(dev->irq);
    console_puts("\nStatus:        0x");
    console_put_hex(virtio_mmio_get_status(dev));
    console_puts("\nInitialized:   ");
    console_puts(dev->initialized ? "yes\n" : "no\n");
    
    /* Afficher les features */
    uint64_t features = virtio_mmio_get_device_features(dev);
    console_puts("Features:      0x");
    console_put_hex((uint32_t)(features >> 32));
    console_put_hex((uint32_t)(features & 0xFFFFFFFF));
    console_puts("\n");
}
