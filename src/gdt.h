/* src/gdt.h */
#ifndef GDT_H
#define GDT_H

#include <stdint.h>

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

void init_gdt(void);

#endif