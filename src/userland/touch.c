/* src/userland/touch.c - Crée un fichier vide s'il n'existe pas déjà (port
 * userland de l'ancienne commande kernel `touch`, cf.
 * src/shell/commands.c:cmd_touch). */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: touch <filename>\n");
    return 1;
  }

  /* Le fichier existe déjà : comportement touch Unix, on ne fait rien. */
  int fd = open(argv[1], O_RDONLY);
  if (fd >= 0) {
    close(fd);
    return 0;
  }

  if (creat(argv[1], 0644) != 0) {
    printf("touch: cannot create file '%s'\n", argv[1]);
    return 1;
  }

  return 0;
}
