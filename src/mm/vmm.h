/* src/mm/vmm.h - Virtual Memory Manager */
#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================
 * Constantes du Paging x86
 * ======================================== */

/* Taille d'une page : 4 KiB */
#define PAGE_SIZE           4096

/* Nombre d'entrées dans un Page Directory / Page Table */
#define PAGES_PER_TABLE     1024
#define TABLES_PER_DIR      1024

/* Flags des entrées de page */
#define PAGE_PRESENT        0x001   /* Page présente en mémoire */
#define PAGE_RW             0x002   /* Page en lecture/écriture (sinon read-only) */
#define PAGE_USER           0x004   /* Page accessible en user mode (Ring 3) */
#define PAGE_WRITETHROUGH   0x008   /* Write-through caching */
#define PAGE_NOCACHE        0x010   /* Désactive le cache pour cette page */
#define PAGE_ACCESSED       0x020   /* Page accédée (mis par le CPU) */
#define PAGE_DIRTY          0x040   /* Page modifiée (mis par le CPU) */
#define PAGE_SIZE_4MB       0x080   /* Page de 4 MB (dans PDE seulement) */
#define PAGE_GLOBAL         0x100   /* Page globale (non flushée sur switch CR3) */

/* Masque pour l'adresse physique dans une entrée (bits 12-31) */
#define PAGE_FRAME_MASK     0xFFFFF000

/* ========================================
 * Macros d'extraction d'index
 * ======================================== */

/* Extrait l'index du Page Directory (bits 22-31) */
#define PAGE_DIR_INDEX(vaddr)   (((uint32_t)(vaddr) >> 22) & 0x3FF)

/* Extrait l'index du Page Table (bits 12-21) */
#define PAGE_TABLE_INDEX(vaddr) (((uint32_t)(vaddr) >> 12) & 0x3FF)

/* Extrait l'offset dans la page (bits 0-11) */
#define PAGE_OFFSET(vaddr)      ((uint32_t)(vaddr) & 0xFFF)

/* Aligne une adresse sur une page */
#define PAGE_ALIGN_DOWN(addr)   ((addr) & PAGE_FRAME_MASK)
#define PAGE_ALIGN_UP(addr)     (((addr) + PAGE_SIZE - 1) & PAGE_FRAME_MASK)

/* ========================================
 * Types
 * ======================================== */

/* Une entrée de Page Directory ou Page Table (32 bits) */
typedef uint32_t page_entry_t;

/* Un Page Table : tableau de 1024 entrées */
typedef struct {
    page_entry_t entries[PAGES_PER_TABLE];
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

/* Un Page Directory : tableau de 1024 entrées */
typedef struct {
    page_entry_t entries[TABLES_PER_DIR];
    
    /* Adresses virtuelles des Page Tables (pour pouvoir les modifier) */
    /* Note: Ces pointeurs ne sont pas dans la structure réelle en mémoire, */
    /* ils sont juste pour notre usage kernel */
    page_table_t* tables[TABLES_PER_DIR];
} __attribute__((aligned(PAGE_SIZE))) page_directory_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le VMM avec Identity Mapping.
 * Configure le paging et l'active.
 */
void vmm_init(void);

/**
 * Mappe une page physique à une adresse virtuelle.
 * 
 * @param phys   Adresse physique (doit être alignée sur 4 KiB)
 * @param virt   Adresse virtuelle (doit être alignée sur 4 KiB)
 * @param flags  Flags de la page (PAGE_PRESENT, PAGE_RW, PAGE_USER, etc.)
 */
void vmm_map_page(uint32_t phys, uint32_t virt, uint32_t flags);

/**
 * Unmap une page virtuelle.
 * 
 * @param virt  Adresse virtuelle à unmapper
 */
void vmm_unmap_page(uint32_t virt);

/**
 * Change le Page Directory actif (switch de contexte).
 * 
 * @param dir  Nouveau Page Directory
 * @return 0 si succès, -1 si erreur
 */
int vmm_switch_directory(page_directory_t* dir);

/**
 * Retourne le Page Directory courant.
 */
page_directory_t* vmm_get_directory(void);

/**
 * Traduit une adresse virtuelle en adresse physique.
 * 
 * @param virt  Adresse virtuelle
 * @return Adresse physique, ou 0 si non mappée
 */
uint32_t vmm_get_physical(uint32_t virt);

/**
 * Vérifie si une page est mappée.
 */
bool vmm_is_mapped(uint32_t virt);

/**
 * Handler de Page Fault (appelé depuis le handler d'exception).
 * 
 * @param error_code  Code d'erreur pushé par le CPU
 * @param fault_addr  Adresse fautive (CR2)
 */
void vmm_page_fault_handler(uint32_t error_code, uint32_t fault_addr);

/**
 * Rend une plage d'adresses accessible en User Mode (Ring 3).
 * Ajoute le flag PAGE_USER aux pages concernées.
 * 
 * @param start  Adresse de début (sera alignée)
 * @param size   Taille en octets
 */
void vmm_set_user_accessible(uint32_t start, uint32_t size);

/* ========================================
 * Fonctions multi-espaces d'adressage
 * ======================================== */

/**
 * Crée un nouveau Page Directory pour un processus utilisateur.
 * Copie les entrées kernel (premiers 16 Mo) depuis le kernel directory.
 * L'espace utilisateur est vide (non mappé).
 * 
 * @return Pointeur vers le nouveau Page Directory, ou NULL si échec
 */
page_directory_t* vmm_create_directory(void);

/**
 * Libère un Page Directory et toutes ses Page Tables utilisateur.
 * NE libère PAS les Page Tables du kernel (partagées).
 * 
 * @param dir  Page Directory à libérer
 */
void vmm_free_directory(page_directory_t* dir);

/**
 * Traduit une adresse virtuelle en adresse physique dans un répertoire spécifique.
 * 
 * @param dir        Page Directory dans lequel chercher
 * @param virt_addr  Adresse virtuelle à traduire
 * @return Adresse physique, ou 0 si non mappée
 */
uint32_t vmm_get_phys_addr(page_directory_t* dir, uint32_t virt_addr);

/**
 * Mappe une page dans un Page Directory spécifique (pas forcément le courant).
 * Utilise une fenêtre temporaire pour accéder aux page tables.
 * 
 * @param dir    Page Directory cible
 * @param phys   Adresse physique à mapper
 * @param virt   Adresse virtuelle de destination
 * @param flags  Flags de la page (PAGE_PRESENT, PAGE_RW, PAGE_USER, etc.)
 * @return 0 si succès, -1 si échec
 */
int vmm_map_page_in_dir(page_directory_t* dir, uint32_t phys, uint32_t virt, uint32_t flags);

/**
 * Retourne le Page Directory du kernel (celui utilisé au boot).
 */
page_directory_t* vmm_get_kernel_directory(void);

/**
 * Clone un espace d'adressage (pour fork).
 * Copie les mappings utilisateur (pas les données, juste les mappings).
 * 
 * @param src  Page Directory source
 * @return Nouveau Page Directory avec les mêmes mappings, ou NULL si échec
 */
page_directory_t* vmm_clone_directory(page_directory_t* src);

#endif /* VMM_H */
