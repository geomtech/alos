#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(test) do { if (!(test)) { \
  printf("stdio-file-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
  return 1; \
} } while (0)

int main(void) {
  const char *path = "/posix-test/stdio-file";
  FILE *stream = fopen(path, "w+b");
  CHECK(stream != NULL);
  CHECK(fwrite("abcdef", 1, 6, stream) == 6 && ftell(stream) == 6);
  CHECK(fseek(stream, 0, SEEK_SET) == 0);
  CHECK(fgetc(stream) == 'a' && ftell(stream) == 1);
  CHECK(ungetc('Z', stream) == 'Z' && ftell(stream) == 0);
  CHECK(fseek(stream, 1, SEEK_CUR) == 0 && fgetc(stream) == 'b');
  CHECK(fseek(stream, 0, SEEK_END) == 0 && fgetc(stream) == EOF &&
        feof(stream));
  CHECK(fseek(stream, 0, SEEK_SET) == 0 && !feof(stream));
  CHECK(fclose(stream) == 0);

  int fd = open(path, O_RDWR);
  CHECK(fd >= 0 && lseek(fd, 2, SEEK_SET) == 2);
  stream = fdopen(fd, "w");
  CHECK(stream != NULL && ftell(stream) == 2);
  CHECK(fputc('X', stream) == 'X');
  CHECK(fgetc(stream) == EOF && errno == EBADF && ferror(stream));
  CHECK(fclose(stream) == 0);
  errno = 0;
  CHECK(read(fd, &fd, 1) == -1 && errno == EBADF);

  fd = open(path, O_RDONLY);
  CHECK(fd >= 0);
  CHECK(fdopen(fd, "w") == NULL && errno == EINVAL);
  char bytes[8] = {0};
  CHECK(read(fd, bytes, sizeof(bytes)) == 6 && !strcmp(bytes, "abXdef"));
  CHECK(close(fd) == 0);

  stream = fopen(path, "a+");
  CHECK(stream != NULL && fseek(stream, 0, SEEK_SET) == 0);
  CHECK(fputc('!', stream) == '!' && ftell(stream) == 7);
  CHECK(fseek(stream, 0, SEEK_SET) == 0);
  memset(bytes, 0, sizeof(bytes));
  CHECK(fread(bytes, 1, sizeof(bytes), stream) == 7 &&
        !strcmp(bytes, "abXdef!"));
  CHECK(fclose(stream) == 0);

  stream = fopen(path, "w");
  CHECK(stream != NULL && fclose(stream) == 0);
  stream = fopen(path, "r");
  CHECK(stream != NULL && fgetc(stream) == EOF && feof(stream));
  CHECK(fclose(stream) == 0);
  CHECK(fopen(path, "rr") == NULL && errno == EINVAL);
  CHECK(fopen("/posix-test/does-not-exist", "r") == NULL && errno == ENOENT);
  CHECK(unlink(path) == 0);
  puts("stdio-file-test: PASS");
  return 0;
}
