/* src/userland/cmd/ls.c - List directory contents */
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
  const char *path = ".";
  int show_all = 0;
  int long_format = 0;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      for (int j = 1; argv[i][j]; j++) {
        if (argv[i][j] == 'a') show_all = 1;
        if (argv[i][j] == 'l') long_format = 1;
      }
    } else {
      path = argv[i];
    }
  }

  userspace_dirent_t entry;
  uint32_t index = 0;
  int count = 0;

  while (1) {
    memset(&entry, 0, sizeof(entry));
    long ret = syscall3(SYS_READDIR, (long)path, index, (long)&entry);
    if (ret != 0) {
      if (ret < 0 && index == 0) {
        printf("ls: cannot access '%s': No such file or directory\n", path);
        return 1;
      }
      break; /* End of directory or error */
    }

    index++;

    if (!show_all && entry.name[0] == '.') {
      continue;
    }

    /* Print entry */
    if (entry.type & 0x02) {
      /* Directory */
      if (long_format) {
        printf("drwxr-xr-x   - %-24s\n", entry.name);
      } else {
        printf("  %-24s [DIR]\n", entry.name);
      }
    } else {
      /* File */
      if (long_format) {
        printf("-rw-r--r-- %5u %-24s\n", entry.size, entry.name);
      } else {
        printf("  %-24s %u B\n", entry.name, entry.size);
      }
    }

    count++;
  }

  if (count == 0 && !show_all) {
    printf("  (empty directory)\n");
  }

  return 0;
}
