/* src/arch/x86_64/usermode.c - User mode support for x86-64 */
#include "usermode.h"
#include "gdt.h"
#include "cpu.h"
#include "../../kernel/klog.h"

/* Kernel stack for syscalls (separate from interrupt stack) */
static uint8_t syscall_stack[16384] __attribute__((aligned(16)));

/**
 * Initialize user mode support.
 * Sets up TSS and SYSCALL/SYSRET mechanism.
 */
void init_usermode(void)
{
    /* Set up SYSCALL kernel stack */
    uint64_t syscall_rsp = (uint64_t)&syscall_stack[sizeof(syscall_stack)];
    syscall_set_kernel_stack(syscall_rsp);
    
    /* Initialize SYSCALL/SYSRET MSRs */
    syscall_init_msr();
    
    KLOG_INFO("USERMODE", "User mode support initialized");
    KLOG_INFO_HEX("USERMODE", "SYSCALL stack: ", syscall_rsp);
}
