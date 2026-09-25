/* src/userland/cmd/mkdir.c - Make directories */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: mkdir <directory...>\n");
    return 1;
  }

  int ret = 0;
  for (int i = 1; i < argc; i++) {
    if (mkdir(argv[i]) != 0) {
      printf("mkdir: cannot create directory '%s'\n", argv[i]);
      ret = 1;
    }
  }
  return ret;
}
