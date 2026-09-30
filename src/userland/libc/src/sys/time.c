#include <errno.h>
#include <sys/syscall.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

static int time_result(long result) {
  if (result < 0) { errno = (int)-result; return -1; }
  return (int)result;
}
int clock_gettime(clockid_t clock, struct timespec *time) {
  return time_result(syscall2(SYS_CLOCK_GETTIME, clock, (long)time));
}
int gettimeofday(struct timeval *value, void *timezone) {
  if (!value) { errno = EINVAL; return -1; }
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now)) return -1;
  value->tv_sec = now.tv_sec;
  value->tv_usec = now.tv_nsec / 1000;
  /* Calendrier ALOS exclusivement UTC, sans regles de changement d'heure. */
  if (timezone) *(struct timezone *)timezone = (struct timezone){0, 0};
  return 0;
}
int nanosleep(const struct timespec *request, struct timespec *remaining) {
  return time_result(syscall2(SYS_NANOSLEEP_POSIX, (long)request, (long)remaining));
}
int usleep(useconds_t microseconds) {
  struct timespec delay = {
    microseconds / 1000000U, (microseconds % 1000000U) * 1000L
  };
  return nanosleep(&delay, NULL);
}
