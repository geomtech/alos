/* src/userland/meminfo.c - Affiche les informations mémoire du tas noyau
 * (port userland de l'ancienne commande kernel `meminfo`, cf.
 * src/shell/commands.c:cmd_meminfo). */
#include <stdio.h>
#include <sys/meminfo.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  struct meminfo info;
  if (meminfo(&info) != 0) {
    printf("meminfo: unable to read memory information\n");
    return 1;
  }

  unsigned int used = info.total_size - info.free_size;
  unsigned int used_blocks = info.block_count - info.free_block_count;

  printf("\n============================================\n");
  printf("         ALOS Memory Information           \n");
  printf("============================================\n\n");

  printf("  Heap Total Size:    %u KB (%u bytes)\n", info.total_size / 1024,
         info.total_size);
  printf("  Heap Free Size:     %u KB (%u bytes)\n", info.free_size / 1024,
         info.free_size);
  printf("  Heap Used Size:     %u KB (%u bytes)\n", used / 1024, used);

  printf("\n");
  printf("  Total Blocks:       %u\n", info.block_count);
  printf("  Free Blocks:        %u\n", info.free_block_count);
  printf("  Used Blocks:        %u\n", used_blocks);

  if (info.total_size > 0) {
    unsigned int percent_used = (used * 100) / info.total_size;
    printf("\n  Memory Usage:       %u%%\n", percent_used);
  }

  printf("\n============================================\n");

  return 0;
}
