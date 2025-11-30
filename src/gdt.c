/* src/gdt.c */
#include "gdt.h"

struct gdt_entry_struct gdt_entries[3];
struct gdt_ptr_struct gdt_ptr;

// Fonction définie en ASM (on l'écrira juste après)
extern void gdt_flush(uint32_t);

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

void init_gdt(void)
{
    gdt_ptr.limit = (sizeof(struct gdt_entry_struct) * 3) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;

    // 0: Null Descriptor (Obligatoire)
    gdt_set_gate(0, 0, 0, 0, 0);

    // 1: Kernel Code Segment
    // Base: 0, Limit: 4GB, Access: 0x9A (Present, Ring 0, Executable, Readable)
    // Granularity: 0xCF (4KB Pages, 32-bit Mode)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // 2: Kernel Data Segment
    // Base: 0, Limit: 4GB, Access: 0x92 (Present, Ring 0, Data, Writable)
    // Granularity: 0xCF
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // Chargement de la GDT via ASM
    gdt_flush((uint32_t)&gdt_ptr);
}