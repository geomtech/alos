/* src/mm/vmm.h - Virtual Memory Manager for x86-64 */
#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================
 * Constantes du Paging x86-64
 * ========================================
 * 
 * x86-64 utilise un paging à 4 niveaux:
 * PML4 -> PDPT -> PD -> PT -> Page
 * 
 * Chaque niveau a 512 entrées de 8 octets.
 * Adresse virtuelle (48 bits utilisés):
 *   [47:39] PML4 index (9 bits)
 *   [38:30] PDPT index (9 bits)
 *   [29:21] PD index (9 bits)
 *   [20:12] PT index (9 bits)
 *   [11:0]  Page offset (12 bits)
 */

/* Taille d'une page : 4 KiB */
#define PAGE_SIZE           4096ULL

/* Nombre d'entrées par table (512 en 64-bit) */
#define ENTRIES_PER_TABLE   512

/* Flags des entrées de page (communs à tous les niveaux) */
#define PAGE_PRESENT        (1ULL << 0)   /* Page présente en mémoire */
#define PAGE_RW             (1ULL << 1)   /* Page en lecture/écriture */
#define PAGE_USER           (1ULL << 2)   /* Page accessible en user mode */
#define PAGE_WRITETHROUGH   (1ULL << 3)   /* Write-through caching */
#define PAGE_NOCACHE        (1ULL << 4)   /* Désactive le cache */
#define PAGE_ACCESSED       (1ULL << 5)   /* Page accédée (mis par CPU) */
#define PAGE_DIRTY          (1ULL << 6)   /* Page modifiée (mis par CPU) */
#define PAGE_HUGE           (1ULL << 7)   /* Page 2MB (PD) ou 1GB (PDPT) */
#define PAGE_GLOBAL         (1ULL << 8)   /* Page globale */
#define PAGE_NX             (1ULL << 63)  /* No-Execute */

/* Masque pour l'adresse physique (bits 12-51) */
#define PAGE_FRAME_MASK     0x000FFFFFFFFFF000ULL

/* ========================================
 * Macros d'extraction d'index
 * ======================================== */

/* Extrait l'index PML4 (bits 39-47) */
#define PML4_INDEX(vaddr)   (((uint64_t)(vaddr) >> 39) & 0x1FF)

/* Extrait l'index PDPT (bits 30-38) */
#define PDPT_INDEX(vaddr)   (((uint64_t)(vaddr) >> 30) & 0x1FF)

/* Extrait l'index PD (bits 21-29) */
#define PD_INDEX(vaddr)     (((uint64_t)(vaddr) >> 21) & 0x1FF)

/* Extrait l'index PT (bits 12-20) */
#define PT_INDEX(vaddr)     (((uint64_t)(vaddr) >> 12) & 0x1FF)

/* Extrait l'offset dans la page (bits 0-11) */
#define PAGE_OFFSET(vaddr)  ((uint64_t)(vaddr) & 0xFFF)

/* Aligne une adresse sur une page */
#define PAGE_ALIGN_DOWN(addr)   ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)     (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/* ========================================
 * Types
 * ======================================== */

/* Une entrée de table de pages (64 bits) */
typedef uint64_t page_entry_t;

/* Structure représentant un espace d'adressage */
typedef struct {
    uint64_t pml4_phys;     /* Adresse physique du PML4 */
    page_entry_t *pml4;     /* Adresse virtuelle du PML4 (via HHDM) */
} page_directory_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le VMM.
 * Note: Limine a déjà configuré le paging, on réutilise ses tables.
 */
void vmm_init(void);

/**
 * Mappe une page physique à une adresse virtuelle.
 * 
 * @param phys   Adresse physique (doit être alignée sur 4 KiB)
 * @param virt   Adresse virtuelle (doit être alignée sur 4 KiB)
 * @param flags  Flags de la page (PAGE_PRESENT, PAGE_RW, PAGE_USER, etc.)
 */
void vmm_map_page(uint64_t phys, uint64_t virt, uint64_t flags);

/**
 * Unmap une page virtuelle.
 * 
 * @param virt  Adresse virtuelle à unmapper
 */
void vmm_unmap_page(uint64_t virt);

/**
 * Change le PML4 actif (switch de contexte).
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
uint64_t vmm_get_physical(uint64_t virt);

/**
 * Vérifie si une page est mappée.
 */
bool vmm_is_mapped(uint64_t virt);

/**
 * Handler de Page Fault (appelé depuis le handler d'exception).
 * 
 * @param error_code  Code d'erreur pushé par le CPU
 * @param fault_addr  Adresse fautive (CR2)
 */
void vmm_page_fault_handler(uint64_t error_code, uint64_t fault_addr);

/**
 * Rend une plage d'adresses accessible en User Mode (Ring 3).
 * 
 * @param start  Adresse de début (sera alignée)
 * @param size   Taille en octets
 */
void vmm_set_user_accessible(uint64_t start, uint64_t size);

/* ========================================
 * Fonctions multi-espaces d'adressage
 * ======================================== */

/**
 * Crée un nouveau PML4 pour un processus utilisateur.
 * Copie les entrées kernel depuis le kernel PML4.
 * 
 * @return Pointeur vers le nouveau Page Directory, ou NULL si échec
 */
page_directory_t* vmm_create_directory(void);

/**
 * Retourne l'adresse physique du PML4 du kernel.
 * Utilisé par le scheduler pour restaurer le CR3 du kernel.
 * 
 * @return Adresse physique du PML4 kernel
 */
uint64_t vmm_get_kernel_cr3(void);

/**
 * Libère un Page Directory et toutes ses tables.
 * 
 * @param dir  Page Directory à libérer
 */
void vmm_free_directory(page_directory_t* dir);

/**
 * Traduit une adresse virtuelle en adresse physique dans un PML4 spécifique.
 * 
 * @param dir        Page Directory dans lequel chercher
 * @param virt_addr  Adresse virtuelle à traduire
 * @return Adresse physique, ou 0 si non mappée
 */
uint64_t vmm_get_phys_addr(page_directory_t* dir, uint64_t virt_addr);

/**
 * Mappe une page dans un PML4 spécifique.
 * 
 * @param dir    Page Directory cible
 * @param phys   Adresse physique à mapper
 * @param virt   Adresse virtuelle de destination
 * @param flags  Flags de la page
 * @return 0 si succès, -1 si échec
 */
int vmm_map_page_in_dir(page_directory_t* dir, uint64_t phys, uint64_t virt, uint64_t flags);

/**
 * Retourne le Page Directory du kernel.
 */
page_directory_t* vmm_get_kernel_directory(void);

/**
 * Clone un espace d'adressage (pour fork).
 * 
 * @param src  Page Directory source
 * @return Nouveau Page Directory, ou NULL si échec
 */
page_directory_t* vmm_clone_directory(page_directory_t* src);

/**
 * Vérifie si une adresse est mappée dans un PML4 spécifique.
 */
bool vmm_is_mapped_in_dir(page_directory_t* dir, uint64_t virt);

/**
 * Copie des données vers un autre espace d'adressage.
 */
int vmm_copy_to_dir(page_directory_t* dir, uint64_t dst_virt, const void* src, uint64_t size);

/**
 * Met à zéro une plage de mémoire dans un autre espace d'adressage.
 */
int vmm_memset_in_dir(page_directory_t* dir, uint64_t dst_virt, uint8_t value, uint64_t size);

/* ========================================
 * Helpers HHDM
 * ======================================== */

/**
 * Convertit une adresse physique en virtuelle via HHDM.
 */
void* vmm_phys_to_virt(uint64_t phys);

/**
 * Convertit une adresse virtuelle en physique.
 */
uint64_t vmm_virt_to_phys(void* virt);

#endif /* VMM_H */
