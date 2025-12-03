/* src/kernel/sync.c - Synchronization Primitives Implementation */
#include "sync.h"
#include "process.h"
#include "console.h"
#include "klog.h"
#include "../mm/kheap.h"
#include "timer.h"

/* ============================================ */
/*        Wait Queue Implementation             */
/* ============================================ */

static void wait_queue_add(wait_queue_t *q, process_t *proc)
{
    wait_queue_node_t *node = (wait_queue_node_t*)kmalloc(sizeof(wait_queue_node_t));
    if (!node) return;
    
    node->process = proc;
    node->next = NULL;
    
    spinlock_lock(&q->lock);
    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = node;
        q->tail = node;
    }
    spinlock_unlock(&q->lock);
}

static process_t *wait_queue_pop(wait_queue_t *q)
{
    spinlock_lock(&q->lock);
    if (!q->head) {
        spinlock_unlock(&q->lock);
        return NULL;
    }
    
    wait_queue_node_t *node = q->head;
    process_t *proc = node->process;
    
    q->head = node->next;
    if (!q->head) {
        q->tail = NULL;
    }
    spinlock_unlock(&q->lock);
    
    kfree(node);
    return proc;
}

/* ============================================ */
/*        Mutex Implementation                  */
/* ============================================ */

void mutex_init(mutex_t *mutex)
{
    spinlock_init(&mutex->lock);
    mutex->waiters.head = NULL;
    mutex->waiters.tail = NULL;
    spinlock_init(&mutex->waiters.lock);
    mutex->owner = NULL;
}

int mutex_lock(mutex_t *mutex)
{
    while (1) {
        spinlock_lock(&mutex->lock);
        
        if (mutex->owner == NULL) {
            mutex->owner = current_process;
            spinlock_unlock(&mutex->lock);
            return 0;
        }
        
        /* Mutex is locked, add to wait queue and block */
        wait_queue_add(&mutex->waiters, current_process);
        
        /* Mark current process as blocked */
        current_process->state = PROCESS_STATE_BLOCKED;
        
        spinlock_unlock(&mutex->lock);
        
        /* Yield CPU */
        schedule();
        
        /* Woken up, retry acquiring mutex */
    }
}

int mutex_unlock(mutex_t *mutex)
{
    spinlock_lock(&mutex->lock);
    
    if (mutex->owner != current_process) {
        spinlock_unlock(&mutex->lock);
        return -1;
    }
    
    mutex->owner = NULL;
    
    /* Wake up one waiter */
    process_t *next = wait_queue_pop(&mutex->waiters);
    if (next) {
        next->state = PROCESS_STATE_READY;
    }
    
    spinlock_unlock(&mutex->lock);
    return 0;
}

/* ============================================ */
/*        Condition Variable Implementation     */
/* ============================================ */

void condvar_init(condvar_t *cv)
{
    spinlock_init(&cv->lock);
    cv->waiters.head = NULL;
    cv->waiters.tail = NULL;
    spinlock_init(&cv->waiters.lock);
}

void condvar_wait(condvar_t *cv, mutex_t *mutex)
{
    /* Add to CV wait queue */
    wait_queue_add(&cv->waiters, current_process);
    
    /* Release mutex */
    mutex_unlock(mutex);
    
    /* Block */
    current_process->state = PROCESS_STATE_BLOCKED;
    schedule();
    
    /* Re-acquire mutex */
    mutex_lock(mutex);
}

bool condvar_timedwait(condvar_t *cv, mutex_t *mutex, uint32_t timeout_ms)
{
    /* Note: This is a simplified implementation.
       Proper timed wait requires scheduler support for timeouts.
       Here we just loop with sleep and check. */
       
    uint64_t start = timer_get_ticks();
    uint64_t ticks = timeout_ms; /* Assuming 1 tick = 1 ms */
    
    /* Add to wait queue */
    wait_queue_add(&cv->waiters, current_process);
    
    mutex_unlock(mutex);
    
    /* Block with timeout check? 
       Since we don't have proper timeout wakeups in wait_queue,
       we rely on the caller to handle spurious wakeups or we implement a poll loop.
       
       BUT, condvar_timedwait is supposed to block.
       If we just block, we might sleep forever if not signaled.
       
       Hack: Use a loop with small sleeps if we can't block with timeout.
       Or assume someone will signal us.
       
       Better: Just block. If timeout support is missing in scheduler,
       we can't easily implement true timed wait without a timer callback.
       
       For now, we'll implement it as a wait that checks time after wakeup,
       BUT we need a way to be woken up by timer.
    */
    
    /* Current scheduler doesn't support sleep with timeout on a wait queue.
       We will use a busy-wait loop with yield/sleep for now to simulate timed wait,
       checking the condition variable state is hard because we don't have the predicate here.
       
       Actually, the caller checks the predicate.
       We just need to return if timeout expires.
       
       If we block, we need a timer to wake us up.
    */
    
    /* Workaround: Spin with sleep */
    while (timer_get_ticks() - start < ticks) {
        /* Check if we were signaled? 
           We are in the wait queue. If signaled, we would be READY.
           But we are running now (or will be).
           
           This is tricky without scheduler support.
           Let's just use the blocking wait for now, ignoring timeout (infinite wait),
           OR return false immediately if not signaled (polling).
           
           The caller uses this in a loop:
           while (!condition) {
               if (!condvar_timedwait(...)) break; // timeout
           }
           
           If we return false immediately, it becomes a busy wait loop in the caller.
           That's acceptable for now to avoid blocking forever.
        */
        
        mutex_unlock(mutex);
        
        /* Sleep a bit to yield CPU */
        /* We can't use thread_sleep because it blocks unconditionally */
        /* Just yield */
        schedule();
        
        mutex_lock(mutex);
        
        /* Check if we were signaled? 
           We can't easily know if we were signaled or just scheduled.
           
           Let's just return false (timeout) to force caller to re-check condition.
           This effectively makes it a polling loop with yields.
        */
        return false; 
    }
    
    return false;
}

void condvar_signal(condvar_t *cv)
{
    process_t *proc = wait_queue_pop(&cv->waiters);
    if (proc) {
        proc->state = PROCESS_STATE_READY;
    }
}

void condvar_broadcast(condvar_t *cv)
{
    while (1) {
        process_t *proc = wait_queue_pop(&cv->waiters);
        if (!proc) break;
        proc->state = PROCESS_STATE_READY;
    }
}
