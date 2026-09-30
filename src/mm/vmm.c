/* src/mm/vmm.c - Virtual Memory Manager for x86-64 */
#include "vmm.h"
#include "pmm.h"
#include "../kernel/console.h"
#include "../kernel/klog.h"
#include "../kernel/process.h"
#include "../include/memlayout.h"
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
static inline void* hhdm_phys_to_virt(uint64_t phys)
{
    return (void*)(phys + hhdm_offset);
}

/**
 * Convertit une adresse virtuelle en physique.
 */
static inline uint64_t hhdm_virt_to_phys(void* virt)
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
        if (table[index] & PAGE_HUGE) {
            return NULL;
        }
        /* Table existe déjà - ajouter PAGE_USER si demandé */
        if (flags & PAGE_USER) {
            table[index] |= PAGE_USER;
        }
        uint64_t phys = table[index] & PAGE_FRAME_MASK;
        return (page_entry_t*)hhdm_phys_to_virt(phys);
    }
    
    /* Créer une nouvelle table */
    page_entry_t* new_table = alloc_table();
    if (new_table == NULL) {
        /* Limiter le spam de logs */
        static int alloc_fail_count = 0;
        if (alloc_fail_count < 3) {
            KLOG_ERROR("VMM", "get_or_create_table: alloc_table failed!");
            KLOG_ERROR_HEX("VMM", "  PMM free blocks: ", (uint32_t)pmm_get_free_blocks());
            alloc_fail_count++;
        }
        return NULL;
    }
    
    /* Ajouter l'entrée */
    uint64_t phys = hhdm_virt_to_phys(new_table);
    table[index] = phys | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
    
    KLOG_DEBUG_HEX("VMM", "  Created table at phys=", (uint32_t)phys);
    
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
    return (page_entry_t*)hhdm_phys_to_virt(phys);
}

static int clone_supervisor_mappings(page_entry_t *dst_pml4,
                                     page_entry_t *src_pml4) {
    for (int i = 0; i < 256; i++) {
        if (!(src_pml4[i] & PAGE_PRESENT)) continue;

        page_entry_t *src_pdpt = get_table(src_pml4, i);
        if (src_pdpt == NULL) continue;
        page_entry_t *dst_pdpt = alloc_table();
        if (dst_pdpt == NULL) return -1;
        dst_pml4[i] = hhdm_virt_to_phys(dst_pdpt) |
                      (src_pml4[i] & 0xFFF) | PAGE_PRESENT;
        dst_pml4[i] &= ~PAGE_USER;

        for (int j = 0; j < 512; j++) {
            if (!(src_pdpt[j] & PAGE_PRESENT)) continue;
            if (src_pdpt[j] & PAGE_HUGE) {
                if (!(src_pdpt[j] & PAGE_USER)) {
                    dst_pdpt[j] = src_pdpt[j] & ~PAGE_OWNED;
                }
                continue;
            }

            page_entry_t *src_pd = get_table(src_pdpt, j);
            if (src_pd == NULL) continue;
            page_entry_t *dst_pd = alloc_table();
            if (dst_pd == NULL) return -1;
            dst_pdpt[j] = hhdm_virt_to_phys(dst_pd) |
                          (src_pdpt[j] & 0xFFF) | PAGE_PRESENT;
            dst_pdpt[j] &= ~PAGE_USER;

            for (int k = 0; k < 512; k++) {
                if (!(src_pd[k] & PAGE_PRESENT)) continue;
                if (src_pd[k] & PAGE_HUGE) {
                    if (!(src_pd[k] & PAGE_USER)) {
                        dst_pd[k] = src_pd[k] & ~PAGE_OWNED;
                    }
                    continue;
                }

                page_entry_t *src_pt = get_table(src_pd, k);
                if (src_pt == NULL) continue;
                page_entry_t *dst_pt = alloc_table();
                if (dst_pt == NULL) return -1;
                dst_pd[k] = hhdm_virt_to_phys(dst_pt) |
                            (src_pd[k] & 0xFFF) | PAGE_PRESENT;
                dst_pd[k] &= ~PAGE_USER;

                for (int l = 0; l < 512; l++) {
                    if ((src_pt[l] & PAGE_PRESENT) &&
                        !(src_pt[l] & PAGE_USER)) {
                        dst_pt[l] = src_pt[l] & ~PAGE_OWNED;
                    }
                }
            }
        }
    }

    return 0;
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
    kernel_directory.pml4 = (page_entry_t*)hhdm_phys_to_virt(kernel_directory.pml4_phys);
    
    current_directory = &kernel_directory;
    
    KLOG_INFO_HEX("VMM", "Kernel PML4 phys: ", (uint32_t)kernel_directory.pml4_phys);
    KLOG_INFO("VMM", "VMM initialized (using Limine paging)");
}

/**
 * Retourne l'adresse physique du PML4 du kernel.
 * Utilisé par le scheduler pour restaurer le CR3 du kernel.
 */
uint64_t vmm_get_kernel_cr3(void)
{
    return kernel_directory.pml4_phys;
}

void vmm_map_page(uint64_t phys, uint64_t virt, uint64_t flags)
{
    if (vmm_map_page_in_dir(current_directory, PAGE_ALIGN_DOWN(phys),
                            PAGE_ALIGN_DOWN(virt), flags | PAGE_PRESENT) != 0) {
        KLOG_ERROR("VMM", "Failed to map page");
    }
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

void vmm_unmap_page_in_dir(page_directory_t *dir, uint64_t virt)
{
    if (dir == NULL) {
        return;
    }

    page_directory_t *saved = current_directory;
    current_directory = dir;
    vmm_unmap_page(virt);
    current_directory = saved;
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

int vmm_query_mapping(page_directory_t* dir, uint64_t virt,
                      vmm_mapping_info_t* info)
{
    if (dir == NULL || dir->pml4 == NULL || info == NULL) {
        return -1;
    }

    info->physical_address = 0;
    info->raw_entry = 0;
    info->page_size = 0;
    info->level = 0;

    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);

    if (!(dir->pml4[pml4_idx] & PAGE_PRESENT)) {
        return -1;
    }

    page_entry_t* pdpt = get_table(dir->pml4, pml4_idx);
    if (pdpt == NULL || !(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        return -1;
    }

    page_entry_t pdpte = pdpt[pdpt_idx];
    if (pdpte & PAGE_HUGE) {
        /* 1 GiB page: physical base bits 30..51. */
        uint64_t base = pdpte & 0x000FFFFFC0000000ULL;
        info->physical_address = base + (virt & 0x3FFFFFFFULL);
        info->raw_entry = pdpte;
        info->page_size = 1ULL << 30;
        info->level = 3;
        return 0;
    }

    page_entry_t* pd = get_table(pdpt, pdpt_idx);
    if (pd == NULL || !(pd[pd_idx] & PAGE_PRESENT)) {
        return -1;
    }

    page_entry_t pde = pd[pd_idx];
    if (pde & PAGE_HUGE) {
        /* 2 MiB page: physical base bits 21..51. */
        uint64_t base = pde & 0x000FFFFFFFE00000ULL;
        info->physical_address = base + (virt & 0x1FFFFFULL);
        info->raw_entry = pde;
        info->page_size = 1ULL << 21;
        info->level = 2;
        return 0;
    }

    page_entry_t* pt = get_table(pd, pd_idx);
    if (pt == NULL || !(pt[pt_idx] & PAGE_PRESENT)) {
        return -1;
    }

    page_entry_t pte = pt[pt_idx];
    info->physical_address =
        (pte & PAGE_FRAME_MASK) + (virt & (PAGE_SIZE - 1));
    info->raw_entry = pte;
    info->page_size = PAGE_SIZE;
    info->level = 1;
    return 0;
}

uint8_t vmm_mapping_pat_index(const vmm_mapping_info_t* info)
{
    if (info == NULL || info->page_size == 0) {
        return 0;
    }

    uint8_t index = 0;

    /* PAT index = PAT:PCD:PWT. */
    if (info->raw_entry & PAGE_WRITETHROUGH) {
        index |= 0x1;
    }
    if (info->raw_entry & PAGE_NOCACHE) {
        index |= 0x2;
    }

    uint64_t pat_bit =
        (info->page_size == PAGE_SIZE) ? PAGE_PAT_4K : PAGE_PAT_HUGE;
    if (info->raw_entry & pat_bit) {
        index |= 0x4;
    }

    return index;
}

uint64_t vmm_pat_index_to_4k_flags(uint8_t pat_index)
{
    uint64_t flags = 0;
    pat_index &= 0x7;

    if (pat_index & 0x1) {
        flags |= PAGE_WRITETHROUGH;
    }
    if (pat_index & 0x2) {
        flags |= PAGE_NOCACHE;
    }
    if (pat_index & 0x4) {
        flags |= PAGE_PAT_4K;
    }

    return flags;
}

uint64_t vmm_get_physical(uint64_t virt)
{
    if (current_directory == NULL) {
        return 0;
    }

    vmm_mapping_info_t info;
    if (vmm_query_mapping(current_directory, virt, &info) != 0) {
        return 0;
    }
    return info.physical_address;
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
    /* Get current RSP for debugging */
    uint64_t current_rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));
    
    /* Log to serial only to avoid recursive page faults on VGA */
    KLOG_ERROR_HEX("VMM", "PAGE FAULT at (high): ", (uint32_t)(fault_addr >> 32));
    KLOG_ERROR_HEX("VMM", "PAGE FAULT at (low): ", (uint32_t)fault_addr);
    KLOG_ERROR_HEX("VMM", "Error code: ", (uint32_t)error_code);
    KLOG_ERROR("VMM", (error_code & 0x1) ? "  - Page-level protection violation" : "  - Non-present page");
    KLOG_ERROR("VMM", (error_code & 0x2) ? "  - Write access" : "  - Read access");
    KLOG_ERROR("VMM", (error_code & 0x4) ? "  - User mode" : "  - Supervisor mode");
    if (error_code & 0x8) {
        KLOG_ERROR("VMM", "  - Reserved bit set");
    }
    if (error_code & 0x10) {
        KLOG_ERROR("VMM", "  - Instruction fetch");
    }
    /* Safety net: If this was a user mode fault, terminate the process instead of halting */
    if (error_code & 0x4) {
        extern void process_terminate_fault(uint64_t int_no, uint64_t rip, uint64_t fault_addr,
                                             uint64_t error_code) __attribute__((noreturn));
        process_terminate_fault(14, 0, fault_addr, error_code);
    }
    
    KLOG_ERROR("VMM", "System halted.");
    
    /* Halt (kernel mode fault only) */
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
    dir->pml4_phys = hhdm_virt_to_phys(pml4);
    
    /* Copier les entrées kernel (higher half: indices 256-511) */
    for (int i = 256; i < 512; i++) {
        pml4[i] = kernel_directory.pml4[i];
    }
    
    /*
     * Les mappings kernel/MMIO de la moitié basse doivent rester accessibles
     * pendant les syscalls, mais leurs tables ne peuvent pas être partagées
     * avec les mappings user du processus. Copier les structures de tables
     * supervisor évite qu'un mapping user modifie le PML4 kernel. Les pages
     * physiques kernel/MMIO restent partagées et ne portent jamais PAGE_OWNED.
     */
    if (clone_supervisor_mappings(pml4, kernel_directory.pml4) != 0) {
        vmm_free_directory(dir);
        return NULL;
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
            if (pdpt[j] & PAGE_HUGE) continue;
            
            page_entry_t* pd = get_table(pdpt, j);
            if (pd == NULL) continue;
            
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PAGE_PRESENT)) continue;
                if (pd[k] & PAGE_HUGE) continue; /* Skip huge pages */
                
                page_entry_t* pt = get_table(pd, k);
                if (pt != NULL) {
                    for (int l = 0; l < 512; l++) {
                        page_entry_t pte = pt[l];
                        if (pte & PAGE_OWNED) {
                            void *page =
                                hhdm_phys_to_virt(pte & PAGE_FRAME_MASK);
                            pmm_free_block(page);
                        }
                    }
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
    vmm_mapping_info_t info;
    if (vmm_query_mapping(dir, virt_addr, &info) != 0) {
        return 0;
    }
    return info.physical_address;
}

int vmm_map_page_in_dir(page_directory_t* dir, uint64_t phys, uint64_t virt, uint64_t flags)
{
    if (dir == NULL || dir->pml4 == NULL ||
        (phys & (PAGE_SIZE - 1)) || (virt & (PAGE_SIZE - 1))) {
        return -1;
    }

    page_entry_t *pdpt =
        get_or_create_table(dir->pml4, PML4_INDEX(virt), flags);
    if (pdpt == NULL) return -1;
    page_entry_t *pd = get_or_create_table(pdpt, PDPT_INDEX(virt), flags);
    if (pd == NULL) return -1;
    page_entry_t *pt = get_or_create_table(pd, PD_INDEX(virt), flags);
    if (pt == NULL) return -1;
    pt[PT_INDEX(virt)] = phys | (flags & (0xFFF | PAGE_NX));
    invlpg(virt);
    return 0;
}

static page_entry_t *page_entry_at(page_directory_t *dir, uint64_t virt,
                                   uint64_t *next) {
    *next = (virt | ((1ULL << 39) - 1)) + 1;
    if (dir == NULL || dir->pml4 == NULL) return NULL;
    page_entry_t *pdpt = get_table(dir->pml4, PML4_INDEX(virt));
    if (pdpt == NULL) return NULL;
    *next = (virt | ((1ULL << 30) - 1)) + 1;
    if (pdpt[PDPT_INDEX(virt)] & PAGE_HUGE) return NULL;
    page_entry_t *pd = get_table(pdpt, PDPT_INDEX(virt));
    if (pd == NULL) return NULL;
    *next = (virt | ((1ULL << 21) - 1)) + 1;
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) return NULL;
    page_entry_t *pt = get_table(pd, PD_INDEX(virt));
    if (pt == NULL) return NULL;
    *next = PAGE_ALIGN_DOWN(virt) + PAGE_SIZE;
    return &pt[PT_INDEX(virt)];
}

uint64_t vmm_get_page_entry(page_directory_t *dir, uint64_t virt) {
    uint64_t next;
    page_entry_t *pte = page_entry_at(dir, virt, &next);
    return pte != NULL ? *pte : 0;
}

static bool table_empty(page_entry_t *table) {
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (table[i] != 0) return false;
    }
    return true;
}

static void prune_empty_tables(page_directory_t *dir, uint64_t start,
                                uint64_t end) {
    for (uint64_t i = PML4_INDEX(start); i <= PML4_INDEX(end - 1); i++) {
        page_entry_t *pdpt = get_table(dir->pml4, i);
        if (pdpt == NULL) continue;
        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            if (pdpt[j] & PAGE_HUGE) continue;
            page_entry_t *pd = get_table(pdpt, j);
            if (pd == NULL) continue;
            for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                if (pd[k] & PAGE_HUGE) continue;
                page_entry_t *pt = get_table(pd, k);
                if (pt != NULL && table_empty(pt)) {
                    pd[k] = 0;
                    free_table(pt);
                }
            }
            if (table_empty(pd)) {
                pdpt[j] = 0;
                free_table(pd);
            }
        }
        if (table_empty(pdpt)) {
            dir->pml4[i] = 0;
            free_table(pdpt);
        }
    }
    if ((read_cr3() & PAGE_FRAME_MASK) == dir->pml4_phys) flush_tlb();
}

void vmm_update_range(page_directory_t *dir, uint64_t start, uint64_t end,
                      uint64_t flags, bool release) {
    for (uint64_t address = start; address < end;) {
        uint64_t next;
        page_entry_t *pte = page_entry_at(dir, address, &next);
        if (pte != NULL && *pte != 0) {
            uint64_t old = *pte;
            if (release) {
                *pte = 0;
            } else {
                *pte = (old & ~(PAGE_PRESENT | PAGE_RW | PAGE_NX)) |
                       (flags & (PAGE_PRESENT | PAGE_RW | PAGE_NX));
            }

            invlpg(address);
            if (release && (old & PAGE_OWNED)) {
                pmm_free_block(hhdm_phys_to_virt(old & PAGE_FRAME_MASK));
            }
        }
        address = next;
    }
    if (release && end > start) prune_empty_tables(dir, start, end);
}

uint64_t vmm_resident_pages(page_directory_t *dir, uint64_t start, uint64_t end) {
    uint64_t count = 0;
    for (uint64_t address = start; address < end;) {
        uint64_t next;
        page_entry_t *pte = page_entry_at(dir, address, &next);
        if (pte != NULL && (*pte & PAGE_FRAME_MASK)) count++;
        address = next;
    }
    return count;
}

uint64_t vmm_first_occupied_end(page_directory_t *dir, uint64_t start,
                                uint64_t end) {
    for (uint64_t address = start; address < end;) {
        uint64_t next;
        page_entry_t *pte = page_entry_at(dir, address, &next);
        if (pte != NULL && *pte != 0) return next;
        if (pte == NULL) {
            vmm_mapping_info_t info;
            if (vmm_query_mapping(dir, address, &info) == 0) return next;
        }
        address = next;
    }
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
    
    /* Copier profondément les pages user détenues. Les mappings user externes
     * (par exemple le framebuffer) restent partagés et sans PAGE_OWNED. */
    for (int i = 0; i < 256; i++) {
        if (!(src->pml4[i] & PAGE_PRESENT)) continue;
        page_entry_t *src_pdpt = get_table(src->pml4, i);
        if (src_pdpt == NULL) continue;

        for (int j = 0; j < 512; j++) {
            if (!(src_pdpt[j] & PAGE_PRESENT)) continue;
            if (src_pdpt[j] & PAGE_HUGE) {
                if (src_pdpt[j] & PAGE_USER) goto fail;
                continue;
            }

            page_entry_t *src_pd = get_table(src_pdpt, j);
            if (src_pd == NULL) continue;
            for (int k = 0; k < 512; k++) {
                if (!(src_pd[k] & PAGE_PRESENT)) continue;
                if (src_pd[k] & PAGE_HUGE) {
                    if (src_pd[k] & PAGE_USER) goto fail;
                    continue;
                }

                page_entry_t *src_pt = get_table(src_pd, k);
                if (src_pt == NULL) continue;
                for (int l = 0; l < 512; l++) {
                    page_entry_t pte = src_pt[l];
                    if (pte == 0 || !(pte & PAGE_USER)) continue;

                    uint64_t virt = ((uint64_t)i << 39) |
                                    ((uint64_t)j << 30) |
                                    ((uint64_t)k << 21) |
                                    ((uint64_t)l << 12);
                    uint64_t phys = pte & PAGE_FRAME_MASK;
                    uint64_t flags = pte & (0xFFF | PAGE_NX);

                    if (pte & PAGE_OWNED) {
                        void *new_page = pmm_alloc_block();
                        if (new_page == NULL) goto fail;
                        void *source_page = hhdm_phys_to_virt(phys);
                        uint8_t *dst_bytes = (uint8_t *)new_page;
                        uint8_t *src_bytes = (uint8_t *)source_page;
                        for (uint64_t n = 0; n < PAGE_SIZE; n++) {
                            dst_bytes[n] = src_bytes[n];
                        }
                        phys = hhdm_virt_to_phys(new_page);
                        flags |= PAGE_OWNED;
                    } else {
                        flags &= ~PAGE_OWNED;
                    }

                    if (vmm_map_page_in_dir(dst, phys, virt, flags) != 0) {
                        if (flags & PAGE_OWNED) {
                            pmm_free_block(hhdm_phys_to_virt(phys));
                        }
                        goto fail;
                    }
                }
            }
        }
    }
    
    return dst;

fail:
    vmm_free_directory(dst);
    return NULL;
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
        
        uint8_t* dst_ptr = (uint8_t*)hhdm_phys_to_virt(phys) + offset;
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
    
    KLOG_DEBUG_HEX("VMM", "memset_in_dir: dst_virt=", (uint32_t)dst_virt);
    KLOG_DEBUG_HEX("VMM", "  size=", (uint32_t)size);
    
    uint64_t remaining = size;
    uint64_t current_virt = dst_virt;
    
    while (remaining > 0) {
        uint64_t page_virt = PAGE_ALIGN_DOWN(current_virt);
        uint64_t offset = current_virt - page_virt;
        uint64_t phys = vmm_get_phys_addr(dir, page_virt);
        
        KLOG_DEBUG_HEX("VMM", "  page_virt=", (uint32_t)page_virt);
        KLOG_DEBUG_HEX("VMM", "  phys=", (uint32_t)phys);
        
        if (phys == 0) {
            KLOG_ERROR("VMM", "memset_in_dir: phys=0!");
            return -1;
        }
        
        uint64_t bytes_in_page = PAGE_SIZE - offset;
        uint64_t to_write = (remaining < bytes_in_page) ? remaining : bytes_in_page;
        
        void* virt_ptr = hhdm_phys_to_virt(phys);
        KLOG_DEBUG_HEX("VMM", "  virt_ptr (high)=", (uint32_t)((uint64_t)virt_ptr >> 32));
        KLOG_DEBUG_HEX("VMM", "  virt_ptr (low)=", (uint32_t)(uint64_t)virt_ptr);
        
        uint8_t* dst_ptr = (uint8_t*)virt_ptr + offset;
        for (uint64_t i = 0; i < to_write; i++) {
            dst_ptr[i] = value;
        }
        
        current_virt += to_write;
        remaining -= to_write;
    }
    
    return 0;
}

/* ========================================
 * Helpers HHDM (exports)
 * ======================================== */

void* vmm_phys_to_virt(uint64_t phys)
{
    return hhdm_phys_to_virt(phys);
}

uint64_t vmm_virt_to_phys(void* virt)
{
    return hhdm_virt_to_phys(virt);
}
