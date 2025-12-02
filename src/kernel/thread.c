/* src/kernel/thread.c - Thread Management Implementation */
#include "thread.h"
#include "process.h"
#include "console.h"
#include "klog.h"
#include "timer.h"
#include "../mm/kheap.h"
#include "../include/string.h"
#include "../arch/x86/tss.h"

/* ========================================
 * Variables globales
 * ======================================== */

/* Thread actuellement en cours d'exécution */
static thread_t *g_current_thread = NULL;

/* Run queues par priorité */
static thread_t *g_run_queues[THREAD_PRIORITY_COUNT] = { NULL };
static spinlock_t g_scheduler_lock;

/* Liste des threads en sleep */
static thread_t *g_sleep_queue = NULL;
static spinlock_t g_sleep_lock;

/* Compteur de TID */
static uint32_t g_next_tid = 1;

/* Flag scheduler actif */
static bool g_scheduler_active = false;

/* Thread idle */
static thread_t *g_idle_thread = NULL;

/* Point d'entrée ASM pour nouveaux threads */
extern void task_entry_point(void);

/* ========================================
 * Fonctions utilitaires internes
 * ======================================== */

/* Forward declarations */
static thread_priority_t scheduler_nice_to_priority(int8_t nice);
static uint32_t scheduler_get_time_slice(thread_t *thread);

static void safe_strcpy(char *dest, const char *src, uint32_t max_len)
{
    uint32_t i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static inline void cpu_cli(void)
{
    __asm__ volatile("cli");
}

static inline void cpu_sti(void)
{
    __asm__ volatile("sti");
}

static inline uint32_t cpu_save_flags(void)
{
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0" : "=r"(flags));
    return flags;
}

static inline void cpu_restore_flags(uint32_t flags)
{
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

/* ========================================
 * Wait Queue Implementation
 * ======================================== */

void wait_queue_init(wait_queue_t *queue)
{
    if (!queue) return;
    queue->head = NULL;
    queue->tail = NULL;
    spinlock_init(&queue->lock);
}

static void wait_queue_enqueue_locked(wait_queue_t *queue, thread_t *thread)
{
    if (!queue || !thread) return;
    
    thread->wait_queue_next = NULL;
    thread->waiting_queue = queue;
    
    if (queue->tail) {
        queue->tail->wait_queue_next = thread;
    } else {
        queue->head = thread;
    }
    queue->tail = thread;
}

static thread_t *wait_queue_dequeue_locked(wait_queue_t *queue)
{
    if (!queue || !queue->head) return NULL;
    
    thread_t *thread = queue->head;
    queue->head = thread->wait_queue_next;
    
    if (!queue->head) {
        queue->tail = NULL;
    }
    
    thread->wait_queue_next = NULL;
    thread->waiting_queue = NULL;
    return thread;
}

void wait_queue_wait(wait_queue_t *queue, wait_queue_predicate_t predicate, void *context)
{
    if (!queue) {
        thread_yield();
        return;
    }
    
    thread_t *thread = g_current_thread;
    if (!thread) {
        thread_yield();
        return;
    }
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&queue->lock);
    
    /* Vérifier le prédicat avant de bloquer */
    while (!predicate || !predicate(context)) {
        /* Ajouter à la wait queue */
        wait_queue_enqueue_locked(queue, thread);
        thread->state = THREAD_STATE_BLOCKED;
        
        spinlock_unlock(&queue->lock);
        
        /* Céder le CPU */
        scheduler_schedule();
        
        spinlock_lock(&queue->lock);
        
        /* Re-vérifier le prédicat */
        if (predicate && predicate(context)) {
            break;
        }
    }
    
    spinlock_unlock(&queue->lock);
    cpu_restore_flags(flags);
}

void wait_queue_wake_one(wait_queue_t *queue)
{
    if (!queue) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&queue->lock);
    thread_t *thread = wait_queue_dequeue_locked(queue);
    spinlock_unlock(&queue->lock);
    
    if (thread) {
        thread->state = THREAD_STATE_READY;
        scheduler_enqueue(thread);
    }
    
    cpu_restore_flags(flags);
}

void wait_queue_wake_all(wait_queue_t *queue)
{
    if (!queue) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&queue->lock);
    thread_t *thread;
    while ((thread = wait_queue_dequeue_locked(queue)) != NULL) {
        thread->state = THREAD_STATE_READY;
        scheduler_enqueue(thread);
    }
    spinlock_unlock(&queue->lock);
    
    cpu_restore_flags(flags);
}

/* ========================================
 * Thread Creation
 * ======================================== */

thread_t *thread_create(const char *name, thread_entry_t entry, void *arg,
                        uint32_t stack_size, thread_priority_t priority)
{
    if (!entry) {
        KLOG_ERROR("THREAD", "thread_create: entry is NULL");
        return NULL;
    }
    
    KLOG_INFO("THREAD", "Creating thread:");
    KLOG_INFO("THREAD", name ? name : "<unnamed>");
    
    /* Allouer la structure thread */
    thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
    if (!thread) {
        KLOG_ERROR("THREAD", "Failed to allocate thread structure");
        return NULL;
    }
    
    /* Allouer la stack */
    if (stack_size == 0) {
        stack_size = THREAD_DEFAULT_STACK_SIZE;
    }
    
    void *stack = kmalloc(stack_size);
    if (!stack) {
        KLOG_ERROR("THREAD", "Failed to allocate thread stack");
        kfree(thread);
        return NULL;
    }
    
    /* Initialiser la structure */
    thread->tid = g_next_tid++;
    if (name) {
        safe_strcpy(thread->name, name, THREAD_NAME_MAX);
    } else {
        thread->name[0] = '\0';
    }
    thread->magic = THREAD_MAGIC;
    
    thread->owner = NULL;  /* Thread kernel, pas de process parent */
    
    thread->state = THREAD_STATE_READY;
    thread->should_terminate = 0;
    thread->exited = false;
    thread->exit_status = 0;
    
    thread->stack_base = stack;
    thread->stack_size = stack_size;
    thread->esp0 = (uint32_t)stack + stack_size;
    
    thread->entry = entry;
    thread->arg = arg;
    
    thread->base_priority = priority;
    thread->priority = priority;
    thread->time_slice_remaining = scheduler_get_time_slice(thread);

    /* Nice value and aging */
    thread->nice = THREAD_NICE_DEFAULT;
    thread->is_boosted = false;
    thread->wait_start_tick = timer_get_ticks();

    /* CPU accounting */
    thread->cpu_ticks = 0;
    thread->context_switches = 0;
    thread->run_start_tick = 0;

    /* SMP preparation */
    thread->cpu_affinity = 0xFFFFFFFF;  /* Can run on any CPU */
    thread->last_cpu = 0;               /* Default to CPU 0 */

    thread->wake_tick = 0;
    thread->waiting_queue = NULL;
    thread->wait_queue_next = NULL;

    thread->sched_next = NULL;
    thread->sched_prev = NULL;
    thread->proc_next = NULL;

    /* Préemption */
    thread->preempt_count = 0;
    thread->preempt_pending = false;
    
    /* Préparer la stack initiale au format interrupt_frame_t
     * pour la préemption (popa + iretd).
     * 
     * L'IRQ handler fait: popa; iretd
     * Donc on doit préparer la stack comme si on avait été interrompu
     * juste avant d'appeler la fonction entry.
     *
     * Layout (du haut vers le bas - ESP pointe vers EDI):
     *   [EFLAGS]   <- iretd pop eflags (avec IF=1 pour activer les interrupts)
     *   [CS]       <- iretd pop cs (segment code kernel = 0x08)
     *   [EIP]      <- iretd pop eip (= task_entry_point_preempt)
     *   [EAX]      <- popa (= entry address)
     *   [ECX]      <- popa (= arg)
     *   [EDX]      <- popa (= 0)
     *   [EBX]      <- popa (= 0)
     *   [ESP_dummy]<- popa ignore this
     *   [EBP]      <- popa (= 0)
     *   [ESI]      <- popa (= 0)
     *   [EDI]      <- popa (= 0) <- ESP pointe ici
     */
    uint32_t *stack_top = (uint32_t *)((uint32_t)stack + stack_size);
    
    /* Simuler ce que le CPU push lors d'une interruption */
    *(--stack_top) = 0x202;                  /* EFLAGS: IF=1 (interrupts enabled) */
    *(--stack_top) = 0x08;                   /* CS: kernel code segment */
    *(--stack_top) = (uint32_t)task_entry_point;  /* EIP: point d'entrée */
    
    /* Simuler pusha (ordre: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI) */
    *(--stack_top) = (uint32_t)entry;        /* EAX = adresse de la fonction */
    *(--stack_top) = (uint32_t)arg;          /* ECX = argument */
    *(--stack_top) = 0;                      /* EDX */
    *(--stack_top) = 0;                      /* EBX */
    *(--stack_top) = 0;                      /* ESP (ignoré par popa) */
    *(--stack_top) = 0;                      /* EBP */
    *(--stack_top) = 0;                      /* ESI */
    *(--stack_top) = 0;                      /* EDI */
    
    thread->esp = (uint32_t)stack_top;
    
    KLOG_INFO_DEC("THREAD", "Created thread TID: ", thread->tid);
    KLOG_INFO_HEX("THREAD", "Stack: ", (uint32_t)stack);
    KLOG_INFO_HEX("THREAD", "ESP: ", thread->esp);
    
    /* Ajouter au scheduler */
    scheduler_enqueue(thread);
    
    return thread;
}

thread_t *thread_create_in_process(process_t *proc, const char *name,
                                   thread_entry_t entry, void *arg,
                                   uint32_t stack_size, thread_priority_t priority)
{
    thread_t *thread = thread_create(name, entry, arg, stack_size, priority);
    if (!thread) return NULL;
    
    thread->owner = proc;
    
    /* Ajouter à la liste des threads du process */
    /* TODO: implement process thread list */
    
    return thread;
}

/* ========================================
 * Thread Control
 * ======================================== */

void thread_exit(int status)
{
    thread_t *thread = g_current_thread;
    
    if (!thread || thread == g_idle_thread) {
        KLOG_ERROR("THREAD", "Cannot exit idle thread!");
        for (;;) __asm__ volatile("hlt");
    }
    
    KLOG_INFO("THREAD", "Thread exiting:");
    KLOG_INFO("THREAD", thread->name);
    
    cpu_cli();
    
    thread->state = THREAD_STATE_ZOMBIE;
    thread->exited = true;
    thread->exit_status = status;
    
    /* Retirer de la run queue */
    scheduler_dequeue(thread);
    
    /* Passer au prochain thread */
    scheduler_schedule();
    
    /* Ne devrait jamais arriver ici */
    for (;;) __asm__ volatile("hlt");
}

int thread_join(thread_t *thread)
{
    if (!thread) return -1;
    
    /* Attendre que le thread se termine */
    while (thread->state != THREAD_STATE_ZOMBIE) {
        thread_yield();
    }
    
    int status = thread->exit_status;
    
    /* Libérer les ressources */
    if (thread->stack_base) {
        kfree(thread->stack_base);
    }
    kfree(thread);
    
    return status;
}

bool thread_kill(thread_t *thread, int status)
{
    if (!thread) return false;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    thread->should_terminate = 1;
    thread->exit_status = status;
    
    /* Si le thread est bloqué ou en sleep, le réveiller */
    if (thread->state == THREAD_STATE_BLOCKED || thread->state == THREAD_STATE_SLEEPING) {
        thread->state = THREAD_STATE_READY;
        scheduler_enqueue(thread);
    }
    
    cpu_restore_flags(flags);
    return true;
}

thread_t *thread_current(void)
{
    return g_current_thread;
}

uint32_t thread_get_tid(void)
{
    return g_current_thread ? g_current_thread->tid : 0;
}

void thread_set_priority(thread_t *thread, thread_priority_t priority)
{
    if (!thread || priority >= THREAD_PRIORITY_COUNT) return;

    uint32_t flags = cpu_save_flags();
    cpu_cli();

    thread_priority_t old_priority = thread->priority;
    thread->priority = priority;
    thread->base_priority = priority;

    /* Si le thread est dans la run queue, le déplacer */
    if (thread->state == THREAD_STATE_READY && old_priority != priority) {
        scheduler_dequeue(thread);
        scheduler_enqueue(thread);
    }

    cpu_restore_flags(flags);
}

/* ========================================
 * Nice to Priority Mapping
 * Convention Unix: nice -20 = max priority, +19 = min priority
 * ======================================== */

static thread_priority_t scheduler_nice_to_priority(int8_t nice)
{
    /* Clamp to valid range */
    if (nice < THREAD_NICE_MIN) nice = THREAD_NICE_MIN;
    if (nice > THREAD_NICE_MAX) nice = THREAD_NICE_MAX;

    /* Nice to Priority mapping:
     * [-20, -10] → UI (4)      - Highest priority
     * [-9,  -5]  → HIGH (3)    - High priority
     * [-4,  +4]  → NORMAL (2)  - Default
     * [+5, +14]  → BACKGROUND (1) - Low priority
     * [+15, +19] → IDLE (0)    - Lowest priority
     */
    if (nice <= -10) {
        return THREAD_PRIORITY_UI;
    } else if (nice <= -5) {
        return THREAD_PRIORITY_HIGH;
    } else if (nice <= 4) {
        return THREAD_PRIORITY_NORMAL;
    } else if (nice <= 14) {
        return THREAD_PRIORITY_BACKGROUND;
    } else {
        return THREAD_PRIORITY_IDLE;
    }
}

/* Time slice par priorité (en ticks)
 * Inverse de la priorité: IDLE a plus de temps, UI a moins
 * pour permettre plus de réactivité aux priorités hautes */
static const uint32_t g_priority_time_slice[THREAD_PRIORITY_COUNT] = {
    20,  /* IDLE: 20 ticks (20 ms) - long quantum */
    15,  /* BACKGROUND: 15 ticks */
    10,  /* NORMAL: 10 ticks (default) */
    7,   /* HIGH: 7 ticks */
    5    /* UI: 5 ticks - court pour réactivité */
};

static uint32_t scheduler_get_time_slice(thread_t *thread)
{
    if (!thread) return THREAD_TIME_SLICE_DEFAULT;

    thread_priority_t pri = thread->priority;
    if (pri >= THREAD_PRIORITY_COUNT) {
        pri = THREAD_PRIORITY_NORMAL;
    }

    return g_priority_time_slice[pri];
}

void thread_set_nice(thread_t *thread, int8_t nice)
{
    if (!thread) return;

    /* Clamp to valid range */
    if (nice < THREAD_NICE_MIN) nice = THREAD_NICE_MIN;
    if (nice > THREAD_NICE_MAX) nice = THREAD_NICE_MAX;

    uint32_t flags = cpu_save_flags();
    cpu_cli();

    thread->nice = nice;

    /* Only recalculate priority if not currently boosted */
    if (!thread->is_boosted) {
        thread_priority_t old_priority = thread->priority;
        thread_priority_t new_priority = scheduler_nice_to_priority(nice);

        thread->priority = new_priority;
        thread->base_priority = new_priority;

        /* Si le thread est dans la run queue, le déplacer */
        if (thread->state == THREAD_STATE_READY && old_priority != new_priority) {
            scheduler_dequeue(thread);
            scheduler_enqueue(thread);
        }
    }

    cpu_restore_flags(flags);
}

int8_t thread_get_nice(thread_t *thread)
{
    if (!thread) return THREAD_NICE_DEFAULT;
    return thread->nice;
}

uint64_t thread_get_cpu_time_ms(thread_t *thread)
{
    if (!thread) return 0;

    /* Timer runs at 1000 Hz, so ticks = milliseconds */
    return thread->cpu_ticks;
}

void thread_yield(void)
{
    if (g_scheduler_active) {
        scheduler_schedule();
    }
}

void thread_sleep_ticks(uint64_t ticks)
{
    if (!g_current_thread || ticks == 0) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    thread_t *thread = g_current_thread;
    thread->wake_tick = timer_get_ticks() + ticks;
    thread->state = THREAD_STATE_SLEEPING;
    
    /* Ajouter à la sleep queue (triée par wake_tick) */
    spinlock_lock(&g_sleep_lock);
    
    if (!g_sleep_queue || thread->wake_tick < g_sleep_queue->wake_tick) {
        thread->sched_next = g_sleep_queue;
        g_sleep_queue = thread;
    } else {
        thread_t *prev = g_sleep_queue;
        while (prev->sched_next && prev->sched_next->wake_tick <= thread->wake_tick) {
            prev = prev->sched_next;
        }
        thread->sched_next = prev->sched_next;
        prev->sched_next = thread;
    }
    
    spinlock_unlock(&g_sleep_lock);
    
    scheduler_schedule();
    
    cpu_restore_flags(flags);
}

void thread_sleep_ms(uint32_t ms)
{
    /* Avec un timer à 1000 Hz, 1 tick = 1 ms */
    thread_sleep_ticks((uint64_t)ms);
}

bool thread_should_exit(void)
{
    return g_current_thread ? (g_current_thread->should_terminate != 0) : false;
}

const char *thread_state_name(thread_state_t state)
{
    switch (state) {
        case THREAD_STATE_READY:    return "READY";
        case THREAD_STATE_RUNNING:  return "RUNNING";
        case THREAD_STATE_BLOCKED:  return "BLOCKED";
        case THREAD_STATE_SLEEPING: return "SLEEPING";
        case THREAD_STATE_ZOMBIE:   return "ZOMBIE";
        default:                    return "UNKNOWN";
    }
}

const char *thread_priority_name(thread_priority_t priority)
{
    switch (priority) {
        case THREAD_PRIORITY_IDLE:       return "IDLE";
        case THREAD_PRIORITY_BACKGROUND: return "BACKGROUND";
        case THREAD_PRIORITY_NORMAL:     return "NORMAL";
        case THREAD_PRIORITY_HIGH:       return "HIGH";
        case THREAD_PRIORITY_UI:         return "UI";
        default:                         return "UNKNOWN";
    }
}

/* ========================================
 * Scheduler Implementation
 * ======================================== */

/* Fonction idle qui tourne quand aucun thread n'est prêt */
static void idle_thread_func(void *arg)
{
    (void)arg;
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Thread principal statique (représente le kernel/shell au boot) */
static thread_t g_main_thread_struct;

void scheduler_init(void)
{
    KLOG_INFO("SCHED", "=== Initializing Scheduler ===");
    
    spinlock_init(&g_scheduler_lock);
    spinlock_init(&g_sleep_lock);
    
    /* Initialiser les run queues */
    for (int i = 0; i < THREAD_PRIORITY_COUNT; i++) {
        g_run_queues[i] = NULL;
    }
    
    /* Créer le "thread main" statique qui représente le code kernel actuel
     * Ce thread n'a pas de stack préparée - il utilise la stack courante
     * et son ESP sera sauvegardé lors du premier context switch.
     */
    thread_t *main_thread = &g_main_thread_struct;
    main_thread->tid = g_next_tid++;
    safe_strcpy(main_thread->name, "main", THREAD_NAME_MAX);
    main_thread->magic = THREAD_MAGIC;
    main_thread->owner = NULL;
    main_thread->state = THREAD_STATE_RUNNING;
    main_thread->should_terminate = 0;
    main_thread->exited = false;
    main_thread->exit_status = 0;
    main_thread->stack_base = NULL;  /* Pas de stack allouée */
    main_thread->stack_size = 0;
    main_thread->esp = 0;  /* Sera rempli lors du premier switch */
    main_thread->esp0 = 0;
    main_thread->entry = NULL;
    main_thread->arg = NULL;
    main_thread->base_priority = THREAD_PRIORITY_NORMAL;
    main_thread->priority = THREAD_PRIORITY_NORMAL;
    main_thread->time_slice_remaining = g_priority_time_slice[THREAD_PRIORITY_NORMAL];

    /* Nice value and aging */
    main_thread->nice = THREAD_NICE_DEFAULT;
    main_thread->is_boosted = false;
    main_thread->wait_start_tick = 0;

    /* CPU accounting */
    main_thread->cpu_ticks = 0;
    main_thread->context_switches = 0;
    main_thread->run_start_tick = 0;

    /* SMP preparation */
    main_thread->cpu_affinity = 0xFFFFFFFF;
    main_thread->last_cpu = 0;

    main_thread->wake_tick = 0;
    main_thread->waiting_queue = NULL;
    main_thread->wait_queue_next = NULL;
    main_thread->sched_next = NULL;
    main_thread->sched_prev = NULL;
    main_thread->proc_next = NULL;
    main_thread->preempt_count = 0;
    main_thread->preempt_pending = false;
    
    g_current_thread = main_thread;
    
    KLOG_INFO("SCHED", "Main thread created (adopts current context)");
    
    /* Créer le thread idle */
    g_idle_thread = thread_create("idle", idle_thread_func, NULL,
                                  THREAD_DEFAULT_STACK_SIZE,
                                  THREAD_PRIORITY_IDLE);
    if (!g_idle_thread) {
        KLOG_ERROR("SCHED", "Failed to create idle thread!");
        return;
    }
    
    /* Retirer idle de la run queue (il sera choisi automatiquement si nécessaire) */
    scheduler_dequeue(g_idle_thread);
    
    KLOG_INFO("SCHED", "Scheduler initialized");
}

void scheduler_start(void)
{
    KLOG_INFO("SCHED", "Starting scheduler");
    g_scheduler_active = true;
}

void scheduler_tick(void)
{
    if (!g_scheduler_active || !g_current_thread) return;

    uint64_t now = timer_get_ticks();

    /* CPU accounting: increment CPU time for running thread */
    if (g_current_thread && g_current_thread != g_idle_thread) {
        g_current_thread->cpu_ticks++;
    }

    /* Réveiller les threads endormis */
    scheduler_wake_sleeping();

    /* Décrémenter le time slice */
    if (g_current_thread != g_idle_thread && g_current_thread->time_slice_remaining > 0) {
        g_current_thread->time_slice_remaining--;
    }

    /* Marquer la préemption comme pending si time slice épuisé */
    if (g_current_thread->time_slice_remaining == 0 &&
        g_current_thread != g_idle_thread) {
        g_current_thread->preempt_pending = true;
    }

    /* Rocket Boost aging: check all run queues except UI for starvation */
    spinlock_lock(&g_scheduler_lock);

    for (int pri = THREAD_PRIORITY_IDLE; pri < THREAD_PRIORITY_UI; pri++) {
        thread_t *thread = g_run_queues[pri];

        while (thread) {
            thread_t *next = thread->sched_next;  /* Save next before we move thread */

            /* Check if thread has been waiting too long */
            if (!thread->is_boosted &&
                (now - thread->wait_start_tick) >= THREAD_AGING_THRESHOLD) {

                /* Remove from current queue */
                if (thread->sched_prev) {
                    thread->sched_prev->sched_next = thread->sched_next;
                } else {
                    g_run_queues[pri] = thread->sched_next;
                }

                if (thread->sched_next) {
                    thread->sched_next->sched_prev = thread->sched_prev;
                }

                /* Boost to UI priority */
                thread->priority = THREAD_PRIORITY_UI;
                thread->is_boosted = true;
                thread->wait_start_tick = now;  /* Reset wait timer */

                /* Insert at head of UI queue */
                thread->sched_prev = NULL;
                thread->sched_next = g_run_queues[THREAD_PRIORITY_UI];

                if (g_run_queues[THREAD_PRIORITY_UI]) {
                    g_run_queues[THREAD_PRIORITY_UI]->sched_prev = thread;
                }
                g_run_queues[THREAD_PRIORITY_UI] = thread;
            }

            thread = next;
        }
    }

    spinlock_unlock(&g_scheduler_lock);
}

/* ========================================
 * Préemption depuis IRQ (nouveau système)
 * ======================================== */

/* Fonction utilisée par scheduler_preempt pour pick sans lock (déjà pris) */
static thread_t *scheduler_pick_next_nolock(void)
{
    /* Parcourir les priorités de la plus haute à la plus basse */
    for (int pri = THREAD_PRIORITY_COUNT - 1; pri >= THREAD_PRIORITY_IDLE; pri--) {
        if (g_run_queues[pri]) {
            thread_t *thread = g_run_queues[pri];
            
            /* Retirer de la queue */
            g_run_queues[pri] = thread->sched_next;
            if (thread->sched_next) {
                thread->sched_next->sched_prev = NULL;
            }
            thread->sched_next = NULL;
            thread->sched_prev = NULL;
            
            return thread;
        }
    }
    
    /* Aucun thread prêt, retourner idle */
    return g_idle_thread;
}

/* Ajoute un thread à la run queue sans prendre le lock */
static void scheduler_enqueue_nolock(thread_t *thread)
{
    if (!thread || thread->state == THREAD_STATE_RUNNING) return;
    
    thread_priority_t pri = thread->priority;
    if (pri >= THREAD_PRIORITY_COUNT) {
        pri = THREAD_PRIORITY_NORMAL;
    }
    
    thread->sched_prev = NULL;
    thread->sched_next = g_run_queues[pri];
    
    if (g_run_queues[pri]) {
        g_run_queues[pri]->sched_prev = thread;
    }
    g_run_queues[pri] = thread;
    
    if (thread->state != THREAD_STATE_RUNNING) {
        thread->state = THREAD_STATE_READY;
    }
}

uint32_t scheduler_preempt(interrupt_frame_t *frame)
{
    if (!g_scheduler_active || !g_current_thread) return 0;
    
    /* Réveiller les threads endormis */
    scheduler_wake_sleeping();
    
    /* Décrémenter le time slice */
    if (g_current_thread != g_idle_thread && g_current_thread->time_slice_remaining > 0) {
        g_current_thread->time_slice_remaining--;
    }
    
    /* Vérifier si on doit préempter */
    thread_t *current = g_current_thread;
    
    /* Ne pas préempter si:
     * - Préemption désactivée (section critique)
     * - Time slice pas encore épuisé
     * - On est déjà le thread idle
     */
    if (current->preempt_count > 0) {
        /* Marquer comme pending pour plus tard */
        if (current->time_slice_remaining == 0) {
            current->preempt_pending = true;
        }
        return 0;  /* Pas de préemption */
    }
    
    if (current->time_slice_remaining > 0 && current != g_idle_thread) {
        return 0;  /* Pas encore épuisé */
    }
    
    /* Essayer de trouver un autre thread */
    spinlock_lock(&g_scheduler_lock);
    
    thread_t *next = scheduler_pick_next_nolock();
    
    if (!next || next == current) {
        /* Personne d'autre, continuer avec le même thread */
        spinlock_unlock(&g_scheduler_lock);
        /* Recharger le time slice si épuisé */
        if (current->time_slice_remaining == 0) {
            current->time_slice_remaining = THREAD_TIME_SLICE_DEFAULT;
        }
        return 0;
    }
    
    /* On va changer de thread ! */

    uint64_t now = timer_get_ticks();

    /* CPU accounting: finalize current thread's run time */
    if (current->run_start_tick > 0) {
        uint64_t run_duration = now - current->run_start_tick;
        current->cpu_ticks += run_duration;
    }

    /* Boost demotion: if current thread was boosted, demote it back */
    if (current->is_boosted) {
        current->is_boosted = false;
        current->priority = scheduler_nice_to_priority(current->nice);
        current->base_priority = current->priority;
    }

    /* Remettre le thread actuel dans la run queue */
    if (current->state == THREAD_STATE_RUNNING) {
        current->state = THREAD_STATE_READY;
        current->wait_start_tick = now;  /* Start aging timer */
        scheduler_enqueue_nolock(current);
    }

    /* CPU accounting: start next thread's run time */
    next->run_start_tick = now;
    next->context_switches++;

    /* Recharger le time slice du nouveau thread (use priority-based slice) */
    next->time_slice_remaining = scheduler_get_time_slice(next);
    next->state = THREAD_STATE_RUNNING;
    next->preempt_pending = false;
    g_current_thread = next;
    
    spinlock_unlock(&g_scheduler_lock);
    
    /* Mettre à jour le TSS pour le nouveau thread */
    if (next->esp0 != 0) {
        tss_set_kernel_stack(next->esp0);
    }
    
    /* Sauvegarder l'ESP du thread préempté.
     * Le frame pointe vers les registres sauvegardés sur la stack.
     * On sauvegarde ce pointeur comme ESP du thread actuel.
     */
    current->esp = (uint32_t)frame;
    
    /* Retourner l'ESP du nouveau thread.
     * Le code ASM va faire: mov esp, eax ; popa ; iretd
     * donc on retourne l'ESP qui pointe vers un frame sauvegardé.
     */
    return next->esp;
}

/* ========================================
 * Contrôle de préemption
 * ======================================== */

void preempt_disable(void)
{
    if (g_current_thread) {
        g_current_thread->preempt_count++;
    }
}

void preempt_enable(void)
{
    if (!g_current_thread) return;
    
    if (g_current_thread->preempt_count > 0) {
        g_current_thread->preempt_count--;
    }
    
    /* Si préemption réactivée et pending, scheduler maintenant */
    if (g_current_thread->preempt_count == 0 && 
        g_current_thread->preempt_pending) {
        g_current_thread->preempt_pending = false;
        scheduler_schedule();
    }
}

bool preempt_enabled(void)
{
    if (!g_current_thread) return true;
    return g_current_thread->preempt_count == 0;
}

void scheduler_wake_sleeping(void)
{
    uint64_t now = timer_get_ticks();
    
    spinlock_lock(&g_sleep_lock);
    
    while (g_sleep_queue && g_sleep_queue->wake_tick <= now) {
        thread_t *thread = g_sleep_queue;
        g_sleep_queue = thread->sched_next;
        thread->sched_next = NULL;
        thread->state = THREAD_STATE_READY;
        
        spinlock_unlock(&g_sleep_lock);
        scheduler_enqueue(thread);
        spinlock_lock(&g_sleep_lock);
    }
    
    spinlock_unlock(&g_sleep_lock);
}

void scheduler_enqueue(thread_t *thread)
{
    if (!thread || thread->state == THREAD_STATE_RUNNING) return;
    
    spinlock_lock(&g_scheduler_lock);
    
    thread_priority_t pri = thread->priority;
    if (pri >= THREAD_PRIORITY_COUNT) {
        pri = THREAD_PRIORITY_NORMAL;
    }
    
    /* Ajouter en fin de queue (FIFO dans la même priorité) */
    thread->sched_prev = NULL;
    thread->sched_next = g_run_queues[pri];
    
    if (g_run_queues[pri]) {
        g_run_queues[pri]->sched_prev = thread;
    }
    g_run_queues[pri] = thread;
    
    if (thread->state != THREAD_STATE_RUNNING) {
        thread->state = THREAD_STATE_READY;
    }
    
    spinlock_unlock(&g_scheduler_lock);
}

void scheduler_dequeue(thread_t *thread)
{
    if (!thread) return;
    
    spinlock_lock(&g_scheduler_lock);
    
    thread_priority_t pri = thread->priority;
    if (pri >= THREAD_PRIORITY_COUNT) {
        pri = THREAD_PRIORITY_NORMAL;
    }
    
    /* Retirer de la liste */
    if (thread->sched_prev) {
        thread->sched_prev->sched_next = thread->sched_next;
    } else if (g_run_queues[pri] == thread) {
        g_run_queues[pri] = thread->sched_next;
    }
    
    if (thread->sched_next) {
        thread->sched_next->sched_prev = thread->sched_prev;
    }
    
    thread->sched_next = NULL;
    thread->sched_prev = NULL;
    
    spinlock_unlock(&g_scheduler_lock);
}

/* Sélectionne le prochain thread à exécuter */
static thread_t *scheduler_pick_next(void)
{
    spinlock_lock(&g_scheduler_lock);
    
    /* Parcourir les priorités de la plus haute à la plus basse */
    for (int pri = THREAD_PRIORITY_COUNT - 1; pri >= THREAD_PRIORITY_IDLE; pri--) {
        if (g_run_queues[pri]) {
            thread_t *thread = g_run_queues[pri];
            
            /* Retirer de la queue */
            g_run_queues[pri] = thread->sched_next;
            if (thread->sched_next) {
                thread->sched_next->sched_prev = NULL;
            }
            thread->sched_next = NULL;
            thread->sched_prev = NULL;
            
            spinlock_unlock(&g_scheduler_lock);
            return thread;
        }
    }
    
    spinlock_unlock(&g_scheduler_lock);
    
    /* Aucun thread prêt, retourner le thread idle */
    return g_idle_thread;
}

/* Fonction ASM de context switch (définie dans switch.s) */
extern void switch_task(uint32_t *old_esp_ptr, uint32_t new_esp, uint32_t new_cr3);

/* ESP dummy pour le premier switch */
static uint32_t g_dummy_esp = 0;

void scheduler_schedule(void)
{
    if (!g_scheduler_active) return;
    
    cpu_cli();
    
    thread_t *current = g_current_thread;
    thread_t *next = scheduler_pick_next();
    
    if (!next) {
        next = g_idle_thread;
    }
    
    /* Si pas de next ou même thread, rien à faire */
    if (!next || next == current) {
        cpu_sti();
        return;
    }
    
    KLOG_INFO("SCHED", "Context switch:");
    if (current) {
        KLOG_INFO("SCHED", "  From:");
        KLOG_INFO("SCHED", current->name);
        KLOG_INFO_HEX("SCHED", "  Old ESP: ", current->esp);
    }
    KLOG_INFO("SCHED", "  To:");
    KLOG_INFO("SCHED", next->name);
    KLOG_INFO_HEX("SCHED", "  New ESP: ", next->esp);

    uint64_t now = timer_get_ticks();

    /* CPU accounting: finalize current thread's run time */
    if (current && current->run_start_tick > 0) {
        uint64_t run_duration = now - current->run_start_tick;
        current->cpu_ticks += run_duration;
    }

    /* Boost demotion: if current thread was boosted, demote it back */
    if (current && current->is_boosted) {
        current->is_boosted = false;
        current->priority = scheduler_nice_to_priority(current->nice);
        current->base_priority = current->priority;
    }

    /* Mettre le thread actuel dans la run queue s'il est toujours READY/RUNNING */
    if (current && (current->state == THREAD_STATE_RUNNING || current->state == THREAD_STATE_READY)) {
        current->state = THREAD_STATE_READY;
        current->wait_start_tick = now;  /* Start aging timer */
        scheduler_enqueue(current);
    }

    /* CPU accounting: start next thread's run time */
    next->run_start_tick = now;
    next->context_switches++;

    /* Basculer vers le nouveau thread */
    next->state = THREAD_STATE_RUNNING;
    g_current_thread = next;
    
    /* Mettre à jour le TSS */
    if (next->esp0 != 0) {
        tss_set_kernel_stack(next->esp0);
    }
    
    /* Context switch !
     * Note: new_cr3 = 0 car tous les threads kernel partagent le même espace mémoire
     */
    if (current) {
        switch_task(&current->esp, next->esp, 0);
    } else {
        /* Premier switch - utiliser un ESP dummy */
        switch_task(&g_dummy_esp, next->esp, 0);
    }
    
    /* On revient ici quand ce thread est reschedulé */
    cpu_sti();
}

/* ========================================
 * Debug
 * ======================================== */

static void print_thread_info(thread_t *thread, bool is_current)
{
    console_put_dec(thread->tid);
    console_puts("  ");

    console_puts(thread_state_name(thread->state));
    console_puts("  ");

    console_puts(thread_priority_name(thread->priority));
    console_puts("  ");

    /* Nice value */
    if (thread->nice < 0) {
        console_puts("-");
        console_put_dec(-thread->nice);
    } else if (thread->nice > 0) {
        console_puts("+");
        console_put_dec(thread->nice);
    } else {
        console_puts(" 0");
    }
    console_puts("  ");

    /* Boosted indicator */
    console_puts(thread->is_boosted ? "B" : " ");
    console_puts("  ");

    /* CPU time in ms */
    console_put_dec(thread->cpu_ticks);
    console_puts("ms  ");

    /* Context switches */
    console_put_dec(thread->context_switches);
    console_puts("  ");

    /* Name */
    console_puts(thread->name);

    if (is_current) {
        console_puts(" <-- current");
    }

    console_puts("\n");
}

void thread_list_debug(void)
{
    console_puts("\n=== Thread List ===\n");
    console_puts("TID  State     Priority   Nice  B  CPU    Ctx  Name\n");
    console_puts("---  -----     --------   ----  -  ---    ---  ----\n");

    cpu_cli();

    /* Afficher le thread courant */
    if (g_current_thread) {
        print_thread_info(g_current_thread, true);
    }

    /* Afficher les threads dans les run queues */
    for (int pri = THREAD_PRIORITY_COUNT - 1; pri >= 0; pri--) {
        thread_t *thread = g_run_queues[pri];
        while (thread) {
            if (thread != g_current_thread) {
                print_thread_info(thread, false);
            }
            thread = thread->sched_next;
        }
    }

    /* Afficher les threads en sleep */
    thread_t *sleep = g_sleep_queue;
    while (sleep) {
        print_thread_info(sleep, false);
        sleep = sleep->sched_next;
    }

    cpu_sti();

    console_puts("\nB = Boosted by aging (Rocket Boost)\n");
    console_puts("===================\n");
}
