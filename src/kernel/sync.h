/* src/kernel/sync.h - Advanced Synchronization Primitives
 * 
 * This file provides:
 * - Mutex: Exclusive locking with owner tracking and priority inheritance
 * - Semaphore: Counting semaphore for limited resources
 * - Condition Variable: Wait on complex conditions (POSIX-like)
 * - Read-Write Lock: Multiple readers, exclusive writer (writer-preferring)
 */
#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "thread.h"
#include "atomic.h"

/* ============================================ */
/*        Mutex - Mutual Exclusion Lock         */
/* ============================================ */

/**
 * Mutex types:
 * - NORMAL: Non-recursive, deadlock on re-lock by owner (recommended)
 * - RECURSIVE: Allows same thread to lock multiple times
 * - ERRORCHECK: Returns error on re-lock (debug mode)
 */
typedef enum {
    MUTEX_TYPE_NORMAL = 0,      /* Non-recursive (default) */
    MUTEX_TYPE_RECURSIVE,       /* Allows recursive locking */
    MUTEX_TYPE_ERRORCHECK       /* Returns error on deadlock */
} mutex_type_t;

/**
 * Mutex structure with owner tracking and priority inheritance
 */
typedef struct mutex {
    spinlock_t lock;            /* Internal spinlock for atomic operations */
    wait_queue_t waiters;       /* Threads waiting to acquire the mutex */
    thread_t *owner;            /* Current owner (NULL if unlocked) */
    uint32_t recursion_count;   /* For recursive mutexes */
    mutex_type_t type;          /* Mutex type */
    thread_priority_t owner_original_priority;  /* For priority inheritance */
} mutex_t;

/* Static initializer for normal mutex */
#define MUTEX_INIT { \
    .lock = {0}, \
    .waiters = {NULL, NULL, {0}}, \
    .owner = NULL, \
    .recursion_count = 0, \
    .type = MUTEX_TYPE_NORMAL, \
    .owner_original_priority = THREAD_PRIORITY_NORMAL \
}

/**
 * Initialize a mutex
 * @param mutex Pointer to mutex structure
 * @param type Mutex type (NORMAL, RECURSIVE, or ERRORCHECK)
 */
void mutex_init(mutex_t *mutex, mutex_type_t type);

/**
 * Acquire the mutex (blocking)
 * Will block if mutex is held by another thread
 * @param mutex Pointer to mutex
 * @return 0 on success, -1 on error (ERRORCHECK: re-lock detected)
 */
int mutex_lock(mutex_t *mutex);

/**
 * Try to acquire the mutex (non-blocking)
 * @param mutex Pointer to mutex
 * @return true if acquired, false if already held
 */
bool mutex_trylock(mutex_t *mutex);

/**
 * Release the mutex
 * @param mutex Pointer to mutex
 * @return 0 on success, -1 on error (not owner)
 */
int mutex_unlock(mutex_t *mutex);

/**
 * Check if current thread owns the mutex
 * @param mutex Pointer to mutex
 * @return true if current thread is owner
 */
bool mutex_is_owner(mutex_t *mutex);

/**
 * Check if mutex is locked
 * @param mutex Pointer to mutex
 * @return true if locked
 */
bool mutex_is_locked(mutex_t *mutex);


/* ============================================ */
/*        Semaphore - Counting Semaphore        */
/* ============================================ */

/**
 * Counting semaphore for resource management
 * count > 0: Resources available
 * count == 0: No resources, threads will block
 */
typedef struct semaphore {
    spinlock_t lock;            /* Internal spinlock */
    wait_queue_t waiters;       /* Threads waiting for resources */
    atomic_t count;             /* Available resource count */
    uint32_t max_count;         /* Maximum count (0 = unlimited) */
} semaphore_t;

/* Static initializer */
#define SEMAPHORE_INIT(initial) { \
    .lock = {0}, \
    .waiters = {NULL, NULL, {0}}, \
    .count = ATOMIC_INIT(initial), \
    .max_count = 0 \
}

/**
 * Initialize a semaphore
 * @param sem Pointer to semaphore
 * @param initial_count Initial resource count
 * @param max_count Maximum count (0 for unlimited)
 */
void semaphore_init(semaphore_t *sem, int32_t initial_count, uint32_t max_count);

/**
 * Acquire a resource (P operation / down / wait)
 * Blocks if count is 0
 * @param sem Pointer to semaphore
 */
void sem_wait(semaphore_t *sem);

/**
 * Try to acquire a resource (non-blocking)
 * @param sem Pointer to semaphore
 * @return true if acquired, false if count was 0
 */
bool sem_trywait(semaphore_t *sem);

/**
 * Acquire with timeout
 * @param sem Pointer to semaphore
 * @param timeout_ms Maximum wait time in milliseconds
 * @return true if acquired, false if timeout
 */
bool sem_timedwait(semaphore_t *sem, uint32_t timeout_ms);

/**
 * Release a resource (V operation / up / post)
 * Wakes one waiting thread if any
 * @param sem Pointer to semaphore
 * @return 0 on success, -1 if max_count exceeded
 */
int sem_post(semaphore_t *sem);

/**
 * Get current semaphore count
 * @param sem Pointer to semaphore
 * @return Current count
 */
int32_t sem_getvalue(semaphore_t *sem);


/* ============================================ */
/*        Condition Variable                    */
/* ============================================ */

/**
 * Condition variable for complex synchronization
 * Must always be used with a mutex
 */
typedef struct condvar {
    spinlock_t lock;            /* Internal spinlock */
    wait_queue_t waiters;       /* Threads waiting on condition */
    uint32_t signal_count;      /* Number of pending signals (for spurious wakeup handling) */
} condvar_t;

/* Static initializer */
#define CONDVAR_INIT { \
    .lock = {0}, \
    .waiters = {NULL, NULL, {0}}, \
    .signal_count = 0 \
}

/**
 * Initialize a condition variable
 * @param cv Pointer to condition variable
 */
void condvar_init(condvar_t *cv);

/**
 * Wait on condition variable
 * Atomically releases mutex and blocks until signaled
 * Re-acquires mutex before returning
 * @param cv Pointer to condition variable
 * @param mutex Pointer to associated mutex (must be held)
 */
void condvar_wait(condvar_t *cv, mutex_t *mutex);

/**
 * Wait on condition variable with timeout
 * @param cv Pointer to condition variable
 * @param mutex Pointer to associated mutex
 * @param timeout_ms Maximum wait time in milliseconds
 * @return true if signaled, false if timeout
 */
bool condvar_timedwait(condvar_t *cv, mutex_t *mutex, uint32_t timeout_ms);

/**
 * Signal one waiting thread
 * @param cv Pointer to condition variable
 */
void condvar_signal(condvar_t *cv);

/**
 * Signal all waiting threads
 * @param cv Pointer to condition variable
 */
void condvar_broadcast(condvar_t *cv);


/* ============================================ */
/*        Read-Write Lock                       */
/* ============================================ */

/**
 * RWLock preference modes
 */
typedef enum {
    RWLOCK_PREFER_WRITER = 0,   /* Writer-preferring (default, avoids writer starvation) */
    RWLOCK_PREFER_READER        /* Reader-preferring (better read throughput) */
} rwlock_preference_t;

/**
 * Read-Write Lock
 * Allows multiple concurrent readers OR one exclusive writer
 * 
 * Writer-preferring by default:
 * - New readers block when a writer is waiting
 * - Prevents writer starvation
 */
typedef struct rwlock {
    spinlock_t lock;            /* Internal spinlock */
    wait_queue_t readers;       /* Readers waiting to acquire */
    wait_queue_t writers;       /* Writers waiting to acquire */
    atomic_t reader_count;      /* Number of active readers */
    thread_t *writer;           /* Current writer (NULL if none) */
    uint32_t writer_wait_count; /* Writers waiting (for preference logic) */
    rwlock_preference_t preference;
} rwlock_t;

/* Static initializer (writer-preferring) */
#define RWLOCK_INIT { \
    .lock = {0}, \
    .readers = {NULL, NULL, {0}}, \
    .writers = {NULL, NULL, {0}}, \
    .reader_count = ATOMIC_INIT(0), \
    .writer = NULL, \
    .writer_wait_count = 0, \
    .preference = RWLOCK_PREFER_WRITER \
}

/**
 * Initialize a read-write lock
 * @param rwlock Pointer to rwlock
 * @param preference Reader or writer preference
 */
void rwlock_init(rwlock_t *rwlock, rwlock_preference_t preference);

/**
 * Acquire read lock (shared)
 * Multiple threads can hold read lock simultaneously
 * Blocks if a writer holds the lock or (writer-preferring) writers are waiting
 * @param rwlock Pointer to rwlock
 */
void rwlock_rdlock(rwlock_t *rwlock);

/**
 * Try to acquire read lock (non-blocking)
 * @param rwlock Pointer to rwlock
 * @return true if acquired, false otherwise
 */
bool rwlock_tryrdlock(rwlock_t *rwlock);

/**
 * Acquire write lock (exclusive)
 * Blocks until all readers and writers release
 * @param rwlock Pointer to rwlock
 */
void rwlock_wrlock(rwlock_t *rwlock);

/**
 * Try to acquire write lock (non-blocking)
 * @param rwlock Pointer to rwlock
 * @return true if acquired, false otherwise
 */
bool rwlock_trywrlock(rwlock_t *rwlock);

/**
 * Release read lock
 * @param rwlock Pointer to rwlock
 */
void rwlock_rdunlock(rwlock_t *rwlock);

/**
 * Release write lock
 * @param rwlock Pointer to rwlock
 */
void rwlock_wrunlock(rwlock_t *rwlock);

/**
 * Upgrade read lock to write lock
 * Must currently hold read lock
 * May block waiting for other readers to release
 * @param rwlock Pointer to rwlock
 * @return true on success, false if upgrade not possible (must release and re-acquire)
 */
bool rwlock_upgrade(rwlock_t *rwlock);

/**
 * Downgrade write lock to read lock
 * Atomically converts exclusive lock to shared lock
 * @param rwlock Pointer to rwlock
 */
void rwlock_downgrade(rwlock_t *rwlock);


/* ============================================ */
/*        Debug / Statistics                    */
/* ============================================ */

/**
 * Get mutex debug info
 */
void mutex_debug(mutex_t *mutex);

/**
 * Get semaphore debug info
 */
void semaphore_debug(semaphore_t *sem);

/**
 * Get rwlock debug info
 */
void rwlock_debug(rwlock_t *rwlock);

#endif /* SYNC_H */
