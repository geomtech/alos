#include <errno.h>
#include <sys/alos_process.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/syscall.h>

static int checked(long value) {
  if (value < 0) { errno = (int)-value; return -1; }
  return (int)value;
}
int statvfs(const char *path, struct statvfs *information) {
  return checked(syscall2(SYS_STATVFS, (long)path, (long)information));
}
int futimes(int fd, const struct timeval times[2]) {
  typedef char timeval_abi_check[sizeof(struct timeval) == 16 ? 1 : -1];
  (void)sizeof(timeval_abi_check);
  return checked(syscall2(SYS_FUTIMES, fd, (long)times));
}
int alos_set_process_title(const char *title) {
  return checked(syscall1(SYS_SET_PROCESS_TITLE, (long)title));
}
