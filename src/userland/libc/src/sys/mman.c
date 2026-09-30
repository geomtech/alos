#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/vminfo.h>
#include <unistd.h>

int *__errno_location(void) {
  long address = syscall0(SYS_ERRNO_LOCATION);
  if (address < 0) {
    static const char message[] = "libc: unable to allocate thread errno\n";
    write(2, message, sizeof(message) - 1);
    _exit(134);
  }
  return (int *)address;
}

static long memory_result(long result) {
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  return result;
}

void *mmap(void *address, size_t length, int prot, int flags, int fd,
            off_t offset) {
  return (void *)memory_result(syscall6(SYS_MMAP, (long)address, length,
                                        prot, flags, fd, offset));
}

int munmap(void *address, size_t length) {
  return (int)memory_result(syscall2(SYS_MUNMAP, (long)address, length));
}

int mprotect(void *address, size_t length, int prot) {
  return (int)memory_result(syscall3(SYS_MPROTECT, (long)address, length, prot));
}

int madvise(void *address, size_t length, int advice) {
  return (int)memory_result(syscall3(SYS_MADVISE, (long)address, length, advice));
}

int mincore(void *address, size_t length, unsigned char *vector) {
  return (int)memory_result(syscall3(SYS_MINCORE, (long)address, length,
                                     (long)vector));
}

int msync(void *address, size_t length, int flags) {
  return (int)memory_result(syscall3(SYS_MSYNC, (long)address, length, flags));
}

int vminfo(vm_info_t *info) {
  return (int)memory_result(syscall1(SYS_VMINFO, (long)info));
}
