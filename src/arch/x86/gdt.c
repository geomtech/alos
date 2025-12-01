/* src/gdt.c - Global Descriptor Table */
#include "gdt.h"

/* 6 entrées: Null, KCode, KData, UCode, UData, TSS */
struct gdt_entry_struct gdt_entries[6];
struct gdt_ptr_struct gdt_ptr;

/* Fonction définie en ASM */
extern void gdt_flush(uint32_t);

/**
 * Configure une entrée de la GDT.
 */
static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= (gran & 0xF0);
    gdt_entries[num].access = access;
}

/**
 * Configure l'entrée TSS dans la GDT.
 * Le TSS a un format légèrement différent.
 */
void gdt_set_tss(int32_t num, uint32_t base, uint32_t limit)
{
    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    
    /* Granularity: 0x00 pour TSS (byte granularity) */
    /* Mais on garde le bit 32-bit (0x40) */
    gdt_entries[num].granularity |= 0x00;
    
    /* Access: 0x89 = Present(1) + DPL(0) + TSS 32-bit Available(0x9) */
    /* E9 = Present(1) + DPL(3) + TSS 32-bit Available(0x9) - si on veut Ring 3 */
    gdt_entries[num].access = 0x89;
}

void init_gdt(void)
{
    /* Taille de la GDT : 6 entrées */
    gdt_ptr.limit = (sizeof(struct gdt_entry_struct) * 6) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;

    /* ========================================
     * Index 0: Null Descriptor (Obligatoire)
     * ======================================== */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* ========================================
     * Index 1: Kernel Code Segment (0x08)
     * ========================================
     * Base: 0, Limit: 4GB
     * Access: 0x9A = Present(1) + DPL(0) + Code(1) + Exec(1) + Readable(1)
     * Granularity: 0xCF = 4KB pages + 32-bit
     */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* ========================================
     * Index 2: Kernel Data Segment (0x10)
     * ========================================
     * Base: 0, Limit: 4GB
     * Access: 0x92 = Present(1) + DPL(0) + Data(0) + Writable(1)
     * Granularity: 0xCF = 4KB pages + 32-bit
     */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* ========================================
     * Index 3: User Code Segment (0x18, selector 0x1B avec RPL=3)
     * ========================================
     * Base: 0, Limit: 4GB
     * Access: 0xFA = Present(1) + DPL(3) + Code(1) + Exec(1) + Readable(1)
     * Granularity: 0xCF = 4KB pages + 32-bit
     */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* ========================================
     * Index 4: User Data Segment (0x20, selector 0x23 avec RPL=3)
     * ========================================
     * Base: 0, Limit: 4GB
     * Access: 0xF2 = Present(1) + DPL(3) + Data(0) + Writable(1)
     * Granularity: 0xCF = 4KB pages + 32-bit
     */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* ========================================
     * Index 5: TSS (0x28)
     * ========================================
     * Sera configuré par init_tss() dans tss.c
     * On met une entrée vide pour l'instant
     */
    gdt_set_gate(5, 0, 0, 0, 0);

    /* Charger la nouvelle GDT */
    gdt_flush((uint32_t)&gdt_ptr);
}