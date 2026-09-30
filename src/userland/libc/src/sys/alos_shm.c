#include <sys/alos_shm.h>
#include <sys/syscall.h>
#include <errno.h>

static long result(long value) {
  if (value < 0) {
    errno = (int)-value;
    return -1;
  }
  return value;
}

int alos_shm_create(size_t size) {
  return (int)result(syscall1(SYS_SHM_CREATE_NATIVE, (long)size));
}

int alos_shm_readonly(int fd) {
  return (int)result(syscall1(SYS_SHM_READONLY, fd));
}

long alos_shm_size(int fd) {
  return result(syscall2(SYS_SHM_INFO, fd, 0));
}

int alos_shm_access(int fd) {
  return (int)result(syscall2(SYS_SHM_INFO, fd, 1));
}

int alos_shm_same(int fd, int other_fd) {
  return (int)result(syscall2(SYS_SHM_SAME, fd, other_fd));
}
