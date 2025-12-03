/* src/kernel/sync.h - Synchronization Primitives */
#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "process.h"
#include "atomic.h"

/* ============================================ */
/*        Spinlock                              */
/* ============================================ */

typedef struct spinlock {
    volatile uint32_t value;
} spinlock_t;

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

/* ============================================ */
/*        Wait Queue                            */
/* ============================================ */

typedef struct wait_queue_node {
    process_t *process;
    struct wait_queue_node *next;
} wait_queue_node_t;

typedef struct wait_queue {
    wait_queue_node_t *head;
    wait_queue_node_t *tail;
    spinlock_t lock;
} wait_queue_t;

/* ============================================ */
/*        Mutex                                 */
/* ============================================ */

typedef struct mutex {
    spinlock_t lock;            /* Internal spinlock */
    wait_queue_t waiters;       /* Processes waiting */
    process_t *owner;           /* Current owner */
} mutex_t;

void mutex_init(mutex_t *mutex);
int mutex_lock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);

/* ============================================ */
/*        Condition Variable                    */
/* ============================================ */

typedef struct condvar {
    spinlock_t lock;
    wait_queue_t waiters;
} condvar_t;

void condvar_init(condvar_t *cv);
void condvar_wait(condvar_t *cv, mutex_t *mutex);
bool condvar_timedwait(condvar_t *cv, mutex_t *mutex, uint32_t timeout_ms);
void condvar_signal(condvar_t *cv);
void condvar_broadcast(condvar_t *cv);

#endif /* SYNC_H */
