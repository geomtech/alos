#ifndef RUNTIME_TEST_H
#define RUNTIME_TEST_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern int thread_create(void (*entry)(void *), void *stack, void *argument);
static uint32_t completed;
static uint32_t failures;

static inline uint64_t milliseconds(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t)) _exit(1);
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static inline void finish_worker(int failed) {
  extern void __libc_thread_fini(void);
  __libc_thread_fini();
  if (failed) __atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED);
  __atomic_add_fetch(&completed, 1, __ATOMIC_RELEASE);
  futex_wake(&completed, 64);
  _exit(0);
}

static inline int launch(void (*entry)(void *), long id) {
  void *stack = mmap(NULL, 64 * 1024, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return stack != MAP_FAILED &&
         thread_create(entry, (char *)stack + 64 * 1024, (void *)id) > 0;
}

static inline int await_workers(unsigned count) {
  for (;;) {
    unsigned current = __atomic_load_n(&completed, __ATOMIC_ACQUIRE);
    if (current == count) return failures == 0;
    if (futex_wait(&completed, current, 15000) && errno != EAGAIN) return 0;
  }
}
#endif
