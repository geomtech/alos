/* src/userland/cmd/cat.c - Concatenate and display files */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

static int cat_file(const char *filename) {
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    printf("cat: %s: No such file or directory\n", filename);
    return 1;
  }

  char buf[512];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    write(STDOUT_FILENO, buf, (size_t)n);
  }

  close(fd);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: cat <file> [file2...]\n");
    return 1;
  }

  int ret = 0;
  for (int i = 1; i < argc; i++) {
    if (cat_file(argv[i]) != 0) {
      ret = 1;
    }
  }
  return ret;
}
