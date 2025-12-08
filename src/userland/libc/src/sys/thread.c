/* src/userland/libc/src/sys/thread.c */
#include "internal/syscall.h"
#include <sys/syscall.h>
#include <unistd.h>

typedef void (*thread_func_t)(void *);

int thread_create(thread_func_t entry, void *stack, void *arg) {
  return syscall3(SYS_THREAD_CREATE, (long)entry, (long)stack, (long)arg);
}
