#include "internal/syscall.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Definitions centrales ajoutees par le coordinateur ABI. */

static long fd_result(long result) {
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  return result;
}

int open(const char *pathname, int flags, ...) {
  long mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = (long)va_arg(args, int);
    va_end(args);
  }
  return (int)fd_result(syscall3(SYS_OPEN, (long)pathname, flags, mode));
}

ssize_t read(int fd, void *buffer, size_t count) {
  return fd_result(syscall3(SYS_READ, fd, (long)buffer, (long)count));
}

ssize_t write(int fd, const void *buffer, size_t count) {
  return fd_result(syscall3(SYS_WRITE, fd, (long)buffer, (long)count));
}

ssize_t pread(int fd, void *buffer, size_t count, off_t offset) {
  return fd_result(syscall4(SYS_PREAD, fd, (long)buffer, count, offset));
}

ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset) {
  return fd_result(syscall4(SYS_PWRITE, fd, (long)buffer, count, offset));
}

int ftruncate(int fd, off_t length) {
  return (int)fd_result(syscall2(SYS_FTRUNCATE, fd, length));
}

int ftruncate64(int fd, int64_t length) {
  return ftruncate(fd, (off_t)length);
}

int close(int fd) {
  return (int)fd_result(syscall1(SYS_CLOSE, fd));
}

off_t lseek(int fd, off_t offset, int whence) {
  return fd_result(syscall3(SYS_LSEEK, fd, (long)offset, whence));
}

int dup(int fd) {
  return (int)fd_result(syscall1(SYS_DUP, fd));
}

int dup2(int fd, int target) {
  return (int)fd_result(syscall2(SYS_DUP2, fd, target));
}
