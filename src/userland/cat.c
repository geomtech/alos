/* src/userland/cat.c - Affiche le contenu d'un fichier (port userland de
 * l'ancienne commande kernel `cat`, cf. src/shell/commands.c:cmd_cat). */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define CHUNK 512

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: cat <filename>\n");
    return 1;
  }

  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    printf("cat: %s: no such file\n", argv[1]);
    return 1;
  }

  char buf[CHUNK];
  int n;
  while ((n = read(fd, buf, CHUNK)) > 0) {
    write(STDOUT_FILENO, buf, n);
  }

  close(fd);
  return 0;
}
