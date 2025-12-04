/* src/kernel/process.c - Process/Thread Management Implementation */
#include "process.h"
#include "thread.h"
#include "console.h"
#include "klog.h"
#include "workqueue.h"
#include "elf.h"
#include "../mm/kheap.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../include/string.h"
#include "../fs/vfs.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/usermode.h"

/* ========================================
 * Constantes
 * ======================================== */

#define KERNEL_STACK_SIZE   16384   /* Taille de la stack kernel par thread (16 KiB) */

/* Utiliser la définition de usermode.h si disponible, sinon définir ici */
#ifndef USER_STACK_SIZE
#define USER_STACK_SIZE     (16 * PAGE_SIZE)  /* 64 KiB */
#endif
#ifdef USER_STACK_SIZE
#undef USER_STACK_SIZE
#define USER_STACK_SIZE     (16 * PAGE_SIZE)  /* 64 KiB - Override pour le kernel */
#endif

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
    
    /* Initialiser le scheduler de threads */
    scheduler_init();
    
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
    idle_process->exit_status = 0;
    
    /* L'ESP sera sauvegardé lors du premier switch */
    idle_process->rsp = 0;
    idle_process->rsp0 = 0;
    
    /* Utiliser le Page Directory du kernel */
    idle_process->pml4 = (uint64_t*)vmm_get_kernel_directory();
    idle_process->cr3 = (uint64_t)idle_process->pml4;  /* Adresse physique pour CR3 */
    
    /* Pas de stack allouée (on utilise la stack du kernel) */
    idle_process->stack_base = NULL;
    idle_process->stack_size = 0;
    
    /* Threads */
    idle_process->main_thread = NULL;
    idle_process->thread_list = NULL;
    idle_process->thread_count = 0;
    
    /* Wait queue pour process_join */
    wait_queue_init(&idle_process->wait_queue);
    
    /* Hiérarchie */
    idle_process->parent = NULL;
    idle_process->first_child = NULL;
    idle_process->sibling_next = NULL;
    idle_process->sibling_prev = NULL;
    
    /* Liste circulaire : pointe vers lui-même */
    idle_process->next = idle_process;
    idle_process->prev = idle_process;
    
    /* Définir comme processus courant et tête de liste */
    current_process = idle_process;
    process_list = idle_process;
    
    /* Activer le multitasking */
    multitasking_enabled = 1;
    
    /* Démarrer le scheduler */
    scheduler_start();

    /* Initialize the reaper thread for zombie cleanup */
    reaper_init();

    /* Initialize the kernel worker pool */
    workqueue_init();

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
    proc->pml4 = (uint64_t*)vmm_get_kernel_directory();
    proc->cr3 = (uint64_t)proc->pml4;  /* Threads kernel partagent le même CR3 */
    
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
    
    uint64_t* stack_top = (uint64_t*)((uint64_t)stack + KERNEL_STACK_SIZE);
    
    /* Placer l'adresse de la fonction thread */
    *(--stack_top) = (uint64_t)function;        /* Adresse de la fonction */
    
    /* Placer l'adresse de task_entry_point comme "adresse de retour" */
    *(--stack_top) = (uint64_t)task_entry_point; /* EIP - où ret sautera */
    
    /* Placer les registres callee-saved (initialisés à 0) */
    *(--stack_top) = 0;                          /* EBX */
    *(--stack_top) = 0;                          /* ESI */
    *(--stack_top) = 0;                          /* EDI */
    *(--stack_top) = 0;                          /* EBP */
    
    /* L'ESP initial pointe vers le haut de ces registres */
    proc->rsp = (uint64_t)stack_top;
    proc->rsp0 = (uint64_t)stack + KERNEL_STACK_SIZE;
    
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
    KLOG_INFO_HEX("TASK", "Stack at: ", (uint64_t)stack);
    KLOG_INFO_HEX("TASK", "Initial ESP: ", proc->rsp);
    
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
    
    /* ========================================
     * Mettre à jour le TSS
     * ========================================
     * Si le nouveau processus est interrompu en Ring 3, le CPU
     * utilisera esp0 du TSS comme stack kernel pour sauvegarder
     * le contexte.
     */
    if (next->rsp0 != 0) {
        tss_set_rsp0(next->rsp0);
    }
    
    /* Effectuer le context switch ASM avec changement de CR3 */
    /* Note: Le changement de Page Directory (CR3) est fait dans switch_task */
    /* pour garantir une transition atomique entre les espaces mémoire */
    switch_task(&prev->rsp, next->rsp, next->cr3);
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
    uint64_t new_rsp = next->rsp;
    
    /* TODO: Ajouter old_process à une liste de "zombie" pour libération ultérieure */
    /* On ne peut pas kfree() ici car on est encore sur sa stack potentiellement */
    (void)old_process;
    
    /* Restaurer le contexte x86-64 (callee-saved registers) */
    asm volatile(
        "mov %0, %%rsp\n"
        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%rbp\n"
        "pop %%rbx\n"
        "sti\n"          /* Réactiver les interruptions juste avant ret */
        "ret\n"
        : : "r"(new_rsp)
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

/* ========================================
 * Exécution de programmes ELF (User Mode)
 * ======================================== */

/* Adresse de base pour les programmes utilisateur */
#define USER_STACK_TOP      0xBFFFF000  /* Sommet de la stack utilisateur */
#define USER_STACK_SIZE     (16 * PAGE_SIZE)  /* 64 KiB */

/**
 * Exécute un programme ELF en créant un nouveau processus User Mode.
 * 
 * @param filename  Chemin du fichier ELF à exécuter
 * @return          PID du nouveau processus, ou -1 si erreur
 */
int process_execute(const char* filename)
{
    if (!multitasking_enabled) {
        KLOG_ERROR("EXEC", "Multitasking not initialized!");
        return -1;
    }
    
    KLOG_INFO("EXEC", "=== Executing Program ===");
    KLOG_INFO("EXEC", filename);
    
    /* Vérifier si le fichier est un ELF valide */
    if (!elf_is_valid(filename)) {
        KLOG_ERROR("EXEC", "Not a valid ELF file");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: ");
        console_puts(filename);
        console_puts(" is not a valid ELF executable\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    /* Allouer la structure du processus */
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (proc == NULL) {
        KLOG_ERROR("EXEC", "Failed to allocate process structure!");
        return -1;
    }
    
    /* Allouer une stack kernel pour ce processus (pour les syscalls) */
    void* kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (kernel_stack == NULL) {
        KLOG_ERROR("EXEC", "Failed to allocate kernel stack!");
        kfree(proc);
        return -1;
    }
    
    /* Initialiser la kernel stack à 0 pour éviter les problèmes de mémoire non initialisée */
    for (uint32_t i = 0; i < KERNEL_STACK_SIZE; i++) {
        ((uint8_t*)kernel_stack)[i] = 0;
    }
    
    /* Initialiser le processus */
    proc->pid = next_pid++;
    
    /* Extraire le nom du fichier pour le nom du processus */
    const char* name = filename;
    for (const char* p = filename; *p; p++) {
        if (*p == '/') name = p + 1;
    }
    safe_strcpy(proc->name, name, sizeof(proc->name));
    
    proc->state = PROCESS_STATE_READY;
    proc->should_terminate = 0;
    
    /* Créer un nouveau Page Directory pour l'isolation mémoire */
    proc->pml4 = (uint64_t*)vmm_create_directory();
    if (proc->pml4 == NULL) {
        KLOG_ERROR("EXEC", "Failed to create page directory!");
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    proc->cr3 = (uint64_t)proc->pml4;
    
    KLOG_INFO_HEX("EXEC", "Created page directory at: ", proc->cr3);
    
    /* Stack kernel */
    proc->stack_base = kernel_stack;
    proc->stack_size = KERNEL_STACK_SIZE;
    proc->rsp0 = (uint64_t)kernel_stack + KERNEL_STACK_SIZE;
    
    /* Charger le fichier ELF */
    elf_load_result_t elf_result;
    int err = elf_load_file(filename, proc, &elf_result);
    if (err != ELF_OK) {
        KLOG_ERROR("EXEC", "Failed to load ELF file");
        vmm_free_directory((page_directory_t*)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    
    KLOG_INFO_HEX("EXEC", "Entry point: ", elf_result.entry_point);
    
    /* Allouer la stack utilisateur dans le Page Directory du processus */
    uint32_t user_stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint32_t addr = user_stack_bottom; addr < USER_STACK_TOP; addr += PAGE_SIZE) {
        if (!vmm_is_mapped_in_dir((page_directory_t*)proc->pml4, addr)) {
            void* phys_page = pmm_alloc_block();
            if (phys_page == NULL) {
                KLOG_ERROR("EXEC", "Failed to allocate user stack!");
                vmm_free_directory((page_directory_t*)proc->pml4);
                kfree(kernel_stack);
                kfree(proc);
                return -1;
            }
            if (vmm_map_page_in_dir((page_directory_t*)proc->pml4, 
                                     (uint64_t)phys_page, addr, 
                                     PAGE_PRESENT | PAGE_RW | PAGE_USER) != 0) {
                KLOG_ERROR("EXEC", "Failed to map user stack page!");
                pmm_free_block(phys_page);
                vmm_free_directory((page_directory_t*)proc->pml4);
                kfree(kernel_stack);
                kfree(proc);
                return -1;
            }
        }
    }
    
    KLOG_INFO_HEX("EXEC", "User stack top: ", USER_STACK_TOP);
    
    /* ========================================
     * Préparer la stack utilisateur
     * ========================================
     * 
     * La libc attend que _start() trouve argc et argv sur la stack:
     *   popl %eax  // argc
     *   popl %ebx  // argv
     * 
     * Donc on doit mettre sur la stack user:
     *   [ESP+0] = argc
     *   [ESP+4] = argv
     * 
     * Pour l'instant, on met argc=0 et argv=NULL.
     */
    uint32_t user_stack_data[2] = { 0, 0 };  /* argc=0, argv=NULL */
    uint32_t user_esp = USER_STACK_TOP - 8;  /* Aligné, pointe vers argc */
    
    if (vmm_copy_to_dir((page_directory_t*)proc->pml4, 
                        user_esp, user_stack_data, sizeof(user_stack_data)) != 0) {
        KLOG_ERROR("EXEC", "Failed to initialize user stack!");
        vmm_free_directory((page_directory_t*)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    
    KLOG_INFO_HEX("EXEC", "User ESP: ", user_esp);
    
    /* ========================================
     * Initialiser les champs du processus
     * ======================================== */
    
    proc->main_thread = NULL;
    proc->thread_list = NULL;
    proc->thread_count = 0;
    proc->exit_status = 0;
    
    /* Initialiser la wait queue pour waitpid */
    wait_queue_init(&proc->wait_queue);
    
    /* Hiérarchie des processus */
    proc->parent = current_process;
    proc->first_child = NULL;
    proc->sibling_next = NULL;
    proc->sibling_prev = NULL;
    
    /* ========================================
     * Créer le thread user mode
     * ========================================
     * 
     * On utilise thread_create_user() qui prépare la stack
     * pour un IRET vers Ring 3.
     */
    
    thread_t* main_thread = thread_create_user(
        proc,
        proc->name,
        elf_result.entry_point,
        user_esp,  /* ESP utilisateur avec argc/argv */
        kernel_stack,
        KERNEL_STACK_SIZE
    );
    
    if (main_thread == NULL) {
        KLOG_ERROR("EXEC", "Failed to create user thread!");
        vmm_free_directory((page_directory_t*)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    
    proc->main_thread = main_thread;
    proc->thread_list = main_thread;
    proc->thread_count = 1;
    
    /* Ne pas libérer kernel_stack ici, il appartient au thread maintenant */
    proc->stack_base = NULL;  /* Le thread gère sa propre stack */
    
    KLOG_INFO_DEC("EXEC", "Process created with PID: ", proc->pid);
    KLOG_INFO_DEC("EXEC", "Main thread TID: ", main_thread->tid);
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("Started process '");
    console_puts(proc->name);
    console_puts("' (PID ");
    console_put_dec(proc->pid);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return proc->pid;
}

/**
 * Lance immédiatement un programme ELF (bloquant).
 * Charge et exécute le programme, puis retourne au shell.
 */
int process_exec_and_wait(const char* filename, int argc, char** argv)
{
    KLOG_INFO("EXEC", "=== Execute and Wait ===");
    KLOG_INFO("EXEC", filename);
    
    /* Vérifier si le fichier est un ELF valide */
    if (!elf_is_valid(filename)) {
        KLOG_ERROR("EXEC", "Not a valid ELF file");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: ");
        console_puts(filename);
        console_puts(" is not a valid ELF executable\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    /* Allouer la structure du processus */
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (proc == NULL) {
        KLOG_ERROR("EXEC", "Failed to allocate process structure!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to allocate process structure\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    /* Allouer une stack kernel pour ce processus (pour les syscalls) */
    void* kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (kernel_stack == NULL) {
        KLOG_ERROR("EXEC", "Failed to allocate kernel stack!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to allocate kernel stack\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        kfree(proc);
        return -1;
    }
    
    /* Initialiser la kernel stack à 0 pour éviter les problèmes de mémoire non initialisée */
    for (uint32_t i = 0; i < KERNEL_STACK_SIZE; i++) {
        ((uint8_t*)kernel_stack)[i] = 0;
    }
    
    /* Initialiser le processus */
    proc->pid = next_pid++;
    
    /* Extraire le nom du fichier pour le nom du processus */
    const char* name = filename;
    for (const char* p = filename; *p; p++) {
        if (*p == '/') name = p + 1;
    }
    safe_strcpy(proc->name, name, sizeof(proc->name));
    
    proc->state = PROCESS_STATE_READY;
    proc->should_terminate = 0;
    
    /* Créer un nouveau Page Directory pour l'isolation mémoire */
    proc->pml4 = (uint64_t*)vmm_create_directory();
    if (proc->pml4 == NULL) {
        KLOG_ERROR("EXEC", "Failed to create page directory!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to create page directory\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    proc->cr3 = (uint64_t)proc->pml4;
    
    KLOG_INFO_HEX("EXEC", "Created page directory at: ", proc->cr3);
    
    /* Stack kernel */
    proc->stack_base = kernel_stack;
    proc->stack_size = KERNEL_STACK_SIZE;
    proc->rsp0 = (uint64_t)kernel_stack + KERNEL_STACK_SIZE;
    
    /* Charger le fichier ELF */
    elf_load_result_t elf_result;
    int err = elf_load_file(filename, proc, &elf_result);
    if (err != ELF_OK) {
        KLOG_ERROR("EXEC", "Failed to load ELF file");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to load ELF file (code ");
        console_put_dec(err);
        console_puts(")\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vmm_free_directory((page_directory_t*)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    
    KLOG_INFO_HEX("EXEC", "Entry point: ", elf_result.entry_point);
    
    /* Allouer la stack utilisateur dans le Page Directory du processus */
    uint32_t user_stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint32_t addr = user_stack_bottom; addr < USER_STACK_TOP; addr += PAGE_SIZE) {
        if (!vmm_is_mapped_in_dir((page_directory_t*)proc->pml4, addr)) {
            void* phys_page = pmm_alloc_block();
            if (phys_page == NULL) {
                KLOG_ERROR("EXEC", "Failed to allocate user stack!");
                console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                console_puts("Error: Failed to allocate user stack (phys memory)\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                vmm_free_directory((page_directory_t*)proc->pml4);
                kfree(kernel_stack);
                kfree(proc);
                return -1;
            }
            if (vmm_map_page_in_dir((page_directory_t*)proc->pml4, 
                                     (uint64_t)phys_page, addr, 
                                     PAGE_PRESENT | PAGE_RW | PAGE_USER) != 0) {
                KLOG_ERROR("EXEC", "Failed to map user stack page!");
                console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                console_puts("Error: Failed to map user stack page\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                pmm_free_block(phys_page);
                vmm_free_directory((page_directory_t*)proc->pml4);
                kfree(kernel_stack);
                kfree(proc);
                return -1;
            }
        }
    }
    
    KLOG_INFO_HEX("EXEC", "User stack top: ", USER_STACK_TOP);
    
    /* ========================================
     * Préparer la stack utilisateur avec argc/argv
     * ======================================== */
    
    /* Allouer un buffer pour la stack utilisateur dans le kernel */
    uint32_t stack_buffer_size = 1024;  /* 1KB devrait être suffisant pour argc/argv */
    uint8_t* stack_buffer = (uint8_t*)kmalloc(stack_buffer_size);
    if (stack_buffer == NULL) {
        KLOG_ERROR("EXEC", "Failed to allocate stack buffer!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to allocate stack buffer\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vmm_free_directory((page_directory_t*)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    
    /* Construire la stack utilisateur dans le buffer */
    uint32_t user_esp = USER_STACK_TOP;
    
    /* D'abord, copier les chaînes d'arguments */
    char* string_ptr = (char*)(stack_buffer + stack_buffer_size);  /* Commencer à la fin du buffer */
    char* argv_ptrs[16];  /* Max 16 arguments */
    
    for (int i = 0; i < argc && i < 16; i++) {
        int len = 0;
        while (argv[i][len]) len++;
        len++;  /* Inclure le null terminator */
        string_ptr -= len;
        if ((uint8_t*)string_ptr < stack_buffer) {
            KLOG_ERROR("EXEC", "Arguments too large for stack buffer!");
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("Error: Arguments too large for stack buffer\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            kfree(stack_buffer);
            vmm_free_directory((page_directory_t*)proc->pml4);
            kfree(kernel_stack);
            kfree(proc);
            return -1;
        }
        for (int j = 0; j < len; j++) {
            string_ptr[j] = argv[i][j];
        }
        argv_ptrs[i] = string_ptr;
    }
    
    /* Aligner sur 4 octets */
    string_ptr = (char*)((uint64_t)string_ptr & ~3);
    
    /* Construire le tableau argv */
    uint64_t* stack_ptr = (uint64_t*)string_ptr;
    stack_ptr--;  /* argv[argc] = NULL */
    *stack_ptr = 0;
    
    for (int i = argc - 1; i >= 0; i--) {
        stack_ptr--;
        *stack_ptr = (uint64_t)argv_ptrs[i];
    }
    
    uint32_t argv_addr = (uint64_t)stack_ptr;  /* Adresse de argv[0] */
    
    /* Pousser argv (pointeur vers argv[0]) */
    stack_ptr--;
    *stack_ptr = argv_addr;
    
    /* Pousser argc */
    stack_ptr--;
    *stack_ptr = (uint64_t)argc;
    
    /* Calculer la taille des données à copier */
    uint32_t data_size = stack_buffer_size - ((uint8_t*)stack_ptr - stack_buffer);
    
    /* Calculer l'ESP utilisateur final */
    /* Les données doivent se terminer à USER_STACK_TOP */
    user_esp = USER_STACK_TOP - data_size;
    
    /* Copier les données de la stack dans le Page Directory du processus */
    if (vmm_copy_to_dir((page_directory_t*)proc->pml4, 
                        user_esp, stack_ptr, data_size) != 0) {
        KLOG_ERROR("EXEC", "Failed to initialize user stack!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to initialize user stack (copy failed)\n");
        console_puts("  User ESP: 0x");
        console_put_hex(user_esp);
        console_puts("\n  Data size: ");
        console_put_dec(data_size);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        kfree(stack_buffer);
        vmm_free_directory((page_directory_t*)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    
    /* Libérer le buffer temporaire */
    kfree(stack_buffer);
    
    KLOG_INFO_HEX("EXEC", "User ESP: ", user_esp);
    
    /* ========================================
     * Initialiser les champs du processus
     * ======================================== */
    
    proc->main_thread = NULL;
    proc->thread_list = NULL;
    proc->thread_count = 0;
    proc->exit_status = 0;
    
    /* Initialiser la wait queue pour waitpid */
    wait_queue_init(&proc->wait_queue);
    
    /* Hiérarchie des processus */
    proc->parent = current_process;
    proc->first_child = NULL;
    proc->sibling_next = NULL;
    proc->sibling_prev = NULL;
    
    /* ========================================
     * Créer le thread user mode
     * ========================================
     * 
     * On utilise thread_create_user() qui prépare la stack
     * pour un IRET vers Ring 3.
     */
    
    thread_t* main_thread = thread_create_user(
        proc,
        proc->name,
        elf_result.entry_point,
        user_esp,  /* ESP utilisateur avec argc/argv */
        kernel_stack,
        KERNEL_STACK_SIZE
    );
    
    if (main_thread == NULL) {
        KLOG_ERROR("EXEC", "Failed to create user thread!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to create user thread\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vmm_free_directory((page_directory_t*)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return -1;
    }
    
    proc->main_thread = main_thread;
    proc->thread_list = main_thread;
    proc->thread_count = 1;
    
    /* Ne pas libérer kernel_stack ici, il appartient au thread maintenant */
    proc->stack_base = NULL;  /* Le thread gère sa propre stack */
    
    KLOG_INFO_DEC("EXEC", "Process created with PID: ", proc->pid);
    KLOG_INFO_DEC("EXEC", "Main thread TID: ", main_thread->tid);
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("Started process '");
    console_puts(filename);
    console_puts("' (PID ");
    console_put_dec(proc->pid);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* EXEC NON-BLOQUANT: On retourne immédiatement */
    /* Le scheduler se chargera d'exécuter le nouveau thread */
    /* Pas de wait loop ici ! */
    
    /* Note: On ne libère PAS le Page Directory ni la structure process */
    /* Ils seront libérés par le reaper thread quand le processus se terminera */
    
    /* Céder le CPU pour donner une chance au nouveau thread de s'exécuter.
     * C'est nécessaire car scheduler_preempt (IRQ timer) ne peut pas switcher
     * vers un thread user - seul scheduler_schedule peut le faire.
     */
    thread_yield();
    
    return 0;
}

/* ========================================
 * Nouvelles fonctions Multithreading
 * ======================================== */

process_t* process_create_kernel(const char* name, thread_entry_t entry, void* arg,
                                 uint32_t stack_size)
{
    if (!multitasking_enabled || !entry) {
        return NULL;
    }
    
    KLOG_INFO("PROC", "Creating kernel process:");
    KLOG_INFO("PROC", name ? name : "<unnamed>");
    
    /* Allouer la structure du processus */
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (!proc) {
        KLOG_ERROR("PROC", "Failed to allocate process structure");
        return NULL;
    }
    
    /* Initialiser */
    proc->pid = next_pid++;
    safe_strcpy(proc->name, name ? name : "", sizeof(proc->name));
    proc->state = PROCESS_STATE_READY;
    proc->should_terminate = 0;
    proc->exit_status = 0;
    
    proc->pml4 = (uint64_t*)vmm_get_kernel_directory();
    proc->cr3 = (uint64_t)proc->pml4;
    
    proc->stack_base = NULL;
    proc->stack_size = 0;
    proc->rsp = 0;
    proc->rsp0 = 0;
    
    proc->thread_count = 0;
    proc->thread_list = NULL;
    
    wait_queue_init(&proc->wait_queue);
    
    proc->parent = current_process;
    proc->first_child = NULL;
    proc->sibling_next = NULL;
    proc->sibling_prev = NULL;
    
    /* Créer le thread principal */
    thread_t* main_thread = thread_create_in_process(proc, name, entry, arg, 
                                                      stack_size, THREAD_PRIORITY_NORMAL);
    if (!main_thread) {
        KLOG_ERROR("PROC", "Failed to create main thread");
        kfree(proc);
        return NULL;
    }
    
    proc->main_thread = main_thread;
    proc->thread_list = main_thread;
    proc->thread_count = 1;
    
    /* Ajouter à la liste des processus */
    asm volatile("cli");
    
    proc->next = current_process->next;
    proc->prev = current_process;
    current_process->next->prev = proc;
    current_process->next = proc;
    
    /* Ajouter comme enfant du processus courant */
    if (current_process) {
        proc->sibling_next = current_process->first_child;
        if (current_process->first_child) {
            current_process->first_child->sibling_prev = proc;
        }
        current_process->first_child = proc;
    }
    
    asm volatile("sti");
    
    KLOG_INFO_DEC("PROC", "Created process PID: ", proc->pid);
    
    return proc;
}

void process_sleep_ms(uint32_t ms)
{
    thread_sleep_ms(ms);
}

void process_yield(void)
{
    thread_yield();
}

static bool process_waiting_still_running(void* context)
{
    process_t* proc = (process_t*)context;
    return proc->state == PROCESS_STATE_ZOMBIE || proc->state == PROCESS_STATE_TERMINATED;
}

int process_join(process_t* proc)
{
    if (!proc) return -1;
    
    /* Attendre que le processus se termine */
    wait_queue_wait(&proc->wait_queue, process_waiting_still_running, proc);
    
    return proc->exit_status;
}

void process_kill(process_t* proc)
{
    if (!proc || proc == idle_process) return;
    
    asm volatile("cli");
    
    proc->should_terminate = 1;
    proc->state = PROCESS_STATE_TERMINATED;
    
    /* Tuer tous les threads */
    thread_t* thread = proc->thread_list;
    while (thread) {
        thread_kill(thread, -1);
        thread = thread->proc_next;
    }
    
    /* Réveiller tous les processus en attente */
    wait_queue_wake_all(&proc->wait_queue);
    
    asm volatile("sti");
}

void process_kill_tree(process_t* proc)
{
    if (!proc) return;
    
    /* Tuer tous les enfants récursivement */
    process_t* child = proc->first_child;
    while (child) {
        process_t* next = child->sibling_next;
        process_kill_tree(child);
        child = next;
    }
    
    /* Tuer le processus lui-même */
    process_kill(proc);
}

process_t* process_current(void)
{
    return current_process;
}

bool process_is_zombie(process_t* proc)
{
    return proc && proc->state == PROCESS_STATE_ZOMBIE;
}

const char* process_state_name(process_state_t state)
{
    switch (state) {
        case PROCESS_STATE_READY:      return "READY";
        case PROCESS_STATE_RUNNING:    return "RUNNING";
        case PROCESS_STATE_BLOCKED:    return "BLOCKED";
        case PROCESS_STATE_ZOMBIE:     return "ZOMBIE";
        case PROCESS_STATE_TERMINATED: return "TERMINATED";
        default:                       return "UNKNOWN";
    }
}

size_t process_snapshot(process_info_t* buffer, size_t capacity)
{
    if (!buffer || capacity == 0) return 0;
    
    size_t count = 0;
    
    asm volatile("cli");
    
    process_t* proc = process_list;
    if (proc) {
        do {
            if (count >= capacity) break;
            
            buffer[count].pid = proc->pid;
            buffer[count].state = proc->state;
            buffer[count].name = proc->name;
            buffer[count].is_current = (proc == current_process);
            buffer[count].time_slice_remaining = 0;
            
            /* Récupérer les infos du thread principal */
            if (proc->main_thread) {
                buffer[count].thread_state = proc->main_thread->state;
                buffer[count].thread_name = proc->main_thread->name;
                buffer[count].time_slice_remaining = proc->main_thread->time_slice_remaining;
            } else {
                buffer[count].thread_state = THREAD_STATE_READY;
                buffer[count].thread_name = "";
            }
            
            count++;
            proc = proc->next;
        } while (proc != process_list);
    }
    
    asm volatile("sti");
    
    return count;
}
