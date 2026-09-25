/* src/userland/cmd/echo.c - Echo arguments to stdout */
#include <stdio.h>

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (i > 1) {
      putchar(' ');
    }
    printf("%s", argv[i]);
  }
  putchar('\n');
  return 0;
}
