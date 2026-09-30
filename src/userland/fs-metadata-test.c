#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vminfo.h>
#include <unistd.h>

#define ROOT "/metadata-fixture"
#define PAYLOAD ROOT "/payload"
#define EMPTY ROOT "/empty"
#define CHECK(x) do { if (!(x)) { printf("fs-metadata-test: FAIL line=%d errno=%d\n", __LINE__, errno); return 1; } } while (0)

static int payload_metadata(const struct stat *metadata) {
  return S_ISREG(metadata->st_mode) && (metadata->st_mode & 07777) == 0640 &&
         metadata->st_size == 17 && metadata->st_ino && metadata->st_dev &&
         metadata->st_nlink == 1 && metadata->st_uid == 42 &&
         metadata->st_gid == 43 && metadata->st_blksize >= 1024 &&
         metadata->st_blocks > 0 && metadata->st_atime == 1700000001 &&
         metadata->st_mtime == 1700000002 && metadata->st_ctime == 1700000003 &&
         !metadata->st_atim.tv_nsec && !metadata->st_mtim.tv_nsec &&
         !metadata->st_ctim.tv_nsec;
}

int main(void) {
  struct stat by_path, by_fd, directory_metadata;
  errno = EDOM;
  CHECK(!stat(PAYLOAD, &by_path) && errno == EDOM);
  CHECK(payload_metadata(&by_path));
  CHECK(!lstat(PAYLOAD, &by_fd) && by_fd.st_ino == by_path.st_ino);
  int fd = open(PAYLOAD, O_RDONLY);
  CHECK(fd >= 0 && !fstat(fd, &by_fd));
  CHECK(payload_metadata(&by_fd) && by_fd.st_ino == by_path.st_ino &&
        by_fd.st_dev == by_path.st_dev);
  CHECK(fdopendir(fd) == NULL && errno == ENOTDIR);
  CHECK(!fstat(fd, &by_fd));
  char content[18] = {0};
  CHECK(read(fd, content, 17) == 17 && !strcmp(content, "metadata-fixture\n"));
  CHECK(!close(fd));
  CHECK(fstat(fd, &by_fd) == -1 && errno == EBADF);
  CHECK(stat(ROOT "/absent", &by_fd) == -1 && errno == ENOENT);
  CHECK(stat("", &by_fd) == -1 && errno == ENOENT);
  CHECK(stat(PAYLOAD "/", &by_fd) == -1 && errno == ENOTDIR);
  CHECK(stat(PAYLOAD "/../empty", &by_fd) == -1 && errno == ENOTDIR);
  CHECK(stat((const char *)(uintptr_t)1, &by_fd) == -1 && errno == EFAULT);
  CHECK(stat(PAYLOAD, (struct stat *)(uintptr_t)1) == -1 && errno == EFAULT);
  CHECK(!stat(EMPTY, &directory_metadata) && S_ISDIR(directory_metadata.st_mode));
  CHECK(opendir(ROOT "/absent") == NULL && errno == ENOENT);
  CHECK(opendir(PAYLOAD) == NULL && errno == ENOTDIR);
  CHECK(fdopendir(-1) == NULL && errno == EBADF);
  CHECK(readdir(NULL) == NULL && errno == EBADF);
  CHECK(closedir(NULL) == -1 && errno == EBADF);
  DIR *directory = opendir(EMPTY);
  CHECK(directory != NULL);
  fd = dirfd(directory);
  CHECK(fd >= 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC));
  void *guard = mmap(NULL, 4096, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(guard != MAP_FAILED);
  CHECK(fstat(fd, guard) == -1 && errno == EFAULT);
  CHECK(syscall2(SYS_READDIR_FD, fd, (long)guard) == -EFAULT);
  CHECK(!munmap(guard, 4096));
  unsigned entries = 0, non_dot = 0;
  struct dirent *entry;
  errno = ERANGE;
  while ((entry = readdir(directory)) != NULL) {
    CHECK(errno == ERANGE && entry->d_ino && entry->d_type == DT_DIR &&
          entry->d_reclen >= sizeof(*entry) &&
          entry->d_off == (off_t)entries + 1);
    if (!strcmp(entry->d_name, "."))
      CHECK(entry->d_ino == directory_metadata.st_ino);
    else if (strcmp(entry->d_name, "..")) non_dot++;
    entries++;
  }
  CHECK(errno == ERANGE && entries == 2 && !non_dot);
  CHECK(readdir(directory) == NULL && errno == ERANGE);
  CHECK(!closedir(directory));
  CHECK(fstat(fd, &by_fd) == -1 && errno == EBADF);

  directory = opendir(EMPTY);
  CHECK(directory != NULL);
  fd = dirfd(directory);
  int duplicate = fcntl(fd, F_DUPFD, 0);
  CHECK(duplicate >= 0 && duplicate != fd);
  DIR *second = fdopendir(duplicate);
  CHECK(second != NULL);
  entry = readdir(directory);
  CHECK(entry != NULL && entry->d_off == 1);
  char first_name[256];
  memcpy(first_name, entry->d_name, sizeof(first_name));
  entry = readdir(second);
  CHECK(entry != NULL && entry->d_off == 2 && strcmp(first_name, entry->d_name));
  CHECK(!closedir(directory) && !fstat(duplicate, &by_fd));
  errno = EDOM;
  CHECK(readdir(second) == NULL && errno == EDOM);
  CHECK(!closedir(second));
  CHECK(fstat(duplicate, &by_fd) == -1 && errno == EBADF);
  struct alos_dirent native;
  CHECK(!alos_readdir(EMPTY, 0, &native) && native.d_type == ALOS_DT_DIR);

  directory = opendir(ROOT "/malformed");
  CHECK(directory != NULL);
  errno = 0;
  CHECK(readdir(directory) == NULL && errno == EIO);
  errno = 0;
  CHECK(readdir(directory) == NULL && errno == EIO);
  CHECK(!closedir(directory));

  vm_info_t before, after;
  for (unsigned i = 0; i < 10; i++) CHECK(!stat(PAYLOAD, &by_fd));
  CHECK(!vminfo(&before));
  for (unsigned i = 0; i < 1000; i++) {
    CHECK(!stat(PAYLOAD, &by_fd) && payload_metadata(&by_fd));
    CHECK(stat(ROOT "/empty/no-entry", &by_fd) == -1 && errno == ENOENT);
  }
  CHECK(!vminfo(&after) && before.physical_free == after.physical_free &&
        before.virtual_size == after.virtual_size);
  puts("fs-metadata-test: PASS");
  return 0;
}
