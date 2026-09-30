/* Les resultats de join survivent a la liberation du thread par le reaper. */
#include "thread_lifecycle.h"
#include "process.h"
#include "uaccess.h"
#include "../mm/kheap.h"
#include "../mm/vm.h"
#include "../include/string.h"
#include "../include/errno.h"
#include "klog.h"

typedef struct thread_completion {
  uint32_t tid;
  int finished, claimed, detached, status;
  uint64_t descriptor, descriptor_size, stack, stack_size;
  wait_queue_t waiters;
  struct thread_completion *next;
} completion_t;

int thread_completion_create(process_t *process, thread_t *thread) {
  completion_t *record = kmalloc(sizeof(*record));
  if (!record) return -ENOMEM;
  memset(record, 0, sizeof(*record));
  record->tid = thread->tid;
  wait_queue_init(&record->waiters);
  record->next = process->thread_completions;
  process->thread_completions = record;
  thread->completion = record;
  return 0;
}

static completion_t *lookup(process_t *process, uint32_t tid) {
  for (completion_t *record = process->thread_completions; record; record = record->next)
    if (record->tid == tid) return record;
  return NULL;
}

static void remove_record(process_t *process, completion_t *record) {
  completion_t **link = &process->thread_completions;
  while (*link && *link != record) link = &(*link)->next;
  if (*link) *link = record->next;
  kfree(record);
}

static void release_detached(process_t *process, completion_t *record) {
  if (record->stack && vm_munmap(process, record->stack, record->stack_size))
    KLOG_ERROR("THREAD", "Unable to reclaim detached stack");
  if (record->descriptor &&
      vm_munmap(process, record->descriptor, record->descriptor_size))
    KLOG_ERROR("THREAD", "Unable to reclaim detached descriptor");
  remove_record(process, record);
}

void thread_completion_finish(thread_t *thread) {
  completion_t *record = thread->completion;
  if (!record) return;
  record->finished = 1;
  record->status = thread->exit_status;
  if (record->detached) release_detached(thread->owner, record);
  else wait_queue_wake_all(&record->waiters);
  thread->completion = NULL;
}

static bool finished(void *context) {
  return ((completion_t *)context)->finished;
}

int thread_user_join(uint32_t tid) {
  process_t *process = process_current();
  completion_t *record = lookup(process, tid);
  if (!record) return -ESRCH;
  if (tid == thread_get_tid()) return -EDEADLK;
  if (record->claimed || record->detached) return -EINVAL;
  record->claimed = 1;
  wait_queue_wait(&record->waiters, finished, record);
  if (thread_current()->should_terminate) return -EINTR;
  remove_record(process, record);
  return 0;
}

int thread_user_detach(uint32_t tid) {
  process_t *process = process_current();
  completion_t *record = lookup(process, tid);
  if (!record) return -ESRCH;
  if (record->claimed || record->detached) return -EINVAL;
  record->detached = 1;
  if (record->finished) release_detached(process, record);
  return 0;
}

int thread_user_register(uint32_t tid, uint64_t descriptor, uint64_t stack,
                         uint64_t stack_size, uint64_t descriptor_size) {
  completion_t *record = lookup(process_current(), tid);
  if (!record) return -ESRCH;
  if (record->descriptor || !descriptor_size || !stack_size ||
      stack_size > 16 * 1024 * 1024 || descriptor_size > 4096 ||
      !user_range_valid((void *)descriptor, descriptor_size, true))
    return -EINVAL;
  record->descriptor = descriptor;
  record->descriptor_size = descriptor_size;
  record->stack = stack;
  record->stack_size = stack_size;
  return 0;
}

void thread_completion_cleanup(process_t *process) {
  while (process->thread_completions)
    remove_record(process, process->thread_completions);
}
