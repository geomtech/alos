#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/alos_process_control.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
  printf("process-control-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
  return 1; \
} } while (0)

static uint64_t milliseconds(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now)) alos_process_exit(1);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static void *spin(void *unused) {
  (void)unused;
  for (;;) __asm__ volatile("" ::: "memory");
}
static void *blocked(void *argument) {
  char byte;
  read((int)(intptr_t)argument, &byte, 1);
  return NULL;
}
static void *protect_output(void *output) {
  struct timespec delay = {0, 20000000};
  if (nanosleep(&delay, NULL) || mprotect(output, 4096, PROT_NONE))
    return (void *)1;
  return NULL;
}

int main(void) {
  alos_process_info_t info;
  CHECK(alos_process_query(getpid(), &info) == 0 && info.pid == (uint32_t)getpid() &&
        info.thread_count == 1 && info.state == ALOS_PROCESS_ALIVE);
  CHECK(alos_process_query(getpid(), (void *)(uintptr_t)1) == -1 &&
        errno == EFAULT);
  CHECK(alos_process_query(0, &info) == -1 && errno == EINVAL);
  CHECK(alos_process_terminate(getpid(), 2) == -1 && errno == EINVAL);
  alos_process_exit_t result;
  CHECK(alos_process_wait(-1, &result, 0) == -1 && errno == ECHILD);
  int child = fork();
  CHECK(child >= 0);
  if (!child) alos_process_exit(37);
  CHECK(alos_process_wait(child, &result, ALOS_PROCESS_WAIT_FOREVER) == child &&
        result.raw_status == 37 && result.reason == ALOS_PROCESS_EXIT_NORMAL);
  CHECK(alos_process_wait(child, &result, 0) == -1 && errno == ECHILD);

  child = fork();
  CHECK(child >= 0);
  if (!child) _exit(73);
  int legacy = 0;
  CHECK(waitpid(child, &legacy, 0) == child && legacy == 73);

  int parent = getpid();
  child = fork();
  CHECK(child >= 0);
  if (!child) {
    if (alos_process_query(parent, &info) != -1 || errno != EPERM ||
        alos_process_terminate(parent, 2) != -1 || errno != EPERM)
      alos_process_exit(1);
    alos_process_exit(0);
  }
  CHECK(alos_process_wait(child, &result, ALOS_PROCESS_WAIT_FOREVER) == child &&
        result.raw_status == 0 && result.reason == ALOS_PROCESS_EXIT_NORMAL);

  int ready[2], data[2];
  CHECK(pipe(ready) == 0 && pipe(data) == 0);
  child = fork();
  CHECK(child >= 0);
  if (!child) {
    close(ready[0]);
    pthread_t a, b;
    if (pthread_create(&a, NULL, spin, NULL) ||
        pthread_create(&b, NULL, blocked, (void *)(intptr_t)data[0]))
      alos_process_exit(1);
    if (write(ready[1], "R", 1) != 1) alos_process_exit(1);
    close(ready[1]);
    for (;;) {
      struct timespec delay = {1, 0};
      nanosleep(&delay, NULL);
    }
  }
  close(ready[1]);
  char byte;
  CHECK(read(ready[0], &byte, 1) == 1 && byte == 'R');
  CHECK(alos_process_query(child, &info) == 0 && info.thread_count == 3);
  result.raw_status = 12345;
  CHECK(alos_process_wait(child, &result, 0) == 0 && result.raw_status == 12345);
  uint64_t begin = milliseconds();
  CHECK(alos_process_wait(child, &result, 10) == 0 &&
        milliseconds() >= begin + 10);
  CHECK(alos_process_terminate(child, -23) == 0);
  CHECK(alos_process_wait(child, &result, 5000) == child &&
        result.raw_status == -23 && result.reason == ALOS_PROCESS_EXIT_FORCED);
  CHECK(alos_process_query(child, &info) == -1 && errno == ESRCH);
  close(ready[0]);
  close(data[0]);
  close(data[1]);

  void *output = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(output != MAP_FAILED);
  child = fork();
  CHECK(child >= 0);
  if (!child) {
    struct timespec delay = {0, 100000000};
    nanosleep(&delay, NULL);
    alos_process_exit(91);
  }
  pthread_t protector;
  CHECK(pthread_create(&protector, NULL, protect_output, output) == 0);
  CHECK(alos_process_wait(child, output, ALOS_PROCESS_WAIT_FOREVER) == -1 &&
        errno == EFAULT);
  void *protection_error;
  CHECK(pthread_join(protector, &protection_error) == 0 && !protection_error);
  CHECK(mprotect(output, 4096, PROT_READ | PROT_WRITE) == 0);
  CHECK(alos_process_wait(child, output, 0) == child &&
        ((alos_process_exit_t *)output)->raw_status == 91);
  CHECK(munmap(output, 4096) == 0);
  puts("process-control-test: PASS (native raw status, no POSIX signals)");
  return 0;
}
