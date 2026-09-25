#include "internal/syscall.h"
#include <sys/syscall.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *status, int options) {
  return (pid_t)syscall3(SYS_WAITPID, pid, (long)status, options);
}
