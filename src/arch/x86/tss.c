/* src/arch/x86/tss.c - Task State Segment Implementation */
#include "tss.h"
#include "gdt.h"

/* ========================================
 * Variables globales
 * ======================================== */

/* Le TSS unique (on n'utilise pas le hardware multitasking) */
static tss_entry_t tss;

/* ========================================
 * Fonction ASM externe
 * ======================================== */

/* Charge le Task Register avec le sélecteur du TSS */
extern void tss_flush(uint32_t tss_selector);

/* ========================================
 * Implémentation
 * ======================================== */

void init_tss(uint32_t kernel_stack)
{
    /* Récupérer l'adresse et la taille du TSS */
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(tss_entry_t) - 1;
    
    /* Ajouter l'entrée TSS dans la GDT (index 5) */
    /* 
     * Access byte pour TSS:
     * - P (Present) = 1
     * - DPL = 0 (Ring 0)
     * - Type = 0x9 (32-bit TSS Available)
     * Total: 0x89
     * 
     * Granularity: 0x00 (byte granularity, 16-bit)
     * Mais on met 0x40 pour indiquer 32-bit
     */
    gdt_set_tss(5, base, limit);
    
    /* Initialiser le TSS à zéro */
    uint8_t* tss_ptr = (uint8_t*)&tss;
    for (uint32_t i = 0; i < sizeof(tss_entry_t); i++) {
        tss_ptr[i] = 0;
    }
    
    /* Configurer les champs importants */
    
    /* ss0: Kernel Data Segment (sélecteur 0x10) */
    tss.ss0 = 0x10;
    
    /* esp0: Stack kernel initiale */
    tss.esp0 = kernel_stack;
    
    /* I/O Map: désactivé (offset au-delà du TSS) */
    tss.iomap_base = sizeof(tss_entry_t);
    
    /* Charger le TSS dans le Task Register */
    /* Le sélecteur est: index * 8 + 0 (GDT, RPL=0) = 5 * 8 = 0x28 */
    tss_flush(0x28);
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

tss_entry_t* tss_get(void)
{
    return &tss;
}
