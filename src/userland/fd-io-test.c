#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
static unsigned char payload[5000];
static unsigned char received[5000];

#define CHECK(condition) do { \
  if (!(condition)) { \
    printf("fd-io-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
    failures++; \
  } \
} while (0)

static void fd_string(char *buffer, int fd) {
  snprintf(buffer, 16, "%d", fd);
}

static int exec_check(int argc, char **argv) {
  if (argc != 4) return 1;
  int closed = atoi(argv[2]), kept = atoi(argv[3]);
  errno = 0;
  if (fcntl(closed, F_GETFD) != -1 || errno != EBADF) return 2;
  if (fcntl(kept, F_GETFD) != 0) return 3;
  if (lseek(kept, 0, SEEK_CUR) < 0) return 4;
  return close(kept) == 0 ? 0 : 5;
}

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "--exec-check") == 0)
    return exec_check(argc, argv);

  /* Fixtures recreees sur le disque de regression, jamais sur disk.img. */
  int fd = open("/fd-io-data", O_RDWR | O_CLOEXEC);
  CHECK(fd >= 0);
  if (fd < 0) return 1;
  CHECK(lseek(fd, 0, SEEK_END) == 32);
  CHECK(lseek(fd, 0, SEEK_SET) == 0);
  char text[8];
  CHECK(read(fd, text, sizeof(text)) == 8);
  CHECK(memcmp(text, "fixture-", 8) == 0);
  CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
  CHECK(fcntl(fd, F_GETFL) == O_RDWR);
  CHECK(write(fd, NULL, 0) == 0);
  CHECK(read(fd, NULL, 0) == 0);
  CHECK(lseek(fd, 0, SEEK_SET) == 0);
  CHECK(write(fd, "abcdef", 6) == 6);

  int copy = dup(fd);
  CHECK(copy >= 0);
  CHECK(fcntl(copy, F_GETFD) == 0);
  CHECK(lseek(copy, 1, SEEK_SET) == 1);
  CHECK(read(fd, text, 1) == 1 && text[0] == 'b');
  CHECK(fcntl(copy, F_SETFD, FD_CLOEXEC) == 0);
  CHECK(fcntl(fd, F_SETFD, 0) == 0);
  CHECK(fcntl(copy, F_GETFD) == FD_CLOEXEC);
  CHECK(fcntl(fd, F_GETFD) == 0);
  CHECK(fcntl(copy, F_SETFL, O_RDONLY | O_NONBLOCK) == 0);
  CHECK(fcntl(fd, F_GETFL) == (O_RDWR | O_NONBLOCK));
  CHECK(fcntl(fd, F_SETFL, O_RDWR) == 0);
  CHECK(fcntl(copy, F_GETFL) == O_RDWR);

  pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    char value;
    int ok = read(copy, &value, 1) == 1 && value == 'c';
    close(copy);
    close(fd);
    return ok ? 0 : 1;
  }
  int status = -1;
  if (child > 0) CHECK(waitpid(child, &status, 0) == child && status == 0);
  CHECK(read(fd, text, 1) == 1 && text[0] == 'd');
  int target = open("/fd-io-zero", O_RDONLY);
  CHECK(target >= 0);
  CHECK(dup2(copy, target) == target);
  CHECK(fcntl(target, F_GETFD) == 0);
  CHECK(dup2(target, target) == target);
  CHECK(close(copy) == 0);
  CHECK(read(target, text, 1) == 1 && text[0] == 'e');
  CHECK(lseek(fd, 0, SEEK_CUR) == 5);
  int high = fcntl(fd, F_DUPFD_CLOEXEC, 20);
  CHECK(high >= 20 && fcntl(high, F_GETFD) == FD_CLOEXEC);

  child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    char closed_string[16], kept_string[16];
    fd_string(closed_string, high);
    fd_string(kept_string, target);
    char *arguments[] = {"/bin/fd-io-test", "--exec-check",
                         closed_string, kept_string, NULL};
    char *environment[] = {NULL};
    execve(arguments[0], arguments, environment);
    return 10;
  }
  status = -1;
  if (child > 0) CHECK(waitpid(child, &status, 0) == child && status == 0);
  CHECK(close(high) == 0);
  CHECK(close(target) == 0);

  for (unsigned i = 0; i < sizeof(payload); i++)
    payload[i] = (unsigned char)(i * 37U + 19U);
  CHECK(lseek(fd, 0, SEEK_END) == 32);
  CHECK(write(fd, payload, sizeof(payload)) == (ssize_t)sizeof(payload));
  CHECK(lseek(fd, 32, SEEK_SET) == 32);
  CHECK(read(fd, received, sizeof(received)) == (ssize_t)sizeof(received));
  CHECK(memcmp(payload, received, sizeof(payload)) == 0);
  CHECK(read(fd, text, 1) == 0);
  off_t end = 32 + (off_t)sizeof(payload);
  CHECK(lseek(fd, 4099, SEEK_END) == end + 4099);
  CHECK(write(fd, "Z", 1) == 1);
  CHECK(lseek(fd, end, SEEK_SET) == end);
  memset(received, 0xff, 4099);
  CHECK(read(fd, received, 4099) == 4099);
  for (unsigned i = 0; i < 4099; i++) {
    if (received[i] != 0) { CHECK(received[i] == 0); break; }
  }
  CHECK(read(fd, text, 1) == 1 && text[0] == 'Z');
  int append = open("/fd-io-data", O_WRONLY | O_APPEND);
  CHECK(append >= 0);
  CHECK(lseek(append, 0, SEEK_SET) == 0);
  CHECK(write(append, "Q", 1) == 1);
  CHECK(lseek(fd, 0, SEEK_END) == end + 4101);
  CHECK(lseek(fd, -2, SEEK_END) == end + 4099);
  CHECK(read(fd, text, 2) == 2 && memcmp(text, "ZQ", 2) == 0);
  CHECK(close(append) == 0);
  CHECK(lseek(fd, -2, SEEK_END) == end + 4099);
  CHECK(read(fd, received, sizeof(received)) == 2);
  CHECK(memcmp(received, "ZQ", 2) == 0);
  CHECK(read(fd, text, 1) == 0);

  off_t sparse_start = lseek(fd, 0, SEEK_END);
  CHECK(sparse_start == end + 4101);
  CHECK(lseek(fd, 65539, SEEK_END) == sparse_start + 65539);
  CHECK(write(fd, "T", 1) == 1);
  CHECK(lseek(fd, sparse_start, SEEK_SET) == sparse_start);
  unsigned remaining = 65539;
  while (remaining) {
    unsigned length = remaining > sizeof(received) ? sizeof(received) : remaining;
    CHECK(read(fd, received, length) == (ssize_t)length);
    for (unsigned i = 0; i < length; i++) {
      if (received[i] != 0) { CHECK(received[i] == 0); break; }
    }
    remaining -= length;
  }
  CHECK(read(fd, text, 1) == 1 && text[0] == 'T');

  int ro = open("/fd-io-data", O_RDONLY);
  int wo = open("/fd-io-data", O_WRONLY);
  errno = 0;
  CHECK(write(ro, "x", 1) == -1 && errno == EBADF);
  errno = 0;
  CHECK(read(wo, text, 1) == -1 && errno == EBADF);
  CHECK(close(ro) == 0 && close(wo) == 0);
  errno = 0;
  CHECK(lseek(fd, -1, SEEK_SET) == -1 && errno == EINVAL);
  errno = 0;
  CHECK(lseek(fd, (off_t)UINT32_MAX + 1, SEEK_SET) == -1 &&
        errno == EOVERFLOW);
  errno = 0;
  CHECK(read(fd, (void *)(uintptr_t)-1, 1) == -1 && errno == EFAULT);
  errno = 0;
  CHECK(write(fd, (void *)(uintptr_t)-1, 1) == -1 && errno == EFAULT);
  CHECK(close(fd) == 0);
  errno = 0;
  CHECK(close(fd) == -1 && errno == EBADF);
  errno = 0;
  CHECK(read(-1, text, 1) == -1 && errno == EBADF);
  errno = 0;
  CHECK(write(-1, text, 1) == -1 && errno == EBADF);
  errno = 0;
  CHECK(lseek(-1, 0, SEEK_SET) == -1 && errno == EBADF);
  errno = 0;
  CHECK(fcntl(-1, F_GETFL) == -1 && errno == EBADF);
  errno = 0;
  CHECK(dup2(-1, 0) == -1 && errno == EBADF);
  errno = 0;
  CHECK(lseek(STDOUT_FILENO, 0, SEEK_SET) == -1 && errno == ESPIPE);
  int console_flags = fcntl(STDIN_FILENO, F_GETFL);
  CHECK(console_flags == O_RDONLY);
  errno = 0;
  CHECK(fcntl(STDIN_FILENO, F_SETFL, console_flags | O_NONBLOCK) == -1 &&
        errno == ENOTSUP);
  CHECK(fcntl(STDIN_FILENO, F_GETFL) == console_flags);
  errno = 0;
  CHECK(open("/fd-io-missing", O_RDONLY) == -1 && errno == ENOENT);
  errno = 0;
  int truncated = open("/fd-io-data", O_RDWR | O_TRUNC);
  CHECK(truncated >= 0);
  if (truncated >= 0) {
    CHECK(lseek(truncated, 0, SEEK_END) == 0);
    CHECK(read(truncated, text, 1) == 0);
    CHECK(close(truncated) == 0);
  }
  errno = 0;
  CHECK(open("/fd-io-data", O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR);
  int directory = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  CHECK(directory >= 0 && fcntl(directory, F_GETFD) == FD_CLOEXEC);
  errno = 0;
  CHECK(read(directory, text, 1) == -1 && errno == EISDIR);
  CHECK(close(directory) == 0);
  const char console_message[] = "fd-io-test: console OK\n";
  CHECK(write(STDOUT_FILENO, console_message, sizeof(console_message) - 1) ==
        (ssize_t)(sizeof(console_message) - 1));
  printf("fd-io-test: %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
