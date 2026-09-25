/* src/userland/cmd/meminfo.c - Display memory information */
#include <stdio.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  meminfo_t info;
  long ret = syscall1(SYS_MEMINFO, (long)&info);
  if (ret != 0) {
    printf("Error: failed to retrieve memory information\n");
    return 1;
  }

  uint32_t used_size = info.total_size - info.free_size;

  printf("\n=== Memory Information ===\n\n");
  printf("  Total Kernel Heap:  %u KB (%u MB)\n", info.total_size / 1024, info.total_size / (1024 * 1024));
  printf("  Used Memory:        %u KB (%u MB)\n", used_size / 1024, used_size / (1024 * 1024));
  printf("  Free Memory:        %u KB (%u MB)\n", info.free_size / 1024, info.free_size / (1024 * 1024));
  printf("  Allocated Blocks:   %u\n", info.block_count - info.free_block_count);
  printf("  Free Blocks:        %u\n\n", info.free_block_count);

  return 0;
}
