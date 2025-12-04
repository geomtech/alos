/* src/arch/x86_64/tss.c - TSS management for x86-64 */
#include "gdt.h"
#include "../../kernel/klog.h"

/*
 * In x86-64, the TSS is primarily used for:
 * 1. RSP0 - Kernel stack pointer for Ring 0 (used on privilege level change)
 * 2. IST1-7 - Interrupt Stack Table entries for specific exceptions
 * 
 * The TSS is initialized in gdt.c, this file provides helper functions.
 */

/**
 * Update the kernel stack pointer in the TSS.
 * Called during context switches to set the stack for the next
 * interrupt/syscall that causes a privilege level change.
 */
void tss_update_rsp0(uint64_t rsp0)
{
    tss_set_rsp0(rsp0);
}

/* init_usermode is defined in usermode.c */
