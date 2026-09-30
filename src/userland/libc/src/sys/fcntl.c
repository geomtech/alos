#include "internal/syscall.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>

int fcntl(int fd, int command, ...) {
  int argument = 0;
  switch (command) {
  case F_DUPFD:
  case F_DUPFD_CLOEXEC:
  case F_SETFD:
  case F_SETFL: {
    va_list args;
    va_start(args, command);
    argument = va_arg(args, int);
    va_end(args);
    break;
  }
  case F_GETFD:
  case F_GETFL:
  case F_SETLK:
  case F_GETLK:
  case F_SETLKW:
    /* Verifier le FD dans le noyau, sans simuler un verrou accorde. */
    break;
  default:
    errno = EINVAL;
    return -1;
  }
  long result = syscall3(SYS_FCNTL, fd, command, argument);
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  return (int)result;
}
