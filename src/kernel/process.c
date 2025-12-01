/* src/kernel/process.c - Process/Thread Management Implementation */
#include "process.h"
#include "console.h"
#include "klog.h"
#include "../mm/kheap.h"
#include "../mm/vmm.h"
#include "../include/string.h"

/* ========================================
 * Variables globales
 * ======================================== */

/* Processus actuellement en cours d'exécution */
process_t* current_process = NULL;

/* Liste de tous les processus (tête de la liste circulaire) */
process_t* process_list = NULL;

/* Processus idle (le kernel main) */
process_t* idle_process = NULL;

/* Compteur de PID */
static uint32_t next_pid = 0;

/* Flag pour savoir si le multitasking est actif */
static int multitasking_enabled = 0;

/* Déclaration externe du point d'entrée ASM */
extern void task_entry_point(void);

/* ========================================
 * Fonctions utilitaires
 * ======================================== */

/**
 * Copie une chaîne de manière sécurisée.
 */
static void safe_strcpy(char* dest, const char* src, uint32_t max_len)
{
    uint32_t i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

/* ========================================
 * Implémentation
 * ======================================== */

void init_multitasking(void)
{
    KLOG_INFO("TASK", "=== Initializing Multitasking ===");
    
    /* Créer le processus idle (représente le kernel actuel) */
    idle_process = (process_t*)kmalloc(sizeof(process_t));
    if (idle_process == NULL) {
        KLOG_ERROR("TASK", "Failed to allocate idle process!");
        return;
    }
    
    /* Initialiser le processus idle */
    idle_process->pid = next_pid++;
    safe_strcpy(idle_process->name, "kernel_idle", sizeof(idle_process->name));
    idle_process->state = PROCESS_STATE_RUNNING;
    idle_process->should_terminate = 0;
    
    /* L'ESP sera sauvegardé lors du premier switch */
    idle_process->esp = 0;
    idle_process->esp0 = 0;
    
    /* Utiliser le Page Directory du kernel */
    idle_process->page_directory = (uint32_t*)vmm_get_directory();
    
    /* Pas de stack allouée (on utilise la stack du kernel) */
    idle_process->stack_base = NULL;
    idle_process->stack_size = 0;
    
    /* Liste circulaire : pointe vers lui-même */
    idle_process->next = idle_process;
    idle_process->prev = idle_process;
    
    /* Définir comme processus courant et tête de liste */
    current_process = idle_process;
    process_list = idle_process;
    
    /* Activer le multitasking */
    multitasking_enabled = 1;
    
    KLOG_INFO("TASK", "Multitasking initialized");
    KLOG_INFO_DEC("TASK", "Idle process PID: ", idle_process->pid);
}

process_t* create_kernel_thread(void (*function)(void), const char* name)
{
    if (!multitasking_enabled) {
        KLOG_ERROR("TASK", "Multitasking not initialized!");
        return NULL;
    }
    
    KLOG_INFO("TASK", "Creating kernel thread:");
    KLOG_INFO("TASK", name);
    
    /* Allouer la structure du processus */
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (proc == NULL) {
        KLOG_ERROR("TASK", "Failed to allocate process structure!");
        return NULL;
    }
    
    /* Allouer la stack kernel pour ce thread */
    void* stack = kmalloc(KERNEL_STACK_SIZE);
    if (stack == NULL) {
        KLOG_ERROR("TASK", "Failed to allocate kernel stack!");
        kfree(proc);
        return NULL;
    }
    
    /* Initialiser le processus */
    proc->pid = next_pid++;
    safe_strcpy(proc->name, name, sizeof(proc->name));
    proc->state = PROCESS_STATE_READY;
    proc->should_terminate = 0;
    
    /* Page Directory (partagé avec le kernel pour les threads kernel) */
    proc->page_directory = (uint32_t*)vmm_get_directory();
    
    /* Stack */
    proc->stack_base = stack;
    proc->stack_size = KERNEL_STACK_SIZE;
    
    /* ========================================
     * Préparer la stack initiale
     * ========================================
     * 
     * La stack doit ressembler à celle d'un processus interrompu
     * par switch_task(). Quand on fera switch_task vers ce processus,
     * il "reprendra" comme s'il avait été interrompu.
     * 
     * Layout de la stack (du haut vers le bas, adresses décroissantes):
     * 
     * [stack_top - 0]  : (Libre)
     * [stack_top - 4]  : Adresse de la fonction (sera pop par task_entry_point)
     * [stack_top - 8]  : Adresse de retour (task_entry_point)
     * [stack_top - 12] : EBX (sauvegardé)
     * [stack_top - 16] : ESI (sauvegardé)
     * [stack_top - 20] : EDI (sauvegardé)
     * [stack_top - 24] : EBP (sauvegardé) <- ESP initial
     */
    
    uint32_t* stack_top = (uint32_t*)((uint32_t)stack + KERNEL_STACK_SIZE);
    
    /* Placer l'adresse de la fonction thread */
    *(--stack_top) = (uint32_t)function;        /* Adresse de la fonction */
    
    /* Placer l'adresse de task_entry_point comme "adresse de retour" */
    *(--stack_top) = (uint32_t)task_entry_point; /* EIP - où ret sautera */
    
    /* Placer les registres callee-saved (initialisés à 0) */
    *(--stack_top) = 0;                          /* EBX */
    *(--stack_top) = 0;                          /* ESI */
    *(--stack_top) = 0;                          /* EDI */
    *(--stack_top) = 0;                          /* EBP */
    
    /* L'ESP initial pointe vers le haut de ces registres */
    proc->esp = (uint32_t)stack_top;
    proc->esp0 = (uint32_t)stack + KERNEL_STACK_SIZE;
    
    /* ========================================
     * Ajouter à la liste circulaire
     * ======================================== */
    
    /* Désactiver les interruptions pendant la modification de la liste */
    asm volatile("cli");
    
    /* Insérer après le processus courant */
    proc->next = current_process->next;
    proc->prev = current_process;
    current_process->next->prev = proc;
    current_process->next = proc;
    
    /* Réactiver les interruptions */
    asm volatile("sti");
    
    KLOG_INFO_DEC("TASK", "Thread created with PID: ", proc->pid);
    KLOG_INFO_HEX("TASK", "Stack at: ", (uint32_t)stack);
    KLOG_INFO_HEX("TASK", "Initial ESP: ", proc->esp);
    
    return proc;
}

void schedule(void)
{
    /* Ne rien faire si le multitasking n'est pas actif */
    if (!multitasking_enabled || current_process == NULL) {
        return;
    }
    
    /* Ne rien faire s'il n'y a qu'un seul processus */
    if (current_process->next == current_process) {
        return;
    }
    
    /* Trouver le prochain processus READY */
    process_t* next = current_process->next;
    process_t* start = next;
    
    do {
        if (next->state == PROCESS_STATE_READY || next->state == PROCESS_STATE_RUNNING) {
            break;
        }
        next = next->next;
    } while (next != start);
    
    /* Si c'est le même processus, ne pas switcher */
    if (next == current_process) {
        return;
    }
    
    /* Effectuer le context switch */
    switch_to(next);
}

void switch_to(process_t* next)
{
    if (next == NULL || next == current_process) {
        return;
    }
    
    /* Sauvegarder le processus actuel */
    process_t* prev = current_process;
    
    /* Marquer l'ancien comme READY (s'il tournait) */
    if (prev->state == PROCESS_STATE_RUNNING) {
        prev->state = PROCESS_STATE_READY;
    }
    
    /* Marquer le nouveau comme RUNNING */
    next->state = PROCESS_STATE_RUNNING;
    current_process = next;
    
    /* Changer de Page Directory si nécessaire */
    /* (Pour l'instant tous les threads kernel partagent le même) */
    /* if (next->page_directory != prev->page_directory) {
        vmm_switch_directory((page_directory_t*)next->page_directory);
    } */
    
    /* Effectuer le context switch ASM */
    switch_task(&prev->esp, next->esp);
}

void process_exit(void)
{
    if (current_process == NULL || current_process == idle_process) {
        /* Ne pas terminer le processus idle ! */
        KLOG_ERROR("TASK", "Cannot exit idle process!");
        for (;;) asm volatile("hlt");
    }
    
    KLOG_INFO("TASK", "Process exiting:");
    KLOG_INFO("TASK", current_process->name);
    
    /* Désactiver les interruptions - resteront désactivées jusqu'au STI dans task_entry_point */
    asm volatile("cli");
    
    /* Marquer comme terminé */
    current_process->state = PROCESS_STATE_TERMINATED;
    
    /* Retirer de la liste circulaire */
    current_process->prev->next = current_process->next;
    current_process->next->prev = current_process->prev;
    
    /* Sauvegarder le pointeur pour libérer la mémoire plus tard */
    process_t* old_process = current_process;
    
    /* Passer au processus suivant */
    process_t* next = current_process->next;
    current_process = next;
    next->state = PROCESS_STATE_RUNNING;
    
    /* Charger le contexte du processus suivant */
    /* Note: on ne peut pas utiliser switch_task car on n'a pas d'ancien ESP valide */
    /* Les interruptions seront réactivées par le STI dans task_entry_point */
    /* ou après le ret si c'est un processus existant */
    uint32_t new_esp = next->esp;
    
    /* TODO: Ajouter old_process à une liste de "zombie" pour libération ultérieure */
    /* On ne peut pas kfree() ici car on est encore sur sa stack potentiellement */
    (void)old_process;
    
    asm volatile(
        "mov %0, %%esp\n"
        "pop %%ebp\n"
        "pop %%edi\n"
        "pop %%esi\n"
        "pop %%ebx\n"
        "sti\n"          /* Réactiver les interruptions juste avant ret */
        "ret\n"
        : : "r"(new_esp)
    );
    
    /* On ne devrait jamais arriver ici */
    for (;;) asm volatile("hlt");
}

uint32_t getpid(void)
{
    if (current_process == NULL) {
        return 0;
    }
    return current_process->pid;
}

void yield(void)
{
    schedule();
}

int should_exit(void)
{
    if (current_process == NULL) {
        return 0;
    }
    return current_process->should_terminate;
}

void process_list_debug(void)
{
    if (process_list == NULL) {
        console_puts("No processes.\n");
        return;
    }
    
    console_puts("\n=== Process List ===\n");
    console_puts("PID  State    Name\n");
    console_puts("---  -----    ----\n");
    
    process_t* proc = process_list;
    do {
        /* PID */
        console_put_dec(proc->pid);
        console_puts("    ");
        
        /* State */
        switch (proc->state) {
            case PROCESS_STATE_READY:      console_puts("READY  "); break;
            case PROCESS_STATE_RUNNING:    console_puts("RUN    "); break;
            case PROCESS_STATE_BLOCKED:    console_puts("BLOCK  "); break;
            case PROCESS_STATE_TERMINATED: console_puts("TERM   "); break;
            default:                       console_puts("???    "); break;
        }
        console_puts("  ");
        
        /* Name */
        console_puts(proc->name);
        
        /* Current indicator */
        if (proc == current_process) {
            console_puts(" <-- current");
        }
        console_puts("\n");
        
        proc = proc->next;
    } while (proc != process_list);
    
    console_puts("====================\n");
}

void kill_all_user_tasks(void)
{
    if (process_list == NULL || idle_process == NULL) {
        return;
    }
    
    /* Désactiver les interruptions pendant la modification */
    asm volatile("cli");
    
    /* Compter les tâches à tuer */
    int killed_count = 0;
    
    /* Parcourir tous les processus et marquer should_terminate */
    process_t* proc = idle_process->next;
    
    while (proc != idle_process) {
        process_t* next_proc = proc->next;
        
        /* Demander au thread de s'arrêter */
        proc->should_terminate = 1;
        
        /* Marquer comme terminé et retirer de la liste */
        proc->state = PROCESS_STATE_TERMINATED;
        killed_count++;
        
        /* Passer au suivant */
        proc = next_proc;
    }
    
    /* Reconstruire la liste : seulement idle */
    idle_process->next = idle_process;
    idle_process->prev = idle_process;
    
    /* Le processus courant DOIT être idle maintenant */
    current_process = idle_process;
    current_process->state = PROCESS_STATE_RUNNING;
    
    /* Réactiver les interruptions */
    asm volatile("sti");
    
    if (killed_count > 0) {
        console_puts("\nKilled ");
        console_put_dec(killed_count);
        console_puts(" task(s)\n");
    }
}
