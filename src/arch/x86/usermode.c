/* src/arch/x86/usermode.c - User Mode (Ring 3) Implementation */
#include "usermode.h"
#include "tss.h"
#include "gdt.h"
#include "../../mm/kheap.h"
#include "../../mm/vmm.h"
#include "../../kernel/console.h"
#include "../../kernel/klog.h"

/* ========================================
 * Variables globales
 * ======================================== */

/* Stack utilisateur allouée */
static void* user_stack = NULL;

/* ========================================
 * Implémentation
 * ======================================== */

void init_usermode(void)
{
    KLOG_INFO("USER", "=== Initializing User Mode Support ===");
    
    /* Récupérer l'adresse actuelle de la stack kernel */
    uint32_t kernel_stack;
    asm volatile("mov %%esp, %0" : "=r"(kernel_stack));
    
    /* Initialiser le TSS avec la stack kernel actuelle */
    /* Note: on pourrait utiliser une stack dédiée, mais pour l'instant
     * on utilise la stack kernel principale */
    init_tss(kernel_stack);
    
    KLOG_INFO("USER", "TSS initialized");
    KLOG_INFO_HEX("USER", "Kernel stack (esp0): ", kernel_stack);
    KLOG_INFO("USER", "User Mode support ready");
}

void jump_to_usermode(void (*function)(void), void* custom_esp)
{
    KLOG_INFO("USER", "Preparing jump to User Mode (Ring 3)...");
    
    uint32_t user_esp;
    
    /* Utiliser la stack fournie ou en allouer une nouvelle */
    if (custom_esp != NULL) {
        user_esp = (uint32_t)custom_esp;
        KLOG_INFO("USER", "Using provided user stack");
        KLOG_INFO_HEX("USER", "Custom ESP: ", user_esp);
        
        /* Rendre la stack utilisateur accessible (quelques pages en dessous de ESP) */
        /* La stack grandit vers le bas, donc on doit mapper les pages en dessous de ESP */
        uint32_t stack_bottom = (user_esp - (16 * PAGE_SIZE)) & ~(PAGE_SIZE - 1);
        vmm_set_user_accessible(stack_bottom, 16 * PAGE_SIZE);
    } else {
        /* Allouer une stack pour le mode utilisateur */
        if (user_stack == NULL) {
            /* Allouer plus pour garantir l'alignement sur une page */
            user_stack = kmalloc(USER_STACK_SIZE + PAGE_SIZE);
            if (user_stack == NULL) {
                KLOG_ERROR("USER", "Failed to allocate user stack!");
                return;
            }
            /* Aligner sur une page */
            user_stack = (void*)(((uint32_t)user_stack + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
        }
        
        /* La stack grandit vers le bas, donc on commence au sommet */
        user_esp = (uint32_t)user_stack + USER_STACK_SIZE;
        
        /* Rendre la stack utilisateur accessible */
        vmm_set_user_accessible((uint32_t)user_stack, USER_STACK_SIZE);
    }
    
    uint32_t user_eip = (uint32_t)function;
    
    KLOG_INFO_HEX("USER", "User stack top (ESP): ", user_esp);
    KLOG_INFO_HEX("USER", "User entry point (EIP): ", user_eip);
    
    /* 
     * IMPORTANT: Rendre les pages accessibles en User Mode !
     * Sans cela, on aura un Page Fault dès qu'on saute en Ring 3.
     */
    
    /* 1. La page contenant le code de la fonction utilisateur */
    vmm_set_user_accessible(user_eip, PAGE_SIZE);
    
    /* 2. La mémoire VGA pour pouvoir afficher quelque chose */
    vmm_set_user_accessible(0xB8000, PAGE_SIZE);
    
    /* Mettre à jour le TSS avec notre stack kernel actuelle */
    uint32_t current_esp;
    asm volatile("mov %%esp, %0" : "=r"(current_esp));
    tss_set_kernel_stack(current_esp);
    
    KLOG_INFO("USER", ">>> Jumping to Ring 3 <<<");
    
    /* Le grand saut ! */
    enter_usermode(user_esp, user_eip);
    
    /* On ne devrait jamais arriver ici */
    KLOG_ERROR("USER", "Returned from User Mode!? This should not happen!");
}

/**
 * Fonction de test pour le mode utilisateur.
 * 
 * ATTENTION: Cette fonction s'exécute en Ring 3 !
 * Elle utilise les syscalls pour communiquer avec le kernel.
 */
void user_mode_test(void)
{
    /* 
     * En Ring 3, on ne peut plus appeler directement les fonctions kernel.
     * On doit utiliser les syscalls via INT 0x80.
     * 
     * Convention:
     *   EAX = numéro du syscall
     *   EBX = argument 1
     *   ECX = argument 2
     *   EDX = argument 3
     */
    
    /* Message à afficher */
    const char* msg = "\n*** Hello from Ring 3 via Syscall! ***\n";
    
    /* SYS_WRITE (4): Afficher le message */
    /* arg1 (EBX) = fd (ignoré), arg2 (ECX) = buffer, arg3 (EDX) = count (0=null-term) */
    int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(4), "b"(0), "c"(msg), "d"(0)
        : "memory"
    );
    
    /* Deuxième message */
    const char* msg2 = "Syscalls are working! User Mode is fully operational.\n";
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(4), "b"(0), "c"(msg2), "d"(0)
        : "memory"
    );
    
    /* SYS_EXIT (1): Terminer le processus */
    /* arg1 (EBX) = exit code */
    asm volatile(
        "int $0x80"
        :
        : "a"(1), "b"(0)
    );
    
    /* Ne devrait jamais arriver ici */
    while (1);
}
