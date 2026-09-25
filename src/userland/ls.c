/* src/userland/ls.c - Liste le contenu d'un répertoire (port userland de
 * l'ancienne commande kernel `ls`, cf. src/shell/commands.c:cmd_ls). */
#include <dirent.h>
#include <stdio.h>

int main(int argc, char **argv) {
  const char *path = (argc >= 2) ? argv[1] : ".";

  struct dirent entry;
  unsigned int index = 0;
  int count = 0;

  printf("\n");
  while (1) {
    int ret = readdir(path, index, &entry);
    if (ret < 0) {
      printf("ls: %s: no such file or directory\n", path);
      return 1;
    }
    if (ret == 1) {
      break; /* Fin du répertoire */
    }

    if (entry.d_type == DT_DIR) {
      printf("[DIR]  %s\n", entry.d_name);
    } else {
      printf("[FILE] %u  %s\n", entry.d_size, entry.d_name);
    }

    index++;
    count++;
  }

  printf("\nTotal: %d items\n", count);
  return 0;
}
