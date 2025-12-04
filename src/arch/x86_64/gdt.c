/* src/arch/x86_64/gdt.c - Global Descriptor Table for x86-64 */
#include "gdt.h"
#include "../../kernel/klog.h"

/* ========================================
 * Static Data
 * ======================================== */

/* GDT entries - 7 entries (including 16-byte TSS) */
static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));

/* GDT pointer */
static struct gdt_ptr gdtr;

/* Task State Segment */
static struct tss tss __attribute__((aligned(16)));

/* Kernel stack for interrupts (16 KiB) */
static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

/* IST stacks for critical exceptions (8 KiB each) */
static uint8_t ist1_stack[8192] __attribute__((aligned(16)));  /* Double Fault */
static uint8_t ist2_stack[8192] __attribute__((aligned(16)));  /* NMI */
static uint8_t ist3_stack[8192] __attribute__((aligned(16)));  /* Machine Check */

/* ========================================
 * Assembly Functions
 * ======================================== */

/* Load GDT and reload segments (defined in assembly) */
extern void gdt_flush(uint64_t gdtr_ptr);
extern void tss_flush(uint16_t tss_selector);

/* ========================================
 * Helper Functions
 * ======================================== */

/**
 * Set a GDT entry.
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit, 
                          uint8_t access, uint8_t granularity)
{
    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;
    
    gdt[index].limit_low = limit & 0xFFFF;
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    
    gdt[index].access = access;
}

/**
 * Set the TSS entry in the GDT (16 bytes in 64-bit mode).
 */
static void gdt_set_tss(int index, uint64_t base, uint32_t limit)
{
    struct tss_entry *tss_entry = (struct tss_entry *)&gdt[index];
    
    tss_entry->limit_low = limit & 0xFFFF;
    tss_entry->base_low = base & 0xFFFF;
    tss_entry->base_middle1 = (base >> 16) & 0xFF;
    tss_entry->access = 0x89;  /* Present, 64-bit TSS Available */
    tss_entry->granularity = ((limit >> 16) & 0x0F);
    tss_entry->base_middle2 = (base >> 24) & 0xFF;
    tss_entry->base_high = (base >> 32) & 0xFFFFFFFF;
    tss_entry->reserved = 0;
}

/* ========================================
 * Public Functions
 * ======================================== */

void gdt_init(void)
{
    KLOG_INFO("GDT", "Initializing 64-bit GDT...");
    
    /* Clear GDT */
    for (int i = 0; i < GDT_ENTRIES; i++) {
        gdt[i] = (struct gdt_entry){0};
    }
    
    /* Index 0: Null Descriptor (required) */
    gdt_set_entry(0, 0, 0, 0, 0);
    
    /* 
     * Index 1: Kernel Code Segment (64-bit)
     * Access: 0x9A = Present(1) + DPL(0) + Code(1) + Exec(1) + Readable(1)
     * Granularity: 0x20 = Long mode (L=1, D=0)
     */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0x20);
    
    /*
     * Index 2: Kernel Data Segment
     * Access: 0x92 = Present(1) + DPL(0) + Data(0) + Writable(1)
     * Granularity: 0x00 (no special flags for data segments in 64-bit)
     */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0x00);
    
    /*
     * Index 3: User Data Segment
     * Access: 0xF2 = Present(1) + DPL(3) + Data(0) + Writable(1)
     * Note: Must come before User Code for SYSRET to work!
     */
    gdt_set_entry(3, 0, 0xFFFFF, 0xF2, 0x00);
    
    /*
     * Index 4: User Code Segment (64-bit)
     * Access: 0xFA = Present(1) + DPL(3) + Code(1) + Exec(1) + Readable(1)
     * Granularity: 0x20 = Long mode (L=1, D=0)
     */
    gdt_set_entry(4, 0, 0xFFFFF, 0xFA, 0x20);
    
    /* Initialize TSS */
    tss = (struct tss){0};
    tss.rsp0 = (uint64_t)&kernel_stack[sizeof(kernel_stack)];
    tss.ist1 = (uint64_t)&ist1_stack[sizeof(ist1_stack)];  /* Double Fault */
    tss.ist2 = (uint64_t)&ist2_stack[sizeof(ist2_stack)];  /* NMI */
    tss.ist3 = (uint64_t)&ist3_stack[sizeof(ist3_stack)];  /* Machine Check */
    tss.iopb_offset = sizeof(struct tss);  /* No IOPB */
    
    /* Index 5-6: TSS (16 bytes in 64-bit mode) */
    gdt_set_tss(5, (uint64_t)&tss, sizeof(struct tss) - 1);
    
    /* Set up GDT pointer */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)&gdt;
    
    /* Load GDT */
    gdt_flush((uint64_t)&gdtr);
    
    /* Load TSS */
    tss_flush(GDT_TSS);
    
    KLOG_INFO("GDT", "GDT initialized");
    KLOG_INFO_HEX("GDT", "GDT base: ", gdtr.base);
    KLOG_INFO_HEX("GDT", "TSS RSP0: ", tss.rsp0);
}

void tss_set_rsp0(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}

struct tss* tss_get(void)
{
    return &tss;
}
