#include "idt.h"
#include "io.h"
#include <stddef.h> // Pour size_t

extern void irq0_handler(void);
extern void irq1_handler(void);
extern void irq11_handler(void);

struct idt_entry_struct idt_entries[IDT_ENTRIES];
struct idt_ptr_struct idt_ptr;

extern void idt_flush(uint32_t); // En ASM

// Définition simple pour memset si besoin
void my_memset(void *ptr, int value, size_t num)
{
    unsigned char *p = ptr;
    while (num--)
        *p++ = (unsigned char)value;
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags = flags;
}

void init_idt(void)
{
    idt_ptr.limit = sizeof(struct idt_entry_struct) * IDT_ENTRIES - 1;
    idt_ptr.base = (uint32_t)&idt_entries;

    my_memset(&idt_entries, 0, sizeof(struct idt_entry_struct) * IDT_ENTRIES);

    // --- REMAPPING DU PIC ---
    // On déplace les IRQ Maître de 0x00 vers 0x20 (32)
    // On déplace les IRQ Esclave de 0x08 vers 0x28 (40)
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    io_wait();

    outb(0x21, 0x20); // Offset Maître (32)
    outb(0xA1, 0x28); // Offset Esclave (40)
    io_wait();

    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    io_wait();

    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, 0x0);
    outb(0xA1, 0x0);

    // IRQ 1 = Clavier.
    // Le PIC a décalé les IRQs à partir de 32. Donc IRQ 1 est à l'index 33.
    // 0x08 = Kernel Code Segment
    // 0x8E = Flags (Present, Ring 0, 32-bit Interrupt Gate)
    idt_set_gate(32, (uint32_t)irq0_handler, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1_handler, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11_handler, 0x08, 0x8E);  /* IRQ 11 = PCnet */

    // Pour l'instant on laisse l'IDT vide de handlers (ça plantera si une IRQ arrive)
    // On charge juste la table vide pour que le CPU sache où elle est.
    idt_flush((uint32_t)&idt_ptr);
}