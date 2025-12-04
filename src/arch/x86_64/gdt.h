/* src/arch/x86_64/gdt.h - Global Descriptor Table for x86-64 */
#ifndef X86_64_GDT_H
#define X86_64_GDT_H

#include <stdint.h>

/* ========================================
 * Segment Selectors
 * ========================================
 * Format: Index * 8 + TI (0=GDT) + RPL (0-3)
 * 
 * Layout optimized for SYSCALL/SYSRET:
 * - Index 0: Null
 * - Index 1: Kernel Code 64-bit (0x08)
 * - Index 2: Kernel Data (0x10)
 * - Index 3: User Data (0x18, selector 0x1B with RPL=3)
 * - Index 4: User Code 64-bit (0x20, selector 0x23 with RPL=3)
 * - Index 5-6: TSS (16 bytes in 64-bit mode)
 */
#define GDT_NULL            0x00
#define GDT_KERNEL_CODE     0x08    /* Index 1, Ring 0 */
#define GDT_KERNEL_DATA     0x10    /* Index 2, Ring 0 */
#define GDT_USER_DATA       0x1B    /* Index 3, Ring 3 (0x18 + 3) */
#define GDT_USER_CODE       0x23    /* Index 4, Ring 3 (0x20 + 3) */
#define GDT_TSS             0x28    /* Index 5, Ring 0 */

/* Number of GDT entries (including 16-byte TSS) */
#define GDT_ENTRIES         7

/* ========================================
 * GDT Entry Structure (8 bytes)
 * ======================================== */
struct gdt_entry {
    uint16_t limit_low;     /* Limit bits 0-15 */
    uint16_t base_low;      /* Base bits 0-15 */
    uint8_t  base_middle;   /* Base bits 16-23 */
    uint8_t  access;        /* Access byte */
    uint8_t  granularity;   /* Granularity and limit bits 16-19 */
    uint8_t  base_high;     /* Base bits 24-31 */
} __attribute__((packed));

/* ========================================
 * TSS Entry Structure (16 bytes in 64-bit)
 * ======================================== */
struct tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle1;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_middle2;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

/* ========================================
 * GDT Pointer Structure
 * ======================================== */
struct gdt_ptr {
    uint16_t limit;         /* Size of GDT - 1 */
    uint64_t base;          /* Address of GDT */
} __attribute__((packed));

/* ========================================
 * Task State Segment (64-bit)
 * ======================================== */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;          /* Stack pointer for Ring 0 */
    uint64_t rsp1;          /* Stack pointer for Ring 1 (unused) */
    uint64_t rsp2;          /* Stack pointer for Ring 2 (unused) */
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt Stack Table 1 */
    uint64_t ist2;          /* Interrupt Stack Table 2 */
    uint64_t ist3;          /* Interrupt Stack Table 3 */
    uint64_t ist4;          /* Interrupt Stack Table 4 */
    uint64_t ist5;          /* Interrupt Stack Table 5 */
    uint64_t ist6;          /* Interrupt Stack Table 6 */
    uint64_t ist7;          /* Interrupt Stack Table 7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O Permission Bitmap offset */
} __attribute__((packed));

/* ========================================
 * Functions
 * ======================================== */

/**
 * Initialize the GDT with all necessary segments.
 */
void gdt_init(void);

/**
 * Set the kernel stack pointer in the TSS.
 * Called during context switches to update RSP0.
 */
void tss_set_rsp0(uint64_t rsp0);

/**
 * Get the TSS structure.
 */
struct tss* tss_get(void);

#endif /* X86_64_GDT_H */
