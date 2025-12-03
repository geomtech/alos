/* src/kernel/workqueue.h - Kernel Work Queue and Worker Pool
 *
 * Provides asynchronous work execution through a pool of worker threads.
 * Work items are queued and executed FIFO by available workers.
 *
 * Features:
 * - FIFO work queue
 * - Configurable number of workers (default: 4)
 * - Graceful shutdown with timeout
 * - Global kernel work pool for easy async work submission
 */
#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "thread.h"
#include "sync.h"

/* ============================================ */
/*           Work Item Structure                */
/* ============================================ */

/**
 * Work function type - function to be executed by worker
 */
typedef void (*work_func_t)(void *arg);

/**
 * Work item - represents a unit of work to be executed
 */
typedef struct work_item {
    work_func_t func;           /* Function to execute */
    void *arg;                  /* Argument to pass to function */
    struct work_item *next;     /* Next item in queue (linked list) */
} work_item_t;

/* ============================================ */
/*           Work Queue Structure               */
/* ============================================ */

/**
 * Work queue - FIFO queue of work items
 */
typedef struct work_queue {
    spinlock_t lock;            /* Protects the queue */
    work_item_t *head;          /* Queue head (dequeue from here) */
    work_item_t *tail;          /* Queue tail (enqueue here) */
    uint32_t count;             /* Number of items in queue */
    semaphore_t work_sem;       /* Workers wait on this */
    volatile bool shutdown;     /* Shutdown flag */
} work_queue_t;

/* ============================================ */
/*           Worker Pool Structure              */
/* ============================================ */

/**
 * Worker pool - manages a set of worker threads
 */
typedef struct worker_pool {
    thread_t **workers;         /* Array of worker threads */
    uint32_t num_workers;       /* Number of workers */
    work_queue_t queue;         /* Shared work queue */
    bool running;               /* Pool is accepting work */
} worker_pool_t;

/* Default number of kernel workers */
#define KERNEL_WORKER_COUNT     4

/* ============================================ */
/*           Worker Pool API                    */
/* ============================================ */

/**
 * Create a worker pool with the specified number of workers.
 * Workers start immediately and wait for work.
 *
 * @param num_workers Number of worker threads to create
 * @return Pointer to the pool, or NULL on failure
 */
worker_pool_t *worker_pool_create(uint32_t num_workers);

/**
 * Submit work to a pool (non-blocking).
 * The work item will be executed by an available worker.
 *
 * @param pool Target worker pool
 * @param func Function to execute
 * @param arg Argument to pass to function
 * @return 0 on success, -1 on failure (pool shutdown or allocation failed)
 */
int worker_pool_submit(worker_pool_t *pool, work_func_t func, void *arg);

/**
 * Shutdown a worker pool with timeout.
 * Sets shutdown flag, wakes all workers, and waits for them to exit.
 *
 * @param pool Worker pool to shutdown
 * @param timeout_ms Maximum time to wait for workers (0 = infinite)
 * @return Number of workers that didn't terminate in time (0 = all terminated)
 */
int worker_pool_shutdown_timeout(worker_pool_t *pool, uint32_t timeout_ms);

/**
 * Shutdown a worker pool (infinite wait).
 * Convenience function that calls worker_pool_shutdown_timeout with timeout=0.
 *
 * @param pool Worker pool to shutdown
 */
void worker_pool_shutdown(worker_pool_t *pool);

/**
 * Destroy a worker pool and free all resources.
 * Must call worker_pool_shutdown first.
 *
 * @param pool Worker pool to destroy
 */
void worker_pool_destroy(worker_pool_t *pool);

/**
 * Get the number of pending work items in a pool.
 *
 * @param pool Worker pool
 * @return Number of items in the queue
 */
uint32_t worker_pool_pending(worker_pool_t *pool);

/* ============================================ */
/*        Global Kernel Worker Pool             */
/* ============================================ */

/**
 * Initialize the global kernel worker pool.
 * Called during kernel initialization.
 */
void workqueue_init(void);

/**
 * Submit work to the global kernel pool.
 * Convenience function for asynchronous kernel work.
 *
 * @param func Function to execute
 * @param arg Argument to pass to function
 * @return 0 on success, -1 on failure
 */
int kwork_submit(work_func_t func, void *arg);

/**
 * Shutdown the global kernel worker pool.
 * Called during kernel shutdown.
 */
void workqueue_shutdown(void);

#endif /* WORKQUEUE_H */
