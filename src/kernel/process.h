/* src/kernel/process.h - Process/Thread Management */
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "thread.h"  /* Include du nouveau système de threads */

/* ========================================
 * Constantes
 * ======================================== */

#define KERNEL_STACK_SIZE   4096    /* Taille de la stack kernel par thread (4 KiB) */
#define MAX_PROCESSES       64      /* Nombre max de processus */
#define PROCESS_NAME_MAX    32      /* Longueur max du nom de processus */

/* États des processus */
typedef enum {
    PROCESS_STATE_READY,        /* Prêt à être exécuté */
    PROCESS_STATE_RUNNING,      /* En cours d'exécution */
    PROCESS_STATE_BLOCKED,      /* En attente (I/O, sleep, etc.) */
    PROCESS_STATE_ZOMBIE,       /* Terminé, en attente de join */
    PROCESS_STATE_TERMINATED    /* Terminé */
} process_state_t;

/* ========================================
 * Structures
 * ======================================== */

/**
 * Registres CPU sauvegardés lors d'un context switch.
 * 
 * Note: On ne sauvegarde que les registres callee-saved
 * car le compilateur s'occupe des caller-saved (EAX, ECX, EDX).
 * 
 * Layout sur la stack après un switch:
 *   [EBP]  <- ESP pointe ici après les pops
 *   [EDI]
 *   [ESI]
 *   [EBX]
 *   [EIP]  <- Adresse de retour (ret sautera ici)
 */
typedef struct {
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t eip;       /* Adresse de retour */
    /* EFLAGS est géré implicitement par pushf/popf si nécessaire */
} __attribute__((packed)) context_t;

/**
 * Structure représentant un processus.
 * Un processus peut contenir plusieurs threads.
 */
typedef struct process {
    uint32_t pid;                   /* Process ID unique */
    char name[PROCESS_NAME_MAX];    /* Nom du processus (debug) */
    process_state_t state;          /* État du processus */
    volatile int should_terminate;  /* Flag pour demander l'arrêt (CTRL+C) */
    int exit_status;                /* Code de sortie */
    
    /* ===== Context ===== */
    uint32_t esp;                   /* Stack Pointer sauvegardé */
    uint32_t esp0;                  /* Stack Pointer kernel (base) */
    uint32_t cr3;                   /* Adresse physique du Page Directory (pour switch CR3) */
    
    /* ===== Mémoire ===== */
    uint32_t* page_directory;       /* Page Directory (pour l'instant = kernel) */
    
    /* ===== Stack ===== */
    void* stack_base;               /* Base de la stack allouée (pour kfree) */
    uint32_t stack_size;            /* Taille de la stack */
    
    /* ===== Threads ===== */
    thread_t* main_thread;          /* Thread principal du processus */
    thread_t* thread_list;          /* Liste de tous les threads */
    uint32_t thread_count;          /* Nombre de threads actifs */
    
    /* ===== Synchronisation ===== */
    wait_queue_t wait_queue;        /* Pour process_join */
    
    /* ===== Hiérarchie ===== */
    struct process* parent;         /* Processus parent */
    struct process* first_child;    /* Premier enfant */
    struct process* sibling_next;   /* Prochain frère */
    struct process* sibling_prev;   /* Frère précédent */
    
    /* ===== Liste chaînée circulaire ===== */
    struct process* next;           /* Processus suivant dans la liste */
    struct process* prev;           /* Processus précédent */
    
} process_t;

/* ========================================
 * Variables globales (extern)
 * ======================================== */

/* Processus actuellement en cours d'exécution */
extern process_t* current_process;

/* Liste de tous les processus (tête de la liste circulaire) */
extern process_t* process_list;

/* Processus idle (le kernel main) */
extern process_t* idle_process;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le système de multitâche.
 * Crée le processus "idle" à partir du contexte actuel.
 */
void init_multitasking(void);

/**
 * Crée un nouveau thread kernel.
 * 
 * @param function  Fonction à exécuter dans le thread
 * @param name      Nom du thread (pour debug)
 * @return          Pointeur vers le nouveau processus, ou NULL si erreur
 */
process_t* create_kernel_thread(void (*function)(void), const char* name);

/**
 * Ordonnanceur (Round Robin).
 * Passe au processus suivant dans la liste.
 */
void schedule(void);

/**
 * Force un changement de contexte vers un processus spécifique.
 * 
 * @param next  Processus vers lequel basculer
 */
void switch_to(process_t* next);

/**
 * Termine le processus courant.
 */
void process_exit(void);

/**
 * Retourne le PID du processus courant.
 */
uint32_t getpid(void);

/**
 * Affiche la liste des processus (debug).
 */
void process_list_debug(void);

/**
 * Yield - Cède volontairement le CPU au prochain processus.
 */
void yield(void);

/**
 * Tue toutes les tâches utilisateur (non-idle).
 * Appelé par CTRL+C.
 */
void kill_all_user_tasks(void);

/**
 * Vérifie si le thread courant doit s'arrêter.
 * À appeler dans les boucles des threads.
 * @return 1 si le thread doit s'arrêter, 0 sinon
 */
int should_exit(void);

/**
 * Exécute un programme ELF en créant un nouveau processus User Mode.
 * Le processus est ajouté au scheduler et s'exécutera lors du prochain cycle.
 * 
 * @param filename  Chemin du fichier ELF à exécuter
 * @return          PID du nouveau processus, ou -1 si erreur
 */
int process_execute(const char* filename);

/**
 * Lance immédiatement un programme ELF (bloquant).
 * Charge et exécute le programme, puis retourne au shell via sys_exit.
 * 
 * @param filename  Chemin du fichier ELF à exécuter
 * @param argc      Nombre d'arguments
 * @param argv      Tableau d'arguments
 * @return          0 si succès, -1 si erreur (ne retourne pas normalement)
 */
int process_exec_and_wait(const char* filename, int argc, char** argv);

/* ========================================
 * Nouvelles fonctions Multithreading
 * ======================================== */

/**
 * Crée un nouveau processus kernel.
 */
process_t* process_create_kernel(const char* name, thread_entry_t entry, void* arg, 
                                 uint32_t stack_size);

/**
 * Met le processus courant en sommeil pendant un nombre de millisecondes.
 */
void process_sleep_ms(uint32_t ms);

/**
 * Cède le CPU au scheduler.
 */
void process_yield(void);

/**
 * Attend la fin d'un processus.
 * @return Code de sortie du processus
 */
int process_join(process_t* proc);

/**
 * Tue un processus et tous ses threads.
 */
void process_kill(process_t* proc);

/**
 * Tue l'arbre de processus (process et tous ses enfants).
 */
void process_kill_tree(process_t* proc);

/**
 * Retourne le processus courant.
 */
process_t* process_current(void);

/**
 * Vérifie si un processus est zombie.
 */
bool process_is_zombie(process_t* proc);

/**
 * Retourne le nom de l'état d'un processus.
 */
const char* process_state_name(process_state_t state);

/**
 * Prend un snapshot de tous les processus.
 */
typedef struct {
    uint32_t pid;
    process_state_t state;
    thread_state_t thread_state;
    const char* name;
    const char* thread_name;
    bool is_current;
    uint32_t time_slice_remaining;
} process_info_t;

size_t process_snapshot(process_info_t* buffer, size_t capacity);

/* ========================================
 * Fonction ASM (définie dans switch.s)
 * ======================================== */

/**
 * Effectue le context switch (défini en assembleur dans switch.s).
 * 
 * @param old_esp_ptr  Pointeur où sauvegarder l'ESP actuel
 * @param new_esp      Nouvel ESP à charger
 * @param new_cr3      Adresse physique du nouveau Page Directory
 * 
 * Note: Le changement de CR3 est effectué de manière atomique dans cette
 * fonction pour garantir que le code kernel reste accessible pendant
 * la transition entre espaces mémoire.
 */
extern void switch_task(uint32_t* old_esp_ptr, uint32_t new_esp, uint32_t new_cr3);

#endif /* PROCESS_H */
