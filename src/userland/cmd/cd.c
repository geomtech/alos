/* src/userland/cmd/cd.c - Change directory (utility) */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *path = "/";
  if (argc > 1) {
    path = argv[1];
  }
  if (chdir(path) != 0) {
    printf("cd: %s: No such directory\n", path);
    return 1;
  }
  return 0;
}
