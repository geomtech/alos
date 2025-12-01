/* src/kernel/syscall.c - System Calls Implementation */
#include "syscall.h"
#include "console.h"
#include "process.h"
#include "klog.h"
#include "../arch/x86/idt.h"
#include "../shell/shell.h"

/* ========================================
 * Déclarations externes
 * ======================================== */

/* Handler ASM dans interrupts.s */
extern void syscall_handler_asm(void);

/* ========================================
 * Implémentation des Syscalls
 * ======================================== */

/**
 * SYS_EXIT (1) - Terminer le processus courant
 * 
 * @param status  Code de sortie (dans EBX)
 */
static int sys_exit(int status)
{
    KLOG_INFO("SYSCALL", "sys_exit called with status:");
    KLOG_INFO_HEX("SYSCALL", "  Exit code: ", (uint32_t)status);
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("\n[SYSCALL] Process exited with code: ");
    console_put_dec(status);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Retourner au shell */
    /* Note: Ceci est temporaire - idéalement on devrait faire un vrai process_exit() */
    asm volatile("sti");  /* Réactiver les interruptions */
    shell_run();          /* Relancer le shell */
    
    /* Ne devrait jamais arriver */
    while (1) {
        asm volatile("hlt");
    }
    
    return 0; /* Jamais atteint */
}

/**
 * SYS_WRITE (4) - Écrire une chaîne
 * 
 * @param fd      File descriptor (ignoré pour l'instant, tout va à la console)
 * @param buf     Pointeur vers la chaîne (dans EBX)
 * @param count   Nombre de caractères (dans ECX), 0 = null-terminated
 */
static int sys_write(int fd, const char* buf, uint32_t count)
{
    (void)fd;  /* Ignoré pour l'instant */
    
    if (buf == NULL) {
        return -1;
    }
    
    KLOG_INFO("SYSCALL", "sys_write called");
    KLOG_INFO_HEX("SYSCALL", "  Buffer at: ", (uint32_t)buf);
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    
    if (count == 0) {
        /* Null-terminated string */
        console_puts(buf);
    } else {
        /* Écrire exactement count caractères */
        for (uint32_t i = 0; i < count && buf[i] != '\0'; i++) {
            console_putc(buf[i]);
        }
    }
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}

/**
 * SYS_GETPID (20) - Obtenir le PID du processus courant
 */
static int sys_getpid(void)
{
    if (current_process != NULL) {
        return current_process->pid;
    }
    return -1;
}

/* ========================================
 * Dispatcher principal
 * ======================================== */

void syscall_dispatcher(syscall_regs_t* regs)
{
    uint32_t syscall_num = regs->eax;
    int result = -1;
    
    KLOG_INFO("SYSCALL", "=== Syscall Dispatcher ===");
    KLOG_INFO_HEX("SYSCALL", "Syscall number (EAX): ", syscall_num);
    KLOG_INFO_HEX("SYSCALL", "Arg1 (EBX): ", regs->ebx);
    KLOG_INFO_HEX("SYSCALL", "Arg2 (ECX): ", regs->ecx);
    KLOG_INFO_HEX("SYSCALL", "Arg3 (EDX): ", regs->edx);
    
    switch (syscall_num) {
        case SYS_EXIT:
            result = sys_exit((int)regs->ebx);
            break;
            
        case SYS_WRITE:
            result = sys_write((int)regs->ebx, (const char*)regs->ecx, regs->edx);
            break;
            
        case SYS_GETPID:
            result = sys_getpid();
            break;
            
        default:
            KLOG_ERROR("SYSCALL", "Unknown syscall number!");
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("[SYSCALL] Unknown syscall: ");
            console_put_dec(syscall_num);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            result = -1;
            break;
    }
    
    /* Retourner le résultat dans EAX */
    regs->eax = (uint32_t)result;
}

/* ========================================
 * Initialisation
 * ======================================== */

void syscall_init(void)
{
    KLOG_INFO("SYSCALL", "=== Initializing Syscall Interface ===");
    
    /* 
     * Enregistrer l'interruption 0x80 avec DPL=3.
     * CRITIQUE: Le DPL doit être 3 pour que Ring 3 puisse déclencher l'interruption !
     * 
     * Flags = 0xEE:
     *   - Bit 7 (0x80): Present = 1
     *   - Bits 5-6 (0x60): DPL = 3 (Ring 3 autorisé)
     *   - Bit 4 (0x00): Storage Segment = 0 (pour une gate)
     *   - Bits 0-3 (0x0E): Type = 0xE (32-bit Interrupt Gate)
     *   
     *   0x80 | 0x60 | 0x0E = 0xEE
     */
    extern void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
    
    /* 0x08 = Kernel Code Segment, 0xEE = Present | DPL=3 | 32-bit Interrupt Gate */
    idt_set_gate(0x80, (uint32_t)syscall_handler_asm, 0x08, 0xEE);
    
    KLOG_INFO("SYSCALL", "INT 0x80 registered (DPL=3)");
    KLOG_INFO("SYSCALL", "Syscall interface ready!");
}
