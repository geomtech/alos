/* src/userland/libc/src/unistd/unistd.c - Standard Unix functions */
#include "internal/syscall.h"
#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <unistd.h>

int open(const char *pathname, int flags, ...) {
  long mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = (long)va_arg(ap, int);
    va_end(ap);
  }
  return (int)syscall3(SYS_OPEN, (long)pathname, (long)flags, mode);
}

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

char *getcwd(char *buf, size_t size) {
  if (syscall2(SYS_GETCWD, (long)buf, (long)size) != 0) {
    return NULL;
  }
  return buf;
}

int mkdir(const char *pathname) {
  return syscall3(SYS_MKDIR, (long)pathname, 0, 0);
}

int rmdir(const char *pathname) {
  return syscall3(SYS_RMDIR, (long)pathname, 0, 0);
}

int spawn_wait(const char *path, int argc, char **argv) {
  return (int)syscall3(SYS_SPAWN_WAIT, (long)path, (long)argc, (long)argv);
}

void _exit(int status) {
  syscall3(SYS_EXIT, status, 0, 0);
  __builtin_unreachable();
}
