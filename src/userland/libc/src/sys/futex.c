#include <errno.h>
#include <sys/futex.h>
#include <sys/syscall.h>
int futex_wait(uint32_t *address, uint32_t expected, uint32_t timeout_ms) {
  long result = syscall3(SYS_FUTEX_WAIT, (long)address, expected, timeout_ms);
  if (result < 0) { errno = (int)-result; return -1; }
  return (int)result;
}
int futex_wake(uint32_t *address, int count) {
  long result = syscall2(SYS_FUTEX_WAKE, (long)address, count);
  if (result < 0) { errno = (int)-result; return -1; }
  return (int)result;
}
