/* src/arch/x86_64/idt.h - Interrupt Descriptor Table for x86-64 */
#ifndef X86_64_IDT_H
#define X86_64_IDT_H

#include <stdint.h>

/* Number of IDT entries */
#define IDT_ENTRIES 256

/* ========================================
 * IDT Gate Types
 * ======================================== */
#define IDT_TYPE_INTERRUPT  0x8E    /* P=1, DPL=0, Interrupt Gate */
#define IDT_TYPE_TRAP       0x8F    /* P=1, DPL=0, Trap Gate */
#define IDT_TYPE_USER_INT   0xEE    /* P=1, DPL=3, Interrupt Gate (for syscalls) */

/* ========================================
 * IDT Entry Structure (16 bytes in 64-bit)
 * ======================================== */
struct idt_entry {
    uint16_t offset_low;    /* Offset bits 0-15 */
    uint16_t selector;      /* Code segment selector */
    uint8_t  ist;           /* IST index (bits 0-2), rest reserved */
    uint8_t  type_attr;     /* Type and attributes */
    uint16_t offset_mid;    /* Offset bits 16-31 */
    uint32_t offset_high;   /* Offset bits 32-63 */
    uint32_t reserved;      /* Reserved, must be 0 */
} __attribute__((packed));

/* ========================================
 * IDT Pointer Structure
 * ======================================== */
struct idt_ptr {
    uint16_t limit;         /* Size of IDT - 1 */
    uint64_t base;          /* Address of IDT */
} __attribute__((packed));

/* ========================================
 * Interrupt Frame (pushed by CPU + ISR stub)
 * ======================================== */
struct interrupt_frame {
    /* Pushed by ISR stub */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, error_code;
    
    /* Pushed by CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

/* ========================================
 * Functions
 * ======================================== */

/**
 * Initialize the IDT with exception and IRQ handlers.
 */
void idt_init(void);

/**
 * Set an IDT entry.
 * 
 * @param num       Interrupt number (0-255)
 * @param handler   Handler function address
 * @param selector  Code segment selector (usually GDT_KERNEL_CODE)
 * @param type_attr Type and attributes (IDT_TYPE_*)
 * @param ist       IST index (0 = no IST, 1-7 = use IST stack)
 */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, 
                  uint8_t type_attr, uint8_t ist);

/**
 * Generic exception handler (called from assembly).
 */
void exception_handler(struct interrupt_frame *frame);

/**
 * Generic IRQ handler (called from assembly).
 */
void irq_handler(struct interrupt_frame *frame);

#endif /* X86_64_IDT_H */
