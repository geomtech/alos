/* src/drivers/virtio/virtio_pci_modern.c - VirtIO PCI Modern (MMIO) Implementation
 *
 * Implémentation du transport VirtIO PCI Modern qui utilise des BARs MMIO
 * au lieu de Port I/O pour accéder aux registres.
 */

#include "virtio_pci_modern.h"
#include "../../kernel/mmio/mmio.h"
#include "../../kernel/klog.h"

/* ============================================ */
/*           Capability Parsing                 */
/* ============================================ */

bool virtio_pci_modern_detect(PCIDevice *pci_dev, virtio_pci_modern_t *modern) {
    if (pci_dev == NULL || modern == NULL) {
        return false;
    }
    
    /* Initialiser la structure */
    modern->has_common_cfg = false;
    modern->has_notify_cfg = false;
    modern->has_isr_cfg = false;
    modern->has_device_cfg = false;
    
    for (int i = 0; i < 6; i++) {
        modern->bar_mapped[i] = NULL;
        modern->bar_size[i] = 0;
    }
    
    modern->common_cfg = NULL;
    modern->notify_base = NULL;
    modern->isr = NULL;
    modern->device_cfg = NULL;
    
    /* Vérifier si le device a une liste de capabilities */
    uint16_t status = pci_config_read_word(pci_dev->bus, pci_dev->slot, 
                                           pci_dev->func, PCI_STATUS);
    
    if (!(status & (1 << 4))) {
        KLOG_INFO("VIRTIO_MODERN", "No capabilities list (Legacy only)");
        return false;
    }
    
    /* Parcourir la liste des capabilities */
    uint8_t cap_ptr = pci_config_read_byte(pci_dev->bus, pci_dev->slot, 
                                           pci_dev->func, PCI_CAPABILITIES_PTR);
    
    int virtio_caps_found = 0;
    
    while (cap_ptr != 0 && cap_ptr != 0xFF) {
        uint8_t cap_id = pci_config_read_byte(pci_dev->bus, pci_dev->slot, 
                                              pci_dev->func, cap_ptr);
        
        if (cap_id == PCI_CAP_ID_VNDR) {
            /* C'est une capability vendor-specific (VirtIO) */
            uint8_t cfg_type = pci_config_read_byte(pci_dev->bus, pci_dev->slot, 
                                                    pci_dev->func, cap_ptr + 3);
            uint8_t bar = pci_config_read_byte(pci_dev->bus, pci_dev->slot, 
                                               pci_dev->func, cap_ptr + 4);
            uint32_t offset = pci_config_read_dword(pci_dev->bus, pci_dev->slot, 
                                                    pci_dev->func, cap_ptr + 8);
            uint32_t length = pci_config_read_dword(pci_dev->bus, pci_dev->slot, 
                                                    pci_dev->func, cap_ptr + 12);
            
            switch (cfg_type) {
                case VIRTIO_PCI_CAP_COMMON_CFG:
                    modern->has_common_cfg = true;
                    modern->common_bar = bar;
                    modern->common_offset = offset;
                    modern->common_length = length;
                    KLOG_INFO("VIRTIO_MODERN", "Found Common Config:");
                    KLOG_INFO_HEX("VIRTIO_MODERN", "  BAR: ", bar);
                    KLOG_INFO_HEX("VIRTIO_MODERN", "  Offset: ", offset);
                    KLOG_INFO_HEX("VIRTIO_MODERN", "  Length: ", length);
                    virtio_caps_found++;
                    break;
                    
                case VIRTIO_PCI_CAP_NOTIFY_CFG:
                    modern->has_notify_cfg = true;
                    modern->notify_bar = bar;
                    modern->notify_offset = offset;
                    modern->notify_length = length;
                    /* Lire le notify_off_multiplier (offset +16 dans la cap) */
                    modern->notify_off_multiplier = pci_config_read_dword(
                        pci_dev->bus, pci_dev->slot, pci_dev->func, cap_ptr + 16);
                    KLOG_INFO("VIRTIO_MODERN", "Found Notify Config:");
                    KLOG_INFO_HEX("VIRTIO_MODERN", "  BAR: ", bar);
                    KLOG_INFO_HEX("VIRTIO_MODERN", "  Multiplier: ", modern->notify_off_multiplier);
                    virtio_caps_found++;
                    break;
                    
                case VIRTIO_PCI_CAP_ISR_CFG:
                    modern->has_isr_cfg = true;
                    modern->isr_bar = bar;
                    modern->isr_offset = offset;
                    modern->isr_length = length;
                    KLOG_INFO("VIRTIO_MODERN", "Found ISR Config:");
                    KLOG_INFO_HEX("VIRTIO_MODERN", "  BAR: ", bar);
                    virtio_caps_found++;
                    break;
                    
                case VIRTIO_PCI_CAP_DEVICE_CFG:
                    modern->has_device_cfg = true;
                    modern->device_bar = bar;
                    modern->device_offset = offset;
                    modern->device_length = length;
                    KLOG_INFO("VIRTIO_MODERN", "Found Device Config:");
                    KLOG_INFO_HEX("VIRTIO_MODERN", "  BAR: ", bar);
                    virtio_caps_found++;
                    break;
                    
                case VIRTIO_PCI_CAP_PCI_CFG:
                    /* PCI config access - pas nécessaire pour nous */
                    break;
            }
        }
        
        /* Passer à la capability suivante */
        cap_ptr = pci_config_read_byte(pci_dev->bus, pci_dev->slot, 
                                       pci_dev->func, cap_ptr + 1);
    }
    
    /* VirtIO Modern nécessite au minimum common_cfg et notify_cfg */
    if (modern->has_common_cfg && modern->has_notify_cfg) {
        KLOG_INFO("VIRTIO_MODERN", "VirtIO Modern detected!");
        KLOG_INFO_HEX("VIRTIO_MODERN", "  Capabilities found: ", virtio_caps_found);
        return true;
    }
    
    KLOG_INFO("VIRTIO_MODERN", "VirtIO Modern not available (missing caps)");
    return false;
}

/* ============================================ */
/*           BAR Mapping                        */
/* ============================================ */

/**
 * Obtient l'adresse et la taille d'un BAR PCI.
 */
static bool get_bar_info(PCIDevice *pci_dev, uint8_t bar_idx, 
                         uint32_t *addr, uint32_t *size) {
    if (bar_idx >= 6) {
        return false;
    }
    
    uint8_t bar_offset = PCI_BAR0 + (bar_idx * 4);
    uint32_t bar_value = pci_config_read_dword(pci_dev->bus, pci_dev->slot, 
                                               pci_dev->func, bar_offset);
    
    /* Vérifier si c'est un BAR MMIO (bit 0 = 0) */
    if (bar_value & 1) {
        KLOG_ERROR("VIRTIO_MODERN", "BAR is I/O, not MMIO!");
        return false;
    }
    
    /* Obtenir l'adresse de base (bits 31:4) */
    *addr = bar_value & 0xFFFFFFF0;
    
    /* Calculer la taille en écrivant 0xFFFFFFFF et relisant */
    pci_config_write_dword(pci_dev->bus, pci_dev->slot, pci_dev->func, 
                           bar_offset, 0xFFFFFFFF);
    uint32_t size_mask = pci_config_read_dword(pci_dev->bus, pci_dev->slot, 
                                               pci_dev->func, bar_offset);
    /* Restaurer la valeur originale */
    pci_config_write_dword(pci_dev->bus, pci_dev->slot, pci_dev->func, 
                           bar_offset, bar_value);
    
    /* Calculer la taille */
    size_mask &= 0xFFFFFFF0;
    *size = (~size_mask) + 1;
    
    return true;
}

int virtio_pci_modern_map(PCIDevice *pci_dev, virtio_pci_modern_t *modern) {
    if (pci_dev == NULL || modern == NULL) {
        return -1;
    }
    
    KLOG_INFO("VIRTIO_MODERN", "Mapping MMIO BARs...");
    
    /* Mapper le BAR pour common_cfg */
    if (modern->has_common_cfg) {
        uint8_t bar_idx = modern->common_bar;
        
        if (modern->bar_mapped[bar_idx] == NULL) {
            uint32_t bar_addr, bar_size;
            if (!get_bar_info(pci_dev, bar_idx, &bar_addr, &bar_size)) {
                KLOG_ERROR("VIRTIO_MODERN", "Failed to get BAR info");
                return -1;
            }
            
            KLOG_INFO_HEX("VIRTIO_MODERN", "  Mapping BAR", bar_idx);
            KLOG_INFO_HEX("VIRTIO_MODERN", "    Phys: ", bar_addr);
            KLOG_INFO_HEX("VIRTIO_MODERN", "    Size: ", bar_size);
            
            modern->bar_mapped[bar_idx] = ioremap(bar_addr, bar_size);
            modern->bar_size[bar_idx] = bar_size;
            
            if (modern->bar_mapped[bar_idx] == NULL) {
                KLOG_ERROR("VIRTIO_MODERN", "Failed to map BAR");
                return -1;
            }
            
            KLOG_INFO_HEX("VIRTIO_MODERN", "    Virt: ", 
                          (uint32_t)(uintptr_t)modern->bar_mapped[bar_idx]);
        }
        
        modern->common_cfg = (volatile void *)((uint8_t *)modern->bar_mapped[bar_idx] 
                                               + modern->common_offset);
        KLOG_INFO_HEX("VIRTIO_MODERN", "  Common cfg at: ", 
                      (uint32_t)(uintptr_t)modern->common_cfg);
    }
    
    /* Mapper le BAR pour notify */
    if (modern->has_notify_cfg) {
        uint8_t bar_idx = modern->notify_bar;
        
        if (modern->bar_mapped[bar_idx] == NULL) {
            uint32_t bar_addr, bar_size;
            if (!get_bar_info(pci_dev, bar_idx, &bar_addr, &bar_size)) {
                return -1;
            }
            
            modern->bar_mapped[bar_idx] = ioremap(bar_addr, bar_size);
            modern->bar_size[bar_idx] = bar_size;
            
            if (modern->bar_mapped[bar_idx] == NULL) {
                return -1;
            }
        }
        
        modern->notify_base = (volatile void *)((uint8_t *)modern->bar_mapped[bar_idx] 
                                                + modern->notify_offset);
        KLOG_INFO_HEX("VIRTIO_MODERN", "  Notify base at: ", 
                      (uint32_t)(uintptr_t)modern->notify_base);
    }
    
    /* Mapper le BAR pour ISR */
    if (modern->has_isr_cfg) {
        uint8_t bar_idx = modern->isr_bar;
        
        KLOG_INFO_HEX("VIRTIO_MODERN", "  ISR BAR index: ", bar_idx);
        KLOG_INFO_HEX("VIRTIO_MODERN", "  ISR offset: ", modern->isr_offset);
        
        if (modern->bar_mapped[bar_idx] == NULL) {
            uint32_t bar_addr, bar_size;
            if (!get_bar_info(pci_dev, bar_idx, &bar_addr, &bar_size)) {
                KLOG_ERROR("VIRTIO_MODERN", "  Failed to get ISR BAR info!");
                return -1;
            }
            
            KLOG_INFO_HEX("VIRTIO_MODERN", "  ISR BAR phys: ", bar_addr);
            KLOG_INFO_HEX("VIRTIO_MODERN", "  ISR BAR size: ", bar_size);
            
            modern->bar_mapped[bar_idx] = ioremap(bar_addr, bar_size);
            modern->bar_size[bar_idx] = bar_size;
            
            if (modern->bar_mapped[bar_idx] == NULL) {
                KLOG_ERROR("VIRTIO_MODERN", "  Failed to map ISR BAR!");
                return -1;
            }
            
            KLOG_INFO_HEX("VIRTIO_MODERN", "  ISR BAR virt: ", 
                          (uint32_t)(uintptr_t)modern->bar_mapped[bar_idx]);
        }
        
        modern->isr = (volatile void *)((uint8_t *)modern->bar_mapped[bar_idx] 
                                        + modern->isr_offset);
        KLOG_INFO_HEX("VIRTIO_MODERN", "  ISR final addr: ", 
                      (uint32_t)(uintptr_t)modern->isr);
    }
    
    /* Mapper le BAR pour device config */
    if (modern->has_device_cfg) {
        uint8_t bar_idx = modern->device_bar;
        
        if (modern->bar_mapped[bar_idx] == NULL) {
            uint32_t bar_addr, bar_size;
            if (!get_bar_info(pci_dev, bar_idx, &bar_addr, &bar_size)) {
                return -1;
            }
            
            modern->bar_mapped[bar_idx] = ioremap(bar_addr, bar_size);
            modern->bar_size[bar_idx] = bar_size;
            
            if (modern->bar_mapped[bar_idx] == NULL) {
                return -1;
            }
        }
        
        modern->device_cfg = (volatile void *)((uint8_t *)modern->bar_mapped[bar_idx] 
                                               + modern->device_offset);
        KLOG_INFO_HEX("VIRTIO_MODERN", "  Device cfg at: ", 
                      (uint32_t)(uintptr_t)modern->device_cfg);
    }
    
    KLOG_INFO("VIRTIO_MODERN", "MMIO mapping complete!");
    return 0;
}

void virtio_pci_modern_unmap(virtio_pci_modern_t *modern) {
    if (modern == NULL) {
        return;
    }
    
    for (int i = 0; i < 6; i++) {
        if (modern->bar_mapped[i] != NULL) {
            iounmap((mmio_addr_t)modern->bar_mapped[i], modern->bar_size[i]);
            modern->bar_mapped[i] = NULL;
        }
    }
    
    modern->common_cfg = NULL;
    modern->notify_base = NULL;
    modern->isr = NULL;
    modern->device_cfg = NULL;
}
