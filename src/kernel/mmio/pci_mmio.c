/* src/kernel/mmio/pci_mmio.c - Implémentation de la configuration PCI MMIO
 *
 * Ce fichier implémente le parsing des BARs PCI et la détection
 * des régions MMIO vs PIO.
 */

#include "pci_mmio.h"
#include "mmio.h"
#include "../../mm/vmm.h"
#include "../klog.h"
#include "../console.h"

/* ========================================
 * Fonctions internes
 * ======================================== */

/**
 * Lit un BAR depuis le Configuration Space PCI.
 */
static uint32_t pci_read_bar(PCIDevice* dev, int bar_index)
{
    uint8_t offset = PCI_BAR0 + (bar_index * 4);
    return pci_config_read_dword(dev->bus, dev->slot, dev->func, offset);
}

/**
 * Écrit une valeur dans un BAR du Configuration Space PCI.
 */
static void pci_write_bar(PCIDevice* dev, int bar_index, uint32_t value)
{
    uint8_t offset = PCI_BAR0 + (bar_index * 4);
    pci_config_write_dword(dev->bus, dev->slot, dev->func, offset, value);
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

uint32_t pci_get_bar_size(PCIDevice* pci_dev, int bar_index)
{
    if (bar_index < 0 || bar_index >= PCI_MAX_BARS) {
        return 0;
    }
    
    /* Sauvegarder la valeur originale du BAR */
    uint32_t original = pci_read_bar(pci_dev, bar_index);
    
    /* Écrire 0xFFFFFFFF pour déterminer la taille */
    pci_write_bar(pci_dev, bar_index, 0xFFFFFFFF);
    
    /* Lire la valeur résultante */
    uint32_t size_mask = pci_read_bar(pci_dev, bar_index);
    
    /* Restaurer la valeur originale */
    pci_write_bar(pci_dev, bar_index, original);
    
    /* Si le BAR est 0 ou 0xFFFFFFFF, il n'est pas utilisé */
    if (size_mask == 0 || size_mask == 0xFFFFFFFF) {
        return 0;
    }
    
    /* Calculer la taille selon le type de BAR */
    if (pci_bar_is_mmio(original)) {
        /* BAR MMIO: masquer les bits de type (bits 0-3) */
        size_mask &= PCI_BAR_MMIO_ADDR_MASK;
    } else {
        /* BAR PIO: masquer les bits de type (bits 0-1) */
        size_mask &= PCI_BAR_PIO_ADDR_MASK;
    }
    
    /* La taille est le complément à 2 du masque + 1 */
    /* size = ~size_mask + 1 = -size_mask (en non signé) */
    return (~size_mask) + 1;
}

int pci_parse_bars(PCIDevice* pci_dev, pci_device_bars_t* bars)
{
    if (pci_dev == NULL || bars == NULL) {
        return -1;
    }
    
    /* Initialiser la structure */
    bars->pci_dev = pci_dev;
    bars->mmio_bar_count = 0;
    bars->pio_bar_count = 0;
    
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        bars->bars[i].type = PCI_BAR_NONE;
        bars->bars[i].base_addr = 0;
        bars->bars[i].size = 0;
        bars->bars[i].is_64bit = false;
        bars->bars[i].prefetchable = false;
        bars->bars[i].bar_index = i;
    }
    
    /* Parser chaque BAR */
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        uint32_t bar_value = pci_read_bar(pci_dev, i);
        
        /* BAR non utilisé si valeur = 0 */
        if (bar_value == 0) {
            continue;
        }
        
        pci_bar_info_t* bar = &bars->bars[i];
        bar->bar_index = i;
        
        if (pci_bar_is_mmio(bar_value)) {
            /* BAR MMIO */
            bar->type = PCI_BAR_REGION_MMIO;
            bar->base_addr = pci_bar_mmio_addr(bar_value);
            bar->is_64bit = pci_bar_is_64bit(bar_value);
            bar->prefetchable = pci_bar_is_prefetchable(bar_value);
            bar->size = pci_get_bar_size(pci_dev, i);
            
            bars->mmio_bar_count++;
            
            /* Si BAR 64-bit, le BAR suivant contient les bits hauts */
            if (bar->is_64bit && i < PCI_MAX_BARS - 1) {
                /* Pour l'instant on ignore les bits hauts (x86-32) */
                /* Le BAR suivant est marqué comme partie du 64-bit */
                i++; /* Sauter le BAR suivant */
            }
        } else {
            /* BAR PIO */
            bar->type = PCI_BAR_REGION_PIO;
            bar->base_addr = pci_bar_pio_addr(bar_value);
            bar->is_64bit = false;
            bar->prefetchable = false;
            bar->size = pci_get_bar_size(pci_dev, i);
            
            bars->pio_bar_count++;
        }
    }
    
    return 0;
}

void* pci_map_bar(pci_bar_info_t* bar_info)
{
    if (bar_info == NULL) {
        return NULL;
    }
    
    /* Seuls les BARs MMIO peuvent être mappés */
    if (bar_info->type != PCI_BAR_REGION_MMIO) {
        KLOG_ERROR("PCI_MMIO", "Cannot map non-MMIO BAR");
        return NULL;
    }
    
    if (bar_info->size == 0) {
        KLOG_ERROR("PCI_MMIO", "Cannot map BAR with size 0");
        return NULL;
    }
    
    /* Utiliser ioremap pour mapper la région */
    /* Les régions prefetchable peuvent utiliser le cache write-combining */
    uint32_t flags = PAGE_NOCACHE;
    if (bar_info->prefetchable) {
        /* Pour les régions prefetchable, on pourrait utiliser write-combining */
        /* Mais pour la simplicité, on garde non-cachable */
        flags = PAGE_NOCACHE;
    }
    
    return (void*)ioremap_flags(bar_info->base_addr, bar_info->size, flags);
}

void pci_unmap_bar(void* virt_addr, pci_bar_info_t* bar_info)
{
    if (virt_addr == NULL || bar_info == NULL) {
        return;
    }
    
    iounmap((mmio_addr_t)virt_addr, bar_info->size);
}

pci_bar_info_t* pci_find_mmio_bar(pci_device_bars_t* bars)
{
    if (bars == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        if (bars->bars[i].type == PCI_BAR_REGION_MMIO) {
            return &bars->bars[i];
        }
    }
    
    return NULL;
}

pci_bar_info_t* pci_find_pio_bar(pci_device_bars_t* bars)
{
    if (bars == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        if (bars->bars[i].type == PCI_BAR_REGION_PIO) {
            return &bars->bars[i];
        }
    }
    
    return NULL;
}

void pci_dump_bars(pci_device_bars_t* bars)
{
    if (bars == NULL || bars->pci_dev == NULL) {
        return;
    }
    
    PCIDevice* dev = bars->pci_dev;
    
    console_puts("\n=== PCI BARs for ");
    console_put_hex(dev->vendor_id);
    console_puts(":");
    console_put_hex(dev->device_id);
    console_puts(" ===\n");
    
    console_puts("MMIO BARs: ");
    console_put_dec(bars->mmio_bar_count);
    console_puts(", PIO BARs: ");
    console_put_dec(bars->pio_bar_count);
    console_puts("\n\n");
    
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        pci_bar_info_t* bar = &bars->bars[i];
        
        if (bar->type == PCI_BAR_NONE) {
            continue;
        }
        
        console_puts("BAR");
        console_put_dec(i);
        console_puts(": ");
        
        if (bar->type == PCI_BAR_REGION_MMIO) {
            console_puts("MMIO ");
            if (bar->is_64bit) {
                console_puts("64-bit ");
            } else {
                console_puts("32-bit ");
            }
            if (bar->prefetchable) {
                console_puts("prefetchable ");
            }
        } else {
            console_puts("PIO  ");
        }
        
        console_puts("\n    Base: 0x");
        console_put_hex(bar->base_addr);
        console_puts("  Size: 0x");
        console_put_hex(bar->size);
        console_puts(" (");
        
        /* Afficher la taille en format lisible */
        if (bar->size >= 1024 * 1024) {
            console_put_dec(bar->size / (1024 * 1024));
            console_puts(" MB");
        } else if (bar->size >= 1024) {
            console_put_dec(bar->size / 1024);
            console_puts(" KB");
        } else {
            console_put_dec(bar->size);
            console_puts(" B");
        }
        console_puts(")\n");
    }
    
    console_puts("\n");
}
