#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/alos_shm.h>
#include <sys/mman.h>
#include <sys/vminfo.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
  printf("msync-test: FAIL line=%d errno=%d\n", __LINE__, errno); return 1; \
} } while (0)

int main(void) {
  uintptr_t text_page = (uintptr_t)main & ~(uintptr_t)4095;
  CHECK(msync((void *)text_page, 4096, MS_SYNC) == -1 && errno == ENOTSUP);
  unsigned char *private = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(private != MAP_FAILED);
  vm_info_t before, after;
  CHECK(vminfo(&before) == 0);
  CHECK(msync(private, 8192, MS_SYNC) == 0 && vminfo(&after) == 0 &&
        after.physical_free == before.physical_free &&
        after.resident_size == before.resident_size);
  private[0] = 0x5a;
  CHECK(msync(private, 1, MS_ASYNC | MS_INVALIDATE) == 0 && private[0] == 0x5a);
  CHECK(msync(private + 1, 1, MS_SYNC) == -1 && errno == EINVAL);
  CHECK(msync(private, 1, MS_SYNC | MS_ASYNC) == -1 && errno == EINVAL);
  CHECK(msync(private, 1, 0x80000000U) == -1 && errno == EINVAL);
  CHECK(msync(private, 1, 0) == -1 && errno == EINVAL);
  CHECK(msync(private, 0, MS_SYNC) == 0);
  CHECK(munmap(private + 4096, 4096) == 0);
  CHECK(msync(private, 8192, MS_SYNC) == -1 && errno == ENOMEM);
  CHECK(msync(private, SIZE_MAX, MS_SYNC) == -1 && errno == ENOMEM);
  CHECK(private[0] == 0x5a && munmap(private, 4096) == 0);

  int fd = alos_shm_create(4096);
  int ro = alos_shm_readonly(fd);
  CHECK(fd >= 0 && ro >= 0);
  unsigned char *a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  unsigned char *b = mmap(NULL, 4096, PROT_READ, MAP_SHARED, ro, 0);
  CHECK(a != MAP_FAILED && b != MAP_FAILED);
  a[0] = 73;
  CHECK(msync(a, 4096, MS_SYNC | MS_INVALIDATE) == 0 &&
        msync(b, 4096, MS_ASYNC) == 0 && b[0] == 73);
  CHECK(mprotect(b, 4096, PROT_READ | PROT_WRITE) == -1 && errno == EACCES);
  CHECK(close(fd) == 0 && close(ro) == 0);
  CHECK(munmap(a, 4096) == 0 && munmap(b, 4096) == 0);

  fd = open("/posix-test/msync-private", O_CREAT | O_EXCL | O_RDWR, 0600);
  CHECK(fd >= 0);
  unsigned char page[4096];
  memset(page, 0x31, sizeof(page));
  CHECK(write(fd, page, sizeof(page)) == sizeof(page));
  a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  CHECK(a != MAP_FAILED);
  a[0] = 0x99;
  CHECK(msync(a, 4096, MS_SYNC) == 0);
  unsigned char byte;
  CHECK(pread(fd, &byte, 1, 0) == 1 && byte == 0x31);
  CHECK(msync(a, 4096, MS_SYNC | MS_INVALIDATE) == -1 && errno == ENOTSUP);
  CHECK(mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0) == MAP_FAILED &&
        errno == ENOTSUP);
  CHECK(munmap(a, 4096) == 0 && close(fd) == 0 &&
        unlink("/posix-test/msync-private") == 0);
  puts("msync-test: PASS (coherent memory; no shared-file writeback)");
  return 0;
}
