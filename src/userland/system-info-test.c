#include <errno.h>
#include <stdio.h>
#include <sys/alos_system.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#define CHECK(x) do { if (!(x)) { printf("system-info-test: FAIL line=%d\n", __LINE__); return 1; } } while (0)

int main(void) {
  alos_system_info_t before, during, after;
  CHECK(!alos_system_info(&before));
  CHECK(before.version == ALOS_SYSTEM_INFO_VERSION &&
        before.struct_size == sizeof(before) &&
        before.valid_fields == ALOS_SYSTEM_INFO_MEMORY_FIELDS);
  CHECK(before.managed_total_bytes > 0 &&
        before.allocatable_free_bytes <= before.managed_total_bytes &&
        !(before.managed_total_bytes % 4096) &&
        !(before.allocatable_free_bytes % 4096) &&
        !before.swap_total_bytes && !before.swap_free_bytes);
  CHECK(alos_system_info(NULL) == -1 && errno == EFAULT);
  CHECK(syscall3(SYS_SYSTEM_INFO, (long)&after, sizeof(after) - 1,
                 ALOS_SYSTEM_INFO_VERSION) == -EINVAL);
  CHECK(syscall3(SYS_SYSTEM_INFO, (long)&after, sizeof(after),
                 ALOS_SYSTEM_INFO_VERSION + 1) == -EINVAL);
  volatile unsigned char *pages = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(pages != MAP_FAILED);
  pages[0] = 1;
  pages[4096] = 2;
  CHECK(!alos_system_info(&during));
  CHECK(during.managed_total_bytes == before.managed_total_bytes &&
        during.allocatable_free_bytes <= before.allocatable_free_bytes &&
        before.allocatable_free_bytes - during.allocatable_free_bytes >= 8192);
  CHECK(!munmap((void *)pages, 8192));
  CHECK(!alos_system_info(&after));
  CHECK(after.managed_total_bytes == before.managed_total_bytes &&
        after.allocatable_free_bytes <= after.managed_total_bytes &&
        after.allocatable_free_bytes >= during.allocatable_free_bytes + 8192);
  puts("system-info-test: PASS (managed RAM, free pages, no swap, checked ABI)");
  return 0;
}
