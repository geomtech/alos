/* src/mm/vmm.c - Virtual Memory Manager for x86-64 */
#include "vmm.h"
#include "pmm.h"
#include "../kernel/console.h"
#include "../kernel/klog.h"
#include "../arch/x86_64/cpu.h"

/* ========================================
 * Variables globales
 * ======================================== */

/* HHDM offset (fourni par Limine) */
static uint64_t hhdm_offset = 0;

/* Kernel page directory */
static page_directory_t kernel_directory;

/* Current page directory */
static page_directory_t *current_directory = NULL;

/* ========================================
 * Fonctions internes
 * ======================================== */

/**
 * Convertit une adresse physique en virtuelle via HHDM.
 */
static inline void* phys_to_virt(uint64_t phys)
{
    return (void*)(phys + hhdm_offset);
}

/**
 * Convertit une adresse virtuelle en physique.
 */
static inline uint64_t virt_to_phys(void* virt)
{
    return (uint64_t)virt - hhdm_offset;
}

/**
 * Alloue une table de pages (4 KiB, alignée).
 * Retourne l'adresse virtuelle (via HHDM).
 */
static page_entry_t* alloc_table(void)
{
    void* table = pmm_alloc_block();
    if (table == NULL) {
        return NULL;
    }
    
    /* Mettre à zéro */
    uint64_t *p = (uint64_t*)table;
    for (int i = 0; i < 512; i++) {
        p[i] = 0;
    }
    
    return (page_entry_t*)table;
}

/**
 * Libère une table de pages.
 */
static void free_table(page_entry_t* table)
{
    pmm_free_block(table);
}

/**
 * Obtient ou crée une entrée de table.
 * Retourne l'adresse virtuelle de la table suivante.
 */
static page_entry_t* get_or_create_table(page_entry_t* table, uint64_t index, uint64_t flags)
{
    if (table[index] & PAGE_PRESENT) {
        /* Table existe déjà */
        uint64_t phys = table[index] & PAGE_FRAME_MASK;
        return (page_entry_t*)phys_to_virt(phys);
    }
    
    /* Créer une nouvelle table */
    page_entry_t* new_table = alloc_table();
    if (new_table == NULL) {
        return NULL;
    }
    
    /* Ajouter l'entrée */
    uint64_t phys = virt_to_phys(new_table);
    table[index] = phys | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
    
    return new_table;
}

/**
 * Obtient une table existante (sans créer).
 */
static page_entry_t* get_table(page_entry_t* table, uint64_t index)
{
    if (!(table[index] & PAGE_PRESENT)) {
        return NULL;
    }
    uint64_t phys = table[index] & PAGE_FRAME_MASK;
    return (page_entry_t*)phys_to_virt(phys);
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

void vmm_init(void)
{
    KLOG_INFO("VMM", "=== Virtual Memory Manager (x86-64) ===");
    
    /* Obtenir l'offset HHDM depuis le kernel */
    extern uint64_t get_hhdm_offset(void);
    hhdm_offset = get_hhdm_offset();
    
    KLOG_INFO_HEX("VMM", "HHDM offset (high): ", (uint32_t)(hhdm_offset >> 32));
    KLOG_INFO_HEX("VMM", "HHDM offset (low): ", (uint32_t)hhdm_offset);
    
    /* Lire le PML4 actuel (configuré par Limine) */
    uint64_t cr3 = read_cr3();
    kernel_directory.pml4_phys = cr3 & PAGE_FRAME_MASK;
    kernel_directory.pml4 = (page_entry_t*)phys_to_virt(kernel_directory.pml4_phys);
    
    current_directory = &kernel_directory;
    
    KLOG_INFO_HEX("VMM", "Kernel PML4 phys: ", (uint32_t)kernel_directory.pml4_phys);
    KLOG_INFO("VMM", "VMM initialized (using Limine paging)");
}

void vmm_map_page(uint64_t phys, uint64_t virt, uint64_t flags)
{
    /* Aligner les adresses */
    phys = PAGE_ALIGN_DOWN(phys);
    virt = PAGE_ALIGN_DOWN(virt);
    
    /* Extraire les index */
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);
    
    /* Traverser/créer les tables */
    page_entry_t* pml4 = current_directory->pml4;
    
    page_entry_t* pdpt = get_or_create_table(pml4, pml4_idx, flags);
    if (pdpt == NULL) {
        KLOG_ERROR("VMM", "Failed to allocate PDPT");
        return;
    }
    
    page_entry_t* pd = get_or_create_table(pdpt, pdpt_idx, flags);
    if (pd == NULL) {
        KLOG_ERROR("VMM", "Failed to allocate PD");
        return;
    }
    
    page_entry_t* pt = get_or_create_table(pd, pd_idx, flags);
    if (pt == NULL) {
        KLOG_ERROR("VMM", "Failed to allocate PT");
        return;
    }
    
    /* Mapper la page */
    pt[pt_idx] = phys | (flags & 0xFFF) | PAGE_PRESENT;
    
    /* Invalider le TLB */
    invlpg(virt);
}

void vmm_unmap_page(uint64_t virt)
{
    virt = PAGE_ALIGN_DOWN(virt);
    
    /* Extraire les index */
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);
    
    /* Traverser les tables */
    page_entry_t* pml4 = current_directory->pml4;
    
    page_entry_t* pdpt = get_table(pml4, pml4_idx);
    if (pdpt == NULL) return;
    
    page_entry_t* pd = get_table(pdpt, pdpt_idx);
    if (pd == NULL) return;
    
    page_entry_t* pt = get_table(pd, pd_idx);
    if (pt == NULL) return;
    
    /* Effacer l'entrée */
    pt[pt_idx] = 0;
    
    /* Invalider le TLB */
    invlpg(virt);
}

int vmm_switch_directory(page_directory_t* dir)
{
    if (dir == NULL) {
        return -1;
    }
    
    current_directory = dir;
    write_cr3(dir->pml4_phys);
    
    return 0;
}

page_directory_t* vmm_get_directory(void)
{
    return current_directory;
}

uint64_t vmm_get_physical(uint64_t virt)
{
    virt = PAGE_ALIGN_DOWN(virt);
    
    /* Extraire les index */
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);
    
    /* Traverser les tables */
    page_entry_t* pml4 = current_directory->pml4;
    
    page_entry_t* pdpt = get_table(pml4, pml4_idx);
    if (pdpt == NULL) return 0;
    
    page_entry_t* pd = get_table(pdpt, pdpt_idx);
    if (pd == NULL) return 0;
    
    /* Vérifier si c'est une huge page (2MB) */
    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & PAGE_FRAME_MASK) + (virt & 0x1FFFFF);
    }
    
    page_entry_t* pt = get_table(pd, pd_idx);
    if (pt == NULL) return 0;
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    return (pt[pt_idx] & PAGE_FRAME_MASK) + PAGE_OFFSET(virt);
}

bool vmm_is_mapped(uint64_t virt)
{
    return vmm_get_physical(virt) != 0;
}

void vmm_set_user_accessible(uint64_t start, uint64_t size)
{
    start = PAGE_ALIGN_DOWN(start);
    uint64_t end = PAGE_ALIGN_UP(start + size);
    
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        /* Extraire les index */
        uint64_t pml4_idx = PML4_INDEX(addr);
        uint64_t pdpt_idx = PDPT_INDEX(addr);
        uint64_t pd_idx = PD_INDEX(addr);
        uint64_t pt_idx = PT_INDEX(addr);
        
        page_entry_t* pml4 = current_directory->pml4;
        
        /* Ajouter USER flag à tous les niveaux */
        if (pml4[pml4_idx] & PAGE_PRESENT) {
            pml4[pml4_idx] |= PAGE_USER;
            
            page_entry_t* pdpt = get_table(pml4, pml4_idx);
            if (pdpt && (pdpt[pdpt_idx] & PAGE_PRESENT)) {
                pdpt[pdpt_idx] |= PAGE_USER;
                
                page_entry_t* pd = get_table(pdpt, pdpt_idx);
                if (pd && (pd[pd_idx] & PAGE_PRESENT)) {
                    pd[pd_idx] |= PAGE_USER;
                    
                    page_entry_t* pt = get_table(pd, pd_idx);
                    if (pt && (pt[pt_idx] & PAGE_PRESENT)) {
                        pt[pt_idx] |= PAGE_USER;
                    }
                }
            }
        }
        
        invlpg(addr);
    }
}

void vmm_page_fault_handler(uint64_t error_code, uint64_t fault_addr)
{
    console_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    console_puts("\n!!! PAGE FAULT !!!\n");
    
    console_puts("Faulting Address (CR2): 0x");
    /* Print 64-bit address */
    console_put_hex((uint32_t)(fault_addr >> 32));
    console_put_hex((uint32_t)fault_addr);
    console_puts("\n");
    
    console_puts("Error Code: 0x");
    console_put_hex((uint32_t)error_code);
    console_puts("\n");
    
    /* Décoder l'error code */
    console_puts("  - ");
    console_puts((error_code & 0x1) ? "Page-level protection violation" : "Non-present page");
    console_puts("\n  - ");
    console_puts((error_code & 0x2) ? "Write access" : "Read access");
    console_puts("\n  - ");
    console_puts((error_code & 0x4) ? "User mode" : "Supervisor mode");
    if (error_code & 0x8) {
        console_puts("\n  - Reserved bit set");
    }
    if (error_code & 0x10) {
        console_puts("\n  - Instruction fetch");
    }
    console_puts("\n");
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Halt */
    console_puts("System halted.\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ========================================
 * Fonctions multi-espaces d'adressage
 * ======================================== */

page_directory_t* vmm_get_kernel_directory(void)
{
    return &kernel_directory;
}

page_directory_t* vmm_create_directory(void)
{
    /* Allouer la structure */
    page_directory_t* dir = (page_directory_t*)pmm_alloc_block();
    if (dir == NULL) {
        return NULL;
    }
    
    /* Allouer le PML4 */
    page_entry_t* pml4 = alloc_table();
    if (pml4 == NULL) {
        pmm_free_block(dir);
        return NULL;
    }
    
    dir->pml4 = pml4;
    dir->pml4_phys = virt_to_phys(pml4);
    
    /* Copier les entrées kernel (higher half: indices 256-511) */
    for (int i = 256; i < 512; i++) {
        pml4[i] = kernel_directory.pml4[i];
    }
    
    KLOG_INFO_HEX("VMM", "Created new PML4 at: ", (uint32_t)dir->pml4_phys);
    
    return dir;
}

void vmm_free_directory(page_directory_t* dir)
{
    if (dir == NULL || dir == &kernel_directory) {
        return;
    }
    
    /* Libérer les tables user (indices 0-255) */
    page_entry_t* pml4 = dir->pml4;
    
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & PAGE_PRESENT)) continue;
        
        page_entry_t* pdpt = get_table(pml4, i);
        if (pdpt == NULL) continue;
        
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & PAGE_PRESENT)) continue;
            
            page_entry_t* pd = get_table(pdpt, j);
            if (pd == NULL) continue;
            
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PAGE_PRESENT)) continue;
                if (pd[k] & PAGE_HUGE) continue; /* Skip huge pages */
                
                page_entry_t* pt = get_table(pd, k);
                if (pt != NULL) {
                    free_table(pt);
                }
            }
            free_table(pd);
        }
        free_table(pdpt);
    }
    
    free_table(pml4);
    pmm_free_block(dir);
}

uint64_t vmm_get_phys_addr(page_directory_t* dir, uint64_t virt_addr)
{
    if (dir == NULL) {
        return 0;
    }
    
    virt_addr = PAGE_ALIGN_DOWN(virt_addr);
    
    uint64_t pml4_idx = PML4_INDEX(virt_addr);
    uint64_t pdpt_idx = PDPT_INDEX(virt_addr);
    uint64_t pd_idx = PD_INDEX(virt_addr);
    uint64_t pt_idx = PT_INDEX(virt_addr);
    
    page_entry_t* pdpt = get_table(dir->pml4, pml4_idx);
    if (pdpt == NULL) return 0;
    
    page_entry_t* pd = get_table(pdpt, pdpt_idx);
    if (pd == NULL) return 0;
    
    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & PAGE_FRAME_MASK) + (virt_addr & 0x1FFFFF);
    }
    
    page_entry_t* pt = get_table(pd, pd_idx);
    if (pt == NULL) return 0;
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    return pt[pt_idx] & PAGE_FRAME_MASK;
}

int vmm_map_page_in_dir(page_directory_t* dir, uint64_t phys, uint64_t virt, uint64_t flags)
{
    if (dir == NULL) {
        return -1;
    }
    
    /* Sauvegarder le directory courant */
    page_directory_t* saved = current_directory;
    current_directory = dir;
    
    vmm_map_page(phys, virt, flags);
    
    /* Restaurer */
    current_directory = saved;
    
    return 0;
}

page_directory_t* vmm_clone_directory(page_directory_t* src)
{
    if (src == NULL) {
        return NULL;
    }
    
    page_directory_t* dst = vmm_create_directory();
    if (dst == NULL) {
        return NULL;
    }
    
    /* Copier les mappings user (indices 0-255) */
    /* Pour l'instant, copie simple des entrées (pas COW) */
    for (int i = 0; i < 256; i++) {
        dst->pml4[i] = src->pml4[i];
    }
    
    return dst;
}

bool vmm_is_mapped_in_dir(page_directory_t* dir, uint64_t virt)
{
    return vmm_get_phys_addr(dir, virt) != 0;
}

int vmm_copy_to_dir(page_directory_t* dir, uint64_t dst_virt, const void* src, uint64_t size)
{
    if (dir == NULL || src == NULL || size == 0) {
        return -1;
    }
    
    const uint8_t* src_ptr = (const uint8_t*)src;
    uint64_t remaining = size;
    uint64_t current_virt = dst_virt;
    
    while (remaining > 0) {
        uint64_t page_virt = PAGE_ALIGN_DOWN(current_virt);
        uint64_t offset = current_virt - page_virt;
        uint64_t phys = vmm_get_phys_addr(dir, page_virt);
        
        if (phys == 0) {
            return -1;
        }
        
        uint64_t bytes_in_page = PAGE_SIZE - offset;
        uint64_t to_copy = (remaining < bytes_in_page) ? remaining : bytes_in_page;
        
        uint8_t* dst_ptr = (uint8_t*)phys_to_virt(phys) + offset;
        for (uint64_t i = 0; i < to_copy; i++) {
            dst_ptr[i] = src_ptr[i];
        }
        
        src_ptr += to_copy;
        current_virt += to_copy;
        remaining -= to_copy;
    }
    
    return 0;
}

int vmm_memset_in_dir(page_directory_t* dir, uint64_t dst_virt, uint8_t value, uint64_t size)
{
    if (dir == NULL || size == 0) {
        return -1;
    }
    
    uint64_t remaining = size;
    uint64_t current_virt = dst_virt;
    
    while (remaining > 0) {
        uint64_t page_virt = PAGE_ALIGN_DOWN(current_virt);
        uint64_t offset = current_virt - page_virt;
        uint64_t phys = vmm_get_phys_addr(dir, page_virt);
        
        if (phys == 0) {
            return -1;
        }
        
        uint64_t bytes_in_page = PAGE_SIZE - offset;
        uint64_t to_write = (remaining < bytes_in_page) ? remaining : bytes_in_page;
        
        uint8_t* dst_ptr = (uint8_t*)phys_to_virt(phys) + offset;
        for (uint64_t i = 0; i < to_write; i++) {
            dst_ptr[i] = value;
        }
        
        current_virt += to_write;
        remaining -= to_write;
    }
    
    return 0;
}

/* ========================================
 * MMIO Mapping
 * ======================================== */

/* Adresse de base pour les mappings MMIO dynamiques */
#define MMIO_VIRT_BASE  0xFFFFFFFF00000000ULL
#define MMIO_VIRT_END   0xFFFFFFFF80000000ULL

static uint64_t mmio_next_virt = MMIO_VIRT_BASE;

volatile void* vmm_map_mmio(uint64_t phys_addr, uint64_t size)
{
    uint64_t phys_aligned = PAGE_ALIGN_DOWN(phys_addr);
    uint64_t offset = phys_addr - phys_aligned;
    
    uint64_t total_size = offset + size;
    uint64_t num_pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    if (mmio_next_virt + (num_pages * PAGE_SIZE) > MMIO_VIRT_END) {
        KLOG_ERROR("VMM", "MMIO space exhausted!");
        return NULL;
    }
    
    uint64_t virt_base = mmio_next_virt;
    
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t phys = phys_aligned + (i * PAGE_SIZE);
        uint64_t virt = virt_base + (i * PAGE_SIZE);
        
        vmm_map_page(phys, virt, PAGE_PRESENT | PAGE_RW | PAGE_NOCACHE);
    }
    
    mmio_next_virt += num_pages * PAGE_SIZE;
    
    return (volatile void*)(virt_base + offset);
}

void vmm_unmap_mmio(volatile void* virt_addr, uint64_t size)
{
    uint64_t virt = (uint64_t)virt_addr;
    uint64_t virt_aligned = PAGE_ALIGN_DOWN(virt);
    uint64_t offset = virt - virt_aligned;
    uint64_t total_size = offset + size;
    uint64_t num_pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (uint64_t i = 0; i < num_pages; i++) {
        vmm_unmap_page(virt_aligned + (i * PAGE_SIZE));
    }
}

/* ========================================
 * Helpers HHDM (exports)
 * ======================================== */

void* vmm_phys_to_virt(uint64_t phys)
{
    return phys_to_virt(phys);
}

uint64_t vmm_virt_to_phys(void* virt)
{
    return virt_to_phys(virt);
}
