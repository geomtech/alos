/* src/userland/rm.c - Supprime un fichier (port userland de l'ancienne
 * commande kernel `rm`, cf. src/shell/commands.c:cmd_rm). */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: rm <file>\n");
    return 1;
  }

  if (unlink(argv[1]) != 0) {
    printf("rm: failed to remove '%s'\n", argv[1]);
    return 1;
  }

  return 0;
}
