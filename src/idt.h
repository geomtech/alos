#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256

struct idt_entry_struct
{
    uint16_t base_low;
    uint16_t sel; // Sélecteur de segment kernel (0x08)
    uint8_t always0;
    uint8_t flags; // Attributs (0x8E = Interrupt Gate)
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr_struct
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void init_idt(void);

#endif