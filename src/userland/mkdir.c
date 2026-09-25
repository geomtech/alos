/* src/userland/mkdir.c - Crée un répertoire (port userland de l'ancienne
 * commande kernel `mkdir`, cf. src/shell/commands.c:cmd_mkdir). */
#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: mkdir <dirname>\n");
    return 1;
  }

  if (mkdir(argv[1], 0755) != 0) {
    printf("mkdir: cannot create directory '%s'\n", argv[1]);
    return 1;
  }

  return 0;
}
