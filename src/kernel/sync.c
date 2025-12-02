/* src/kernel/sync.c - Advanced Synchronization Primitives Implementation
 * 
 * Implements:
 * - Mutex with owner tracking and priority inheritance
 * - Counting Semaphore
 * - Condition Variable (POSIX-like)
 * - Read-Write Lock (writer-preferring by default)
 */
#include "sync.h"
#include "console.h"
#include "klog.h"
#include "timer.h"

/* ============================================ */
/*        Helpers                               */
/* ============================================ */

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

/* Get current timer tick count (from timer.c) */
extern uint64_t timer_get_ticks(void);

/* ============================================ */
/*        Mutex Implementation                  */
/* ============================================ */

void mutex_init(mutex_t *mutex, mutex_type_t type)
{
    if (!mutex) return;
    
    spinlock_init(&mutex->lock);
    wait_queue_init(&mutex->waiters);
    mutex->owner = NULL;
    mutex->recursion_count = 0;
    mutex->type = type;
    mutex->owner_original_priority = THREAD_PRIORITY_NORMAL;
}

/**
 * Apply priority inheritance if needed
 * If a high-priority thread is blocked by a low-priority owner,
 * boost the owner's priority to prevent priority inversion.
 */
static void mutex_apply_priority_inheritance(mutex_t *mutex, thread_t *waiter)
{
    if (!mutex || !mutex->owner || !waiter) return;
    
    thread_t *owner = mutex->owner;
    
    /* If waiter has higher priority than owner, boost owner */
    if (waiter->priority > owner->priority) {
        /* Save original priority only on first boost */
        if (owner->priority == owner->base_priority) {
            mutex->owner_original_priority = owner->base_priority;
        }
        /* Boost owner priority */
        owner->priority = waiter->priority;
    }
}

/**
 * Restore original priority when mutex is released
 */
static void mutex_restore_priority(mutex_t *mutex)
{
    if (!mutex || !mutex->owner) return;
    
    thread_t *owner = mutex->owner;
    
    /* Restore to base priority */
    /* Note: In a full implementation, we'd check all mutexes held by this thread */
    owner->priority = owner->base_priority;
}

int mutex_lock(mutex_t *mutex)
{
    if (!mutex) return -1;
    
    thread_t *current = thread_current();
    if (!current) return -1;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&mutex->lock);
    
    /* Check if already owned by current thread */
    if (mutex->owner == current) {
        switch (mutex->type) {
            case MUTEX_TYPE_RECURSIVE:
                /* Allow recursive lock */
                mutex->recursion_count++;
                spinlock_unlock(&mutex->lock);
                cpu_restore_flags(flags);
                return 0;
                
            case MUTEX_TYPE_ERRORCHECK:
                /* Return error on deadlock */
                spinlock_unlock(&mutex->lock);
                cpu_restore_flags(flags);
                KLOG_ERROR("MUTEX", "Deadlock detected: thread already owns mutex");
                return -1;
                
            case MUTEX_TYPE_NORMAL:
            default:
                /* Deadlock - will block forever (intended behavior for debugging) */
                KLOG_ERROR("MUTEX", "Deadlock: thread re-locking non-recursive mutex");
                /* Fall through to blocking - this will deadlock */
                break;
        }
    }
    
    /* Try to acquire */
    while (mutex->owner != NULL) {
        /* Apply priority inheritance */
        mutex_apply_priority_inheritance(mutex, current);
        
        /* Add to wait queue */
        current->state = THREAD_STATE_BLOCKED;
        current->waiting_queue = &mutex->waiters;
        
        /* Simple manual enqueue to waiters */
        thread_t **tail = &mutex->waiters.head;
        while (*tail) {
            tail = &((*tail)->wait_queue_next);
        }
        *tail = current;
        current->wait_queue_next = NULL;
        if (!mutex->waiters.tail) {
            mutex->waiters.tail = current;
        }
        
        spinlock_unlock(&mutex->lock);
        
        /* Yield CPU */
        scheduler_schedule();
        
        spinlock_lock(&mutex->lock);
        
        /* Remove from wait queue (we were woken) */
        current->waiting_queue = NULL;
    }
    
    /* Acquire the mutex */
    mutex->owner = current;
    mutex->recursion_count = 1;
    mutex->owner_original_priority = current->base_priority;
    
    spinlock_unlock(&mutex->lock);
    cpu_restore_flags(flags);
    
    return 0;
}

bool mutex_trylock(mutex_t *mutex)
{
    if (!mutex) return false;
    
    thread_t *current = thread_current();
    if (!current) return false;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&mutex->lock);
    
    /* Check if already owned by current thread (recursive case) */
    if (mutex->owner == current && mutex->type == MUTEX_TYPE_RECURSIVE) {
        mutex->recursion_count++;
        spinlock_unlock(&mutex->lock);
        cpu_restore_flags(flags);
        return true;
    }
    
    /* Try to acquire if unlocked */
    if (mutex->owner == NULL) {
        mutex->owner = current;
        mutex->recursion_count = 1;
        mutex->owner_original_priority = current->base_priority;
        spinlock_unlock(&mutex->lock);
        cpu_restore_flags(flags);
        return true;
    }
    
    spinlock_unlock(&mutex->lock);
    cpu_restore_flags(flags);
    return false;
}

int mutex_unlock(mutex_t *mutex)
{
    if (!mutex) return -1;
    
    thread_t *current = thread_current();
    if (!current) return -1;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&mutex->lock);
    
    /* Verify ownership */
    if (mutex->owner != current) {
        spinlock_unlock(&mutex->lock);
        cpu_restore_flags(flags);
        KLOG_ERROR("MUTEX", "Unlock by non-owner!");
        return -1;
    }
    
    /* Handle recursive mutex */
    if (mutex->type == MUTEX_TYPE_RECURSIVE && mutex->recursion_count > 1) {
        mutex->recursion_count--;
        spinlock_unlock(&mutex->lock);
        cpu_restore_flags(flags);
        return 0;
    }
    
    /* Restore original priority (undo priority inheritance) */
    mutex_restore_priority(mutex);
    
    /* Release the mutex */
    mutex->owner = NULL;
    mutex->recursion_count = 0;
    
    /* Wake one waiting thread */
    thread_t *waiter = mutex->waiters.head;
    if (waiter) {
        mutex->waiters.head = waiter->wait_queue_next;
        if (!mutex->waiters.head) {
            mutex->waiters.tail = NULL;
        }
        waiter->wait_queue_next = NULL;
        waiter->state = THREAD_STATE_READY;
        scheduler_enqueue(waiter);
    }
    
    spinlock_unlock(&mutex->lock);
    cpu_restore_flags(flags);
    
    return 0;
}

bool mutex_is_owner(mutex_t *mutex)
{
    if (!mutex) return false;
    return mutex->owner == thread_current();
}

bool mutex_is_locked(mutex_t *mutex)
{
    if (!mutex) return false;
    return mutex->owner != NULL;
}


/* ============================================ */
/*        Semaphore Implementation              */
/* ============================================ */

void semaphore_init(semaphore_t *sem, int32_t initial_count, uint32_t max_count)
{
    if (!sem) return;
    
    spinlock_init(&sem->lock);
    wait_queue_init(&sem->waiters);
    atomic_set(&sem->count, initial_count);
    sem->max_count = max_count;
}

void sem_wait(semaphore_t *sem)
{
    if (!sem) return;
    
    thread_t *current = thread_current();
    if (!current) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&sem->lock);
    
    /* Try to decrement, block if count is 0 */
    while (atomic_load(&sem->count) <= 0) {
        /* Add to wait queue */
        current->state = THREAD_STATE_BLOCKED;
        current->waiting_queue = &sem->waiters;
        
        thread_t **tail = &sem->waiters.head;
        while (*tail) {
            tail = &((*tail)->wait_queue_next);
        }
        *tail = current;
        current->wait_queue_next = NULL;
        if (!sem->waiters.tail) {
            sem->waiters.tail = current;
        }
        
        spinlock_unlock(&sem->lock);
        scheduler_schedule();
        spinlock_lock(&sem->lock);
        
        current->waiting_queue = NULL;
    }
    
    /* Decrement count */
    atomic_dec(&sem->count);
    
    spinlock_unlock(&sem->lock);
    cpu_restore_flags(flags);
}

bool sem_trywait(semaphore_t *sem)
{
    if (!sem) return false;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&sem->lock);
    
    int32_t count = atomic_load(&sem->count);
    if (count > 0) {
        atomic_dec(&sem->count);
        spinlock_unlock(&sem->lock);
        cpu_restore_flags(flags);
        return true;
    }
    
    spinlock_unlock(&sem->lock);
    cpu_restore_flags(flags);
    return false;
}

bool sem_timedwait(semaphore_t *sem, uint32_t timeout_ms)
{
    if (!sem) return false;
    
    thread_t *current = thread_current();
    if (!current) return false;
    
    uint64_t start_tick = timer_get_ticks();
    /* Assuming 1000 Hz timer (1 tick = 1 ms) */
    uint64_t timeout_ticks = timeout_ms;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&sem->lock);
    
    while (atomic_load(&sem->count) <= 0) {
        /* Check timeout */
        if (timer_get_ticks() - start_tick >= timeout_ticks) {
            spinlock_unlock(&sem->lock);
            cpu_restore_flags(flags);
            return false;
        }
        
        /* Add to wait queue */
        current->state = THREAD_STATE_BLOCKED;
        current->waiting_queue = &sem->waiters;
        
        thread_t **tail = &sem->waiters.head;
        while (*tail) {
            tail = &((*tail)->wait_queue_next);
        }
        *tail = current;
        current->wait_queue_next = NULL;
        if (!sem->waiters.tail) {
            sem->waiters.tail = current;
        }
        
        /* Set wake time for timeout */
        current->wake_tick = start_tick + timeout_ticks;
        current->state = THREAD_STATE_SLEEPING;
        
        spinlock_unlock(&sem->lock);
        scheduler_schedule();
        spinlock_lock(&sem->lock);
        
        current->waiting_queue = NULL;
        current->wake_tick = 0;
    }
    
    /* Decrement count */
    atomic_dec(&sem->count);
    
    spinlock_unlock(&sem->lock);
    cpu_restore_flags(flags);
    return true;
}

int sem_post(semaphore_t *sem)
{
    if (!sem) return -1;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&sem->lock);
    
    /* Check max count */
    if (sem->max_count > 0 && (uint32_t)atomic_load(&sem->count) >= sem->max_count) {
        spinlock_unlock(&sem->lock);
        cpu_restore_flags(flags);
        return -1;  /* Would exceed max */
    }
    
    /* Increment count */
    atomic_inc(&sem->count);
    
    /* Wake one waiting thread */
    thread_t *waiter = sem->waiters.head;
    if (waiter) {
        sem->waiters.head = waiter->wait_queue_next;
        if (!sem->waiters.head) {
            sem->waiters.tail = NULL;
        }
        waiter->wait_queue_next = NULL;
        waiter->state = THREAD_STATE_READY;
        scheduler_enqueue(waiter);
    }
    
    spinlock_unlock(&sem->lock);
    cpu_restore_flags(flags);
    
    return 0;
}

int32_t sem_getvalue(semaphore_t *sem)
{
    if (!sem) return 0;
    return atomic_load(&sem->count);
}


/* ============================================ */
/*        Condition Variable Implementation     */
/* ============================================ */

void condvar_init(condvar_t *cv)
{
    if (!cv) return;
    
    spinlock_init(&cv->lock);
    wait_queue_init(&cv->waiters);
    cv->signal_count = 0;
}

void condvar_wait(condvar_t *cv, mutex_t *mutex)
{
    if (!cv || !mutex) return;
    
    thread_t *current = thread_current();
    if (!current) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&cv->lock);
    
    /* Add to wait queue BEFORE releasing mutex (atomic condition) */
    current->state = THREAD_STATE_BLOCKED;
    current->waiting_queue = &cv->waiters;
    
    thread_t **tail = &cv->waiters.head;
    while (*tail) {
        tail = &((*tail)->wait_queue_next);
    }
    *tail = current;
    current->wait_queue_next = NULL;
    if (!cv->waiters.tail) {
        cv->waiters.tail = current;
    }
    
    spinlock_unlock(&cv->lock);
    
    /* Release the mutex - this allows other threads to signal us */
    mutex_unlock(mutex);
    
    /* Block until signaled */
    scheduler_schedule();
    
    /* Re-acquire mutex before returning */
    mutex_lock(mutex);
    
    current->waiting_queue = NULL;
    
    cpu_restore_flags(flags);
}

bool condvar_timedwait(condvar_t *cv, mutex_t *mutex, uint32_t timeout_ms)
{
    if (!cv || !mutex) return false;
    
    thread_t *current = thread_current();
    if (!current) return false;
    
    uint64_t start_tick = timer_get_ticks();
    uint64_t timeout_ticks = timeout_ms;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&cv->lock);
    
    /* Add to wait queue */
    current->state = THREAD_STATE_SLEEPING;  /* Use SLEEPING for timeout support */
    current->waiting_queue = &cv->waiters;
    current->wake_tick = start_tick + timeout_ticks;
    
    thread_t **tail = &cv->waiters.head;
    while (*tail) {
        tail = &((*tail)->wait_queue_next);
    }
    *tail = current;
    current->wait_queue_next = NULL;
    if (!cv->waiters.tail) {
        cv->waiters.tail = current;
    }
    
    spinlock_unlock(&cv->lock);
    
    /* Release mutex */
    mutex_unlock(mutex);
    
    /* Block until signaled or timeout */
    scheduler_schedule();
    
    /* Check if we timed out */
    bool timed_out = (timer_get_ticks() - start_tick >= timeout_ticks);
    
    /* Remove from wait queue if still there (timeout case) */
    if (timed_out) {
        spinlock_lock(&cv->lock);
        /* Remove self from queue */
        thread_t **prev = &cv->waiters.head;
        while (*prev && *prev != current) {
            prev = &((*prev)->wait_queue_next);
        }
        if (*prev == current) {
            *prev = current->wait_queue_next;
            if (cv->waiters.tail == current) {
                cv->waiters.tail = (*prev) ? *prev : NULL;
            }
        }
        spinlock_unlock(&cv->lock);
    }
    
    current->waiting_queue = NULL;
    current->wake_tick = 0;
    
    /* Re-acquire mutex */
    mutex_lock(mutex);
    
    cpu_restore_flags(flags);
    
    return !timed_out;
}

void condvar_signal(condvar_t *cv)
{
    if (!cv) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&cv->lock);
    
    cv->signal_count++;
    
    /* Wake one waiting thread */
    thread_t *waiter = cv->waiters.head;
    if (waiter) {
        cv->waiters.head = waiter->wait_queue_next;
        if (!cv->waiters.head) {
            cv->waiters.tail = NULL;
        }
        waiter->wait_queue_next = NULL;
        waiter->wake_tick = 0;  /* Cancel any timeout */
        waiter->state = THREAD_STATE_READY;
        scheduler_enqueue(waiter);
    }
    
    spinlock_unlock(&cv->lock);
    cpu_restore_flags(flags);
}

void condvar_broadcast(condvar_t *cv)
{
    if (!cv) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&cv->lock);
    
    cv->signal_count++;
    
    /* Wake ALL waiting threads */
    thread_t *waiter;
    while ((waiter = cv->waiters.head) != NULL) {
        cv->waiters.head = waiter->wait_queue_next;
        waiter->wait_queue_next = NULL;
        waiter->wake_tick = 0;
        waiter->state = THREAD_STATE_READY;
        scheduler_enqueue(waiter);
    }
    cv->waiters.tail = NULL;
    
    spinlock_unlock(&cv->lock);
    cpu_restore_flags(flags);
}


/* ============================================ */
/*        Read-Write Lock Implementation        */
/* ============================================ */

void rwlock_init(rwlock_t *rwlock, rwlock_preference_t preference)
{
    if (!rwlock) return;
    
    spinlock_init(&rwlock->lock);
    wait_queue_init(&rwlock->readers);
    wait_queue_init(&rwlock->writers);
    atomic_set(&rwlock->reader_count, 0);
    rwlock->writer = NULL;
    rwlock->writer_wait_count = 0;
    rwlock->preference = preference;
}

void rwlock_rdlock(rwlock_t *rwlock)
{
    if (!rwlock) return;
    
    thread_t *current = thread_current();
    if (!current) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    /* Block if:
     * 1. A writer holds the lock, OR
     * 2. (Writer-preferring) Writers are waiting
     */
    while (rwlock->writer != NULL ||
           (rwlock->preference == RWLOCK_PREFER_WRITER && rwlock->writer_wait_count > 0)) {
        
        /* Add to readers wait queue */
        current->state = THREAD_STATE_BLOCKED;
        current->waiting_queue = &rwlock->readers;
        
        thread_t **tail = &rwlock->readers.head;
        while (*tail) {
            tail = &((*tail)->wait_queue_next);
        }
        *tail = current;
        current->wait_queue_next = NULL;
        if (!rwlock->readers.tail) {
            rwlock->readers.tail = current;
        }
        
        spinlock_unlock(&rwlock->lock);
        scheduler_schedule();
        spinlock_lock(&rwlock->lock);
        
        current->waiting_queue = NULL;
    }
    
    /* Acquire read lock */
    atomic_inc(&rwlock->reader_count);
    
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
}

bool rwlock_tryrdlock(rwlock_t *rwlock)
{
    if (!rwlock) return false;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    /* Can acquire if no writer and (reader-preferring OR no waiting writers) */
    if (rwlock->writer == NULL &&
        (rwlock->preference == RWLOCK_PREFER_READER || rwlock->writer_wait_count == 0)) {
        atomic_inc(&rwlock->reader_count);
        spinlock_unlock(&rwlock->lock);
        cpu_restore_flags(flags);
        return true;
    }
    
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
    return false;
}

void rwlock_wrlock(rwlock_t *rwlock)
{
    if (!rwlock) return;
    
    thread_t *current = thread_current();
    if (!current) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    /* Announce we're waiting (for writer-preferring logic) */
    rwlock->writer_wait_count++;
    
    /* Block until no readers and no writer */
    while (rwlock->writer != NULL || atomic_load(&rwlock->reader_count) > 0) {
        /* Add to writers wait queue */
        current->state = THREAD_STATE_BLOCKED;
        current->waiting_queue = &rwlock->writers;
        
        thread_t **tail = &rwlock->writers.head;
        while (*tail) {
            tail = &((*tail)->wait_queue_next);
        }
        *tail = current;
        current->wait_queue_next = NULL;
        if (!rwlock->writers.tail) {
            rwlock->writers.tail = current;
        }
        
        spinlock_unlock(&rwlock->lock);
        scheduler_schedule();
        spinlock_lock(&rwlock->lock);
        
        current->waiting_queue = NULL;
    }
    
    /* Acquire write lock */
    rwlock->writer_wait_count--;
    rwlock->writer = current;
    
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
}

bool rwlock_trywrlock(rwlock_t *rwlock)
{
    if (!rwlock) return false;
    
    thread_t *current = thread_current();
    if (!current) return false;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    /* Can acquire if no readers and no writer */
    if (rwlock->writer == NULL && atomic_load(&rwlock->reader_count) == 0) {
        rwlock->writer = current;
        spinlock_unlock(&rwlock->lock);
        cpu_restore_flags(flags);
        return true;
    }
    
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
    return false;
}

void rwlock_rdunlock(rwlock_t *rwlock)
{
    if (!rwlock) return;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    int32_t new_count = atomic_dec(&rwlock->reader_count);
    
    /* If last reader and writers are waiting, wake one writer */
    if (new_count == 0 && rwlock->writers.head != NULL) {
        thread_t *waiter = rwlock->writers.head;
        rwlock->writers.head = waiter->wait_queue_next;
        if (!rwlock->writers.head) {
            rwlock->writers.tail = NULL;
        }
        waiter->wait_queue_next = NULL;
        waiter->state = THREAD_STATE_READY;
        scheduler_enqueue(waiter);
    }
    
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
}

void rwlock_wrunlock(rwlock_t *rwlock)
{
    if (!rwlock) return;
    
    thread_t *current = thread_current();
    if (!current || rwlock->writer != current) {
        KLOG_ERROR("RWLOCK", "Write unlock by non-owner!");
        return;
    }
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    rwlock->writer = NULL;
    
    /* Decide who to wake based on preference */
    if (rwlock->preference == RWLOCK_PREFER_WRITER && rwlock->writers.head != NULL) {
        /* Wake one writer */
        thread_t *waiter = rwlock->writers.head;
        rwlock->writers.head = waiter->wait_queue_next;
        if (!rwlock->writers.head) {
            rwlock->writers.tail = NULL;
        }
        waiter->wait_queue_next = NULL;
        waiter->state = THREAD_STATE_READY;
        scheduler_enqueue(waiter);
    } else {
        /* Wake all readers (they can all proceed), then one writer if any */
        thread_t *waiter;
        while ((waiter = rwlock->readers.head) != NULL) {
            rwlock->readers.head = waiter->wait_queue_next;
            waiter->wait_queue_next = NULL;
            waiter->state = THREAD_STATE_READY;
            scheduler_enqueue(waiter);
        }
        rwlock->readers.tail = NULL;
        
        /* If no readers were waiting, wake a writer */
        if (atomic_load(&rwlock->reader_count) == 0 && rwlock->writers.head != NULL) {
            waiter = rwlock->writers.head;
            rwlock->writers.head = waiter->wait_queue_next;
            if (!rwlock->writers.head) {
                rwlock->writers.tail = NULL;
            }
            waiter->wait_queue_next = NULL;
            waiter->state = THREAD_STATE_READY;
            scheduler_enqueue(waiter);
        }
    }
    
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
}

bool rwlock_upgrade(rwlock_t *rwlock)
{
    if (!rwlock) return false;
    
    thread_t *current = thread_current();
    if (!current) return false;
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    /* Must be a reader */
    if (atomic_load(&rwlock->reader_count) == 0) {
        spinlock_unlock(&rwlock->lock);
        cpu_restore_flags(flags);
        return false;
    }
    
    /* If we're the only reader, can upgrade directly */
    if (atomic_load(&rwlock->reader_count) == 1 && rwlock->writer == NULL) {
        atomic_dec(&rwlock->reader_count);
        rwlock->writer = current;
        spinlock_unlock(&rwlock->lock);
        cpu_restore_flags(flags);
        return true;
    }
    
    /* Otherwise, release read lock and acquire write lock (may deadlock with another upgrader) */
    /* For safety, we decline the upgrade - caller should release and re-acquire */
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
    return false;
}

void rwlock_downgrade(rwlock_t *rwlock)
{
    if (!rwlock) return;
    
    thread_t *current = thread_current();
    if (!current || rwlock->writer != current) {
        KLOG_ERROR("RWLOCK", "Downgrade by non-owner!");
        return;
    }
    
    uint32_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&rwlock->lock);
    
    /* Convert write lock to read lock */
    rwlock->writer = NULL;
    atomic_inc(&rwlock->reader_count);
    
    /* Wake all waiting readers (they can join us) */
    thread_t *waiter;
    while ((waiter = rwlock->readers.head) != NULL) {
        rwlock->readers.head = waiter->wait_queue_next;
        waiter->wait_queue_next = NULL;
        waiter->state = THREAD_STATE_READY;
        scheduler_enqueue(waiter);
    }
    rwlock->readers.tail = NULL;
    
    spinlock_unlock(&rwlock->lock);
    cpu_restore_flags(flags);
}


/* ============================================ */
/*        Debug Functions                       */
/* ============================================ */

void mutex_debug(mutex_t *mutex)
{
    if (!mutex) {
        console_puts("Mutex: NULL\n");
        return;
    }
    
    console_puts("Mutex Debug:\n");
    console_puts("  Type: ");
    console_puts(mutex->type == MUTEX_TYPE_NORMAL ? "NORMAL" :
                 mutex->type == MUTEX_TYPE_RECURSIVE ? "RECURSIVE" : "ERRORCHECK");
    console_puts("\n  Owner: ");
    console_puts(mutex->owner ? mutex->owner->name : "<none>");
    console_puts(" (TID ");
    console_put_dec(mutex->owner ? mutex->owner->tid : 0);
    console_puts(")\n  Recursion: ");
    console_put_dec(mutex->recursion_count);
    console_puts("\n  Waiters: ");
    console_puts(mutex->waiters.head ? "yes" : "none");
    console_puts("\n");
}

void semaphore_debug(semaphore_t *sem)
{
    if (!sem) {
        console_puts("Semaphore: NULL\n");
        return;
    }
    
    console_puts("Semaphore Debug:\n");
    console_puts("  Count: ");
    console_put_dec((uint32_t)atomic_load(&sem->count));
    console_puts("\n  Max: ");
    console_put_dec(sem->max_count);
    console_puts(" (0=unlimited)\n  Waiters: ");
    console_puts(sem->waiters.head ? "yes" : "none");
    console_puts("\n");
}

void rwlock_debug(rwlock_t *rwlock)
{
    if (!rwlock) {
        console_puts("RWLock: NULL\n");
        return;
    }
    
    console_puts("RWLock Debug:\n");
    console_puts("  Preference: ");
    console_puts(rwlock->preference == RWLOCK_PREFER_WRITER ? "WRITER" : "READER");
    console_puts("\n  Readers: ");
    console_put_dec((uint32_t)atomic_load(&rwlock->reader_count));
    console_puts("\n  Writer: ");
    console_puts(rwlock->writer ? rwlock->writer->name : "<none>");
    console_puts(" (TID ");
    console_put_dec(rwlock->writer ? rwlock->writer->tid : 0);
    console_puts(")\n  Writers waiting: ");
    console_put_dec(rwlock->writer_wait_count);
    console_puts("\n  Reader waiters: ");
    console_puts(rwlock->readers.head ? "yes" : "none");
    console_puts("\n  Writer waiters: ");
    console_puts(rwlock->writers.head ? "yes" : "none");
    console_puts("\n");
}
