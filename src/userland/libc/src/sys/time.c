#include <errno.h>
#include <sys/syscall.h>
#include <time.h>

static int time_result(long result) {
  if (result < 0) { errno = (int)-result; return -1; }
  return (int)result;
}
int clock_gettime(clockid_t clock, struct timespec *time) {
  return time_result(syscall2(SYS_CLOCK_GETTIME, clock, (long)time));
}
int nanosleep(const struct timespec *request, struct timespec *remaining) {
  return time_result(syscall2(SYS_NANOSLEEP_POSIX, (long)request, (long)remaining));
}
