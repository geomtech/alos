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
    thread->time_slice_remaining = THREAD_TIME_SLICE_DEFAULT;
    
    thread->wake_tick = 0;
    thread->waiting_queue = NULL;
    thread->wait_queue_next = NULL;
    
    thread->sched_next = NULL;
    thread->sched_prev = NULL;
    thread->proc_next = NULL;
    
    /* Préparer la stack initiale pour switch_task
     * 
     * switch_task fait: pop ebp; pop edi; pop esi; pop ebx; ret
     * task_entry_point fait: pop eax (fonction); call eax
     * 
     * Layout nécessaire (du bas vers le haut de la stack):
     *   [Argument]                 <- pour la fonction (optionnel)
     *   [Adresse de la fonction]   <- task_entry_point fait pop eax; call eax
     *   [task_entry_point]         <- switch_task fait ret vers ici
     *   [EBX = 0]                  <- switch_task fait pop ebx
     *   [ESI = 0]                  <- switch_task fait pop esi
     *   [EDI = 0]                  <- switch_task fait pop edi
     *   [EBP = 0]                  <- switch_task fait pop ebp (ESP pointe ici)
     */
    uint32_t *stack_top = (uint32_t *)((uint32_t)stack + stack_size);
    
    /* L'argument sera récupéré par la fonction via EAX qui pointe vers arg */
    *(--stack_top) = (uint32_t)arg;          /* Argument (pour plus tard si besoin) */
    *(--stack_top) = (uint32_t)entry;        /* Adresse de la fonction - pop eax */
    *(--stack_top) = (uint32_t)task_entry_point;  /* Adresse de retour - ret */
    *(--stack_top) = 0;                      /* EBX - pop ebx */
    *(--stack_top) = 0;                      /* ESI - pop esi */
    *(--stack_top) = 0;                      /* EDI - pop edi */
    *(--stack_top) = 0;                      /* EBP - pop ebp (ESP initial) */
    
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
    main_thread->time_slice_remaining = THREAD_TIME_SLICE_DEFAULT;
    main_thread->wake_tick = 0;
    main_thread->waiting_queue = NULL;
    main_thread->wait_queue_next = NULL;
    main_thread->sched_next = NULL;
    main_thread->sched_prev = NULL;
    main_thread->proc_next = NULL;
    
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
    
    /* Réveiller les threads endormis */
    scheduler_wake_sleeping();
    
    /* Décrémenter le time slice */
    if (g_current_thread != g_idle_thread && g_current_thread->time_slice_remaining > 0) {
        g_current_thread->time_slice_remaining--;
    }
    
    /* NOTE: On ne fait PAS de préemption automatique ici car on est 
     * dans un handler d'interruption. La préemption se fait via:
     * - thread_yield() explicite
     * - thread_sleep_ms() / thread_sleep_ticks()
     * - wait_queue_wait()
     * 
     * Pour une vraie préemption, il faudrait modifier le handler d'IRQ
     * pour sauvegarder/restaurer le contexte complet de façon sûre.
     */
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
    
    /* Mettre le thread actuel dans la run queue s'il est toujours READY/RUNNING */
    if (current && (current->state == THREAD_STATE_RUNNING || current->state == THREAD_STATE_READY)) {
        current->state = THREAD_STATE_READY;
        scheduler_enqueue(current);
    }
    
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

void thread_list_debug(void)
{
    console_puts("\n=== Thread List ===\n");
    console_puts("TID   State     Priority   Name\n");
    console_puts("---   -----     --------   ----\n");
    
    cpu_cli();
    
    /* Afficher le thread courant */
    if (g_current_thread) {
        console_put_dec(g_current_thread->tid);
        console_puts("     ");
        console_puts(thread_state_name(g_current_thread->state));
        console_puts("   ");
        console_puts(thread_priority_name(g_current_thread->priority));
        console_puts("     ");
        console_puts(g_current_thread->name);
        console_puts(" <-- current\n");
    }
    
    /* Afficher les threads dans les run queues */
    for (int pri = THREAD_PRIORITY_COUNT - 1; pri >= 0; pri--) {
        thread_t *thread = g_run_queues[pri];
        while (thread) {
            if (thread != g_current_thread) {
                console_put_dec(thread->tid);
                console_puts("     ");
                console_puts(thread_state_name(thread->state));
                console_puts("   ");
                console_puts(thread_priority_name(thread->priority));
                console_puts("     ");
                console_puts(thread->name);
                console_puts("\n");
            }
            thread = thread->sched_next;
        }
    }
    
    /* Afficher les threads en sleep */
    thread_t *sleep = g_sleep_queue;
    while (sleep) {
        console_put_dec(sleep->tid);
        console_puts("     ");
        console_puts(thread_state_name(sleep->state));
        console_puts("   ");
        console_puts(thread_priority_name(sleep->priority));
        console_puts("     ");
        console_puts(sleep->name);
        console_puts("\n");
        sleep = sleep->sched_next;
    }
    
    cpu_sti();
    
    console_puts("===================\n");
}
