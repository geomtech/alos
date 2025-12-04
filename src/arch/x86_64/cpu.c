/* src/arch/x86_64/cpu.c - CPU initialization for x86-64 */
#include "cpu.h"
#include "gdt.h"
#include "../../kernel/klog.h"

/* External syscall entry point (defined in interrupts.s) */
extern void syscall_entry(void);

/**
 * Initialize CPU-specific features for x86-64.
 */
void cpu_init(void)
{
    /* Enable NX bit (No-Execute) if available */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_NXE;
    wrmsr(MSR_EFER, efer);
    
    KLOG_INFO("CPU", "x86-64 CPU initialized");
    KLOG_INFO_HEX("CPU", "EFER: ", efer);
}

/**
 * Initialize SYSCALL/SYSRET mechanism.
 * 
 * This sets up the MSRs needed for fast system calls:
 * - STAR: Contains segment selectors for SYSCALL/SYSRET
 * - LSTAR: Contains the kernel entry point for SYSCALL
 * - SFMASK: Contains the RFLAGS mask (bits to clear on SYSCALL)
 */
void syscall_init_msr(void)
{
    /* Enable SYSCALL/SYSRET in EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
    
    /* 
     * STAR MSR format:
     * Bits 63:48 - SYSRET CS and SS (CS = value, SS = value + 8)
     *              For user mode: CS = 0x1B (user code), SS = 0x23 (user data)
     *              But SYSRET adds 16 to get CS and 8 to get SS
     *              So we store: (0x18 | 3) for CS, which becomes 0x1B
     *              Actually: SYSRET loads CS from STAR[63:48]+16, SS from STAR[63:48]+8
     *              So for user CS=0x1B, SS=0x23: store 0x0B (0x1B-16=0x0B, but with RPL...)
     *              
     * Actually the correct way:
     * SYSRET: CS = STAR[63:48] + 16, SS = STAR[63:48] + 8
     * For CS=0x1B (user code 64-bit): STAR[63:48] = 0x1B - 16 = 0x0B... but that's wrong
     * 
     * The standard layout is:
     * - Kernel CS at GDT index 1 (selector 0x08)
     * - Kernel DS at GDT index 2 (selector 0x10)
     * - User DS at GDT index 3 (selector 0x18, with RPL=3 -> 0x1B)
     * - User CS at GDT index 4 (selector 0x20, with RPL=3 -> 0x23)
     * 
     * Wait, that's different from our current GDT layout. Let me recalculate:
     * Our GDT: Null(0), KCode(1), KData(2), UCode(3), UData(4), TSS(5)
     * 
     * For SYSCALL: CS = STAR[47:32], SS = STAR[47:32] + 8
     * For SYSRET to 64-bit: CS = STAR[63:48] + 16, SS = STAR[63:48] + 8
     * 
     * We want:
     * - SYSCALL: CS = 0x08 (kernel code), SS = 0x10 (kernel data)
     * - SYSRET: CS = 0x1B (user code), SS = 0x23 (user data)
     * 
     * So: STAR[47:32] = 0x08 (kernel CS)
     *     STAR[63:48] = 0x1B - 16 = 0x0B... but that gives SS = 0x13, not 0x23
     * 
     * The issue is our GDT layout. For SYSRET to work correctly, we need:
     * - User data segment right after kernel data
     * - User code segment right after user data
     * 
     * Standard AMD64 layout for SYSCALL/SYSRET:
     * Index 1: Kernel Code (0x08)
     * Index 2: Kernel Data (0x10)
     * Index 3: User Data (0x18, selector 0x1B with RPL=3)
     * Index 4: User Code (0x20, selector 0x23 with RPL=3)
     * 
     * Then: STAR[63:48] = 0x13 (0x10 | 3)
     *       SYSRET: CS = 0x13 + 16 = 0x23, SS = 0x13 + 8 = 0x1B
     * 
     * Hmm, that gives CS=0x23 and SS=0x1B, which is swapped!
     * 
     * Actually for 64-bit SYSRET:
     * CS.Selector = STAR[63:48] + 16
     * SS.Selector = STAR[63:48] + 8
     * 
     * So if we want CS=0x23, SS=0x1B:
     * STAR[63:48] = 0x23 - 16 = 0x13
     * SS = 0x13 + 8 = 0x1B ✓
     * 
     * This means User Code must be at selector 0x23 and User Data at 0x1B.
     * Our current layout has them swapped. We need to fix the GDT.
     * 
     * For now, let's use the layout that works:
     * STAR[47:32] = 0x08 (kernel code selector for SYSCALL)
     * STAR[63:48] = 0x13 (base for SYSRET: CS=0x23, SS=0x1B)
     */
    
    /* 
     * With our GDT layout (UCode=0x18, UData=0x20), we need to swap them
     * for SYSCALL/SYSRET to work. The GDT will be updated in gdt.c.
     * 
     * New layout:
     * Index 3: User Data (0x18, selector 0x1B with RPL=3)  
     * Index 4: User Code 64-bit (0x20, selector 0x23 with RPL=3)
     * 
     * STAR = (0x13 << 48) | (0x08 << 32)
     * SYSCALL: CS = 0x08, SS = 0x10
     * SYSRET 64-bit: CS = 0x13 + 16 = 0x23, SS = 0x13 + 8 = 0x1B
     */
    uint64_t star = ((uint64_t)0x0013 << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);
    
    /* LSTAR = kernel entry point for SYSCALL */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    
    /* 
     * SFMASK = RFLAGS bits to clear on SYSCALL
     * Clear: IF (interrupts), TF (trap), DF (direction), AC (alignment check)
     * This ensures a clean state when entering kernel mode
     */
    wrmsr(MSR_SFMASK, 0x47700);  /* Clear IF, TF, DF, AC, NT, IOPL */
    
    KLOG_INFO("CPU", "SYSCALL/SYSRET initialized");
    KLOG_INFO_HEX("CPU", "STAR: ", star);
    KLOG_INFO_HEX("CPU", "LSTAR: ", (uint64_t)syscall_entry);
}
