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

/* ========================================
 * Fonctions multi-espaces d'adressage
 * ========================================
 * 
 * Pour modifier un Page Directory qui n'est pas le courant,
 * on utilise une "fenêtre temporaire" : une page réservée
 * dans l'espace kernel qui est remappée temporairement vers
 * la page table cible.
 */

/* Adresse de la fenêtre temporaire (une page dans l'espace kernel) */
#define TEMP_MAP_ADDR  0x00FF0000  /* Juste avant 16 Mo */

/* Nombre d'entrées kernel à copier (16 Mo = 4 tables) */
#define KERNEL_TABLES_COUNT  4

/**
 * Mappe temporairement une page physique pour y accéder.
 * Utilise la fenêtre temporaire à TEMP_MAP_ADDR.
 */
static void* vmm_temp_map(uint32_t phys_addr)
{
    /* Mapper la page physique dans la fenêtre temporaire */
    uint32_t dir_index = PAGE_DIR_INDEX(TEMP_MAP_ADDR);
    uint32_t table_index = PAGE_TABLE_INDEX(TEMP_MAP_ADDR);
    
    /* La page table pour TEMP_MAP_ADDR devrait exister (c'est dans les 16 Mo) */
    if (page_tables[dir_index] != NULL) {
        page_tables[dir_index][table_index] = (phys_addr & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_RW;
        vmm_invlpg(TEMP_MAP_ADDR);
    }
    
    return (void*)TEMP_MAP_ADDR;
}

/**
 * Annule le mapping temporaire.
 */
static void vmm_temp_unmap(void)
{
    uint32_t dir_index = PAGE_DIR_INDEX(TEMP_MAP_ADDR);
    uint32_t table_index = PAGE_TABLE_INDEX(TEMP_MAP_ADDR);
    
    if (page_tables[dir_index] != NULL) {
        page_tables[dir_index][table_index] = 0;
        vmm_invlpg(TEMP_MAP_ADDR);
    }
}

page_directory_t* vmm_get_kernel_directory(void)
{
    return (page_directory_t*)kernel_page_directory;
}

page_directory_t* vmm_create_directory(void)
{
    /* Allouer un bloc pour le Page Directory (4 Ko aligné) */
    void* dir_phys = pmm_alloc_block();
    if (dir_phys == NULL) {
        KLOG_ERROR("VMM", "Failed to allocate page directory");
        return NULL;
    }
    
    KLOG_INFO_HEX("VMM", "Creating new page directory at: ", (uint32_t)dir_phys);
    
    /* Mapper temporairement le nouveau directory pour l'initialiser */
    page_entry_t* new_dir = (page_entry_t*)vmm_temp_map((uint32_t)dir_phys);
    
    /* Initialiser toutes les entrées à 0 */
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        new_dir[i] = 0;
    }
    
    /* Copier les entrées kernel (premiers 16 Mo) depuis le kernel directory.
     * Ces entrées pointent vers les mêmes Page Tables que le kernel,
     * donc le code kernel est visible depuis tous les processus.
     */
    for (int i = 0; i < KERNEL_TABLES_COUNT; i++) {
        new_dir[i] = kernel_page_directory[i];
    }
    
    vmm_temp_unmap();
    
    KLOG_INFO("VMM", "New directory created with kernel mappings");
    
    return (page_directory_t*)dir_phys;
}

void vmm_free_directory(page_directory_t* dir)
{
    if (dir == NULL) {
        return;
    }
    
    /* Ne pas libérer le kernel directory ! */
    if ((uint32_t)dir == (uint32_t)kernel_page_directory) {
        KLOG_ERROR("VMM", "Attempted to free kernel directory!");
        return;
    }
    
    KLOG_INFO_HEX("VMM", "Freeing page directory at: ", (uint32_t)dir);
    
    /* Mapper temporairement le directory pour lire ses entrées */
    page_entry_t* dir_entries = (page_entry_t*)vmm_temp_map((uint32_t)dir);
    
    /* Libérer les Page Tables utilisateur (après les entrées kernel) */
    for (int i = KERNEL_TABLES_COUNT; i < TABLES_PER_DIR; i++) {
        if (dir_entries[i] & PAGE_PRESENT) {
            uint32_t table_phys = dir_entries[i] & PAGE_FRAME_MASK;
            pmm_free_block((void*)table_phys);
        }
    }
    
    vmm_temp_unmap();
    
    /* Libérer le Page Directory lui-même */
    pmm_free_block((void*)dir);
    
    KLOG_INFO("VMM", "Directory freed");
}

uint32_t vmm_get_phys_addr(page_directory_t* dir, uint32_t virt_addr)
{
    if (dir == NULL) {
        return 0;
    }
    
    uint32_t dir_index = PAGE_DIR_INDEX(virt_addr);
    uint32_t table_index = PAGE_TABLE_INDEX(virt_addr);
    uint32_t offset = PAGE_OFFSET(virt_addr);
    
    /* Si c'est le kernel directory actif, utiliser le chemin rapide */
    if ((uint32_t)dir == (uint32_t)kernel_page_directory) {
        if (page_tables[dir_index] == NULL) {
            return 0;
        }
        page_entry_t entry = page_tables[dir_index][table_index];
        if (!(entry & PAGE_PRESENT)) {
            return 0;
        }
        return (entry & PAGE_FRAME_MASK) + offset;
    }
    
    /* Mapper temporairement le directory */
    page_entry_t* dir_entries = (page_entry_t*)vmm_temp_map((uint32_t)dir);
    
    /* Vérifier si la Page Table existe */
    if (!(dir_entries[dir_index] & PAGE_PRESENT)) {
        vmm_temp_unmap();
        return 0;
    }
    
    uint32_t table_phys = dir_entries[dir_index] & PAGE_FRAME_MASK;
    vmm_temp_unmap();
    
    /* Mapper temporairement la Page Table */
    page_entry_t* table_entries = (page_entry_t*)vmm_temp_map(table_phys);
    
    page_entry_t entry = table_entries[table_index];
    vmm_temp_unmap();
    
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }
    
    return (entry & PAGE_FRAME_MASK) + offset;
}

int vmm_map_page_in_dir(page_directory_t* dir, uint32_t phys, uint32_t virt, uint32_t flags)
{
    if (dir == NULL) {
        return -1;
    }
    
    /* Aligner les adresses */
    phys = PAGE_ALIGN_DOWN(phys);
    virt = PAGE_ALIGN_DOWN(virt);
    
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t table_index = PAGE_TABLE_INDEX(virt);
    
    /* Si c'est le kernel directory, utiliser vmm_map_page normal */
    if ((uint32_t)dir == (uint32_t)kernel_page_directory) {
        vmm_map_page(phys, virt, flags);
        return 0;
    }
    
    /* Mapper temporairement le directory */
    page_entry_t* dir_entries = (page_entry_t*)vmm_temp_map((uint32_t)dir);
    
    uint32_t table_phys;
    
    /* Vérifier si la Page Table existe */
    if (!(dir_entries[dir_index] & PAGE_PRESENT)) {
        /* Allouer une nouvelle Page Table */
        void* new_table = pmm_alloc_block();
        if (new_table == NULL) {
            vmm_temp_unmap();
            KLOG_ERROR("VMM", "Failed to allocate page table");
            return -1;
        }
        
        /* Initialiser la nouvelle Page Table à zéro */
        /* D'abord sauvegarder l'entrée dir */
        vmm_temp_unmap();
        
        page_entry_t* new_table_entries = (page_entry_t*)vmm_temp_map((uint32_t)new_table);
        for (int i = 0; i < PAGES_PER_TABLE; i++) {
            new_table_entries[i] = 0;
        }
        vmm_temp_unmap();
        
        /* Re-mapper le directory pour ajouter l'entrée */
        dir_entries = (page_entry_t*)vmm_temp_map((uint32_t)dir);
        
        /* Ajouter la nouvelle Page Table au directory */
        uint32_t dir_flags = PAGE_PRESENT | PAGE_RW;
        if (flags & PAGE_USER) {
            dir_flags |= PAGE_USER;
        }
        dir_entries[dir_index] = ((uint32_t)new_table & PAGE_FRAME_MASK) | dir_flags;
        
        table_phys = (uint32_t)new_table;
    } else {
        /* Mettre à jour les flags si nécessaire */
        if (flags & PAGE_USER) {
            dir_entries[dir_index] |= PAGE_USER;
        }
        table_phys = dir_entries[dir_index] & PAGE_FRAME_MASK;
    }
    
    vmm_temp_unmap();
    
    /* Mapper temporairement la Page Table pour ajouter l'entrée */
    page_entry_t* table_entries = (page_entry_t*)vmm_temp_map(table_phys);
    
    /* Ajouter l'entrée de page */
    table_entries[table_index] = (phys & PAGE_FRAME_MASK) | (flags & 0xFFF) | PAGE_PRESENT;
    
    vmm_temp_unmap();
    
    /* Si ce directory est le courant, invalider le TLB */
    if ((uint32_t)dir == current_directory_phys) {
        vmm_invlpg(virt);
    }
    
    return 0;
}

page_directory_t* vmm_clone_directory(page_directory_t* src)
{
    if (src == NULL) {
        return NULL;
    }
    
    /* Créer un nouveau directory avec les mappings kernel */
    page_directory_t* new_dir = vmm_create_directory();
    if (new_dir == NULL) {
        return NULL;
    }
    
    KLOG_INFO("VMM", "Cloning page directory...");
    
    /* Mapper le source directory */
    page_entry_t* src_entries = (page_entry_t*)vmm_temp_map((uint32_t)src);
    
    /* Copier les entrées utilisateur (on copie les références, pas les données)
     * Note: Pour un vrai fork COW, il faudrait marquer les pages read-only
     * et les copier sur écriture. Pour l'instant, on fait une copie simple.
     */
    uint32_t user_entries[TABLES_PER_DIR - KERNEL_TABLES_COUNT];
    for (int i = KERNEL_TABLES_COUNT; i < TABLES_PER_DIR; i++) {
        user_entries[i - KERNEL_TABLES_COUNT] = src_entries[i];
    }
    vmm_temp_unmap();
    
    /* Mapper le nouveau directory */
    page_entry_t* dst_entries = (page_entry_t*)vmm_temp_map((uint32_t)new_dir);
    
    /* Pour chaque Page Table utilisateur présente, créer une copie */
    for (int i = KERNEL_TABLES_COUNT; i < TABLES_PER_DIR; i++) {
        uint32_t src_entry = user_entries[i - KERNEL_TABLES_COUNT];
        
        if (src_entry & PAGE_PRESENT) {
            /* Allouer une nouvelle Page Table */
            void* new_table = pmm_alloc_block();
            if (new_table == NULL) {
                vmm_temp_unmap();
                vmm_free_directory(new_dir);
                KLOG_ERROR("VMM", "Clone failed: out of memory");
                return NULL;
            }
            
            /* Sauvegarder et copier le contenu de la table source */
            uint32_t src_table_phys = src_entry & PAGE_FRAME_MASK;
            vmm_temp_unmap();
            
            /* Lire la table source */
            page_entry_t* src_table = (page_entry_t*)vmm_temp_map(src_table_phys);
            uint32_t table_copy[PAGES_PER_TABLE];
            for (int j = 0; j < PAGES_PER_TABLE; j++) {
                table_copy[j] = src_table[j];
            }
            vmm_temp_unmap();
            
            /* Écrire dans la nouvelle table */
            page_entry_t* dst_table = (page_entry_t*)vmm_temp_map((uint32_t)new_table);
            for (int j = 0; j < PAGES_PER_TABLE; j++) {
                dst_table[j] = table_copy[j];
            }
            vmm_temp_unmap();
            
            /* Re-mapper le directory destination */
            dst_entries = (page_entry_t*)vmm_temp_map((uint32_t)new_dir);
            
            /* Ajouter l'entrée avec les mêmes flags */
            uint32_t entry_flags = src_entry & 0xFFF;
            dst_entries[i] = ((uint32_t)new_table & PAGE_FRAME_MASK) | entry_flags;
        }
    }
    
    vmm_temp_unmap();
    
    KLOG_INFO("VMM", "Directory cloned successfully");
    
    return new_dir;
}
