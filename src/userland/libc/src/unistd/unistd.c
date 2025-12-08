/* src/userland/libc/src/unistd/unistd.c - Standard Unix functions */
#include "internal/syscall.h"
#include <sys/syscall.h>
#include <unistd.h>

ssize_t read(int fd, void *buf, size_t count) {
  return syscall3(SYS_READ, fd, (long)buf, (long)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

int close(int fd) { return syscall3(SYS_CLOSE, fd, 0, 0); }

int unlink(const char *pathname) {
  return syscall3(SYS_UNLINK, (long)pathname, 0, 0);
}

off_t lseek(int fd, off_t offset, int whence) {
  return syscall3(SYS_LSEEK, fd, (long)offset, whence);
}

int getpid(void) { return syscall0(SYS_GETPID); }

int getuid(void) { return syscall0(SYS_GETUID); }

unsigned int sleep(unsigned int seconds) {
  return syscall3(SYS_SLEEP, seconds * 1000, 0, 0);
}

int chdir(const char *path) { return syscall3(SYS_CHDIR, (long)path, 0, 0); }

int rmdir(const char *pathname) {
  return syscall3(SYS_RMDIR, (long)pathname, 0, 0);
}

void _exit(int status) {
  syscall3(SYS_EXIT, status, 0, 0);
  __builtin_unreachable();
}
