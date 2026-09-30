#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>

static int stat_result(long result) {
  if (result < 0) { errno = (int)-result; return -1; }
  return (int)result;
}

int stat(const char *path, struct stat *metadata) {
  return stat_result(syscall3(SYS_STAT, (long)path, (long)metadata, 0));
}

int lstat(const char *path, struct stat *metadata) {
  return stat_result(syscall3(SYS_STAT, (long)path, (long)metadata,
                              ALOS_STAT_NOFOLLOW));
}

int fstat(int fd, struct stat *metadata) {
  return stat_result(syscall2(SYS_FSTAT, fd, (long)metadata));
}
