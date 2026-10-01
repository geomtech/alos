/* src/kernel/thread.c - Thread Management Implementation */
#include "thread.h"
#include "display.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../include/string.h"
#include "../mm/kheap.h"
#include "../mm/vmm.h"
#include "../mm/vm.h"
#include "console.h"
#include "klog.h"
#include "process.h"
#include "sync.h"
#include "timer.h"
#include "syscall.h"
#include "../arch/x86_64/xstate.h"
#include "../arch/x86_64/cpu.h"
#include "tls.h"
#include "thread_lifecycle.h"
#include "../fs/pipe.h"
#include "native_network_cleanup.h"

/* Fonction ASM pour sauter vers un thread user (premier switch) */
extern void jump_to_user(uint64_t rsp, uint64_t rip, uint64_t cr3);

/* ========================================
 * Variables globales
 * ======================================== */

/* Thread actuellement en cours d'exécution */
static thread_t *g_current_thread = NULL;
static thread_t *g_timeout_list;

static void timeout_remove(thread_t *thread) {
  thread_t **link = &g_timeout_list;
  while (*link && *link != thread) link = &(*link)->timeout_next;
  if (*link) *link = thread->timeout_next;
  thread->timeout_next = NULL;
}

/* Run queues par priorité */
static thread_t *g_run_queues[THREAD_PRIORITY_COUNT] = {NULL};
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

/* Reaper thread for zombie cleanup */
static thread_t *g_reaper_thread = NULL;
static thread_t *g_zombie_list = NULL;
static mutex_t g_reaper_mutex;
static condvar_t g_reaper_cv;

/* Point d'entrée ASM pour nouveaux threads */
extern void task_entry_point(void);

/* ========================================
 * Fonctions utilitaires internes
 * ======================================== */

/* Forward declarations */
static thread_priority_t scheduler_nice_to_priority(int8_t nice);
static uint32_t scheduler_get_time_slice(thread_t *thread);

static void safe_strcpy(char *dest, const char *src, uint32_t max_len) {
  uint32_t i;
  for (i = 0; i < max_len - 1 && src[i]; i++) {
    dest[i] = src[i];
  }
  dest[i] = '\0';
}

static inline void cpu_cli(void) { __asm__ volatile("cli"); }

static inline void cpu_sti(void) { __asm__ volatile("sti"); }

static inline uint64_t cpu_save_flags(void) {
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0" : "=r"(flags));
  return flags;
}

static inline void cpu_restore_flags(uint64_t flags) {
  __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

/* ========================================
 * Wait Queue Implementation
 * ======================================== */

void wait_queue_init(wait_queue_t *queue) {
  if (!queue)
    return;
  queue->head = NULL;
  queue->tail = NULL;
  spinlock_init(&queue->lock);
}

static void wait_queue_enqueue_locked(wait_queue_t *queue, thread_t *thread) {
  if (!queue || !thread)
    return;

  thread->wait_queue_next = NULL;
  thread->waiting_queue = queue;

  if (queue->tail) {
    queue->tail->wait_queue_next = thread;
  } else {
    queue->head = thread;
  }
  queue->tail = thread;
}

static thread_t *wait_queue_dequeue_locked(wait_queue_t *queue) {
  if (!queue || !queue->head)
    return NULL;

  thread_t *thread = queue->head;
  queue->head = thread->wait_queue_next;

  if (!queue->head) {
    queue->tail = NULL;
  }

  thread->wait_queue_next = NULL;
  thread->waiting_queue = NULL;
  return thread;
}

/* Remove a specific thread from a wait queue (for timeout forced removal) */
bool wait_queue_remove(wait_queue_t *queue, thread_t *thread) {
  if (!queue || !thread)
    return false;

  spinlock_lock(&queue->lock);

  /* Search for the thread in the queue */
  thread_t *prev = NULL;
  thread_t *curr = queue->head;

  while (curr && curr != thread) {
    prev = curr;
    curr = curr->wait_queue_next;
  }

  if (!curr) {
    /* Thread not found in queue */
    spinlock_unlock(&queue->lock);
    return false;
  }

  /* Remove thread from queue */
  if (prev) {
    prev->wait_queue_next = curr->wait_queue_next;
  } else {
    queue->head = curr->wait_queue_next;
  }

  if (curr == queue->tail) {
    queue->tail = prev;
  }

  curr->wait_queue_next = NULL;
  curr->waiting_queue = NULL;
  curr->current_wait_queue = NULL;

  spinlock_unlock(&queue->lock);
  return true;
}

bool wait_queue_wait_timeout(wait_queue_t *queue,
                             wait_queue_predicate_t predicate, void *context,
                             uint32_t timeout_ms) {
  if (!queue) {
    thread_yield();
    return true; /* Assume success if no queue */
  }

  thread_t *thread = g_current_thread;
  if (!thread) {
    thread_yield();
    return true;
  }

  uint32_t flags = cpu_save_flags();
  cpu_cli();

  /* Setup timeout if specified */
  if (timeout_ms > 0) {
    thread->timeout_tick = timer_get_ticks() + timeout_ms;
    thread->timeout_next = g_timeout_list;
    g_timeout_list = thread;
  } else {
    thread->timeout_tick = 0; /* No timeout */
  }
  thread->wait_result = 0; /* Success by default */
  thread->current_wait_queue = queue;

  spinlock_lock(&queue->lock);

  /* Vérifier le prédicat avant de bloquer */
  while (!predicate || !predicate(context)) {
    if (thread->should_terminate) break;
    /* Check if we timed out while checking predicate */
    if (thread->wait_result == -ETIMEDOUT) {
      break;
    }

    /* Ajouter à la wait queue */
    wait_queue_enqueue_locked(queue, thread);
    thread->state = THREAD_STATE_BLOCKED;

    spinlock_unlock(&queue->lock);

    /* Céder le CPU - scheduler will check timeout */
    scheduler_schedule();

    /* Back from sleep - check what happened */
    spinlock_lock(&queue->lock);
    if (thread->should_terminate) break;

    /* If we timed out, exit the loop */
    if (thread->wait_result == -ETIMEDOUT) {
      break;
    }

    /* Re-vérifier le prédicat */
    if (!predicate) break;
    if (predicate && predicate(context)) {
      break;
    }
  }

  spinlock_unlock(&queue->lock);

  /* Cleanup timeout state */
  timeout_remove(thread);
  thread->timeout_tick = 0;
  thread->current_wait_queue = NULL;

  int result = thread->wait_result;
  thread->wait_result = 0;

  cpu_restore_flags(flags);

  return (result == 0); /* true = success/signal, false = timeout */
}

void wait_queue_wait(wait_queue_t *queue, wait_queue_predicate_t predicate,
                     void *context) {
  /* Call timeout version with no timeout */
  wait_queue_wait_timeout(queue, predicate, context, 0);
}

void wait_queue_wake_one(wait_queue_t *queue) {
  if (!queue)
    return;

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

void wait_queue_wake_all(wait_queue_t *queue) {
  if (!queue)
    return;

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
                        uint32_t stack_size, thread_priority_t priority) {
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
  memset(thread, 0, sizeof(*thread));
  thread->xstate = xstate_create();
  if (!thread->xstate) {
    kfree(stack);
    kfree(thread);
    return NULL;
  }
  thread->tid = g_next_tid++;
  if (name) {
    safe_strcpy(thread->name, name, THREAD_NAME_MAX);
  } else {
    thread->name[0] = '\0';
  }
  thread->magic = THREAD_MAGIC;
  thread->errno_address = 0;

  thread->owner = NULL; /* Thread kernel, pas de process parent */

  thread->state = THREAD_STATE_READY;
  thread->should_terminate = 0;
  thread->exited = false;
  thread->exit_status = 0;
  thread->first_switch = true; /* Premier switch à venir */

  thread->stack_base = stack;
  thread->stack_size = stack_size;
  thread->rsp0 = (uint64_t)stack + stack_size;

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
  thread->cpu_affinity = 0xFFFFFFFF; /* Can run on any CPU */
  thread->last_cpu = 0;              /* Default to CPU 0 */

  thread->wake_tick = 0;
  thread->waiting_queue = NULL;
  thread->wait_queue_next = NULL;

  /* Timeout support */
  thread->timeout_tick = 0;
  thread->wait_result = 0;
  thread->current_wait_queue = NULL;

  /* Join support */
  wait_queue_init(&thread->join_waiters);

  /* Reaper support */
  thread->zombie_next = NULL;

  /* Blocking syscall support */
  thread->needs_yield = false;
  thread->syscall_ctx = NULL;

  thread->sched_next = NULL;
  thread->sched_prev = NULL;
  thread->proc_next = NULL;

  /* Préemption */
  thread->preempt_count = 0;
  thread->preempt_pending = false;

  /* Préparer la stack initiale au FORMAT IRQ UNIFIÉ (x86-64).
   *
   * Ce format est compatible avec la préemption par IRQ timer.
   * Le stub IRQ fait: POP_ALL, add rsp 16, iretq
   *
   * Layout de la stack (du bas vers le haut, RSP pointe vers R15):
   *   === IRETQ Frame x86-64 (5 éléments) ===
   *   SS          <- Kernel Data (0x10)
   *   RSP         <- Stack top du thread
   *   RFLAGS      <- 0x202 (IF=1)
   *   CS          <- Kernel Code (0x08)
   *   RIP         <- task_entry_point
   *   === Error code / Int number (2 éléments) ===
   *   error_code  <- 0 (dummy)
   *   int_no      <- 32 (timer IRQ)
   *   === PUSH_ALL (15 registres) ===
   *   RAX, RCX, RDX, RBX, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15
   *
   * task_entry_point attend:
   *   R12 = entry function pointer
   *   R13 = argument (void*)
   */
  uint64_t *stack_top = (uint64_t *)((uint64_t)stack + stack_size);

  /* === IRETQ Frame x86-64 (5 éléments) ===
   * En mode 64 bits, IRETQ restaure également RSP/SS. Le frame doit donc
   * conserver les 5 qwords, même pour un retour Ring 0 -> Ring 0.
   */
  *(--stack_top) = 0x10;                         /* SS: Kernel Data */
  *(--stack_top) = (uint64_t)stack + stack_size; /* RSP */
  *(--stack_top) = 0x202;                        /* RFLAGS: IF=1 */
  *(--stack_top) = 0x08;                         /* CS: Kernel Code */
  *(--stack_top) = (uint64_t)task_entry_point;   /* RIP */

  /* === Error code / Int number (2 éléments) === */
  *(--stack_top) = 0;  /* error_code (dummy) */
  *(--stack_top) = 32; /* int_no (timer IRQ) */

  /* === PUSH_ALL (15 registres, ordre: rax, rcx, rdx, rbx, rbp, rsi, rdi,
   * r8-r15) === On push dans l'ordre inverse de POP_ALL pour que RSP pointe
   * vers R15. POP_ALL fait: pop r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi,
   * rbp, rbx, rdx, rcx, rax
   */
  *(--stack_top) = 0;               /* RAX */
  *(--stack_top) = 0;               /* RCX */
  *(--stack_top) = 0;               /* RDX */
  *(--stack_top) = 0;               /* RBX */
  *(--stack_top) = 0;               /* RBP */
  *(--stack_top) = 0;               /* RSI */
  *(--stack_top) = 0;               /* RDI */
  *(--stack_top) = 0;               /* R8 */
  *(--stack_top) = 0;               /* R9 */
  *(--stack_top) = 0;               /* R10 */
  *(--stack_top) = 0;               /* R11 */
  *(--stack_top) = (uint64_t)entry; /* R12 = entry function */
  *(--stack_top) = (uint64_t)arg;   /* R13 = argument */
  *(--stack_top) = 0;               /* R14 */
  *(--stack_top) = 0;               /* R15 */

  thread->rsp = (uint64_t)stack_top;

  KLOG_INFO_DEC("THREAD", "Created thread TID: ", thread->tid);
  KLOG_INFO_HEX("THREAD", "Stack: ", (uint64_t)stack);
  KLOG_INFO_HEX("THREAD", "ESP: ", thread->rsp);

  /* Ajouter au scheduler */
  scheduler_enqueue(thread);

  return thread;
}

thread_t *thread_create_in_process(process_t *proc, const char *name,
                                   thread_entry_t entry, void *arg,
                                   uint32_t stack_size,
                                   thread_priority_t priority) {
  thread_t *thread = thread_create(name, entry, arg, stack_size, priority);
  if (!thread)
    return NULL;

  thread->owner = proc;

  /* Ajouter à la liste des threads du process */
  /* TODO: implement process thread list */

  return thread;
}

/**
 * Crée un thread user mode pour un processus.
 * Le thread démarrera en Ring 3 à l'adresse entry_point avec la stack user_rsp.
 *
 * @param proc        Processus propriétaire
 * @param name        Nom du thread
 * @param entry_point Point d'entrée en user mode (EIP)
 * @param user_rsp    Stack pointer user mode (ESP)
 * @param arg         Argument à passer au thread (sera dans RDI)
 * @param kernel_stack Stack kernel pré-allouée (pour les syscalls)
 * @param kernel_stack_size Taille de la kernel stack
 * @return Thread créé, ou NULL si erreur
 */
thread_t *thread_create_user(process_t *proc, const char *name,
                             uint64_t entry_point, uint64_t user_rsp, void *arg,
                             void *kernel_stack, uint64_t kernel_stack_size) {
  if (!proc || !kernel_stack) {
    KLOG_ERROR("THREAD", "thread_create_user: invalid parameters");
    return NULL;
  }

  KLOG_INFO("THREAD", "Creating user thread:");
  KLOG_INFO("THREAD", name ? name : "<unnamed>");
  KLOG_INFO_HEX("THREAD", "Entry point: ", entry_point);
  KLOG_INFO_HEX("THREAD", "User ESP: ", user_rsp);

  /* Allouer la structure thread */
  thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!thread) {
    KLOG_ERROR("THREAD", "Failed to allocate thread structure");
    return NULL;
  }

  /* Initialiser la structure */
  memset(thread, 0, sizeof(*thread));
  thread->xstate = xstate_create();
  if (!thread->xstate) {
    kfree(thread);
    return NULL;
  }
  thread->tid = g_next_tid++;
  if (name) {
    safe_strcpy(thread->name, name, THREAD_NAME_MAX);
  } else {
    thread->name[0] = '\0';
  }
  thread->magic = THREAD_MAGIC;
  thread->errno_address = 0;

  thread->owner = proc;

  thread->state = THREAD_STATE_READY;
  thread->should_terminate = 0;
  thread->exited = false;
  thread->exit_status = 0;
  thread->first_switch = true; /* Premier switch - utiliser jump_to_user */

  /* La stack du thread est la kernel stack (pour les syscalls) */
  thread->stack_base = kernel_stack;
  thread->stack_size = kernel_stack_size;
  thread->rsp0 = (uint64_t)kernel_stack + kernel_stack_size;

  thread->entry = NULL; /* Pas de fonction entry pour user threads */
  thread->arg = NULL;

  /* Donner une priorité élevée aux threads utilisateur pour qu'ils soient
   * schedulés */
  thread->base_priority = THREAD_PRIORITY_UI;
  thread->priority = THREAD_PRIORITY_UI;
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
  thread->cpu_affinity = 0xFFFFFFFF;
  thread->last_cpu = 0;

  thread->wake_tick = 0;
  thread->waiting_queue = NULL;
  thread->wait_queue_next = NULL;

  /* Timeout support */
  thread->timeout_tick = 0;
  thread->wait_result = 0;
  thread->current_wait_queue = NULL;

  /* Join support */
  wait_queue_init(&thread->join_waiters);

  /* Reaper support */
  thread->zombie_next = NULL;

  /* Blocking syscall support */
  thread->needs_yield = false;
  thread->syscall_ctx = NULL;

  thread->sched_next = NULL;
  thread->sched_prev = NULL;
  thread->proc_next = NULL;

  /* Préemption */
  thread->preempt_count = 0;
  thread->preempt_pending = false;

  /* ========================================
   * Préparer la stack au FORMAT IRQ UNIFIÉ vers User Mode (Ring 3) - x86-64
   * ========================================
   *
   * Ce format est compatible avec la préemption par IRQ timer.
   * Le stub IRQ fait: POP_ALL, add rsp 16, iretq
   *
   * Layout de la stack (du bas vers le haut, RSP pointe vers R15):
   *   === IRETQ Frame (5 éléments) ===
   *   SS          <- User Data (0x1B)
   *   RSP         <- User stack pointer
   *   RFLAGS      <- 0x202 (IF=1)
   *   CS          <- User Code (0x23)
   *   RIP         <- User entry point
   *   === Error code / Int number (2 éléments) ===
   *   error_code  <- 0 (dummy)
   *   int_no      <- 0x80 (syscall, pour cohérence)
   *   === PUSH_ALL (15 registres) ===
   *   RAX, RCX, RDX, RBX, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15
   *
   * GDT layout:
   *   Index 3: User Data Segment -> selector = 3*8 | 3 = 0x1B
   *   Index 4: User Code Segment -> selector = 4*8 | 3 = 0x23
   */
  uint64_t *kstack_top =
      (uint64_t *)((uint64_t)kernel_stack + kernel_stack_size);

  /* === IRETQ Frame (5 éléments) === */
  *(--kstack_top) = 0x1B;        /* SS: User Data Segment (index 3, RPL=3) */
  *(--kstack_top) = user_rsp;    /* RSP: User stack pointer */
  *(--kstack_top) = 0x202;       /* RFLAGS: IF=1 (interrupts enabled) */
  *(--kstack_top) = 0x23;        /* CS: User Code Segment (index 4, RPL=3) */
  *(--kstack_top) = entry_point; /* RIP: User entry point */

  /* === Error code / Int number (2 éléments) === */
  *(--kstack_top) = 0;    /* error_code (dummy) */
  *(--kstack_top) = 0x80; /* int_no (syscall) */

  /* === PUSH_ALL (15 registres, ordre: rax, rcx, rdx, rbx, rbp, rsi, rdi,
   * r8-r15) === On push dans l'ordre inverse de POP_ALL pour que RSP pointe
   * vers R15. POP_ALL fait: pop r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi,
   * rbp, rbx, rdx, rcx, rax
   */
  *(--kstack_top) = 0;             /* RAX */
  *(--kstack_top) = 0;             /* RCX */
  *(--kstack_top) = 0;             /* RDX */
  *(--kstack_top) = 0;             /* RBX */
  *(--kstack_top) = 0;             /* RBP */
  *(--kstack_top) = 0;             /* RSI */
  *(--kstack_top) = (uint64_t)arg; /* RDI = argument pour la fonction user */
  *(--kstack_top) = 0;             /* R8 */
  *(--kstack_top) = 0;             /* R9 */
  *(--kstack_top) = 0;             /* R10 */
  *(--kstack_top) = 0;             /* R11 */
  *(--kstack_top) = 0;             /* R12 */
  *(--kstack_top) = 0;             /* R13 */
  *(--kstack_top) = 0;             /* R14 */
  *(--kstack_top) = 0;             /* R15 */

  thread->rsp = (uint64_t)kstack_top;

  KLOG_INFO_DEC("THREAD", "Created user thread TID: ", thread->tid);
  KLOG_INFO_HEX("THREAD", "Kernel stack (high): ",
                (uint32_t)((uint64_t)kernel_stack >> 32));
  KLOG_INFO_HEX("THREAD",
                "Kernel stack (low): ", (uint32_t)(uint64_t)kernel_stack);
  KLOG_INFO_HEX("THREAD", "RSP0 (high): ", (uint32_t)(thread->rsp0 >> 32));
  KLOG_INFO_HEX("THREAD", "RSP0 (low): ", (uint32_t)thread->rsp0);

  if (tls_setup_thread(proc, thread) != 0) {
    KLOG_ERROR("THREAD", "Unable to initialize ELF TLS");
    xstate_destroy(thread->xstate);
    kfree(thread);
    return NULL;
  }
  if (thread_completion_create(proc, thread)) {
    vm_munmap(proc, thread->tls_mapping, thread->tls_mapping_size);
    xstate_destroy(thread->xstate);
    kfree(thread);
    return NULL;
  }
  /* Ajouter au scheduler */
  scheduler_enqueue(thread);

  return thread;
}

thread_t *thread_create_user_from_frame(process_t *proc, const char *name,
                                        const interrupt_frame_t *frame,
                                        void *kernel_stack,
                                        uint64_t kernel_stack_size) {
  if (frame == NULL) {
    return NULL;
  }

  thread_t *thread =
      thread_create_user(proc, name, frame->rip, frame->rsp, NULL,
                         kernel_stack, kernel_stack_size);
  if (thread == NULL) {
    return NULL;
  }

  interrupt_frame_t *child_frame = (interrupt_frame_t *)thread->rsp;
  memcpy(child_frame, frame, sizeof(*child_frame));
  child_frame->rax = 0;
  thread_t *parent = thread_current();
  vm_munmap(proc, thread->tls_mapping, thread->tls_mapping_size);
  thread->fs_base = parent->fs_base;
  thread->tls_mapping = parent->tls_mapping;
  thread->tls_mapping_size = parent->tls_mapping_size;
  uint64_t tid = thread->tid;
  if (thread->fs_base)
    vmm_copy_to_dir((page_directory_t *)proc->pml4,
                     thread->fs_base + 24, &tid, sizeof(tid));
  xstate_save(parent->xstate);
  memcpy(thread->xstate, parent->xstate, PAGE_SIZE);
  return thread;
}

/* ========================================
 * Thread Control
 * ======================================== */

void thread_exit(int status) {
  thread_t *thread = g_current_thread;

  if (!thread || thread == g_idle_thread) {
    KLOG_ERROR("THREAD", "Cannot exit idle thread!");
    for (;;)
      __asm__ volatile("hlt");
  }

  KLOG_INFO("THREAD", "Thread exiting:");
  KLOG_INFO("THREAD", thread->name);

  cpu_cli();

  thread->exited = true;
  thread->exit_status = status;

  /* Wake up any threads waiting to join us */
  wait_queue_wake_all(&thread->join_waiters);

  thread->state = THREAD_STATE_ZOMBIE;

  /* Retirer de la run queue */
  scheduler_dequeue(thread);

  /* Add to reaper zombie list for cleanup */
  reaper_add_zombie(thread);

  /* Passer au prochain thread */
  scheduler_schedule();

  /* Ne devrait jamais arriver ici */
  for (;;)
    __asm__ volatile("hlt");
}

/* Predicate for thread_join: check if thread is zombie */
static bool is_thread_zombie(void *context) {
  thread_t *thread = (thread_t *)context;
  return (thread && thread->state == THREAD_STATE_ZOMBIE);
}

int thread_join_timeout(thread_t *thread, uint32_t timeout_ms) {
  if (!thread)
    return -1;

  /* Wait for thread to become zombie using proper wait queue */
  bool success = wait_queue_wait_timeout(&thread->join_waiters,
                                         is_thread_zombie, thread, timeout_ms);

  if (!success) {
    /* Timeout occurred */
    return -ETIMEDOUT;
  }

  /* Thread has exited, return its exit status */
  /* Note: Resource cleanup is done by the reaper thread */
  return thread->exit_status;
}

int thread_join(thread_t *thread) {
  /* Call timeout version with infinite wait */
  return thread_join_timeout(thread, 0);
}

bool thread_kill(thread_t *thread, int status) {
  if (!thread)
    return false;

  uint32_t flags = cpu_save_flags();
  cpu_cli();

  thread->should_terminate = 1;
  thread->exit_status = status;
  timeout_remove(thread);
  if (thread->waiting_queue) wait_queue_remove(thread->waiting_queue, thread);
  if (thread->state == THREAD_STATE_SLEEPING) {
    thread_t **link = &g_sleep_queue;
    while (*link && *link != thread) link = &(*link)->sched_next;
    if (*link) *link = thread->sched_next;
    thread->sched_next = NULL;
  }

  /* Si le thread est bloqué ou en sleep, le réveiller */
  if (thread->state == THREAD_STATE_BLOCKED ||
      thread->state == THREAD_STATE_SLEEPING) {
    thread->state = THREAD_STATE_READY;
    scheduler_enqueue(thread);
  }

  cpu_restore_flags(flags);
  return true;
}

thread_t *thread_current(void) { return g_current_thread; }

uint32_t thread_get_tid(void) {
  return g_current_thread ? g_current_thread->tid : 0;
}

void thread_set_priority(thread_t *thread, thread_priority_t priority) {
  if (!thread || priority >= THREAD_PRIORITY_COUNT)
    return;

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

static thread_priority_t scheduler_nice_to_priority(int8_t nice) {
  /* Clamp to valid range */
  if (nice < THREAD_NICE_MIN)
    nice = THREAD_NICE_MIN;
  if (nice > THREAD_NICE_MAX)
    nice = THREAD_NICE_MAX;

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
    20, /* IDLE: 20 ticks (20 ms) - long quantum */
    15, /* BACKGROUND: 15 ticks */
    10, /* NORMAL: 10 ticks (default) */
    7,  /* HIGH: 7 ticks */
    5   /* UI: 5 ticks - court pour réactivité */
};

static uint32_t scheduler_get_time_slice(thread_t *thread) {
  if (!thread)
    return THREAD_TIME_SLICE_DEFAULT;

  thread_priority_t pri = thread->priority;
  if (pri >= THREAD_PRIORITY_COUNT) {
    pri = THREAD_PRIORITY_NORMAL;
  }

  return g_priority_time_slice[pri];
}

void thread_set_nice(thread_t *thread, int8_t nice) {
  if (!thread)
    return;

  /* Clamp to valid range */
  if (nice < THREAD_NICE_MIN)
    nice = THREAD_NICE_MIN;
  if (nice > THREAD_NICE_MAX)
    nice = THREAD_NICE_MAX;

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

int8_t thread_get_nice(thread_t *thread) {
  if (!thread)
    return THREAD_NICE_DEFAULT;
  return thread->nice;
}

uint64_t thread_get_cpu_time_ms(thread_t *thread) {
  if (!thread)
    return 0;

  /* Timer runs at 1000 Hz, so ticks = milliseconds */
  return thread->cpu_ticks;
}

void thread_yield(void) {
  if (g_scheduler_active) {
    scheduler_schedule();
  }
}

void thread_sleep_ticks(uint64_t ticks) {
  if (!g_current_thread || ticks == 0)
    return;

  uint64_t flags = cpu_save_flags();
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
    while (prev->sched_next &&
           prev->sched_next->wake_tick <= thread->wake_tick) {
      prev = prev->sched_next;
    }
    thread->sched_next = prev->sched_next;
    prev->sched_next = thread;
  }

  spinlock_unlock(&g_sleep_lock);

  /* scheduler_schedule() gère lui-même cli/sti et le context switch.
   * Au retour, les interruptions seront dans l'état approprié. */
  scheduler_schedule();

  /* Restaurer l'état des interruptions d'avant l'appel */
  cpu_restore_flags(flags);
}

void thread_sleep_ms(uint32_t ms) {
  /* Avec un timer à 1000 Hz, 1 tick = 1 ms */
  thread_sleep_ticks((uint64_t)ms);
}

bool thread_should_exit(void) {
  return g_current_thread ? (g_current_thread->should_terminate != 0) : false;
}

const char *thread_state_name(thread_state_t state) {
  switch (state) {
  case THREAD_STATE_READY:
    return "READY";
  case THREAD_STATE_RUNNING:
    return "RUNNING";
  case THREAD_STATE_BLOCKED:
    return "BLOCKED";
  case THREAD_STATE_SLEEPING:
    return "SLEEPING";
  case THREAD_STATE_ZOMBIE:
    return "ZOMBIE";
  default:
    return "UNKNOWN";
  }
}

const char *thread_priority_name(thread_priority_t priority) {
  switch (priority) {
  case THREAD_PRIORITY_IDLE:
    return "IDLE";
  case THREAD_PRIORITY_BACKGROUND:
    return "BACKGROUND";
  case THREAD_PRIORITY_NORMAL:
    return "NORMAL";
  case THREAD_PRIORITY_HIGH:
    return "HIGH";
  case THREAD_PRIORITY_UI:
    return "UI";
  default:
    return "UNKNOWN";
  }
}

/* ========================================
 * Scheduler Implementation
 * ======================================== */

/* Fonction idle qui tourne quand aucun thread n'est prêt */
static void idle_thread_func(void *arg) {
  (void)arg;
  KLOG_INFO("IDLE", "Idle thread started, enabling interrupts");
  for (;;) {
    /* Enable interrupts and halt until next IRQ */
    __asm__ volatile("sti; hlt");

    /* After waking from hlt (IRQ occurred), check if another thread is ready.
     * This is necessary because the IRQ handler (e.g., keyboard) may have
     * woken up a thread, but scheduler_preempt() only runs on timer IRQ. */
    scheduler_schedule();
  }
}

/* Thread principal statique (représente le kernel/shell au boot) */
static thread_t g_main_thread_struct;

void scheduler_init(void) {
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
  main_thread->errno_address = 0;
  main_thread->xstate = xstate_create();
  if (!main_thread->xstate) {
    KLOG_ERROR("SCHED", "Cannot allocate initial CPU state");
    for (;;) __asm__ volatile("cli; hlt");
  }
  xstate_save(main_thread->xstate);
  main_thread->owner = NULL;
  main_thread->state = THREAD_STATE_RUNNING;
  main_thread->should_terminate = 0;
  main_thread->exited = false;
  main_thread->exit_status = 0;
  main_thread->stack_base = NULL; /* Pas de stack allouée */
  main_thread->stack_size = 0;
  main_thread->rsp = 0; /* Sera rempli lors du premier switch */
  main_thread->rsp0 = 0;
  main_thread->entry = NULL;
  main_thread->arg = NULL;
  main_thread->base_priority = THREAD_PRIORITY_NORMAL;
  main_thread->priority = THREAD_PRIORITY_NORMAL;
  main_thread->time_slice_remaining =
      g_priority_time_slice[THREAD_PRIORITY_NORMAL];

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

  /* Timeout support */
  main_thread->timeout_tick = 0;
  main_thread->wait_result = 0;
  main_thread->current_wait_queue = NULL;

  /* Join support */
  wait_queue_init(&main_thread->join_waiters);

  /* Reaper support */
  main_thread->zombie_next = NULL;

  /* Blocking syscall support */
  main_thread->needs_yield = false;
  main_thread->syscall_ctx = NULL;

  main_thread->sched_next = NULL;
  main_thread->sched_prev = NULL;
  main_thread->proc_next = NULL;
  main_thread->preempt_count = 0;
  main_thread->preempt_pending = false;

  g_current_thread = main_thread;

  KLOG_INFO("SCHED", "Main thread created (adopts current context)");

  /* Créer le thread idle */
  g_idle_thread =
      thread_create("idle", idle_thread_func, NULL, THREAD_DEFAULT_STACK_SIZE,
                    THREAD_PRIORITY_IDLE);
  if (!g_idle_thread) {
    KLOG_ERROR("SCHED", "Failed to create idle thread!");
    return;
  }

  /* Retirer idle de la run queue (il sera choisi automatiquement si nécessaire)
   */
  scheduler_dequeue(g_idle_thread);

  KLOG_INFO("SCHED", "Scheduler initialized");
}

void scheduler_start(void) {
  KLOG_INFO("SCHED", "Starting scheduler");
  g_scheduler_active = true;
}

void scheduler_tick(void) {
  if (!g_scheduler_active || !g_current_thread)
    return;

  uint64_t now = timer_get_ticks();

  /* Comptabilisation unique au tick PIT ; les switches ne l'ajoutent pas
   * une seconde fois. Le temps bloque n'appartient pas au thread. */
  if (g_current_thread && g_current_thread != g_idle_thread) {
    g_current_thread->cpu_ticks++;
  }

  /* Réveiller les threads endormis */
  scheduler_wake_sleeping();

  /* Check blocked threads with expired timeouts */
  check_thread_timeouts();

  /* Décrémenter le time slice */
  if (g_current_thread != g_idle_thread &&
      g_current_thread->time_slice_remaining > 0) {
    g_current_thread->time_slice_remaining--;
  }

  /* Marquer la préemption comme pending si time slice épuisé */
  if (g_current_thread->time_slice_remaining == 0 &&
      g_current_thread != g_idle_thread) {
    g_current_thread->preempt_pending = true;
  }

  /* Rocket Boost aging: check all run queues except UI for starvation
   * Note: We're in IRQ context (timer), use IRQ-safe spinlock for safety */
  uint64_t sched_flags = spinlock_irqsave(&g_scheduler_lock);

  for (int pri = THREAD_PRIORITY_IDLE; pri < THREAD_PRIORITY_UI; pri++) {
    thread_t *thread = g_run_queues[pri];

    while (thread) {
      thread_t *next = thread->sched_next; /* Save next before we move thread */

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
        thread->wait_start_tick = now; /* Reset wait timer */

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

  spinlock_irqrestore(&g_scheduler_lock, sched_flags);
}

/* ========================================
 * Préemption depuis IRQ (nouveau système)
 * ======================================== */

/* Fonction utilisée par scheduler_preempt pour pick sans lock (déjà pris) */
static thread_t *scheduler_pick_next_nolock(void) {
  /* Parcourir les priorités de la plus haute à la plus basse */
  for (int pri = THREAD_PRIORITY_COUNT - 1; pri >= THREAD_PRIORITY_IDLE;
       pri--) {
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
static void scheduler_enqueue_nolock(thread_t *thread) {
  if (!thread || thread == g_idle_thread ||
      thread->state == THREAD_STATE_RUNNING)
    return;

  thread_priority_t pri = thread->priority;
  if (pri >= THREAD_PRIORITY_COUNT) {
    pri = THREAD_PRIORITY_NORMAL;
  }

  thread->sched_next = NULL;
  thread_t *tail = g_run_queues[pri];
  while (tail && tail->sched_next) tail = tail->sched_next;
  thread->sched_prev = tail;
  if (tail) tail->sched_next = thread;
  else g_run_queues[pri] = thread;

  if (thread->state != THREAD_STATE_RUNNING) {
    thread->state = THREAD_STATE_READY;
  }
}

uint64_t scheduler_preempt(interrupt_frame_t *frame) {
  if (!g_scheduler_active || !g_current_thread)
    return 0;
  if ((frame->cs & 3) == 3 && g_current_thread->should_terminate)
    thread_exit(g_current_thread->exit_status);

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
    return 0; /* Pas de préemption */
  }

  if (current->time_slice_remaining > 0 && current != g_idle_thread) {
    return 0; /* Pas encore épuisé */
  }

  /* Ne pas interrompre les sections kernel historiques non preempt-safe.
   * Les frames IRQ Ring 3 et switch_task sont desormais identiques. */
  if ((frame->cs & 3) != 3 && current != g_idle_thread) return 0;
  spinlock_lock(&g_scheduler_lock);

  thread_t *next = scheduler_pick_next_nolock();
  if (!next || next == current) {
    spinlock_unlock(&g_scheduler_lock);
    /* Recharger le time slice si épuisé */
    if (current->time_slice_remaining == 0) {
      current->time_slice_remaining = THREAD_TIME_SLICE_DEFAULT;
    }
    return 0;
  }

  /* On va changer de thread ! */

  uint64_t now = timer_get_ticks();

  /* Boost demotion: if current thread was boosted, demote it back */
  if (current->is_boosted) {
    current->is_boosted = false;
    current->priority = scheduler_nice_to_priority(current->nice);
    current->base_priority = current->priority;
  }

  /* Remettre le thread actuel dans la run queue */
  if (current->state == THREAD_STATE_RUNNING) {
    current->state = THREAD_STATE_READY;
    current->wait_start_tick = now; /* Start aging timer */
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

  current_process = next->owner ? next->owner : idle_process;
  if (next->rsp0 != 0) {
    tss_set_rsp0(next->rsp0);
  }
  xstate_save(current->xstate);
  current->fs_base = rdmsr(0xC0000100);
  xstate_restore(next->xstate);
  wrmsr(0xC0000100, next->fs_base);
  next->first_switch = false;
  write_cr3(next->owner ? next->owner->cr3 : vmm_get_kernel_cr3());

  /* Sauvegarder l'ESP du thread préempté.
   * Le frame pointe vers les registres sauvegardés sur la stack.
   * On sauvegarde ce pointeur comme ESP du thread actuel.
   */
  current->rsp = (uint64_t)frame;

  /* Retourner l'ESP du nouveau thread.
   * Le code ASM va faire: mov esp, eax ; popa ; iretd
   * donc on retourne l'ESP qui pointe vers un frame sauvegardé.
   */
  return next->rsp;
}

/* ========================================
 * Contrôle de préemption
 * ======================================== */

void preempt_disable(void) {
  if (g_current_thread) {
    g_current_thread->preempt_count++;
  }
}

void preempt_enable(void) {
  if (!g_current_thread)
    return;

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

bool preempt_enabled(void) {
  if (!g_current_thread)
    return true;
  return g_current_thread->preempt_count == 0;
}

void scheduler_wake_sleeping(void) {
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

void scheduler_enqueue(thread_t *thread) {
  if (!thread || thread->state == THREAD_STATE_RUNNING)
    return;

  /* Use IRQ-safe spinlock since this can be called from IRQ context
   * (e.g., condvar_broadcast from tcp_handle_packet in IRQ handler) */
  uint64_t flags = spinlock_irqsave(&g_scheduler_lock);

  thread_priority_t pri = thread->priority;
  if (pri >= THREAD_PRIORITY_COUNT) {
    pri = THREAD_PRIORITY_NORMAL;
  }

  scheduler_enqueue_nolock(thread);

  spinlock_irqrestore(&g_scheduler_lock, flags);
}

void scheduler_dequeue(thread_t *thread) {
  if (!thread)
    return;

  /* Use IRQ-safe spinlock for consistency with scheduler_enqueue */
  uint64_t flags = spinlock_irqsave(&g_scheduler_lock);

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

  spinlock_irqrestore(&g_scheduler_lock, flags);
}

/* Sélectionne le prochain thread à exécuter */
static thread_t *scheduler_pick_next(void) {
  spinlock_lock(&g_scheduler_lock);

  /* Parcourir les priorités de la plus haute à la plus basse */
  for (int pri = THREAD_PRIORITY_COUNT - 1; pri >= THREAD_PRIORITY_IDLE;
       pri--) {
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
extern void switch_task(uint64_t *old_rsp_ptr, uint64_t new_rsp,
                        uint64_t new_cr3);

/* ESP dummy pour le premier switch */
static uint64_t g_dummy_rsp = 0;

void scheduler_schedule(void) {
  if (!g_scheduler_active)
    return;

  /* Conserver l'état IF du contexte appelant.
   * Un syscall INT 0x80 arrive avec IF=0, alors qu'un thread kernel normal
   * appelle généralement le scheduler avec IF=1. */
  uint64_t irq_flags = cpu_save_flags();
  cpu_cli();

  thread_t *current = g_current_thread;
  thread_t *next = scheduler_pick_next();

  if (!next) {
    next = g_idle_thread;
  }

  /* Si pas de next ou même thread, rien à faire */
  if (!next || next == current) {
    cpu_restore_flags(irq_flags);
    return;
  }

  uint64_t now = timer_get_ticks();

  /* Boost demotion: if current thread was boosted, demote it back */
  if (current && current->is_boosted) {
    current->is_boosted = false;
    current->priority = scheduler_nice_to_priority(current->nice);
    current->base_priority = current->priority;
  }

  /* Remettre le thread actuel dans la run queue s'il est toujours READY/RUNNING
   */
  if (current && (current->state == THREAD_STATE_RUNNING ||
                  current->state == THREAD_STATE_READY)) {
    current->state = THREAD_STATE_READY;
    current->wait_start_tick = now; /* Start aging timer */
    scheduler_enqueue(current);
  }

  /* CPU accounting: start next thread's run time */
  next->run_start_tick = now;
  next->context_switches++;

  /* Basculer vers le nouveau thread */
  next->time_slice_remaining = scheduler_get_time_slice(next);
  next->preempt_pending = false;
  next->state = THREAD_STATE_RUNNING;
  g_current_thread = next;

  /* IMPORTANT: Synchroniser current_process avec le thread actuel
   * pour maintenir la compatibilité avec les syscalls qui utilisent
   * current_process. Si le thread a un owner (processus user), utiliser
   * celui-ci. Sinon (thread kernel), current_process reste NULL ou
   * idle_process.
   */
  extern process_t *current_process;
  current_process = next->owner ? next->owner : idle_process;

  /* TSS.RSP0 doit toujours correspondre au thread cible.
   * C'est la stack à utiliser lors de la PROCHAINE transition Ring3 -> Ring0 ;
   * cela ne modifie pas le RSP courant, même si un thread user reprend
   * au milieu d'un syscall. Sans cette mise à jour, le prochain IRQ/syscall
   * user peut entrer sur la stack kernel d'un autre thread. */
  if (next->rsp0 != 0) {
    tss_set_rsp0(next->rsp0);
  }

  /* Context switch :
   * Utiliser le CR3 du processus owner si disponible (user),
   * sinon le CR3 du kernel (thread noyau).
   */
  uint64_t new_cr3;
  if (next->owner && next->owner->cr3) {
    new_cr3 = next->owner->cr3;
  } else {
    new_cr3 = vmm_get_kernel_cr3();
  }

  /* Sanity check CR3 */
  if (new_cr3 < 0x1000 || (new_cr3 & 0xFFF) != 0) {
    KLOG_ERROR_HEX("SCHED", "FATAL: Invalid CR3 = ", (uint32_t)new_cr3);
    for (;;)
      asm volatile("hlt");
  }

  /* Marquer first_switch comme false pour les threads user */
  if (next->first_switch) {
    next->first_switch = false;
  }

  /* Context switch coopératif x86-64.
   *
   * switch_task sauvegarde le frame IRETQ complet :
   * [SS, RSP, RFLAGS, CS, RIP, error_code, int_no, RAX...R15].
   * Ce format est également celui utilisé par les threads user.
   */
  if (current) {
    xstate_save(current->xstate);
    current->fs_base = rdmsr(0xC0000100);
  }
  xstate_restore(next->xstate);
  wrmsr(0xC0000100, next->fs_base);
  if (current) {
    switch_task(&current->rsp, next->rsp, new_cr3);
  } else {
    /* Premier switch - utiliser un RSP dummy */
    switch_task(&g_dummy_rsp, next->rsp, new_cr3);
  }

  /* On revient ici quand ce thread est reschedulé.
   * switch_task revient en Ring 0 avec IF désactivé ; restaurer exactement
   * l'état du contexte qui avait appelé scheduler_schedule(). */
  cpu_restore_flags(irq_flags);
}

/* ========================================
 * Timeout Checking (called from scheduler_tick)
 * ======================================== */

void check_thread_timeouts(void) {
  thread_t *thread = g_timeout_list;
  uint64_t deadline = timer_get_ticks();
  while (thread) {
    thread_t *next = thread->timeout_next;
    if (thread->timeout_tick <= deadline) {
      timeout_remove(thread);
      if (thread->state == THREAD_STATE_BLOCKED && thread->waiting_queue) {
        wait_queue_remove(thread->waiting_queue, thread);
        thread->wait_result = -ETIMEDOUT;
        thread->state = THREAD_STATE_READY;
        scheduler_enqueue(thread);
      }
    }
    thread = next;
  }
}

/* ========================================
 * Reaper Thread - Zombie Cleanup
 * ======================================== */

/* Forward declaration */
extern void mutex_init(mutex_t *mutex, mutex_type_t type);
extern int mutex_lock(mutex_t *mutex);
extern int mutex_unlock(mutex_t *mutex);
extern void condvar_init(condvar_t *cv);
extern void condvar_wait(condvar_t *cv, mutex_t *mutex);
extern void condvar_signal(condvar_t *cv);

static void reaper_thread_func(void *arg) {
  (void)arg;

  KLOG_INFO("REAPER", "Reaper thread started");

  for (;;) {
    mutex_lock(&g_reaper_mutex);

    /* Wait for zombies */
    while (g_zombie_list == NULL) {
      condvar_wait(&g_reaper_cv, &g_reaper_mutex);
    }

    /* Dequeue a zombie */
    thread_t *zombie = g_zombie_list;
    g_zombie_list = zombie->zombie_next;
    zombie->zombie_next = NULL;

    mutex_unlock(&g_reaper_mutex);

    /* Clean up the zombie */
    KLOG_INFO("REAPER", "Cleaning up zombie thread:");
    KLOG_INFO("REAPER", zombie->name);

    pipe_thread_cleanup(zombie);
    native_network_thread_cleanup(zombie);

    /* Si le thread a un processus owner, vérifier s'il faut le nettoyer */
    process_t *orphan_process = NULL;
    if (zombie->owner) {
      process_t *proc = zombie->owner;
      proc->exited_thread_cpu_ms += thread_get_cpu_time_ms(zombie);
      thread_t **link = &proc->thread_list;
      while (*link && *link != zombie) link = &(*link)->proc_next;
      if (*link) *link = zombie->proc_next;
      if (proc->main_thread == zombie) proc->main_thread = NULL;

      /* Décrémenter le compteur de threads du processus */
      if (proc->thread_count > 1 && zombie->errno_address != 0) {
        preempt_disable();
        if (vm_munmap(proc, zombie->errno_address, PAGE_SIZE) != 0) {
          KLOG_ERROR("REAPER", "Unable to release thread errno mapping");
        }
        preempt_enable();
      }
      if (proc->thread_count > 1 && zombie->tls_mapping != 0) {
        preempt_disable();
        if (vm_munmap(proc, zombie->tls_mapping, zombie->tls_mapping_size))
          KLOG_ERROR("REAPER", "Unable to release TLS mapping");
        preempt_enable();
      }
      proc->thread_count--;
      thread_completion_finish(zombie);

      /* Si c'était le dernier thread, marquer le processus ZOMBIE et réveiller le parent */
      if (proc->thread_count == 0) {
        KLOG_INFO("REAPER", "Last thread of process exited:");
        KLOG_INFO("REAPER", proc->name);

        proc_log_last_thread_exited(proc->pid);

        /* Marquer le processus comme zombie AVANT de réveiller les threads en
         * attente (process_join/waitpid), pour qu'ils voient un état cohérent
         * dès leur réveil. */
        proc->state = PROCESS_STATE_ZOMBIE;
        display_release_if_owner(proc->pid);

        /* Chaque processus possède sa table, mais les descriptions ouvertes
         * héritées sont partagées et ne ferment la ressource qu'à la dernière
         * référence. */
        file_table_destroy(proc->fd_table);
        shm_cleanup_process(proc);
        vm_cleanup(proc);
        thread_completion_cleanup(proc);

        /* Conserver exit_status (fourni par sys_exit ou par ce thread) */
        if (proc->exit_status == 0 && zombie->exit_status != 0) {
          proc->exit_status = zombie->exit_status;
        }

        proc_log_zombie(proc->pid, proc->exit_status);

        /* Libérer les ressources lourdes si possible (Page Directory et stack kernel) */
        if (proc->pml4 &&
            proc->pml4 != (uint64_t *)vmm_get_kernel_directory()) {
          KLOG_INFO("REAPER", "Freeing user page directory");
          vmm_free_directory((page_directory_t *)proc->pml4);
          proc->pml4 = NULL;
        }

        if (proc->stack_base) {
          kfree(proc->stack_base);
          proc->stack_base = NULL;
        }

        proc_log_waking_parent(proc->pid);
        if (process_complete_exit(proc)) {
          orphan_process = proc;
        }
      }

      zombie->owner = NULL;
    }

    /* Free thread stack */
    if (zombie->stack_base) {
      kfree(zombie->stack_base);
      zombie->stack_base = NULL;
    }

    /* Don't free the main thread structure (it's static) */
    if (zombie != &g_main_thread_struct) {
      xstate_destroy(zombie->xstate);
      kfree(zombie);
    }

    if (orphan_process != NULL) {
      process_reap(orphan_process);
    }
  }
}

void reaper_init(void) {
  KLOG_INFO("REAPER", "Initializing reaper thread");

  /* Initialize synchronization primitives */
  mutex_init(&g_reaper_mutex, MUTEX_TYPE_NORMAL);
  condvar_init(&g_reaper_cv);

  /* Create the reaper thread with low priority */
  g_reaper_thread =
      thread_create("reaper", reaper_thread_func, NULL,
                    THREAD_DEFAULT_STACK_SIZE, THREAD_PRIORITY_BACKGROUND);

  if (!g_reaper_thread) {
    KLOG_ERROR("REAPER", "Failed to create reaper thread!");
    return;
  }

  /* Set nice value to low priority (+10) */
  thread_set_nice(g_reaper_thread, 10);

  KLOG_INFO("REAPER", "Reaper thread initialized");
}

void reaper_add_zombie(thread_t *thread) {
  if (!thread)
    return;

  /* Don't add the reaper thread itself to avoid deadlock */
  if (thread == g_reaper_thread) {
    KLOG_ERROR("REAPER", "Cannot add reaper thread to zombie list!");
    return;
  }

  /* Note: We're called from thread_exit with interrupts disabled,
   * so we need to be careful about locking.
   * Use trylock or accept that mutex_lock might enable interrupts briefly.
   */

  /* Add to zombie list head */
  spinlock_lock(&g_reaper_mutex.lock);

  thread->zombie_next = g_zombie_list;
  g_zombie_list = thread;

  spinlock_unlock(&g_reaper_mutex.lock);

  /* Signal the reaper - use low-level wake since we can't block here */
  condvar_signal(&g_reaper_cv);
}

/* ========================================
 * Debug
 * ======================================== */

static void print_thread_info(thread_t *thread, bool is_current) {
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

void thread_list_debug(void) {
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

int thread_get_all_info(void *out_table, int max_entries) {
  if (!out_table || max_entries <= 0) return 0;
  proc_info_t *info = (proc_info_t *)out_table;
  int count = 0;

  cpu_cli();

  #define RECORD_THREAD(th, st) do { \
    if ((th) && count < max_entries) { \
      info[count].tid = (th)->tid; \
      info[count].pid = (th)->owner ? (th)->owner->pid : 0; \
      safe_strcpy(info[count].name, (th)->name ? (th)->name : "", sizeof(info[count].name)); \
      info[count].state = (st); \
      info[count].rip = (uint64_t)(th)->entry; \
      info[count].rsp = (th)->rsp; \
      count++; \
    } \
  } while(0)

  if (g_current_thread) {
    RECORD_THREAD(g_current_thread, THREAD_STATE_RUNNING);
  }

  for (int pri = THREAD_PRIORITY_COUNT - 1; pri >= 0; pri--) {
    thread_t *t = g_run_queues[pri];
    while (t && count < max_entries) {
      if (t != g_current_thread) {
        RECORD_THREAD(t, THREAD_STATE_READY);
      }
      t = t->sched_next;
    }
  }

  thread_t *sleep = g_sleep_queue;
  while (sleep && count < max_entries) {
    RECORD_THREAD(sleep, THREAD_STATE_BLOCKED);
    sleep = sleep->sched_next;
  }

  thread_t *zombie = g_zombie_list;
  while (zombie && count < max_entries) {
    RECORD_THREAD(zombie, THREAD_STATE_ZOMBIE);
    zombie = zombie->zombie_next;
  }

  #undef RECORD_THREAD

  cpu_sti();
  return count;
}
