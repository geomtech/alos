#include "runtime-test.h"
#include <sys/time.h>
#include <string.h>

static void worker(void *argument) {
  long ms = (long)argument;
  struct timespec delay = {0, ms * 1000000};
  uint64_t begin = milliseconds();
  if (nanosleep(&delay, NULL) || milliseconds() < begin + ms)
    finish_worker(1);
  finish_worker(0);
}

int main(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_REALTIME, &t) || t.tv_sec < 1700000000) return 1;
  struct timespec coarse;
  if (clock_gettime(CLOCK_REALTIME_COARSE, &coarse) ||
      coarse.tv_sec < t.tv_sec || coarse.tv_nsec < 0 ||
      coarse.tv_nsec >= 1000000000 || coarse.tv_nsec % 1000000) return 1;
  uint64_t sleep_begin = milliseconds();
  if (usleep(20001) || milliseconds() < sleep_begin + 21 || usleep(0)) return 1;
  struct timeval wall;
  if (gettimeofday(&wall, NULL) || wall.tv_sec < t.tv_sec ||
      wall.tv_usec < 0 || wall.tv_usec >= 1000000) return 1;
  struct timezone zone = {123, 456};
  if (gettimeofday(&wall, &zone) || zone.tz_minuteswest || zone.tz_dsttime)
    return 1;
  struct timespec boot, mono;
  if (clock_gettime(CLOCK_MONOTONIC, &mono) ||
      clock_gettime(CLOCK_BOOTTIME, &boot) ||
      boot.tv_sec * 1000000000 + boot.tv_nsec <
          mono.tv_sec * 1000000000 + mono.tv_nsec) return 1;
  struct timespec pause = {0, 20000000}, after;
  if (nanosleep(&pause, NULL) || clock_gettime(CLOCK_BOOTTIME, &after) ||
      after.tv_sec * 1000000000 + after.tv_nsec <
          boot.tv_sec * 1000000000 + boot.tv_nsec + 20000000) return 1;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &after) ||
      after.tv_sec * 1000000000 + after.tv_nsec <
          boot.tv_sec * 1000000000 + boot.tv_nsec) return 1;
  if (clock_gettime(CLOCK_MONOTONIC_COARSE, &after) ||
      after.tv_sec * 1000000000 + after.tv_nsec <
          boot.tv_sec * 1000000000 + boot.tv_nsec) return 1;
  time_t epoch = -1;
  struct tm date;
  if (!gmtime_r(&epoch, &date) || date.tm_year != 69 || date.tm_mon != 11 ||
      date.tm_mday != 31 || date.tm_sec != 59 || timegm(&date) != epoch)
    return 1;
  epoch = 951782400; /* 2000-02-29 UTC. */
  if (!gmtime_r(&epoch, &date) || date.tm_year != 100 || date.tm_mon != 1 ||
      date.tm_mday != 29 || date.tm_yday != 59 || date.tm_wday != 2 ||
      timegm(&date) != epoch) return 1;
  char formatted[80];
  const char *expected = "2000-02-29 00:00:00 Tue 060 2000-W09 +0000 UTC";
  if (strftime(formatted, sizeof(formatted), "%F %T %a %j %G-W%V %z %Z", &date) != strlen(expected) ||
      strcmp(formatted, expected)) return 1;
  if (strftime(formatted, 4, "%Y", &date) != 0) return 1;
  time_t wall_seconds;
  time_t time_result = time(&wall_seconds);
  if (time_result != wall_seconds || wall_seconds < t.tv_sec ||
      !localtime_r(&epoch, &date) || date.tm_hour || date.tm_gmtoff ||
      date.tm_isdst || mktime(&date) != epoch) return 1;
  date.tm_mday = 30;
  if (mktime(&date) != epoch + 86400 || date.tm_mon != 2 ||
      date.tm_mday != 1 || date.tm_yday != 60) return 1;
  struct timespec cpu_begin, cpu_after;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_begin)) return 1;
  uint64_t wall_begin = milliseconds();
  for (;;) {
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_after)) return 1;
    int64_t cpu_elapsed = (cpu_after.tv_sec - cpu_begin.tv_sec) * 1000000000 +
                           cpu_after.tv_nsec - cpu_begin.tv_nsec;
    uint64_t wall_elapsed = milliseconds() - wall_begin;
    if (cpu_elapsed < 0 || cpu_elapsed > (int64_t)(wall_elapsed + 2) * 1000000 ||
        wall_elapsed > 5000) return 1;
    if (cpu_elapsed >= 30000000) break;
  }
  cpu_begin = cpu_after;
  pause.tv_nsec = 100000000;
  if (nanosleep(&pause, NULL) ||
      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_after) ||
      (cpu_after.tv_sec - cpu_begin.tv_sec) * 1000000000 +
          cpu_after.tv_nsec - cpu_begin.tv_nsec > 10000000) return 1;
  uint64_t previous = milliseconds();
  for (int i = 0; i < 1000; i++) {
    uint64_t now = milliseconds();
    if (now < previous) return 1;
    previous = now;
  }
  if (!launch(worker, 1) || !launch(worker, 10) || !launch(worker, 100) ||
      !await_workers(3)) { puts("time-test: FAIL"); return 1; }
  uint32_t word = 0;
  uint64_t begin = milliseconds();
  if (futex_wait(&word, 0, 20) != -1 || errno != ETIMEDOUT ||
      milliseconds() < begin + 20) return 1;
  if (futex_wait(&word, 1, 20) != -1 || errno != EAGAIN ||
      futex_wake(&word, 1) != 0) return 1;
  struct timespec invalid = {0, 1000000000};
  if (nanosleep(&invalid, NULL) != -1 || errno != EINVAL) return 1;
  puts("time-test: PASS (blocking sleeps, monotonic, realtime, futex timeout)");
  return 0;
}
