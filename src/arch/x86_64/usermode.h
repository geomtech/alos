/* src/arch/x86_64/usermode.h - User mode support for x86-64 */
#ifndef X86_64_USERMODE_H
#define X86_64_USERMODE_H

#include <stdint.h>

/**
 * Initialize user mode support.
 * Sets up TSS and syscall mechanism.
 */
void init_usermode(void);

/**
 * Update the kernel stack pointer in TSS.
 * Called during context switches.
 */
void tss_update_rsp0(uint64_t rsp0);

/**
 * Jump to user mode.
 * 
 * @param rsp  User stack pointer
 * @param rip  User instruction pointer (entry point)
 * @param cr3  User page table (0 = use current)
 */
extern void jump_to_user(uint64_t rsp, uint64_t rip, uint64_t cr3);

/**
 * Set the kernel stack for SYSCALL instruction.
 */
extern void syscall_set_kernel_stack(uint64_t rsp);

#endif /* X86_64_USERMODE_H */
