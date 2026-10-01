/* src/userland/libc/src/unistd/unistd.c - Standard Unix functions */
#include "internal/syscall.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/alos_system.h>
#include <sys/meminfo.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

int unlink(const char *pathname) {
  return syscall3(SYS_UNLINK, (long)pathname, 0, 0);
}

int getpid(void) { return syscall0(SYS_GETPID); }

pid_t fork(void) { return (pid_t)syscall0(SYS_FORK); }

int execve(const char *pathname, char *const argv[], char *const envp[]) {
  return (int)syscall3(SYS_EXECVE, (long)pathname, (long)argv, (long)envp);
}

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
  return (int)syscall3(SYS_SPAWN_WAIT, (long)path, (long)argc, (long)argv);
}

int rmdir(const char *pathname) {
  return syscall3(SYS_RMDIR, (long)pathname, 0, 0);
}


int mkdir(const char *pathname, ...) {
  return (int)syscall3(SYS_MKDIR, (long)pathname, 0, 0);
}

int creat(const char *pathname, int mode) {
  (void)mode;
  return (int)syscall3(SYS_CREATE, (long)pathname, 0, 0);
}

int alos_readdir(const char *path, unsigned int index, struct alos_dirent *entry) {
  return (int)syscall3(SYS_READDIR, (long)path, (long)index, (long)entry);
}

int meminfo(struct meminfo *info) {
  return (int)syscall3(SYS_MEMINFO, (long)info, 0, 0);
}

void _exit(int status) {
  syscall3(SYS_EXIT, status, 0, 0);
  __builtin_unreachable();
}

long sysconf(int name) {
  if (name == _SC_PAGESIZE) return 4096;
  /* Le noyau configure exactement un CPU ; le SMP n'est pas implemente. */
  if (name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) return 1;
  if (name == _SC_PHYS_PAGES) {
    /* Capacite RAM utilisable geree par le PMM au boot, pas l'adresse max. */
    alos_system_info_t info;
    if (alos_system_info(&info) < 0) return -1;
    return (long)(info.managed_total_bytes / 4096);
  }
  errno = EINVAL;
  return -1;
}

int getpagesize(void) { return (int)sysconf(_SC_PAGESIZE); }

int isatty(int fd) {
  long result = syscall1(SYS_ISATTY, fd);
  if (result < 0) { errno = (int)-result; return 0; }
  return (int)result;
}
