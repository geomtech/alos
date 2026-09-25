/* src/userland/cmd/wget.c - Download files over HTTP */
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: wget <url> [destination_path]\n");
    printf("Example: wget http://10.0.2.2:8000/index.html /index.html\n");
    return 1;
  }

  const char *url = argv[1];
  char dest[256];

  if (argc >= 3) {
    strncpy(dest, argv[2], sizeof(dest) - 1);
    dest[sizeof(dest) - 1] = '\0';
  } else {
    /* Determine filename from URL */
    const char *last_slash = strrchr(url, '/');
    if (last_slash && *(last_slash + 1) != '\0') {
      strncpy(dest, last_slash + 1, sizeof(dest) - 1);
      dest[sizeof(dest) - 1] = '\0';
    } else {
      strcpy(dest, "index.html");
    }
  }

  printf("Downloading %s to %s...\n", url, dest);
  long ret = syscall2(SYS_WGET, (long)url, (long)dest);
  if (ret == 0) {
    printf("Download successful! Saved to %s\n", dest);
    return 0;
  } else {
    printf("Download failed!\n");
    return 1;
  }
}
