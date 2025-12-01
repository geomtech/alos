/* src/mm/vmm.c - Virtual Memory Manager Implementation */
#include "vmm.h"
#include "pmm.h"
#include "../kernel/console.h"
#include "../kernel/klog.h"

/* ========================================
 * Variables globales
 * ======================================== */

/* 
 * On alloue statiquement les structures de paging pour éviter
 * les problèmes de poule et oeuf avec le heap/pmm.
 * 
 * Ces structures DOIVENT être alignées sur 4 KiB.
 */

/* Page Directory du kernel - aligné sur 4 KiB */
static page_entry_t kernel_page_directory[TABLES_PER_DIR] __attribute__((aligned(PAGE_SIZE)));

/* Page Tables pour identity mapping des premiers 16 Mo (4 tables nécessaires) */
static page_entry_t kernel_page_tables[4][PAGES_PER_TABLE] __attribute__((aligned(PAGE_SIZE)));

/* Pointeurs vers les Page Tables (pour usage kernel) */
static page_entry_t* page_tables[TABLES_PER_DIR];

/* Page Directory courant (adresse physique) */
static uint32_t current_directory_phys = 0;

/* ========================================
 * Fonctions inline pour manipuler CR0/CR3
 * ======================================== */

/**
 * Charge une adresse dans CR3 (Page Directory Base Register).
 */
static inline void vmm_load_cr3(uint32_t addr)
{
    asm volatile("mov %0, %%cr3" : : "r"(addr) : "memory");
}

/**
 * Lit la valeur de CR3.
 */
static inline uint32_t vmm_read_cr3(void)
{
    uint32_t val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

/**
 * Active le paging en mettant le bit PG (31) de CR0.
 */
static inline void vmm_enable_paging(void)
{
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* Set bit 31 (PG) */
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

/**
 * Lit la valeur de CR2 (adresse de page fault).
 */
static inline uint32_t vmm_read_cr2(void)
{
    uint32_t val;
    asm volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

/**
 * Invalide une entrée TLB pour une adresse virtuelle.
 */
static inline void vmm_invlpg(uint32_t addr)
{
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

void vmm_init(void)
{
    KLOG_INFO("VMM", "=== Virtual Memory Manager ===");
    
    /* Initialiser le Page Directory */
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        kernel_page_directory[i] = 0;
        page_tables[i] = NULL;
    }
    
    /* ========================================
     * Identity Mapping des premiers 16 Mo
     * ========================================
     * Cela couvre :
     * - 0x00000000 - 0x000FFFFF : BIOS, IVT, BDA, VGA (1 Mo)
     * - 0x00100000 - 0x00FFFFFF : Kernel code, heap, etc. (15 Mo)
     * 
     * 16 Mo = 4096 pages = 4 Page Tables
     */
    
    KLOG_INFO("VMM", "Identity mapping first 16 MB...");
    
    /* Configurer les 4 Page Tables pour 16 Mo */
    for (int table = 0; table < 4; table++) {
        /* Initialiser chaque Page Table */
        for (int page = 0; page < PAGES_PER_TABLE; page++) {
            uint32_t phys_addr = (table * PAGES_PER_TABLE + page) * PAGE_SIZE;
            /* Identity map: virtuelle = physique */
            kernel_page_tables[table][page] = phys_addr | PAGE_PRESENT | PAGE_RW;
        }
        
        /* Ajouter la Page Table au Page Directory */
        kernel_page_directory[table] = ((uint32_t)&kernel_page_tables[table]) | PAGE_PRESENT | PAGE_RW;
        page_tables[table] = kernel_page_tables[table];
    }
    
    KLOG_INFO_HEX("VMM", "Page Directory at: ", (uint32_t)kernel_page_directory);
    KLOG_INFO("VMM", "Mapped 4096 pages (16 MB)");
    
    /* Sauvegarder l'adresse physique du Page Directory */
    current_directory_phys = (uint32_t)kernel_page_directory;
    
    /* Charger le Page Directory dans CR3 */
    KLOG_INFO("VMM", "Loading Page Directory into CR3...");
    vmm_load_cr3(current_directory_phys);
    
    /* Activer le paging ! */
    KLOG_INFO("VMM", "Enabling paging...");
    vmm_enable_paging();
    
    KLOG_INFO("VMM", "Paging enabled successfully!");
}

void vmm_map_page(uint32_t phys, uint32_t virt, uint32_t flags)
{
    /* S'assurer que les adresses sont alignées */
    phys = PAGE_ALIGN_DOWN(phys);
    virt = PAGE_ALIGN_DOWN(virt);
    
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t table_index = PAGE_TABLE_INDEX(virt);
    
    /* Vérifier si la Page Table existe, sinon la créer */
    if (page_tables[dir_index] == NULL) {
        /* Allouer une nouvelle Page Table */
        void* new_table = pmm_alloc_block();
        if (new_table == NULL) {
            KLOG_ERROR("VMM", "Failed to allocate page table!");
            return;
        }
        
        /* Mettre la table à zéro */
        page_entry_t* table = (page_entry_t*)new_table;
        for (int i = 0; i < PAGES_PER_TABLE; i++) {
            table[i] = 0;
        }
        
        /* Enregistrer la table */
        page_tables[dir_index] = table;
        
        /* Ajouter au Page Directory avec flags USER si demandé */
        uint32_t dir_flags = PAGE_PRESENT | PAGE_RW;
        if (flags & PAGE_USER) {
            dir_flags |= PAGE_USER;
        }
        kernel_page_directory[dir_index] = ((uint32_t)new_table & PAGE_FRAME_MASK) | dir_flags;
        
        KLOG_INFO_HEX("VMM", "Created page table for addr: ", virt);
    } else {
        /* Si la table existe mais qu'on veut des flags USER, mettre à jour le PDE */
        if (flags & PAGE_USER) {
            kernel_page_directory[dir_index] |= PAGE_USER;
        }
    }
    
    /* Mapper la page */
    page_tables[dir_index][table_index] = (phys & PAGE_FRAME_MASK) | (flags & 0xFFF) | PAGE_PRESENT;
    
    /* Invalider l'entrée TLB pour cette adresse */
    vmm_invlpg(virt);
}

void vmm_unmap_page(uint32_t virt)
{
    virt = PAGE_ALIGN_DOWN(virt);
    
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t table_index = PAGE_TABLE_INDEX(virt);
    
    /* Vérifier que la Page Table existe */
    if (page_tables[dir_index] == NULL) {
        return;
    }
    
    /* Effacer l'entrée */
    page_tables[dir_index][table_index] = 0;
    
    /* Invalider le TLB */
    vmm_invlpg(virt);
}

int vmm_switch_directory(page_directory_t* dir)
{
    if (dir == NULL) {
        return -1;
    }
    
    current_directory_phys = (uint32_t)dir;
    vmm_load_cr3(current_directory_phys);
    
    return 0;
}

page_directory_t* vmm_get_directory(void)
{
    return (page_directory_t*)current_directory_phys;
}

uint32_t vmm_get_physical(uint32_t virt)
{
    virt = PAGE_ALIGN_DOWN(virt);
    
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t table_index = PAGE_TABLE_INDEX(virt);
    
    /* Vérifier que la Page Table existe */
    if (page_tables[dir_index] == NULL) {
        return 0;
    }
    
    page_entry_t entry = page_tables[dir_index][table_index];
    
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }
    
    return entry & PAGE_FRAME_MASK;
}

bool vmm_is_mapped(uint32_t virt)
{
    return vmm_get_physical(virt) != 0 || virt == 0;  /* 0 est mappé mais retourne 0 */
}

void vmm_set_user_accessible(uint32_t start, uint32_t size)
{
    start = PAGE_ALIGN_DOWN(start);
    uint32_t end = PAGE_ALIGN_UP(start + size);
    
    KLOG_INFO("VMM", "Setting user access for range:");
    KLOG_INFO_HEX("VMM", "  Start: ", start);
    KLOG_INFO_HEX("VMM", "  End:   ", end);
    
    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t dir_index = PAGE_DIR_INDEX(addr);
        uint32_t table_index = PAGE_TABLE_INDEX(addr);
        
        /* Vérifier que la Page Table existe */
        if (page_tables[dir_index] == NULL) {
            continue;
        }
        
        /* Ajouter le flag USER à la page */
        page_tables[dir_index][table_index] |= PAGE_USER;
        
        /* Ajouter aussi le flag USER au Page Directory entry */
        kernel_page_directory[dir_index] |= PAGE_USER;
        
        /* Invalider le TLB */
        vmm_invlpg(addr);
    }
    
    KLOG_INFO("VMM", "User access granted");
}

void vmm_page_fault_handler(uint32_t error_code, uint32_t fault_addr)
{
    console_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    console_puts("\n!!! PAGE FAULT !!!\n");
    
    console_puts("Faulting Address (CR2): 0x");
    console_put_hex(fault_addr);
    console_puts("\n");
    
    console_puts("Error Code: 0x");
    console_put_hex(error_code);
    console_puts("\n");
    
    /* Décoder l'error code */
    console_puts("  - ");
    console_puts((error_code & 0x1) ? "Page-level protection violation" : "Non-present page");
    console_puts("\n  - ");
    console_puts((error_code & 0x2) ? "Write access" : "Read access");
    console_puts("\n  - ");
    console_puts((error_code & 0x4) ? "User mode" : "Supervisor mode");
    console_puts("\n");
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Halt - on ne peut pas continuer après un page fault non géré */
    console_puts("System halted.\n");
    for (;;) {
        asm volatile("hlt");
    }
}
