#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/alos_process.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
  printf("fs-attributes-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
  return 1; \
} } while (0)

static int title_is(const char *expected) {
  proc_info_t records[64];
  int count = (int)syscall2(SYS_PS_INFO, (long)records, 64);
  for (int i = 0; i < count; ++i)
    if (records[i].pid == (uint32_t)getpid() &&
        records[i].tid == (uint32_t)gettid())
      return !strcmp(records[i].name, expected);
  return 0;
}

int main(void) {
  struct statvfs before, during, after;
  CHECK(statvfs("/posix-test", &before) == 0);
  CHECK(before.f_bsize >= 1024 && before.f_frsize == before.f_bsize &&
        before.f_blocks * before.f_frsize == 64U * 1024U * 1024U &&
        before.f_bavail == before.f_bfree && before.f_bfree <= before.f_blocks &&
        before.f_favail == before.f_ffree && before.f_ffree <= before.f_files &&
        before.f_fsid && before.f_namemax == 255 && (before.f_flag & ST_NOSUID));
  CHECK(statvfs("/does-not-exist", &after) == -1 && errno == ENOENT);
  CHECK(statvfs("/posix-test", (struct statvfs *)(uintptr_t)1) == -1 &&
        errno == EFAULT);
  CHECK(statvfs((const char *)(uintptr_t)1, &after) == -1 && errno == EFAULT);
  const char *path = "/posix-test/attributes";
  int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
  CHECK(fd >= 0);
  char data[4096];
  memset(data, 0x5a, sizeof(data));
  CHECK(write(fd, data, sizeof(data)) == sizeof(data));
  CHECK(statvfs(path, &during) == 0 && during.f_fsid == before.f_fsid &&
        during.f_ffree + 1 == before.f_ffree &&
        during.f_bfree < before.f_bfree);
  struct timeval times[2] = {{1700000041, 123456}, {1700000042, 654321}};
  CHECK(futimes(fd, times) == 0);
  struct stat metadata;
  CHECK(fstat(fd, &metadata) == 0 && metadata.st_atime == 1700000041 &&
        metadata.st_mtime == 1700000042 && metadata.st_atim.tv_nsec == 0 &&
        metadata.st_mtim.tv_nsec == 0);
  CHECK(close(fd) == 0);
  fd = open(path, O_RDONLY);
  CHECK(fd >= 0 && fstat(fd, &metadata) == 0 && metadata.st_mtime == 1700000042);
  times[1].tv_usec = 1000000;
  CHECK(futimes(fd, times) == -1 && errno == EINVAL);
  CHECK(fstat(fd, &metadata) == 0 && metadata.st_mtime == 1700000042);
  times[1].tv_usec = 0;
  times[1].tv_sec = (int64_t)UINT32_MAX + 1;
  CHECK(futimes(fd, times) == -1 && errno == EOVERFLOW);
  CHECK(futimes(fd, (const struct timeval *)(uintptr_t)1) == -1 && errno == EFAULT);
  struct timeval now;
  CHECK(gettimeofday(&now, NULL) == 0 && futimes(fd, NULL) == 0);
  CHECK(fstat(fd, &metadata) == 0 && metadata.st_atime >= now.tv_sec &&
        metadata.st_atime == metadata.st_mtime &&
        metadata.st_mtime == metadata.st_ctime);
  CHECK(close(fd) == 0 && futimes(fd, NULL) == -1 && errno == EBADF);
  CHECK(unlink(path) == 0);
  CHECK(statvfs("/posix-test", &after) == 0 && after.f_ffree == before.f_ffree &&
        after.f_bfree == during.f_bfree +
            (sizeof(data) + before.f_frsize - 1) / before.f_frsize);
  CHECK(alos_set_process_title("native-title") == 0 && title_is("native-title"));
  CHECK(alos_set_process_title("") == -1 && errno == EINVAL);
  CHECK(alos_set_process_title("012345678901234567890123456789012") == -1 &&
        errno == ENAMETOOLONG);
  CHECK(alos_set_process_title((const char *)(uintptr_t)1) == -1 &&
        errno == EFAULT && title_is("native-title"));
  puts("fs-attributes-test: PASS");
  return 0;
}
