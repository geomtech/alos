#include "runtime-test.h"
#include <sys/wait.h>
static _Thread_local int initialized = 37;
static _Thread_local uint64_t word = 0x123456789ABCDEF0ULL;
static _Thread_local uint64_t zero;
static _Thread_local char aligned[256] __attribute__((aligned(256)));

static void worker(void *argument) {
  uint64_t id = (uint64_t)argument;
  if (initialized != 37 || word != 0x123456789ABCDEF0ULL || zero ||
      ((uint64_t)aligned & 255)) finish_worker(1);
  initialized = (int)id;
  word = id * 65537;
  zero = id;
  aligned[0] = (char)id;
  uint64_t start = syscall0(SYS_CONTEXT_SWITCHES);
  do {
    for (volatile unsigned i = 0; i < 20000; i++);
    if (initialized != (int)id || word != id * 65537 || zero != id ||
        aligned[0] != (char)id) finish_worker(1);
  } while ((uint64_t)syscall0(SYS_CONTEXT_SWITCHES) - start < 50);
  finish_worker(0);
}

int main(void) {
  if (initialized != 37 || zero || ((uint64_t)aligned & 255)) return 1;
  initialized = 99;
  word = 123;
  zero = 88;
  pid_t child = fork();
  if (child < 0) return 1;
  if (!child) {
    if (initialized != 99 || word != 123 || zero != 88) _exit(1);
    initialized = 44;
    _exit(0);
  }
  int status;
  if (waitpid(child, &status, 0) != child || status || initialized != 99)
    return 1;
  for (long i = 1; i <= 4; i++) if (!launch(worker, i)) return 1;
  if (!await_workers(4) || initialized != 99 || word != 123 || zero != 88) {
    puts("tls-test: FAIL"); return 1;
  }
  puts("tls-test: PASS (Clang ELF TLS, alignment, fork, preemption)");
  return 0;
}
