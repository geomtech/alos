/* src/kernel/process.h - Process/Thread Management */
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

/* ========================================
 * Constantes
 * ======================================== */

#define KERNEL_STACK_SIZE   4096    /* Taille de la stack kernel par thread (4 KiB) */
#define MAX_PROCESSES       64      /* Nombre max de processus */

/* États des processus */
typedef enum {
    PROCESS_STATE_READY,        /* Prêt à être exécuté */
    PROCESS_STATE_RUNNING,      /* En cours d'exécution */
    PROCESS_STATE_BLOCKED,      /* En attente (I/O, sleep, etc.) */
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
 * Structure représentant un processus/thread.
 */
typedef struct process {
    uint32_t pid;                   /* Process ID unique */
    char name[32];                  /* Nom du processus (debug) */
    process_state_t state;          /* État du processus */
    volatile int should_terminate;  /* Flag pour demander l'arrêt (CTRL+C) */
    
    /* ===== Context ===== */
    uint32_t esp;                   /* Stack Pointer sauvegardé */
    uint32_t esp0;                  /* Stack Pointer kernel (base) */
    
    /* ===== Mémoire ===== */
    uint32_t* page_directory;       /* Page Directory (pour l'instant = kernel) */
    
    /* ===== Stack ===== */
    void* stack_base;               /* Base de la stack allouée (pour kfree) */
    uint32_t stack_size;            /* Taille de la stack */
    
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

/* ========================================
 * Fonction ASM (définie dans switch.s)
 * ======================================== */

/**
 * Effectue le context switch.
 * 
 * @param old_esp_ptr  Pointeur où sauvegarder l'ESP actuel
 * @param new_esp      Nouvel ESP à charger
 */
extern void switch_task(uint32_t* old_esp_ptr, uint32_t new_esp);

#endif /* PROCESS_H */
