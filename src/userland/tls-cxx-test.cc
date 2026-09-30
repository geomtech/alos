#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
static unsigned constructors, destructors;
struct Local {
  int value;
  Local() : value(73) { __atomic_add_fetch(&constructors, 1, __ATOMIC_RELAXED); }
  ~Local() { __atomic_add_fetch(&destructors, 1, __ATOMIC_RELAXED); }
};
thread_local Local local;
static void *worker(void *argument) {
  long id = (long)argument;
  if (local.value != 73) return (void *)1;
  local.value = (int)id;
  long before = syscall0(SYS_CONTEXT_SWITCHES);
  do {
    for (volatile unsigned i = 0; i < 10000; i++);
    if (local.value != id) return (void *)1;
  } while (syscall0(SYS_CONTEXT_SWITCHES) - before < 50);
  return argument;
}
int main() {
  pthread_t threads[4];
  for (long i = 0; i < 4; i++)
    if (pthread_create(&threads[i], NULL, worker, (void *)(i + 5))) return 1;
  for (long i = 0; i < 4; i++) {
    void *result;
    if (pthread_join(threads[i], &result) || result != (void *)(i + 5)) return 1;
  }
  if (constructors != 4 || destructors != 4) {
    puts("tls-cxx-test: FAIL"); return 1;
  }
  puts("tls-cxx-test: PASS (thread_local objects, constructors/destructors, preemption)");
  return 0;
}
