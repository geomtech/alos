#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../../../../include/path_abi.h"

static long checked(long result) {
  if (result < 0) { errno = (int)-result; return -1; }
  return result;
}
int openat(int dirfd, const char* path, int flags, ...) {
  unsigned mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, unsigned);
    va_end(args);
  }
  return (int)checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_OPEN, dirfd,
                               (long)path, flags, mode, 0));
}
int fstatat(int dirfd, const char* path, struct stat* output, int flags) {
  return (int)checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_STAT, dirfd,
                               (long)path, flags, (long)output, 0));
}
int unlinkat(int dirfd, const char* path, int flags) {
  return (int)checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_UNLINK, dirfd,
                               (long)path, flags, 0, 0));
}
char* realpath(const char* path, char* resolved) {
  char* output = resolved ? resolved : malloc(PATH_MAX);
  if (!output) { errno = ENOMEM; return NULL; }
  if (checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_REALPATH, AT_FDCWD,
                        (long)path, 0, (long)output, PATH_MAX)) < 0) {
    int error = errno;
    if (!resolved) free(output);
    errno = error;
    return NULL;
  }
  return output;
}
int rename(const char* old_path, const char* new_path) {
  return (int)checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_RENAME, AT_FDCWD,
                               (long)old_path, 0, (long)new_path, 0));
}
int chmod(const char* path, mode_t mode) {
  return (int)checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_CHMOD, AT_FDCWD,
                               (long)path, mode, 0, 0));
}
int symlink(const char* target, const char* path) {
  return (int)checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_SYMLINK, AT_FDCWD,
                               (long)path, 0, (long)target, 0));
}
ssize_t readlink(const char* path, char* buffer, size_t size) {
  return checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_READLINK, AT_FDCWD,
                           (long)path, 0, (long)buffer, size));
}
long pathconf(const char* path, int name) {
  if (name != _PC_NAME_MAX) { errno = EINVAL; return -1; }
  return checked(syscall6(SYS_NATIVE_PATH, ALOS_PATH_NAME_MAX, AT_FDCWD,
                           (long)path, 0, 0, 0));
}
