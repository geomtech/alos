/* src/userland/cmd/touch.c - Change file timestamps or create empty file */
#include <stdio.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: touch <file...>\n");
    return 1;
  }

  int ret = 0;
  for (int i = 1; i < argc; i++) {
    long r = syscall1(SYS_CREATE, (long)argv[i]);
    if (r != 0) {
      printf("touch: cannot touch '%s'\n", argv[i]);
      ret = 1;
    }
  }
  return ret;
}
