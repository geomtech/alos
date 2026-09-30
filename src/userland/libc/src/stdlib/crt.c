#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/futex.h>
#include <errno.h>
#include <stdlib.h>

typedef void (*init_fn)(void);
extern init_fn __preinit_array_start[], __preinit_array_end[];
extern init_fn __init_array_start[], __init_array_end[];
extern init_fn __fini_array_start[], __fini_array_end[];
void *__dso_handle = &__dso_handle;

typedef struct finalizer {
  void (*function)(void *);
  void *argument;
  void *dso;
  struct finalizer *next;
} finalizer_t;

static finalizer_t *global_finalizers;
static _Thread_local finalizer_t *thread_finalizers;
static uint32_t finalizer_lock;

static void lock_finalizers(void) {
  while (__atomic_exchange_n(&finalizer_lock, 1, __ATOMIC_ACQUIRE)) {
    if (futex_wait(&finalizer_lock, 1, 0) < 0 && errno != EAGAIN) {
      puts("crt: finalizer lock failed");
      _exit(134);
    }
  }
}

static void unlock_finalizers(void) {
  __atomic_store_n(&finalizer_lock, 0, __ATOMIC_RELEASE);
  futex_wake(&finalizer_lock, 1);
}

int __cxa_atexit(void (*function)(void *), void *argument, void *dso) {
  finalizer_t *node = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (node == MAP_FAILED) return -1;
  lock_finalizers();
  *node = (finalizer_t){function, argument, dso, global_finalizers};
  global_finalizers = node;
  unlock_finalizers();
  return 0;
}

int __cxa_thread_atexit(void (*function)(void *), void *argument, void *dso) {
  finalizer_t *node = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (node == MAP_FAILED) return -1;
  *node = (finalizer_t){function, argument, dso, thread_finalizers};
  thread_finalizers = node;
  return 0;
}

void __libc_thread_fini(void) {
  while (thread_finalizers) {
    finalizer_t *node = thread_finalizers;
    thread_finalizers = node->next;
    node->function(node->argument);
    munmap(node, 4096);
  }
  extern void __pthread_key_fini(void) __attribute__((weak));
  if (__pthread_key_fini) __pthread_key_fini();
}

void __cxa_finalize(void *dso) {
  lock_finalizers();
  finalizer_t **link = &global_finalizers;
  while (*link) {
    finalizer_t *node = *link;
    if (!dso || dso == node->dso) {
      *link = node->next;
      unlock_finalizers();
      node->function(node->argument);
      munmap(node, 4096);
      lock_finalizers();
      link = &global_finalizers;
    } else link = &node->next;
  }
  unlock_finalizers();
}

void __libc_init(char **envp) {
  environ = envp;
  for (init_fn *p = __preinit_array_start; p != __preinit_array_end; p++) (*p)();
  for (init_fn *p = __init_array_start; p != __init_array_end; p++) (*p)();
}

void __libc_fini(void) {
  __libc_thread_fini();
  __cxa_finalize(NULL);
  for (init_fn *p = __fini_array_end; p != __fini_array_start;) (*--p)();
}
