#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/alos_shm.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
  printf("unix-socket-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
  return 1; \
} } while (0)

static ssize_t send_right(int endpoint, int fd, const char *bytes, size_t n) {
  union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control;
  memset(&control, 0, sizeof(control));
  struct iovec iov = {(void *)bytes, n};
  struct msghdr m = {0};
  m.msg_iov = &iov; m.msg_iovlen = 1;
  m.msg_control = control.bytes; m.msg_controllen = sizeof(control.bytes);
  struct cmsghdr *header = CMSG_FIRSTHDR(&m);
  header->cmsg_len = CMSG_LEN(sizeof(int));
  header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
  memcpy(CMSG_DATA(header), &fd, sizeof(fd));
  return sendmsg(endpoint, &m, MSG_NOSIGNAL);
}

static ssize_t receive_right(int endpoint, int *fd, char *bytes, size_t n,
                              int flags) {
  union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control;
  struct iovec iov = {bytes, n};
  struct msghdr m = {0};
  m.msg_iov = &iov; m.msg_iovlen = 1;
  m.msg_control = control.bytes; m.msg_controllen = sizeof(control.bytes);
  ssize_t result = recvmsg(endpoint, &m, flags);
  if (result < 0) return result;
  struct cmsghdr *header = CMSG_FIRSTHDR(&m);
  if (!header || header->cmsg_level != SOL_SOCKET ||
      header->cmsg_type != SCM_RIGHTS || header->cmsg_len != CMSG_LEN(sizeof(int)) ||
      (m.msg_flags & MSG_CTRUNC)) { errno = EIO; return -1; }
  memcpy(fd, CMSG_DATA(header), sizeof(*fd));
  return result;
}

static void *writer(void *argument) {
  int fd = (int)(intptr_t)argument;
  return (void *)(intptr_t)(write(fd, "Z", 1) == 1 ? 0 : 1);
}

static int interprocess(void) {
  int pair[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  int child = fork();
  CHECK(child >= 0);
  if (!child) {
    close(pair[0]);
    int fd;
    char tag;
    if (receive_right(pair[1], &fd, &tag, 1, 0) != 1 || tag != 'S')
      _exit(11);
    unsigned char *mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED || mapping[37] != 42) _exit(12);
    mapping[37] = 91;
    if (munmap(mapping, 4096) || close(fd) ||
        send(pair[1], "A", 1, MSG_NOSIGNAL) != 1 || close(pair[1])) _exit(13);
    _exit(0);
  }
  CHECK(close(pair[1]) == 0);
  int fd = alos_shm_create(4096);
  CHECK(fd >= 0);
  unsigned char *mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, 0);
  CHECK(mapping != MAP_FAILED);
  mapping[37] = 42;
  CHECK(send_right(pair[0], fd, "S", 1) == 1 && close(fd) == 0);
  char tag;
  CHECK(recv(pair[0], &tag, 1, 0) == 1 && tag == 'A' && mapping[37] == 91);
  CHECK(recv(pair[0], &tag, 1, 0) == 0);
  int status;
  CHECK(waitpid(child, &status, 0) == child && status == 0);
  CHECK(munmap(mapping, 4096) == 0 && close(pair[0]) == 0);
  puts("unix-socket-test: parent/child SHM-right PASS");
  return 0;
}

static int release_chain(void) {
  int current[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, current) == 0);
  int anchor = current[1], sender = current[0];
  for (unsigned i = 0; i < 128; ++i) {
    int next[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, next) == 0);
    CHECK(send_right(sender, next[1], "C", 1) == 1);
    CHECK(close(next[1]) == 0 && close(sender) == 0);
    sender = next[0];
  }
  CHECK(close(sender) == 0 && close(anchor) == 0);
  puts("unix-socket-test: deep acyclic close PASS");
  return 0;
}

int main(void) {
  CHECK(interprocess() == 0);
  CHECK(release_chain() == 0);
  int pair[2];
  CHECK(socketpair(AF_INET, SOCK_STREAM, 0, pair) == -1 && errno == EAFNOSUPPORT);
  CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == -1 && errno == EPROTONOSUPPORT);
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, (void *)1) == -1 && errno == EFAULT);
  CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
  CHECK(fcntl(pair[0], F_GETFD) == FD_CLOEXEC);
  CHECK(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
  char byte;
  CHECK(recv(pair[0], &byte, 1, 0) == -1 && errno == EAGAIN);
  CHECK(send_right(pair[0], pair[1], "x", 1) == -1 && errno == EOPNOTSUPP);
  CHECK(send_right(pair[0], -1, "x", 1) == -1 && errno == EBADF);
  CHECK(sendmsg(pair[0], (void *)1, 0) == -1 && errno == EFAULT);
  int forwarded[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, forwarded) == 0);
  CHECK(send_right(pair[0], forwarded[1], "T", 1) == 1);
  CHECK(close(forwarded[1]) == 0);
  int channel;
  char marker;
  CHECK(receive_right(pair[1], &channel, &marker, 1, 0) == 1 && marker == 'T');
  CHECK(write(forwarded[0], "transport", 9) == 9);
  char transported[9];
  CHECK(read(channel, transported, 9) == 9 && !memcmp(transported, "transport", 9));
  CHECK(close(forwarded[0]) == 0 && read(channel, &marker, 1) == 0 && close(channel) == 0);

  int a[2], b[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, a) == 0 &&
        socketpair(AF_UNIX, SOCK_STREAM, 0, b) == 0);
  CHECK(send_right(a[0], b[1], "1", 1) == 1);
  CHECK(send_right(b[0], a[1], "2", 1) == -1 && errno == EOPNOTSUPP);
  CHECK(read(a[1], &marker, 1) == 1 && marker == '1');
  CHECK(send_right(b[0], a[1], "3", 1) == 1);
  CHECK(receive_right(b[1], &channel, &marker, 1, 0) == 1 && marker == '3');
  CHECK(close(channel) == 0 && close(a[0]) == 0 && close(a[1]) == 0 &&
        close(b[0]) == 0 && close(b[1]) == 0);

  int shm = alos_shm_create(4096);
  CHECK(shm >= 0);
  unsigned char *mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
  CHECK(mapping != MAP_FAILED);
  mapping[19] = 73;
  CHECK(send_right(pair[0], shm, "hello", 5) == 5);
  CHECK(close(shm) == 0);
  int received;
  char bytes[16] = {0};
  CHECK(receive_right(pair[1], &received, bytes, 2, MSG_CMSG_CLOEXEC) == 2);
  CHECK(!memcmp(bytes, "he", 2) && fcntl(received, F_GETFD) == FD_CLOEXEC);
  CHECK(alos_shm_size(received) == 4096);
  unsigned char *other = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, received, 0);
  CHECK(other != MAP_FAILED && other[19] == 73);
  other[19] = 91;
  CHECK(mapping[19] == 91);
  CHECK(read(pair[1], bytes, 16) == 3 && !memcmp(bytes, "llo", 3));
  CHECK(munmap(other, 4096) == 0 && munmap(mapping, 4096) == 0 && close(received) == 0);

  char path[] = "/posix-test/unix-right-XXXXXX";
  int file = mkstemp(path);
  CHECK(file >= 0 && write(file, "abcdef", 6) == 6 && lseek(file, 0, SEEK_SET) == 0);
  CHECK(fcntl(file, F_SETFD, FD_CLOEXEC) == 0);
  CHECK(send_right(pair[0], file, "F", 1) == 1);
  CHECK(receive_right(pair[1], &received, bytes, 1, 0) == 1);
  CHECK(fcntl(received, F_GETFD) == 0);
  CHECK(read(received, bytes, 2) == 2 && !memcmp(bytes, "ab", 2));
  CHECK(read(file, bytes, 2) == 2 && !memcmp(bytes, "cd", 2));
  CHECK(close(received) == 0);

  CHECK(send_right(pair[0], file, "R", 1) == 1);
  int occupied[32], count = 0, fd;
  while ((fd = dup(STDOUT_FILENO)) >= 0 && count < 32) occupied[count++] = fd;
  CHECK(errno == EMFILE && count > 0);
  CHECK(receive_right(pair[1], &received, bytes, 1, 0) == -1 && errno == EMFILE);
  CHECK(close(occupied[--count]) == 0);
  CHECK(receive_right(pair[1], &received, bytes, 1, 0) == 1 && bytes[0] == 'R');
  CHECK(close(received) == 0);
  while (count) CHECK(close(occupied[--count]) == 0);
  CHECK(send_right(pair[0], file, "D", 1) == 1);
  struct iovec iov = {bytes, 1};
  struct msghdr discard = {0};
  discard.msg_iov = &iov; discard.msg_iovlen = 1;
  CHECK(recvmsg(pair[1], &discard, 0) == 1 && bytes[0] == 'D' &&
        (discard.msg_flags & MSG_CTRUNC));
  CHECK(close(file) == 0 && unlink(path) == 0);

  for (int i = 0; i < 32; ++i) CHECK(write(pair[0], "q", 1) == 1);
  CHECK(write(pair[0], "q", 1) == -1 && errno == EAGAIN);
  struct pollfd writable = {pair[0], POLLOUT, 0};
  CHECK(poll(&writable, 1, 0) == 0);
  CHECK(fcntl(pair[0], F_SETFL, 0) == 0);
  pthread_t worker;
  CHECK(pthread_create(&worker, NULL, writer, (void *)(intptr_t)pair[0]) == 0);
  CHECK(read(pair[1], &byte, 1) == 1 && byte == 'q');
  void *error;
  CHECK(pthread_join(worker, &error) == 0 && !error);
  for (int i = 0; i < 32; ++i) CHECK(read(pair[1], &byte, 1) == 1);
  CHECK(byte == 'Z');

  int pipefd[2];
  CHECK(pipe(pipefd) == 0);
  CHECK(pthread_create(&worker, NULL, writer, (void *)(intptr_t)pair[0]) == 0);
  struct pollfd sources[2] = {{pair[1], POLLIN, 0}, {pipefd[0], POLLIN, 0}};
  CHECK(poll(sources, 2, 2000) == 1 && (sources[0].revents & POLLIN));
  CHECK(read(pair[1], &byte, 1) == 1 && byte == 'Z');
  CHECK(pthread_join(worker, &error) == 0 && !error);
  CHECK(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
  CHECK(close(pair[0]) == 0);
  sources[0].revents = 0;
  CHECK(poll(sources, 1, 0) == 1 && (sources[0].revents & POLLHUP));
  CHECK(read(pair[1], &byte, 1) == 0);
  CHECK(send(pair[1], "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
  CHECK(close(pair[1]) == 0);
  puts("unix-socket-test: PASS");
  return 0;
}
