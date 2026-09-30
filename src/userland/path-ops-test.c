#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
  printf("path-ops-test: FAIL line=%d errno=%d\n", __LINE__, errno); return 1; \
} } while (0)

int main(void) {
  const char *root = "/posix-test/at-root";
  CHECK(mkdir(root, 0700) == 0);
  CHECK(mkdir("/posix-test/at-root/child", 0700) == 0);
  int directory = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  CHECK(directory >= 0);
  CHECK(chdir("/bin") == 0);
  int file = openat(directory, "file", O_CREAT | O_EXCL | O_RDWR, 0600);
  CHECK(file >= 0 && write(file, "abc", 3) == 3);
  struct stat metadata, original;
  CHECK(fstat(file, &original) == 0);
  CHECK(fstatat(directory, "child/../file", &metadata, 0) == 0 &&
        metadata.st_ino == original.st_ino && metadata.st_size == 3);
  CHECK(fstatat(directory, "file/../child", &metadata, 0) == -1 && errno == ENOTDIR);
  CHECK(openat(file, "x", O_RDONLY) == -1 && errno == ENOTDIR);
  CHECK(openat(999, "x", O_RDONLY) == -1 && errno == EBADF);
  int absolute = openat(999, "/metadata-fixture/payload", O_RDONLY);
  CHECK(absolute >= 0 && close(absolute) == 0);
  CHECK(openat(directory, (const char *)(uintptr_t)1, O_RDONLY) == -1 &&
        errno == EFAULT);
  CHECK(fstatat(directory, "file", (void *)(uintptr_t)1, 0) == -1 && errno == EFAULT);
  CHECK(fstatat(directory, "file", &metadata, 0x40) == -1 && errno == EINVAL);
  CHECK(openat(directory, "file", O_CREAT | O_EXCL | O_RDWR, 0600) == -1 &&
        errno == EEXIST);

  CHECK(symlink("file", "/posix-test/at-root/link") == 0);
  CHECK(fstatat(directory, "link", &metadata, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISLNK(metadata.st_mode) && metadata.st_size == 4 && metadata.st_blocks == 0);
  CHECK(openat(directory, "link", O_RDONLY | O_NOFOLLOW) == -1 && errno == ELOOP);
  CHECK(open("/posix-test/at-root/link", O_RDONLY | O_NOFOLLOW) == -1 &&
        errno == ELOOP);
  CHECK(openat(directory, "link", O_RDONLY) == -1 && errno == ENOTSUP);
  char target[16];
  memset(target, '!', sizeof(target));
  CHECK(readlink("/posix-test/at-root/link", target, 2) == 2 &&
        !memcmp(target, "fi", 2) && target[2] == '!');
  CHECK(readlink("/posix-test/at-root/link", target, sizeof(target)) == 4 &&
        !memcmp(target, "file", 4) && target[4] == '!');
  CHECK(readlink("/posix-test/at-root/link", target, 0) == -1 && errno == EINVAL);
  CHECK(readlink("/posix-test/at-root/link", (void *)(uintptr_t)1, 4) == -1 &&
        errno == EFAULT);
  char long_target[61];
  memset(long_target, 'x', 60);
  long_target[60] = 0;
  CHECK(symlink(long_target, "/posix-test/at-root/long-link") == -1 && errno == ENOTSUP);
  CHECK(unlinkat(directory, "link", 0) == 0);
  CHECK(fstatat(directory, "link", &metadata, AT_SYMLINK_NOFOLLOW) == -1 &&
        errno == ENOENT);

  char canonical[4096] = "unchanged";
  CHECK(realpath("/posix-test/at-root/child/../file", canonical) == canonical &&
        !strcmp(canonical, "/posix-test/at-root/file"));
  char *allocated = realpath("../posix-test/at-root/file", NULL);
  CHECK(allocated && !strcmp(allocated, "/posix-test/at-root/file"));
  free(allocated);
  strcpy(canonical, "unchanged");
  CHECK(realpath("/posix-test/at-root/file/../child", canonical) == NULL &&
        errno == ENOTDIR && !strcmp(canonical, "unchanged"));
  CHECK(realpath("/posix-test/absent", canonical) == NULL && errno == ENOENT);
  CHECK(pathconf(root, _PC_NAME_MAX) == 255);
  CHECK(pathconf(root, 999) == -1 && errno == EINVAL);
  CHECK(chmod("/posix-test/at-root/file", 0000) == -1 && errno == ENOTSUP);
  CHECK(fstat(file, &metadata) == 0 && (metadata.st_mode & 0777) == 0600);

  CHECK(rename("/posix-test/at-root/file", "/posix-test/at-root/new") == 0);
  CHECK(openat(directory, "file", O_RDONLY) == -1 && errno == ENOENT);
  CHECK(fstatat(directory, "new", &metadata, 0) == 0 &&
        metadata.st_ino == original.st_ino);
  char bytes[4] = {0};
  CHECK(pread(file, bytes, 3, 0) == 3 && !strcmp(bytes, "abc"));
  CHECK(rename("/posix-test/at-root/new", "/posix-test/at-root/new") == 0);
  int destination = openat(directory, "dst", O_CREAT | O_EXCL | O_RDWR, 0600);
  CHECK(destination >= 0 && write(destination, "D", 1) == 1 && close(destination) == 0);
  CHECK(rename("/posix-test/at-root/new", "/posix-test/at-root/dst") == -1 &&
        errno == ENOTSUP);
  CHECK(fstatat(directory, "new", &metadata, 0) == 0 && metadata.st_size == 3);
  CHECK(rename(root, "/posix-test/moved") == -1 && errno == ENOTSUP);
  CHECK(unlinkat(directory, "new", AT_REMOVEDIR) == -1 && errno == ENOTDIR);
  CHECK(unlinkat(directory, "child", 0) == -1 && errno == EISDIR);
  CHECK(unlinkat(directory, "new", 0x40) == -1 && errno == EINVAL);
  int child = openat(directory, "child", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  CHECK(child >= 0);
  int nested = openat(child, "nested", O_CREAT | O_EXCL | O_RDWR, 0600);
  CHECK(nested >= 0 && close(nested) == 0);
  CHECK(unlinkat(directory, "child", AT_REMOVEDIR) == -1 && errno == ENOTEMPTY);
  CHECK(unlinkat(child, "nested", 0) == 0);
  CHECK(unlinkat(directory, "child/", AT_REMOVEDIR) == 0);
  CHECK(fstat(child, &metadata) == 0 && metadata.st_nlink == 0);
  CHECK(openat(child, "orphan", O_CREAT | O_EXCL | O_RDWR, 0600) == -1 &&
        errno == ENOENT);
  CHECK(close(child) == 0);
  CHECK(close(file) == 0 && unlinkat(directory, "new", 0) == 0 &&
        unlinkat(directory, "dst", 0) == 0 && close(directory) == 0);
  CHECK(chdir("/") == 0 && rmdir(root) == 0);
  puts("path-ops-test: PASS (inode-anchored operations; explicit capability limits)");
  return 0;
}
