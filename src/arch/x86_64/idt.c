/* src/arch/x86_64/idt.c - Interrupt Descriptor Table for x86-64 */
#include "idt.h"
#include "gdt.h"
#include "io.h"
#include "../../kernel/klog.h"
#include "../../kernel/console.h"

/* ========================================
 * External ISR/IRQ Stubs (defined in interrupts.s)
 * ======================================== */

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

/* IRQ handlers (IRQ 0-15 mapped to ISR 32-47) */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* Syscall handler */
extern void isr128(void);

/* ========================================
 * Static Data
 * ======================================== */

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr idtr;

/* Exception names for debugging */
static const char *exception_names[] = {
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
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor Injection",
    "VMM Communication", "Security Exception", "Reserved"
};

/* ========================================
 * Assembly Functions
 * ======================================== */

extern void idt_flush(uint64_t idtr_ptr);

/* ========================================
 * PIC Remapping
 * ======================================== */

/**
 * Remap the PIC to avoid conflicts with CPU exceptions.
 * Master PIC: IRQ 0-7 -> INT 32-39
 * Slave PIC: IRQ 8-15 -> INT 40-47
 */
static void pic_remap(void)
{
    /* Save masks */
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);
    
    /* Start initialization sequence (ICW1) */
    outb(0x20, 0x11);  /* Master PIC */
    outb(0xA0, 0x11);  /* Slave PIC */
    io_wait();
    
    /* Set vector offsets (ICW2) */
    outb(0x21, 0x20);  /* Master: IRQ 0-7 -> INT 32-39 */
    outb(0xA1, 0x28);  /* Slave: IRQ 8-15 -> INT 40-47 */
    io_wait();
    
    /* Configure cascading (ICW3) */
    outb(0x21, 0x04);  /* Master: slave on IRQ2 */
    outb(0xA1, 0x02);  /* Slave: cascade identity */
    io_wait();
    
    /* Set 8086 mode (ICW4) */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();
    
    /* Restore masks */
    outb(0x21, mask1);
    outb(0xA1, mask2);
}

/* ========================================
 * Public Functions
 * ======================================== */

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector,
                  uint8_t type_attr, uint8_t ist)
{
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = ist & 0x07;
    idt[num].type_attr = type_attr;
    idt[num].reserved = 0;
}

void idt_init(void)
{
    KLOG_INFO("IDT", "Initializing 64-bit IDT...");
    
    /* Clear IDT */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = (struct idt_entry){0};
    }
    
    /* Remap PIC */
    pic_remap();
    
    /* Set up exception handlers (ISR 0-31) */
    idt_set_gate(0, (uint64_t)isr0, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(1, (uint64_t)isr1, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(2, (uint64_t)isr2, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 2);  /* NMI: IST2 */
    idt_set_gate(3, (uint64_t)isr3, GDT_KERNEL_CODE, IDT_TYPE_TRAP, 0);       /* Breakpoint: trap */
    idt_set_gate(4, (uint64_t)isr4, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(5, (uint64_t)isr5, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(6, (uint64_t)isr6, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(7, (uint64_t)isr7, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(8, (uint64_t)isr8, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 1);  /* Double Fault: IST1 */
    idt_set_gate(9, (uint64_t)isr9, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 3);  /* MCE: IST3 */
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(22, (uint64_t)isr22, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(23, (uint64_t)isr23, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(24, (uint64_t)isr24, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(25, (uint64_t)isr25, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(26, (uint64_t)isr26, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(27, (uint64_t)isr27, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(28, (uint64_t)isr28, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(29, (uint64_t)isr29, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(30, (uint64_t)isr30, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(31, (uint64_t)isr31, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);
    
    /* Set up IRQ handlers (IRQ 0-15 -> INT 32-47) */
    idt_set_gate(32, (uint64_t)irq0, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* Timer */
    idt_set_gate(33, (uint64_t)irq1, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* Keyboard */
    idt_set_gate(34, (uint64_t)irq2, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* Cascade */
    idt_set_gate(35, (uint64_t)irq3, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* COM2 */
    idt_set_gate(36, (uint64_t)irq4, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* COM1 */
    idt_set_gate(37, (uint64_t)irq5, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* LPT2 */
    idt_set_gate(38, (uint64_t)irq6, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* Floppy */
    idt_set_gate(39, (uint64_t)irq7, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* LPT1 */
    idt_set_gate(40, (uint64_t)irq8, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* RTC */
    idt_set_gate(41, (uint64_t)irq9, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);   /* Free */
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);  /* Free */
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);  /* Network */
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);  /* PS/2 Mouse */
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);  /* FPU */
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);  /* Primary ATA */
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, IDT_TYPE_INTERRUPT, 0);  /* Secondary ATA */
    
    /* Syscall handler (INT 0x80) - accessible from Ring 3 */
    idt_set_gate(0x80, (uint64_t)isr128, GDT_KERNEL_CODE, IDT_TYPE_USER_INT, 0);
    
    /* Set up IDT pointer */
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;
    
    /* Load IDT */
    idt_flush((uint64_t)&idtr);
    
    /* Enable IRQs (unmask all) */
    outb(0x21, 0x00);  /* Master PIC */
    outb(0xA1, 0x00);  /* Slave PIC */
    
    KLOG_INFO("IDT", "IDT initialized");
    KLOG_INFO_HEX("IDT", "IDT base: ", idtr.base);
}

/* ========================================
 * Exception Handler
 * ======================================== */

/* Page fault handler from VMM */
extern void vmm_page_fault_handler(uint64_t error_code, uint64_t fault_addr);

void exception_handler(struct interrupt_frame *frame)
{
    uint64_t int_no = frame->int_no;
    uint64_t error_code = frame->error_code;
    
    /* Page Fault (exception 14) - delegate to VMM */
    if (int_no == 14) {
        uint64_t fault_addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
        vmm_page_fault_handler(error_code, fault_addr);
        return;
    }
    
    /* Debug Exception (INT 0x01) - handle TF flag and hardware breakpoints */
    if (int_no == 1) {
        uint64_t dr6, dr7;
        
        /* Read debug registers */
        __asm__ volatile("mov %%dr6, %0" : "=r"(dr6));
        __asm__ volatile("mov %%dr7, %0" : "=r"(dr7));
        
        KLOG_ERROR("DEBUG", "=== DEBUG EXCEPTION ===");
        KLOG_ERROR_HEX("DEBUG", "DR6 (status): ", (uint32_t)dr6);
        KLOG_ERROR_HEX("DEBUG", "DR7 (control): ", (uint32_t)dr7);
        KLOG_ERROR_HEX("DEBUG", "RFLAGS: ", (uint32_t)frame->rflags);
        KLOG_ERROR_HEX("DEBUG", "RIP (high): ", (uint32_t)(frame->rip >> 32));
        KLOG_ERROR_HEX("DEBUG", "RIP (low): ", (uint32_t)frame->rip);
        KLOG_ERROR_HEX("DEBUG", "RSP (high): ", (uint32_t)(frame->rsp >> 32));
        KLOG_ERROR_HEX("DEBUG", "RSP (low): ", (uint32_t)frame->rsp);
        KLOG_ERROR_HEX("DEBUG", "CS: ", (uint32_t)frame->cs);
        KLOG_ERROR_HEX("DEBUG", "SS: ", (uint32_t)frame->ss);
        
        /* Check if Trap Flag (TF, bit 8) is set */
        if (frame->rflags & (1 << 8)) {
            KLOG_ERROR("DEBUG", "*** TRAP FLAG IS SET - Clearing ***");
            /* Clear TF in saved RFLAGS so execution can continue */
            frame->rflags &= ~(1ULL << 8);
        }
        
        /* Clear DR6 (debug status register) */
        __asm__ volatile("xor %%rax, %%rax; mov %%rax, %%dr6" ::: "rax");
        
        /* If only TF was the cause, we can continue */
        /* DR6 bit 14 (BS) indicates single-step trap */
        if (dr6 & (1 << 14)) {
            KLOG_ERROR("DEBUG", "Single-step trap - continuing");
            return;
        }
        
        /* For other debug causes (hardware breakpoints), continue for now */
        KLOG_ERROR("DEBUG", "Debug exception handled - continuing");
        return;
    }
    
    /* Kernel panic for other exceptions - use serial only */
    cli();
    
    const char *name = (int_no < 32) ? exception_names[int_no] : "Unknown";
    
    KLOG_ERROR("PANIC", "=== KERNEL PANIC ===");
    KLOG_ERROR("PANIC", name);
    KLOG_ERROR_HEX("PANIC", "INT: ", (uint32_t)int_no);
    KLOG_ERROR_HEX("PANIC", "Error code: ", (uint32_t)error_code);
    KLOG_ERROR_HEX("PANIC", "RIP (high): ", (uint32_t)(frame->rip >> 32));
    KLOG_ERROR_HEX("PANIC", "RIP (low): ", (uint32_t)frame->rip);
    KLOG_ERROR_HEX("PANIC", "RSP (high): ", (uint32_t)(frame->rsp >> 32));
    KLOG_ERROR_HEX("PANIC", "RSP (low): ", (uint32_t)frame->rsp);
    KLOG_ERROR("PANIC", "System halted.");
    
    /* Halt */
    while (1) {
        __asm__ volatile("hlt");
    }
}

/* ========================================
 * IRQ Handler
 * ======================================== */

/* External handlers */
extern void timer_handler_c(void);
extern void keyboard_handler_c(void);
extern void mouse_irq_handler(void);
extern void network_irq_handler(void);
extern void ata_irq_handler(void);

void irq_handler(struct interrupt_frame *frame)
{
    uint64_t irq = frame->int_no - 32;
    
    switch (irq) {
        case 0:  /* Timer */
            timer_handler_c();
            break;
        case 1:  /* Keyboard */
            keyboard_handler_c();
            break;
        case 11: /* Network */
            network_irq_handler();
            break;
        case 12: /* PS/2 Mouse */
            mouse_irq_handler();
            break;
        case 14: /* Primary ATA */
            ata_irq_handler();
            break;
        case 15: /* Secondary ATA */
            /* Ignore for now */
            break;
        default:
            /* Unknown IRQ */
            break;
    }
    
    /* Send EOI to PIC */
    if (irq >= 8) {
        outb(0xA0, 0x20);  /* Slave PIC */
    }
    outb(0x20, 0x20);      /* Master PIC */
}
