#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/vminfo.h>

#define PAGE 4096
static int failures;
#define CHECK(condition) do { if (!(condition)) { \
  printf("mincore-test: FAIL line %d errno=%d\n", __LINE__, errno); ++failures; \
} } while (0)

int main(void) {
  unsigned char vector[4] = {0xcc, 0xcc, 0xcc, 0xcc};
  unsigned char *memory = mmap(NULL, 3 * PAGE, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(memory != MAP_FAILED);
  if (memory == MAP_FAILED) return 1;
  errno = EDOM;
  vm_info_t before, after;
  CHECK(vminfo(&before) == 0);
  CHECK(mincore(memory, 3 * PAGE, vector) == 0 && errno == EDOM);
  CHECK(vector[0] == 0 && vector[1] == 0 && vector[2] == 0 && vector[3] == 0xcc);
  CHECK(vminfo(&after) == 0 && before.resident_size == after.resident_size);
  CHECK(before.physical_free == after.physical_free);
  memory[0] = 17;
  memory[2 * PAGE] = 33;
  CHECK(mincore(memory, 2 * PAGE + 1, vector) == 0);
  CHECK(vector[0] == 1 && vector[1] == 0 && vector[2] == 1 && vector[3] == 0xcc);
  CHECK(mprotect(memory, 3 * PAGE, PROT_NONE) == 0);
  CHECK(mincore(memory, 3 * PAGE, vector) == 0);
  CHECK(vector[0] == 1 && vector[1] == 0 && vector[2] == 1);
  CHECK(mprotect(memory, 3 * PAGE, PROT_READ | PROT_WRITE) == 0);
  CHECK(madvise(memory, 3 * PAGE, MADV_DONTNEED) == 0);
  CHECK(mincore(memory, 3 * PAGE, vector) == 0);
  CHECK(vector[0] == 0 && vector[1] == 0 && vector[2] == 0);
  memory[0] = 9;
  CHECK(mprotect(memory, PAGE, PROT_READ) == 0);
  CHECK(mincore(memory, 1, vector) == 0 && vector[0] == 1);
  CHECK(mincore(memory, 0, NULL) == 0);
  errno = 0;
  CHECK(mincore(memory + 1, PAGE, vector) == -1 && errno == EINVAL);
  CHECK(mincore(memory + 1, 0, NULL) == -1 && errno == EINVAL);
  errno = 0;
  CHECK(mincore(memory, (size_t)-1, vector) == -1 && errno == ENOMEM);
  CHECK(mincore((void *)(uintptr_t)0xfffffffffffff000ULL, PAGE, vector) == -1 &&
        errno == ENOMEM);
  errno = 0;
  CHECK(mincore(memory, PAGE, (unsigned char *)(uintptr_t)1) == -1 && errno == EFAULT);
  CHECK(mincore(memory, PAGE, memory) == -1 && errno == EFAULT);
  unsigned char *blocked = mmap(NULL, PAGE, PROT_NONE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(blocked != MAP_FAILED);
  if (blocked != MAP_FAILED) {
    errno = 0;
    CHECK(mincore(memory, PAGE, blocked) == -1 && errno == EFAULT);
    CHECK(mincore(blocked, PAGE, vector) == 0 && vector[0] == 0);
    CHECK(munmap(blocked, PAGE) == 0);
  }
  CHECK(munmap(memory + PAGE, PAGE) == 0);
  memset(vector, 0xcc, sizeof(vector));
  errno = 0;
  CHECK(mincore(memory, 3 * PAGE, vector) == -1 && errno == ENOMEM);
  CHECK(vector[0] == 0xcc && vector[1] == 0xcc && vector[2] == 0xcc);
  CHECK(munmap(memory, PAGE) == 0 && munmap(memory + 2 * PAGE, PAGE) == 0);

  /* Hors de l'arene mmap : PTE reelles de la stack, du heap et de l'ELF. */
  uintptr_t stack_page = (uintptr_t)vector & ~(uintptr_t)(PAGE - 1);
  CHECK(mincore((void *)stack_page, PAGE, vector) == 0 && vector[0] == 1);
  uintptr_t text_page = (uintptr_t)main & ~(uintptr_t)(PAGE - 1);
  CHECK(mincore((void *)text_page, PAGE, vector) == 0 && vector[0] == 1);
  unsigned char *heap = malloc(PAGE);
  CHECK(heap != NULL);
  if (heap) {
    heap[0] = 1;
    CHECK(mincore((void *)((uintptr_t)heap & ~(uintptr_t)(PAGE - 1)), PAGE, vector) == 0 &&
          vector[0] == 1);
    free(heap);
  }
  unsigned char large_vector[258];
  memset(large_vector, 0xcc, sizeof(large_vector));
  memory = mmap(NULL, 257 * PAGE, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(memory != MAP_FAILED);
  if (memory != MAP_FAILED) {
    memory[256 * PAGE] = 1;
    CHECK(mincore(memory, 257 * PAGE, large_vector) == 0);
    for (size_t i = 0; i < 256; ++i) CHECK(large_vector[i] == 0);
    CHECK(large_vector[256] == 1 && large_vector[257] == 0xcc);
    CHECK(munmap(memory, 257 * PAGE) == 0);
  }
  if (failures) { printf("mincore-test: FAIL %d\n", failures); return 1; }
  puts("mincore-test: PASS");
  return 0;
}
