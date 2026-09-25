/* src/userland/rmdir.c - Supprime un répertoire vide (port userland de
 * l'ancienne commande kernel `rmdir`, cf. src/shell/commands.c:cmd_rmdir). */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: rmdir <directory>\n");
    return 1;
  }

  if (rmdir(argv[1]) != 0) {
    printf("rmdir: failed to remove '%s' (directory not empty?)\n", argv[1]);
    return 1;
  }

  return 0;
}
