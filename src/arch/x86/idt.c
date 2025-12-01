#include "idt.h"
#include "io.h"
#include <stddef.h> // Pour size_t

/* Exception handlers (ISR 0-31) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ handlers */
extern void irq0_handler(void);
extern void irq1_handler(void);
extern void irq11_handler(void);
extern void irq14_handler(void);
extern void irq15_handler(void);

struct idt_entry_struct idt_entries[IDT_ENTRIES];
struct idt_ptr_struct idt_ptr;

extern void idt_flush(uint32_t); // En ASM

/* Noms des exceptions pour debug */
static const char* exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Exception",
    "Virtualization Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Security Exception", "Reserved"
};

/* Handler C appelé depuis l'ASM */
void exception_handler_c(void)
{
    /* Lire les paramètres de la stack */
    uint32_t* esp;
    asm volatile("mov %%esp, %0" : "=r"(esp));
    
    /* Après pusha (8 regs * 4 = 32 bytes), on a exception_num et error_code */
    uint32_t exception_num = esp[8];  /* pusha = 8 dwords */
    uint32_t error_code = esp[9];
    uint32_t eip = esp[10];
    
    /* Écrire directement en VGA mémoire pour le debug */
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    
    /* Écran rouge avec message d'erreur */
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x4F20; /* Fond rouge, espace */
    }
    
    /* Titre */
    const char* title = "=== KERNEL PANIC ===";
    for (int i = 0; title[i]; i++) {
        vga[i] = 0x4F00 | title[i];
    }
    
    /* Nom de l'exception */
    const char* name = (exception_num < 32) ? exception_names[exception_num] : "Unknown";
    vga += 80; /* Ligne suivante */
    const char* prefix = "Exception: ";
    for (int i = 0; prefix[i]; i++) {
        vga[i] = 0x4F00 | prefix[i];
    }
    vga += 11;
    for (int i = 0; name[i]; i++) {
        vga[i] = 0x4F00 | name[i];
    }
    
    /* Numéro d'exception et code d'erreur en hex */
    vga = (volatile uint16_t*)0xB8000 + 160; /* Ligne 2 */
    char buf[64];
    
    /* Afficher "Exception #XX  Error: XXXXXXXX  EIP: XXXXXXXX" */
    const char* hex = "0123456789ABCDEF";
    const char* msg1 = "Exception #";
    for (int i = 0; msg1[i]; i++) vga[i] = 0x4F00 | msg1[i];
    vga += 11;
    vga[0] = 0x4F00 | hex[(exception_num >> 4) & 0xF];
    vga[1] = 0x4F00 | hex[exception_num & 0xF];
    vga += 4;
    
    const char* msg2 = "Error: 0x";
    for (int i = 0; msg2[i]; i++) vga[i] = 0x4F00 | msg2[i];
    vga += 9;
    for (int i = 7; i >= 0; i--) {
        vga[7 - i] = 0x4F00 | hex[(error_code >> (i * 4)) & 0xF];
    }
    vga += 10;
    
    const char* msg3 = "EIP: 0x";
    for (int i = 0; msg3[i]; i++) vga[i] = 0x4F00 | msg3[i];
    vga += 7;
    for (int i = 7; i >= 0; i--) {
        vga[7 - i] = 0x4F00 | hex[(eip >> (i * 4)) & 0xF];
    }
    
    /* Halt infini */
    asm volatile("cli");
    while (1) {
        asm volatile("hlt");
    }
}

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

    // --- Installer les handlers d'exceptions (0-31) ---
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

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

    // IRQ 0 = Timer, IRQ 1 = Clavier, IRQ 11 = PCnet, IRQ 14/15 = IDE
    idt_set_gate(32, (uint32_t)irq0_handler, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1_handler, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11_handler, 0x08, 0x8E);  /* IRQ 11 = PCnet */
    idt_set_gate(46, (uint32_t)irq14_handler, 0x08, 0x8E);  /* IRQ 14 = IDE Primary */
    idt_set_gate(47, (uint32_t)irq15_handler, 0x08, 0x8E);  /* IRQ 15 = IDE Secondary */

    idt_flush((uint32_t)&idt_ptr);
}