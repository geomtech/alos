/* src/kernel/mmio/mmio.c - Implémentation de l'abstraction MMIO
 *
 * Ce fichier implémente les fonctions de mapping MMIO (ioremap/iounmap)
 * et la gestion des régions MMIO pour éviter les conflits.
 */

#include "mmio.h"
#include "../../mm/vmm.h"
#include "../../mm/kheap.h"
#include "../klog.h"
#include "../console.h"

/* ========================================
 * Variables globales
 * ======================================== */

/* Liste chaînée des régions MMIO enregistrées */
static mmio_region_t* mmio_regions = NULL;

/* Compteur de régions pour statistiques */
static int mmio_region_count = 0;

/* Adresse de base pour les mappings MMIO dynamiques
 * On utilise une zone au-dessus de l'identity mapping (16 Mo)
 * mais en dessous de l'espace utilisateur.
 * 
 * Zone MMIO: 0x01000000 - 0x10000000 (16 Mo - 256 Mo)
 * Cela donne 240 Mo d'espace pour les mappings MMIO.
 */
#define MMIO_VIRT_BASE  0x01000000  /* 16 Mo */
#define MMIO_VIRT_END   0x10000000  /* 256 Mo */

/* Prochain slot disponible pour mapping MMIO */
static uint32_t mmio_next_virt = MMIO_VIRT_BASE;

/* Flag d'initialisation */
static bool mmio_initialized = false;

/* ========================================
 * Fonctions internes
 * ======================================== */

/**
 * Aligne une adresse vers le bas sur PAGE_SIZE.
 */
static inline uint32_t mmio_align_down(uint32_t addr)
{
    return addr & ~(PAGE_SIZE - 1);
}

/**
 * Aligne une adresse vers le haut sur PAGE_SIZE.
 */
static inline uint32_t mmio_align_up(uint32_t addr)
{
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/**
 * Vérifie si deux régions se chevauchent.
 */
static bool mmio_regions_overlap(uint32_t start1, uint32_t size1,
                                  uint32_t start2, uint32_t size2)
{
    uint32_t end1 = start1 + size1;
    uint32_t end2 = start2 + size2;
    
    return (start1 < end2) && (start2 < end1);
}

/**
 * Trouve une région MMIO par son adresse virtuelle.
 */
static mmio_region_t* mmio_find_region_by_virt(uint32_t virt_addr)
{
    mmio_region_t* region = mmio_regions;
    
    while (region != NULL) {
        if (region->virt_addr == virt_addr) {
            return region;
        }
        region = region->next;
    }
    
    return NULL;
}

/**
 * Vérifie si une région physique est déjà mappée.
 */
static mmio_region_t* mmio_find_region_by_phys(uint32_t phys_addr, uint32_t size)
{
    mmio_region_t* region = mmio_regions;
    
    while (region != NULL) {
        if (mmio_regions_overlap(region->phys_addr, region->size, 
                                  phys_addr, size)) {
            return region;
        }
        region = region->next;
    }
    
    return NULL;
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

void mmio_init(void)
{
    if (mmio_initialized) {
        return;
    }
    
    KLOG_INFO("MMIO", "=== MMIO Subsystem Initialization ===");
    KLOG_INFO_HEX("MMIO", "Virtual address range: ", MMIO_VIRT_BASE);
    KLOG_INFO_HEX("MMIO", "                   to: ", MMIO_VIRT_END);
    
    mmio_regions = NULL;
    mmio_region_count = 0;
    mmio_next_virt = MMIO_VIRT_BASE;
    mmio_initialized = true;
    
    KLOG_INFO("MMIO", "MMIO subsystem initialized");
}

mmio_addr_t ioremap(uint32_t phys_addr, uint32_t size)
{
    /* Flags par défaut: non-cachable, read/write */
    return ioremap_flags(phys_addr, size, PAGE_NOCACHE);
}

mmio_addr_t ioremap_flags(uint32_t phys_addr, uint32_t size, uint32_t flags)
{
    if (!mmio_initialized) {
        KLOG_ERROR("MMIO", "ioremap called before mmio_init!");
        return NULL;
    }
    
    if (size == 0) {
        KLOG_ERROR("MMIO", "ioremap: size cannot be 0");
        return NULL;
    }
    
    /* Calculer l'offset dans la page */
    uint32_t offset = phys_addr & (PAGE_SIZE - 1);
    
    /* Aligner l'adresse physique vers le bas */
    uint32_t phys_aligned = mmio_align_down(phys_addr);
    
    /* Calculer la taille alignée (incluant l'offset) */
    uint32_t size_aligned = mmio_align_up(size + offset);
    
    /* Vérifier qu'on a assez d'espace virtuel */
    if (mmio_next_virt + size_aligned > MMIO_VIRT_END) {
        KLOG_ERROR("MMIO", "ioremap: out of virtual address space!");
        return NULL;
    }
    
    /* Vérifier les conflits avec les régions existantes */
    mmio_region_t* existing = mmio_find_region_by_phys(phys_aligned, size_aligned);
    if (existing != NULL) {
        /* Région déjà mappée - retourner le mapping existant si compatible */
        if (existing->phys_addr == phys_aligned && 
            existing->size >= size_aligned) {
            KLOG_INFO("MMIO", "ioremap: reusing existing mapping");
            return (mmio_addr_t)(existing->virt_addr + offset);
        }
        KLOG_ERROR("MMIO", "ioremap: conflicting region exists!");
        return NULL;
    }
    
    /* Allouer l'adresse virtuelle */
    uint32_t virt_addr = mmio_next_virt;
    mmio_next_virt += size_aligned;
    
    /* Mapper chaque page avec les attributs MMIO */
    uint32_t page_flags = PAGE_PRESENT | PAGE_RW | flags;
    
    for (uint32_t off = 0; off < size_aligned; off += PAGE_SIZE) {
        vmm_map_page(phys_aligned + off, virt_addr + off, page_flags);
    }
    
    /* Enregistrer la région */
    mmio_register_region(phys_aligned, virt_addr, size_aligned, "ioremap");
    
    KLOG_INFO_HEX("MMIO", "ioremap: phys ", phys_addr);
    KLOG_INFO_HEX("MMIO", "      -> virt ", virt_addr + offset);
    KLOG_INFO_HEX("MMIO", "         size ", size);
    
    /* Retourner l'adresse virtuelle avec l'offset original */
    return (mmio_addr_t)(virt_addr + offset);
}

void iounmap(mmio_addr_t virt_addr, uint32_t size)
{
    if (virt_addr == NULL) {
        return;
    }
    
    /* Aligner l'adresse vers le bas */
    uint32_t virt = (uint32_t)(uintptr_t)virt_addr;
    uint32_t virt_aligned = mmio_align_down(virt);
    
    /* Trouver la région */
    mmio_region_t* region = mmio_find_region_by_virt(virt_aligned);
    if (region == NULL) {
        KLOG_WARN("MMIO", "iounmap: region not found");
        return;
    }
    
    /* Unmapper chaque page */
    for (uint32_t off = 0; off < region->size; off += PAGE_SIZE) {
        vmm_unmap_page(region->virt_addr + off);
    }
    
    /* Désenregistrer la région */
    mmio_unregister_region(region->virt_addr);
    
    KLOG_INFO_HEX("MMIO", "iounmap: freed region at ", virt_aligned);
}

int mmio_register_region(uint32_t phys_addr, uint32_t virt_addr,
                         uint32_t size, const char* name)
{
    /* Vérifier les conflits */
    mmio_region_t* existing = mmio_find_region_by_phys(phys_addr, size);
    if (existing != NULL) {
        KLOG_ERROR("MMIO", "register_region: conflict detected!");
        return -1;
    }
    
    /* Allouer une nouvelle structure de région */
    mmio_region_t* region = (mmio_region_t*)kmalloc(sizeof(mmio_region_t));
    if (region == NULL) {
        KLOG_ERROR("MMIO", "register_region: out of memory");
        return -1;
    }
    
    /* Initialiser la région */
    region->phys_addr = phys_addr;
    region->virt_addr = virt_addr;
    region->size = size;
    region->flags = 0;
    region->name = name;
    region->next = NULL;
    
    /* Ajouter à la liste */
    if (mmio_regions == NULL) {
        mmio_regions = region;
    } else {
        mmio_region_t* last = mmio_regions;
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = region;
    }
    
    mmio_region_count++;
    
    return 0;
}

void mmio_unregister_region(uint32_t virt_addr)
{
    mmio_region_t* prev = NULL;
    mmio_region_t* current = mmio_regions;
    
    while (current != NULL) {
        if (current->virt_addr == virt_addr) {
            /* Retirer de la liste */
            if (prev == NULL) {
                mmio_regions = current->next;
            } else {
                prev->next = current->next;
            }
            
            /* Libérer la mémoire */
            kfree(current);
            mmio_region_count--;
            
            return;
        }
        
        prev = current;
        current = current->next;
    }
}

bool mmio_is_mmio_address(uint32_t phys_addr)
{
    mmio_region_t* region = mmio_regions;
    
    while (region != NULL) {
        if (phys_addr >= region->phys_addr &&
            phys_addr < region->phys_addr + region->size) {
            return true;
        }
        region = region->next;
    }
    
    return false;
}

void mmio_dump_regions(void)
{
    console_puts("\n=== MMIO Regions ===\n");
    console_puts("Count: ");
    console_put_dec(mmio_region_count);
    console_puts("\n\n");
    
    mmio_region_t* region = mmio_regions;
    int idx = 0;
    
    while (region != NULL) {
        console_puts("[");
        console_put_dec(idx);
        console_puts("] ");
        console_puts(region->name ? region->name : "(unnamed)");
        console_puts("\n    Phys: 0x");
        console_put_hex(region->phys_addr);
        console_puts(" -> Virt: 0x");
        console_put_hex(region->virt_addr);
        console_puts("\n    Size: 0x");
        console_put_hex(region->size);
        console_puts(" (");
        console_put_dec(region->size);
        console_puts(" bytes)\n");
        
        region = region->next;
        idx++;
    }
    
    console_puts("\n");
}
