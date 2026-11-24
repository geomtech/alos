#include <string.h>
#include <unistd.h>
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

int gethostname(char *name, size_t length) {
  struct utsname info;
  if (uname(&info) != 0) return -1;
  size_t size = strlen(info.nodename);
  if (size >= length) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memcpy(name, info.nodename, size + 1);
  return 0;
}
