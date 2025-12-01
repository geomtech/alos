/* src/arch/x86/tss.h - Task State Segment */
#ifndef TSS_H
#define TSS_H

#include <stdint.h>

/* ========================================
 * Task State Segment (TSS) Structure
 * ========================================
 * Le TSS est utilisé par le CPU pour sauvegarder/restaurer
 * l'état lors des changements de privilège (Ring 3 -> Ring 0).
 * 
 * En mode protégé 32-bit, on n'utilise pas le hardware task switching,
 * mais le TSS est quand même nécessaire pour :
 * - ss0/esp0 : Stack kernel à utiliser lors d'une interruption en Ring 3
 */
typedef struct tss_entry {
    uint32_t prev_tss;      /* Lien vers le TSS précédent (non utilisé) */
    
    /* Stack pointers pour chaque niveau de privilège */
    uint32_t esp0;          /* Stack pointer Ring 0 (IMPORTANT!) */
    uint32_t ss0;           /* Stack segment Ring 0 (IMPORTANT!) */
    uint32_t esp1;          /* Stack pointer Ring 1 (non utilisé) */
    uint32_t ss1;           /* Stack segment Ring 1 (non utilisé) */
    uint32_t esp2;          /* Stack pointer Ring 2 (non utilisé) */
    uint32_t ss2;           /* Stack segment Ring 2 (non utilisé) */
    
    /* Registres de contrôle */
    uint32_t cr3;           /* Page Directory (pour le task switching hardware) */
    uint32_t eip;           /* Instruction pointer */
    uint32_t eflags;        /* Flags */
    
    /* Registres généraux */
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    
    /* Segments */
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    
    /* LDT selector */
    uint32_t ldt;
    
    /* I/O Map */
    uint16_t trap;          /* Trap on task switch */
    uint16_t iomap_base;    /* I/O Map Base Address */
    
} __attribute__((packed)) tss_entry_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le TSS et le charge dans le Task Register.
 */
void init_tss(uint32_t kernel_stack);

/**
 * Met à jour le esp0 du TSS (appelé lors d'un context switch).
 * @param esp0  Nouvelle valeur de la stack kernel
 */
void tss_set_kernel_stack(uint32_t esp0);

/**
 * Récupère le TSS (pour accès externe si nécessaire).
 */
tss_entry_t* tss_get(void);

#endif /* TSS_H */
