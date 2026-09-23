/* src/userland/libc/src/unistd/unistd.c - Standard Unix functions */
#include "internal/syscall.h"
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

/**
 * open() - Ouvre un fichier via SYS_OPEN.
 *
 * Le troisième argument (mode) n'est utilisé que pour O_CREAT et n'est pas
 * supporté par le kernel pour l'instant : il est accepté pour compatibilité
 * avec la signature standard mais ignoré.
 */
int open(const char *pathname, int flags, ...) {
  return (int)syscall3(SYS_OPEN, (long)pathname, (long)flags, 0);
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
  if (buf == NULL || size == 0)
    return (char *)0;
  if (syscall3(SYS_GETCWD, (long)buf, (long)size, 0) != 0)
    return (char *)0;
  return buf;
}

int spawn_wait(const char *path, int argc, char **argv) {
  return syscall3(SYS_SPAWN_WAIT, (long)path, (long)argc, (long)argv);
}

int rmdir(const char *pathname) {
  return syscall3(SYS_RMDIR, (long)pathname, 0, 0);
}

void _exit(int status) {
  syscall3(SYS_EXIT, status, 0, 0);
  __builtin_unreachable();
}
