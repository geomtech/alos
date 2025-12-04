/* src/arch/x86_64/cpu.h - CPU control and MSR access for x86-64 */
#ifndef X86_64_CPU_H
#define X86_64_CPU_H

#include <stdint.h>

/* ========================================
 * Model Specific Registers (MSRs)
 * ======================================== */

/* SYSCALL/SYSRET MSRs */
#define MSR_EFER            0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR            0xC0000081  /* Segment selectors for SYSCALL/SYSRET */
#define MSR_LSTAR           0xC0000082  /* Long mode SYSCALL target RIP */
#define MSR_CSTAR           0xC0000083  /* Compatibility mode SYSCALL target (unused) */
#define MSR_SFMASK          0xC0000084  /* SYSCALL RFLAGS mask */

/* EFER bits */
#define EFER_SCE            (1 << 0)    /* SYSCALL Enable */
#define EFER_LME            (1 << 8)    /* Long Mode Enable */
#define EFER_LMA            (1 << 10)   /* Long Mode Active */
#define EFER_NXE            (1 << 11)   /* No-Execute Enable */

/* FS/GS Base MSRs */
#define MSR_FS_BASE         0xC0000100
#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102  /* Swapped with GS_BASE on SWAPGS */

/* APIC MSRs */
#define MSR_APIC_BASE       0x0000001B

/* ========================================
 * Control Registers
 * ======================================== */

/**
 * Read CR0 register.
 */
static inline uint64_t read_cr0(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

/**
 * Write CR0 register.
 */
static inline void write_cr0(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr0" : : "r"(val) : "memory");
}

/**
 * Read CR2 register (page fault linear address).
 */
static inline uint64_t read_cr2(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

/**
 * Read CR3 register (page table base).
 */
static inline uint64_t read_cr3(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

/**
 * Write CR3 register (page table base).
 */
static inline void write_cr3(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

/**
 * Read CR4 register.
 */
static inline uint64_t read_cr4(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

/**
 * Write CR4 register.
 */
static inline void write_cr4(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr4" : : "r"(val) : "memory");
}

/* ========================================
 * MSR Access
 * ======================================== */

/**
 * Read a Model Specific Register.
 */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/**
 * Write a Model Specific Register.
 */
static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

/* ========================================
 * TLB Management
 * ======================================== */

/**
 * Invalidate a single TLB entry.
 */
static inline void invlpg(uint64_t addr)
{
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/**
 * Flush entire TLB by reloading CR3.
 */
static inline void flush_tlb(void)
{
    write_cr3(read_cr3());
}

/* ========================================
 * CPU Identification
 * ======================================== */

/**
 * Execute CPUID instruction.
 */
static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, 
                         uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

/**
 * Execute CPUID with subleaf.
 */
static inline void cpuid_ext(uint32_t leaf, uint32_t subleaf,
                             uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

/* ========================================
 * Segment Registers
 * ======================================== */

/**
 * Read CS register.
 */
static inline uint16_t read_cs(void)
{
    uint16_t val;
    __asm__ volatile("mov %%cs, %0" : "=r"(val));
    return val;
}

/**
 * Read DS register.
 */
static inline uint16_t read_ds(void)
{
    uint16_t val;
    __asm__ volatile("mov %%ds, %0" : "=r"(val));
    return val;
}

/**
 * Read SS register.
 */
static inline uint16_t read_ss(void)
{
    uint16_t val;
    __asm__ volatile("mov %%ss, %0" : "=r"(val));
    return val;
}

/**
 * Load DS register.
 */
static inline void load_ds(uint16_t sel)
{
    __asm__ volatile("mov %0, %%ds" : : "r"(sel));
}

/**
 * Load ES register.
 */
static inline void load_es(uint16_t sel)
{
    __asm__ volatile("mov %0, %%es" : : "r"(sel));
}

/**
 * Load FS register.
 */
static inline void load_fs(uint16_t sel)
{
    __asm__ volatile("mov %0, %%fs" : : "r"(sel));
}

/**
 * Load GS register.
 */
static inline void load_gs(uint16_t sel)
{
    __asm__ volatile("mov %0, %%gs" : : "r"(sel));
}

/**
 * Load SS register.
 */
static inline void load_ss(uint16_t sel)
{
    __asm__ volatile("mov %0, %%ss" : : "r"(sel));
}

/* ========================================
 * Initialization
 * ======================================== */

/**
 * Initialize CPU-specific features.
 */
void cpu_init(void);

/**
 * Initialize SYSCALL/SYSRET mechanism.
 */
void syscall_init_msr(void);

#endif /* X86_64_CPU_H */
