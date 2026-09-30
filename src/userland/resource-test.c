#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/alos_resource.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;
#define CHECK(condition) do { \
  if (!(condition)) { \
    printf("[resource-test] FAIL line %d: %s errno=%d\n", \
           __LINE__, #condition, errno); \
    failures++; \
  } \
} while (0)

static void *worker(void *argument) {
  int nice;
  (void)argument;
  CHECK(alos_thread_set_nice(19) == 0);
  CHECK(alos_thread_get_nice(&nice) == 0 && nice == 19);
  return NULL;
}

int main(void) {
  struct rlimit limit;
  CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
  CHECK(limit.rlim_cur == 32 && limit.rlim_max == 32);
  int descriptors[32], count = 0;
  while (count < 32) {
    int fd = open("/", O_RDONLY);
    if (fd < 0) break;
    descriptors[count++] = fd;
  }
  CHECK(count <= 29);
  CHECK(errno == EMFILE);
  while (count) CHECK(close(descriptors[--count]) == 0);
  limit.rlim_cur = 16;
  errno = 0;
  CHECK(setrlimit(RLIMIT_NOFILE, &limit) == -1 && errno == ENOTSUP);
  CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur == 32);
  limit.rlim_cur = 33;
  CHECK(setrlimit(RLIMIT_NOFILE, &limit) == -1 && errno == EINVAL);
  CHECK(getrlimit(RLIMIT_DATA, &limit) == 0 &&
        limit.rlim_cur == RLIM_INFINITY && limit.rlim_max == RLIM_INFINITY);
  CHECK(setrlimit(RLIMIT_DATA, &limit) == -1 && errno == ENOTSUP);
  CHECK(getrlimit(RLIMIT_NICE, &limit) == -1 && errno == ENOTSUP);
  CHECK(getrlimit(RLIMIT_NOFILE, NULL) == -1 && errno == EFAULT);
  CHECK(getrlimit(RLIMIT_NOFILE, (struct rlimit *)(uintptr_t)1) == -1 &&
        errno == EFAULT);
  CHECK(setrlimit(RLIMIT_NOFILE, (const struct rlimit *)(uintptr_t)1) == -1 &&
        errno == EFAULT);
  CHECK(syscall2(290, RLIMIT_NOFILE, 1) == -EFAULT);
  CHECK(syscall3(292, 0, 0, 1) == -EFAULT);
  CHECK(syscall3(292, 2, 0, 0) == -EINVAL);
  CHECK(getpriority(PRIO_PROCESS, 0) == -1 && errno == ENOTSUP);
  CHECK(setpriority(PRIO_PROCESS, 0, 0) == -1 && errno == ENOTSUP);
  int original = 0, nice = 0;
  CHECK(alos_thread_get_nice(&original) == 0);
  CHECK(alos_thread_set_nice(-1) == 0);
  errno = 0;
  CHECK(alos_thread_get_nice(&nice) == 0 && nice == -1 && errno == 0);
  CHECK(alos_thread_set_nice(-20) == 0);
  CHECK(alos_thread_get_nice(&nice) == 0 && nice == -20);
  CHECK(!alos_thread_can_set_nice(-21) && !alos_thread_can_set_nice(20));
  CHECK(alos_thread_can_set_nice(-20) && alos_thread_can_set_nice(19));
  CHECK(alos_thread_set_nice(20) == -1 && errno == EINVAL);
  CHECK(alos_thread_get_nice(&nice) == 0 && nice == -20);
  pthread_t thread;
  int created = pthread_create(&thread, NULL, worker, NULL);
  CHECK(created == 0);
  if (!created) CHECK(pthread_join(thread, NULL) == 0);
  CHECK(alos_thread_get_nice(&nice) == 0 && nice == -20);
  CHECK(alos_thread_set_nice(original) == 0);
  printf("[resource-test] %s (%d failures)\n",
         failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
