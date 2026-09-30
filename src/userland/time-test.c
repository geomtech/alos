#include "runtime-test.h"

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
