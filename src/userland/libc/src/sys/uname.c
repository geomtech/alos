#include <errno.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

int uname(struct utsname *name) {
  long result = syscall1(ALOS_SYS_UNAME, (long)name);
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  return 0;
}
