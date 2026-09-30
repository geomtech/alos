#include <sched.h>
#include <sys/syscall.h>
#include <errno.h>
int sched_yield(void) {
  long result = syscall1(SYS_NANOSLEEP, 0);
  if (result < 0) { errno = (int)-result; return -1; }
  return (int)result;
}
