/* src/gdt.h */
#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* ========================================
 * Sélecteurs de segments
 * ========================================
 * Format: Index * 8 + TI (0=GDT) + RPL (0-3)
 */
#define GDT_KERNEL_CODE     0x08    /* Index 1, Ring 0 */
#define GDT_KERNEL_DATA     0x10    /* Index 2, Ring 0 */
#define GDT_USER_CODE       0x1B    /* Index 3, Ring 3 (0x18 + 3) */
#define GDT_USER_DATA       0x23    /* Index 4, Ring 3 (0x20 + 3) */
#define GDT_TSS             0x28    /* Index 5, Ring 0 */

// Structure d'une entrée de la GDT (8 octets)
struct gdt_entry_struct
{
    uint16_t limit_low;    // 16 bits de poids faible de la limite
    uint16_t base_low;     // 16 bits de poids faible de la base
    uint8_t base_middle;   // 8 bits suivants de la base
    uint8_t access;        // Octet d'accès (Droits, Ring 0/3, Type)
    uint8_t granularity;   // Granularité et 4 bits de poids fort de la limite
    uint8_t base_high;     // 8 derniers bits de la base
} __attribute__((packed)); // Important : pas de padding du compilateur !

// Structure du pointeur spécial pour l'instruction LGDT
struct gdt_ptr_struct
{
    uint16_t limit; // Taille de la table - 1
    uint32_t base;  // Adresse de la table
} __attribute__((packed));

/**
 * Initialise la GDT avec tous les segments nécessaires.
 */
void init_gdt(void);

/**
 * Ajoute une entrée TSS dans la GDT (appelé par tss.c).
 */
void gdt_set_tss(int32_t num, uint32_t base, uint32_t limit);

#endif