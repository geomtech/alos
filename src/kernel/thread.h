/* src/kernel/thread.h - Thread Management (Multithreading avancé) */
#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================
 * Constantes
 * ======================================== */

#define THREAD_NAME_MAX         32          /* Longueur max du nom de thread */
#define THREAD_DEFAULT_STACK_SIZE (16 * 1024)  /* 16 KiB par défaut */
#define THREAD_MAGIC            0x54485244  /* 'THRD' */
#define MAX_THREADS             64          /* Nombre max de threads */

/* Time slice par défaut (en ticks) */
#define THREAD_TIME_SLICE_DEFAULT 10

/* Nice values - Unix convention */
#define THREAD_NICE_MIN         -20     /* Highest priority */
#define THREAD_NICE_MAX         19      /* Lowest priority */
#define THREAD_NICE_DEFAULT     0       /* Normal priority */

/* Aging threshold for Rocket Boost (in ticks) */
#define THREAD_AGING_THRESHOLD  100     /* 100ms before boost */

/* ========================================
 * Interrupt Frame - Contexte CPU complet
 * Utilisé pour la préemption depuis IRQ
 * ======================================== */

typedef struct interrupt_frame {
    /* Registres poussés par pusha (ordre inverse) */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;  /* ESP ignoré par popa */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    
    /* Poussés par le CPU lors de l'interruption */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    /* Si changement de ring (Ring 3 → Ring 0) : */
    uint32_t user_esp;
    uint32_t user_ss;
} __attribute__((packed)) interrupt_frame_t;

/* ========================================
 * Types de priorité
 * ======================================== */

typedef enum {
    THREAD_PRIORITY_IDLE = 0,       /* Idle - seulement quand rien d'autre */
    THREAD_PRIORITY_BACKGROUND,     /* Tâches de fond */
    THREAD_PRIORITY_NORMAL,         /* Priorité normale */
    THREAD_PRIORITY_HIGH,           /* Haute priorité */
    THREAD_PRIORITY_UI,             /* Interface utilisateur - réactive */
    THREAD_PRIORITY_COUNT
} thread_priority_t;

/* ========================================
 * États des threads
 * ======================================== */

typedef enum {
    THREAD_STATE_READY,             /* Prêt à être exécuté */
    THREAD_STATE_RUNNING,           /* En cours d'exécution */
    THREAD_STATE_BLOCKED,           /* Bloqué (wait queue, sleep, I/O) */
    THREAD_STATE_SLEEPING,          /* En attente (sleep) */
    THREAD_STATE_ZOMBIE             /* Terminé, en attente de cleanup */
} thread_state_t;

/* ========================================
 * Structures forward declarations
 * ======================================== */

typedef struct thread thread_t;
typedef struct process process_t;
typedef struct wait_queue wait_queue_t;
typedef struct spinlock spinlock_t;

/* Point d'entrée de thread */
typedef void (*thread_entry_t)(void *arg);

/* Prédicat pour wait_queue_wait */
typedef bool (*wait_queue_predicate_t)(void *context);

/* ========================================
 * Spinlock
 * ======================================== */

struct spinlock {
    volatile uint32_t value;
};

static inline void spinlock_init(spinlock_t *lock)
{
    if (lock) {
        lock->value = 0;
    }
}

static inline void spinlock_lock(spinlock_t *lock)
{
    if (!lock) return;
    while (__sync_lock_test_and_set(&lock->value, 1) != 0) {
        while (lock->value) {
            __asm__ volatile("pause");
        }
    }
}

static inline void spinlock_unlock(spinlock_t *lock)
{
    if (!lock) return;
    __sync_lock_release(&lock->value);
}

static inline bool spinlock_trylock(spinlock_t *lock)
{
    if (!lock) return false;
    return __sync_lock_test_and_set(&lock->value, 1) == 0;
}

/* ========================================
 * Wait Queue - Synchronisation
 * ======================================== */

struct wait_queue {
    thread_t *head;
    thread_t *tail;
    spinlock_t lock;
};

/* ========================================
 * Structure Thread
 * ======================================== */

struct thread {
    /* Identité */
    uint32_t tid;                   /* Thread ID unique */
    char name[THREAD_NAME_MAX];     /* Nom du thread (debug) */
    uint32_t magic;                 /* Magic number pour validation */
    
    /* Lien avec le processus */
    process_t *owner;               /* Processus propriétaire */
    
    /* État */
    thread_state_t state;           /* État actuel */
    volatile int should_terminate;  /* Flag pour demander l'arrêt */
    bool exited;                    /* Thread a appelé exit */
    int exit_status;                /* Code de sortie */
    
    /* Contexte CPU */
    uint32_t esp;                   /* Stack Pointer sauvegardé */
    uint32_t esp0;                  /* Kernel Stack top */
    
    /* Stack */
    void *stack_base;               /* Base de la stack allouée */
    uint32_t stack_size;            /* Taille de la stack */
    
    /* Point d'entrée */
    thread_entry_t entry;           /* Fonction d'entrée */
    void *arg;                      /* Argument pour la fonction */
    
    /* Scheduling */
    thread_priority_t base_priority;    /* Priorité de base */
    thread_priority_t priority;         /* Priorité actuelle (peut être boostée) */
    uint32_t time_slice_remaining;      /* Ticks restants avant préemption */

    /* Nice value and aging (Rocket Boost) */
    int8_t nice;                        /* Nice value: -20 (high) to +19 (low) */
    bool is_boosted;                    /* Thread is temporarily boosted by aging */
    uint64_t wait_start_tick;           /* When thread entered wait/ready state */

    /* CPU accounting */
    uint64_t cpu_ticks;                 /* Total CPU time consumed (in ticks) */
    uint64_t context_switches;          /* Number of times scheduled */
    uint64_t run_start_tick;            /* When thread started running (for accounting) */

    /* SMP preparation */
    uint32_t cpu_affinity;              /* CPU affinity mask (0xFFFFFFFF = any CPU) */
    uint32_t last_cpu;                  /* Last CPU this thread ran on */

    /* Sleep */
    uint64_t wake_tick;             /* Tick auquel réveiller le thread */
    
    /* Wait queue */
    wait_queue_t *waiting_queue;    /* Queue sur laquelle on attend */
    thread_t *wait_queue_next;      /* Prochain dans la wait queue */
    
    /* Liste chaînée pour le scheduler */
    thread_t *sched_next;           /* Prochain dans la run queue */
    thread_t *sched_prev;           /* Précédent dans la run queue */
    
    /* Liste de tous les threads d'un process */
    thread_t *proc_next;            /* Prochain thread du même process */
    
    /* Préemption */
    volatile uint32_t preempt_count;    /* > 0 = préemption désactivée */
    volatile bool preempt_pending;      /* Préemption demandée mais différée */
};

/* ========================================
 * Fonctions publiques - Préemption Control
 * ======================================== */

/**
 * Désactive la préemption (peut être imbriqué).
 */
void preempt_disable(void);

/**
 * Réactive la préemption et préempte si nécessaire.
 */
void preempt_enable(void);

/**
 * Vérifie si la préemption est activée.
 */
bool preempt_enabled(void);

/* ========================================
 * Fonctions publiques - Wait Queue
 * ======================================== */

/**
 * Initialise une wait queue.
 */
void wait_queue_init(wait_queue_t *queue);

/**
 * Attend sur une wait queue jusqu'à ce que le prédicat soit vrai.
 * Bloque le thread courant si le prédicat est faux.
 */
void wait_queue_wait(wait_queue_t *queue, wait_queue_predicate_t predicate, void *context);

/**
 * Réveille un thread de la wait queue.
 */
void wait_queue_wake_one(wait_queue_t *queue);

/**
 * Réveille tous les threads de la wait queue.
 */
void wait_queue_wake_all(wait_queue_t *queue);

/* ========================================
 * Fonctions publiques - Thread
 * ======================================== */

/**
 * Crée un nouveau thread kernel.
 */
thread_t *thread_create(const char *name, thread_entry_t entry, void *arg, 
                        uint32_t stack_size, thread_priority_t priority);

/**
 * Crée un thread dans un processus existant.
 */
thread_t *thread_create_in_process(process_t *proc, const char *name,
                                   thread_entry_t entry, void *arg,
                                   uint32_t stack_size, thread_priority_t priority);

/**
 * Termine le thread courant.
 */
void thread_exit(int status) __attribute__((noreturn));

/**
 * Attend la terminaison d'un thread.
 * @return Code de sortie du thread
 */
int thread_join(thread_t *thread);

/**
 * Tue un thread.
 */
bool thread_kill(thread_t *thread, int status);

/**
 * Retourne le thread courant.
 */
thread_t *thread_current(void);

/**
 * Retourne le TID du thread courant.
 */
uint32_t thread_get_tid(void);

/**
 * Définit la priorité d'un thread.
 */
void thread_set_priority(thread_t *thread, thread_priority_t priority);

/**
 * Définit la nice value d'un thread (-20 à +19).
 * Recalcule automatiquement la priorité.
 */
void thread_set_nice(thread_t *thread, int8_t nice);

/**
 * Retourne la nice value d'un thread.
 */
int8_t thread_get_nice(thread_t *thread);

/**
 * Retourne le temps CPU utilisé par un thread (en millisecondes).
 */
uint64_t thread_get_cpu_time_ms(thread_t *thread);

/**
 * Cède le CPU au scheduler.
 */
void thread_yield(void);

/**
 * Met le thread courant en sommeil pour un nombre de ticks.
 */
void thread_sleep_ticks(uint64_t ticks);

/**
 * Met le thread courant en sommeil pour un nombre de millisecondes.
 */
void thread_sleep_ms(uint32_t ms);

/**
 * Vérifie si le thread doit s'arrêter.
 */
bool thread_should_exit(void);

/**
 * Retourne le nom de l'état d'un thread.
 */
const char *thread_state_name(thread_state_t state);

/**
 * Retourne le nom de la priorité d'un thread.
 */
const char *thread_priority_name(thread_priority_t priority);

/* ========================================
 * Scheduler
 * ======================================== */

/**
 * Initialise le scheduler de threads.
 */
void scheduler_init(void);

/**
 * Lance le scheduler (appelé une fois au boot).
 */
void scheduler_start(void);

/**
 * Appelé par le timer IRQ pour la préemption.
 */
void scheduler_tick(void);

/**
 * Appelé par l'IRQ timer pour préempter depuis le contexte d'interruption.
 * @param frame Pointeur vers les registres sauvegardés sur la stack
 * @return Nouveau ESP si préemption, 0 si pas de changement
 */
uint32_t scheduler_preempt(interrupt_frame_t *frame);

/**
 * Force un context switch.
 */
void scheduler_schedule(void);

/**
 * Ajoute un thread à la run queue.
 */
void scheduler_enqueue(thread_t *thread);

/**
 * Retire un thread de la run queue.
 */
void scheduler_dequeue(thread_t *thread);

/**
 * Réveille les threads dont le sleep est terminé.
 */
void scheduler_wake_sleeping(void);

/* ========================================
 * Debug
 * ======================================== */

/**
 * Affiche les threads en cours.
 */
void thread_list_debug(void);

#endif /* THREAD_H */
