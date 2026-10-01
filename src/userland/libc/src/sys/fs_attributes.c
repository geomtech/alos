#include <errno.h>
#include <fcntl.h>
#include <sys/alos_process.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <unistd.h>

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
int utimes(const char *path, const struct timeval times[2]) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  int result = futimes(fd, times);
  int saved_errno = errno;
  if (close(fd) < 0 && result == 0) {
    result = -1;
    saved_errno = errno;
  }
  errno = saved_errno;
  return result;
}
static int timespec_to_timeval(const struct timespec input[2],
                               struct timeval output[2]) {
  if (!input) return 0;
  int now_count = 0;
  for (int i = 0; i < 2; ++i) {
    if (input[i].tv_nsec == UTIME_OMIT) { errno = ENOTSUP; return -1; }
    if (input[i].tv_nsec == UTIME_NOW) { ++now_count; continue; }
    if (input[i].tv_nsec < 0 || input[i].tv_nsec > 999999999) {
      errno = EINVAL;
      return -1;
    }
    output[i].tv_sec = input[i].tv_sec;
    output[i].tv_usec = input[i].tv_nsec / 1000;
  }
  if (now_count == 2) return 0;
  if (now_count) { errno = ENOTSUP; return -1; }
  return 1;
}
int futimens(int fd, const struct timespec times[2]) {
  struct timeval converted[2];
  int mode = timespec_to_timeval(times, converted);
  if (mode < 0) return -1;
  return futimes(fd, mode ? converted : NULL);
}
int utimensat(int dirfd, const char *path, const struct timespec times[2],
              int flags) {
  if (flags & ~AT_SYMLINK_NOFOLLOW) { errno = EINVAL; return -1; }
  if (flags) { errno = ENOTSUP; return -1; }
  int fd = openat(dirfd, path, O_RDONLY);
  if (fd < 0) return -1;
  int result = futimens(fd, times);
  int saved_errno = errno;
  if (close(fd) < 0 && result == 0) {
    result = -1;
    saved_errno = errno;
  }
  errno = saved_errno;
  return result;
}
int alos_set_process_title(const char *title) {
  return checked(syscall1(SYS_SET_PROCESS_TITLE, (long)title));
}
