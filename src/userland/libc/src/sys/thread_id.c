#include <errno.h>
#include <limits.h>
#include <sys/syscall.h>
#include <unistd.h>

pid_t gettid(void) {
  long result = syscall0(SYS_GETTID);
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  if (result == 0) {
    errno = ESRCH;
    return -1;
  }
  if (result > INT_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  return (pid_t)result;
}
