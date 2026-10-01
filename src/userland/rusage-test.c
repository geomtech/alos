#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define PAGE 4096UL

static int failures;

#define CHECK(condition) do { \
  if (!(condition)) { \
    printf("rusage-test: FAIL line %d: %s errno=%d\n", \
           __LINE__, #condition, errno); \
    failures++; \
  } \
} while (0)

static uint64_t usage_ms(const struct rusage *usage) {
  return (uint64_t)usage->ru_utime.tv_sec * 1000 +
         (uint64_t)usage->ru_utime.tv_usec / 1000;
}

static uint64_t monotonic_ms(void) {
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return 0;
  return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

static void spin_for_ms(uint64_t duration) {
  volatile uint64_t sink = 0;
  uint64_t start = monotonic_ms();
  while (monotonic_ms() - start < duration) {
    for (unsigned i = 0; i < 1000; ++i) sink += i;
  }
  (void)sink;
}

static void *worker(void *argument) {
  (void)argument;
  spin_for_ms(220);
  return NULL;
}

static int count_entries(const char *path) {
  DIR *directory = opendir(path);
  if (!directory) return -1;
  int count = 0;
  while (readdir(directory)) count++;
  int saved = errno;
  closedir(directory);
  errno = saved;
  return count;
}

int main(void) {
  struct rusage before, after, main_thread_before, main_thread_after;
  CHECK(getrusage(RUSAGE_SELF, &before) == 0);
  CHECK(before.ru_maxrss > 0);
  CHECK(before.ru_stime.tv_sec == 0 && before.ru_stime.tv_usec == 0);
  spin_for_ms(220);
  CHECK(getrusage(RUSAGE_SELF, &after) == 0);
  CHECK(usage_ms(&after) >= usage_ms(&before) + 100);

  CHECK(getrusage(RUSAGE_THREAD, &main_thread_before) == 0);
  CHECK(getrusage(RUSAGE_SELF, &before) == 0);
  pthread_t thread;
  int created = pthread_create(&thread, NULL, worker, NULL);
  CHECK(created == 0);
  if (created == 0) CHECK(pthread_join(thread, NULL) == 0);
  CHECK(getrusage(RUSAGE_SELF, &after) == 0);
  CHECK(getrusage(RUSAGE_THREAD, &main_thread_after) == 0);
  uint64_t process_delta = usage_ms(&after) - usage_ms(&before);
  uint64_t main_delta =
      usage_ms(&main_thread_after) - usage_ms(&main_thread_before);
  CHECK(process_delta >= 100);
  CHECK(main_delta + 80 < process_delta);

  long peak_before = after.ru_maxrss;
  unsigned char *memory = mmap(NULL, 4 * 1024 * 1024,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(memory != MAP_FAILED);
  if (memory != MAP_FAILED) {
    volatile unsigned char *resident = memory;
    for (size_t offset = 0; offset < 4 * 1024 * 1024; offset += PAGE)
      resident[offset] = (unsigned char)(offset / PAGE);
    CHECK(getrusage(RUSAGE_SELF, &after) == 0);
    if (after.ru_maxrss <= peak_before) {
      printf("rusage-test: maxrss before=%ld after=%ld\n",
             peak_before, after.ru_maxrss);
    }
    CHECK(after.ru_maxrss > peak_before);
    CHECK(munmap(memory, 4 * 1024 * 1024) == 0);
    CHECK(getrusage(RUSAGE_SELF, &before) == 0);
    CHECK(before.ru_maxrss >= after.ru_maxrss);
  }

  errno = 0;
  CHECK(getrusage(12345, &after) == -1 && errno == EINVAL);
  errno = 0;
  CHECK(getrusage(RUSAGE_CHILDREN, &after) == -1 && errno == EINVAL);
  errno = 0;
  CHECK(getrusage(RUSAGE_SELF, NULL) == -1 && errno == EFAULT);
  errno = 0;
  CHECK(getrusage(RUSAGE_SELF, (struct rusage *)(uintptr_t)1) == -1 &&
        errno == EFAULT);

  const char *temporary_directory = "/posix-test";
  int root_before = count_entries(temporary_directory);
  if (root_before < 0) {
    temporary_directory = "/";
    root_before = count_entries(temporary_directory);
  }
  CHECK(root_before >= 0);
  FILE *file = tmpfile();
  CHECK(file != NULL);
  int root_during = count_entries(temporary_directory);
  CHECK(root_during == root_before + 1);
  if (file) {
    CHECK(fwrite("temporary", 1, 9, file) == 9);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    char buffer[16] = {0};
    CHECK(fread(buffer, 1, 9, file) == 9 && strcmp(buffer, "temporary") == 0);
    CHECK(fclose(file) == 0);
  }
  CHECK(count_entries(temporary_directory) == root_before);

  if (failures) {
    printf("rusage-test: FAIL %d\n", failures);
    return 1;
  }
  puts("rusage-test: PASS");
  return 0;
}
