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

void jump_to_usermode(void (*function)(void))
{
    KLOG_INFO("USER", "Preparing jump to User Mode (Ring 3)...");
    
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
    uint32_t user_esp = (uint32_t)user_stack + USER_STACK_SIZE;
    uint32_t user_eip = (uint32_t)function;
    
    KLOG_INFO_HEX("USER", "User stack base: ", (uint32_t)user_stack);
    KLOG_INFO_HEX("USER", "User stack top (ESP): ", user_esp);
    KLOG_INFO_HEX("USER", "User entry point (EIP): ", user_eip);
    
    /* 
     * IMPORTANT: Rendre les pages accessibles en User Mode !
     * Sans cela, on aura un Page Fault dès qu'on saute en Ring 3.
     */
    
    /* 1. La page contenant le code de la fonction utilisateur */
    vmm_set_user_accessible(user_eip, PAGE_SIZE);
    
    /* 2. La stack utilisateur */
    vmm_set_user_accessible((uint32_t)user_stack, USER_STACK_SIZE);
    
    /* 3. La mémoire VGA pour pouvoir afficher quelque chose */
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
 * Elle n'a PAS accès aux fonctions kernel (console, etc.)
 * car celles-ci nécessitent des syscalls (pas encore implémentés).
 * 
 * Pour l'instant, on fait juste une boucle infinie.
 * Si on n'a pas de Triple Fault, c'est que ça marche !
 */
void user_mode_test(void)
{
    /* 
     * En Ring 3, on ne peut pas :
     * - Accéder directement à la mémoire VGA (0xB8000) sans mapping
     * - Utiliser les ports I/O (sauf si IOPL=3)
     * - Exécuter des instructions privilégiées (cli, sti, hlt, etc.)
     * 
     * Pour l'instant, on fait juste une boucle infinie.
     * Plus tard, on implémentera des syscalls pour interagir avec le kernel.
     */
    
    /* Simple boucle infinie avec un compteur */
    volatile uint32_t counter = 0;
    
    while (1) {
        counter++;
        
        /* 
         * On pourrait essayer d'écrire directement en mémoire VGA
         * mais ça pourrait causer une Page Fault si la mémoire n'est
         * pas mappée pour l'utilisateur.
         * 
         * Pour un vrai test visuel, on utilise la mémoire VGA directement
         * (risqué mais permet de voir si on est vraiment en Ring 3)
         */
        
        /* Écriture directe en VGA - caractère en haut à droite de l'écran */
        /* 0xB8000 + (0 * 80 + 79) * 2 = 0xB809E */
        volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
        
        /* Afficher un caractère qui tourne: | / - \ */
        char spinner[] = "|/-\\";
        vga[79] = (uint16_t)(0x0F00 | spinner[(counter >> 16) & 3]);
        
        /* Petite pause */
        for (volatile int i = 0; i < 10000; i++);
    }
}
