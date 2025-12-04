/* src/kernel/workqueue.c - Kernel Work Queue Implementation */
#include "workqueue.h"
#include "thread.h"
#include "sync.h"
#include "timer.h"
#include "console.h"
#include "klog.h"
#include "../mm/kheap.h"
#include "../include/string.h"

/* ============================================ */
/*           Global Kernel Worker Pool          */
/* ============================================ */

static worker_pool_t *g_kernel_pool = NULL;

/* ============================================ */
/*           Internal Functions                 */
/* ============================================ */

/**
 * Worker thread function - runs in a loop processing work items
 */
static void worker_thread_func(void *arg)
{
    worker_pool_t *pool = (worker_pool_t *)arg;
    work_queue_t *queue = &pool->queue;
    
    KLOG_INFO("WORKER", "Worker thread started");
    
    while (!queue->shutdown) {
        /* Wait for work (blocks if queue empty) */
        sem_wait(&queue->work_sem);
        
        /* Check if we should shutdown */
        if (queue->shutdown) {
            break;
        }
        
        /* Dequeue work item */
        spinlock_lock(&queue->lock);
        
        work_item_t *item = queue->head;
        if (item) {
            queue->head = item->next;
            if (queue->head == NULL) {
                queue->tail = NULL;
            }
            queue->count--;
        }
        
        spinlock_unlock(&queue->lock);
        
        /* Execute work if we got an item */
        if (item) {
            if (item->func) {
                item->func(item->arg);
            }
            kfree(item);
        }
    }
    
    KLOG_INFO("WORKER", "Worker thread exiting");
    
    /* Ne jamais retourner - appeler thread_exit pour terminer proprement */
    thread_exit(0);
    
    /* Ne devrait jamais arriver ici */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ============================================ */
/*           Worker Pool API                    */
/* ============================================ */

worker_pool_t *worker_pool_create(uint32_t num_workers)
{
    if (num_workers == 0) {
        num_workers = KERNEL_WORKER_COUNT;
    }
    
    KLOG_INFO("WORKQ", "Creating worker pool with workers:");
    KLOG_INFO_DEC("WORKQ", "Count: ", num_workers);
    
    /* Allocate pool structure */
    worker_pool_t *pool = (worker_pool_t *)kmalloc(sizeof(worker_pool_t));
    if (!pool) {
        KLOG_ERROR("WORKQ", "Failed to allocate worker pool");
        return NULL;
    }
    
    /* Initialize work queue */
    spinlock_init(&pool->queue.lock);
    pool->queue.head = NULL;
    pool->queue.tail = NULL;
    pool->queue.count = 0;
    pool->queue.shutdown = false;
    
    /* Initialize semaphore (starts at 0 - no work available) */
    semaphore_init(&pool->queue.work_sem, 0, 0);  /* 0 = unlimited max */
    
    /* Allocate worker thread array */
    pool->workers = (thread_t **)kmalloc(num_workers * sizeof(thread_t *));
    if (!pool->workers) {
        KLOG_ERROR("WORKQ", "Failed to allocate worker array");
        kfree(pool);
        return NULL;
    }
    
    pool->num_workers = num_workers;
    pool->running = true;
    
    /* Create worker threads */
    for (uint32_t i = 0; i < num_workers; i++) {
        char name[THREAD_NAME_MAX];
        /* Simple name formatting */
        name[0] = 'w';
        name[1] = 'o';
        name[2] = 'r';
        name[3] = 'k';
        name[4] = 'e';
        name[5] = 'r';
        name[6] = '-';
        name[7] = '0' + (i % 10);
        name[8] = '\0';
        
        pool->workers[i] = thread_create(name, worker_thread_func, pool,
                                         THREAD_DEFAULT_STACK_SIZE,
                                         THREAD_PRIORITY_BACKGROUND);
        
        if (!pool->workers[i]) {
            KLOG_ERROR("WORKQ", "Failed to create worker thread");
            /* Cleanup already created workers */
            pool->queue.shutdown = true;
            for (uint32_t j = 0; j < i; j++) {
                sem_post(&pool->queue.work_sem);  /* Wake worker to exit */
            }
            /* Wait a bit for cleanup, then free */
            thread_sleep_ms(100);
            kfree(pool->workers);
            kfree(pool);
            return NULL;
        }
        
        /* Set nice value for background work (+5) */
        thread_set_nice(pool->workers[i], 5);
    }
    
    KLOG_INFO("WORKQ", "Worker pool created successfully");
    return pool;
}

int worker_pool_submit(worker_pool_t *pool, work_func_t func, void *arg)
{
    if (!pool || !func) {
        return -1;
    }
    
    if (!pool->running || pool->queue.shutdown) {
        KLOG_ERROR("WORKQ", "Cannot submit work - pool is shutdown");
        return -1;
    }
    
    /* Allocate work item */
    work_item_t *item = (work_item_t *)kmalloc(sizeof(work_item_t));
    if (!item) {
        KLOG_ERROR("WORKQ", "Failed to allocate work item");
        return -1;
    }
    
    item->func = func;
    item->arg = arg;
    item->next = NULL;
    
    /* Enqueue work item (at tail for FIFO) */
    spinlock_lock(&pool->queue.lock);
    
    if (pool->queue.tail) {
        pool->queue.tail->next = item;
    } else {
        pool->queue.head = item;
    }
    pool->queue.tail = item;
    pool->queue.count++;
    
    spinlock_unlock(&pool->queue.lock);
    
    /* Signal that work is available */
    sem_post(&pool->queue.work_sem);
    
    return 0;
}

int worker_pool_shutdown_timeout(worker_pool_t *pool, uint32_t timeout_ms)
{
    if (!pool) {
        return 0;
    }
    
    KLOG_INFO("WORKQ", "Shutting down worker pool");
    
    /* Signal shutdown */
    pool->running = false;
    pool->queue.shutdown = true;
    
    /* Wake all workers so they can see shutdown flag */
    for (uint32_t i = 0; i < pool->num_workers; i++) {
        sem_post(&pool->queue.work_sem);
    }
    
    /* Wait for workers to terminate */
    int not_terminated = 0;
    uint64_t start_tick = timer_get_ticks();
    uint32_t remaining_timeout = timeout_ms;
    
    for (uint32_t i = 0; i < pool->num_workers; i++) {
        if (!pool->workers[i]) {
            continue;
        }
        
        /* Calculate remaining timeout */
        if (timeout_ms > 0) {
            uint64_t elapsed = timer_get_ticks() - start_tick;
            if (elapsed >= timeout_ms) {
                remaining_timeout = 1;  /* Minimal timeout */
            } else {
                remaining_timeout = timeout_ms - (uint32_t)elapsed;
            }
        }
        
        /* Try to join worker with timeout */
        int result = thread_join_timeout(pool->workers[i], remaining_timeout);
        
        if (result == -ETIMEDOUT) {
            KLOG_ERROR("WORKQ", "Worker thread did not terminate in time");
            not_terminated++;
        }
    }
    
    if (not_terminated == 0) {
        KLOG_INFO("WORKQ", "All workers terminated successfully");
    } else {
        KLOG_ERROR("WORKQ", "Some workers did not terminate:");
        console_put_dec(not_terminated);
        console_puts("\n");
    }
    
    return not_terminated;
}

void worker_pool_shutdown(worker_pool_t *pool)
{
    worker_pool_shutdown_timeout(pool, 0);  /* Infinite wait */
}

void worker_pool_destroy(worker_pool_t *pool)
{
    if (!pool) {
        return;
    }
    
    /* Free any remaining work items */
    spinlock_lock(&pool->queue.lock);
    
    work_item_t *item = pool->queue.head;
    while (item) {
        work_item_t *next = item->next;
        kfree(item);
        item = next;
    }
    
    pool->queue.head = NULL;
    pool->queue.tail = NULL;
    pool->queue.count = 0;
    
    spinlock_unlock(&pool->queue.lock);
    
    /* Free worker array (threads are cleaned by reaper) */
    if (pool->workers) {
        kfree(pool->workers);
    }
    
    /* Free pool structure */
    kfree(pool);
    
    KLOG_INFO("WORKQ", "Worker pool destroyed");
}

uint32_t worker_pool_pending(worker_pool_t *pool)
{
    if (!pool) {
        return 0;
    }
    
    spinlock_lock(&pool->queue.lock);
    uint32_t count = pool->queue.count;
    spinlock_unlock(&pool->queue.lock);
    
    return count;
}

/* ============================================ */
/*        Global Kernel Worker Pool             */
/* ============================================ */

void workqueue_init(void)
{
    KLOG_INFO("WORKQ", "Initializing global kernel worker pool");
    
    g_kernel_pool = worker_pool_create(KERNEL_WORKER_COUNT);
    
    if (!g_kernel_pool) {
        KLOG_ERROR("WORKQ", "Failed to create kernel worker pool!");
        return;
    }
    
    KLOG_INFO("WORKQ", "Kernel worker pool initialized");
}

int kwork_submit(work_func_t func, void *arg)
{
    if (!g_kernel_pool) {
        KLOG_ERROR("WORKQ", "Kernel worker pool not initialized");
        return -1;
    }
    
    return worker_pool_submit(g_kernel_pool, func, arg);
}

void workqueue_shutdown(void)
{
    if (g_kernel_pool) {
        worker_pool_shutdown_timeout(g_kernel_pool, WORKER_SHUTDOWN_TIMEOUT_MS);
        worker_pool_destroy(g_kernel_pool);
        g_kernel_pool = NULL;
    }
}
