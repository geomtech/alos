#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/alos_shm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
  printf("[shared-memory-test] FAIL line %d: %s errno=%d\n", \
         __LINE__, #c, errno); ++failures; } } while (0)

int main(void) {
  CHECK(alos_shm_create(0) == -1 && errno == EINVAL);
  CHECK(alos_shm_create(ALOS_SHM_MAX_SIZE + 1) == -1 && errno == EINVAL);
  CHECK(alos_shm_readonly(-1) == -1 && errno == EBADF);
  CHECK(alos_shm_size(1) == -1 && errno == EINVAL);
  int fd = alos_shm_create(8192);
  CHECK(fd >= 0);
  if (fd < 0) return 1;
  CHECK(alos_shm_size(fd) == 8192);
  CHECK(alos_shm_access(fd) == O_RDWR);
  CHECK(fcntl(fd, F_GETFD) & FD_CLOEXEC);
  int ro = alos_shm_readonly(fd);
  int duplicate = dup(ro);
  CHECK(ro >= 0 && duplicate >= 0);
  CHECK(alos_shm_access(ro) == O_RDONLY);
  CHECK(alos_shm_access(duplicate) == O_RDONLY);
  CHECK(shm_map(ro) == MAP_FAILED && errno == ENOTSUP);
  int listener = ipc_listen("readonly-shm-test");
  int client = ipc_connect("readonly-shm-test");
  int server = listener >= 0 ? ipc_accept(listener, 100) : -1;
  CHECK(listener >= 0 && client >= 0 && server >= 0);
  if (listener >= 0 && client >= 0 && server >= 0) {
    ipc_message_t message = {.length = 1, .attachment_fd = ro};
    message.data[0] = 'R';
    CHECK(ipc_send(client, &message) == -ENOTSUP);
    ipc_message_t incoming = {.attachment_fd = -1};
    CHECK(ipc_receive(server, &incoming, IPC_NONBLOCK) == 0);
  }
  if (server >= 0) close(server);
  if (client >= 0) close(client);
  if (listener >= 0) close(listener);
  CHECK(alos_shm_same(fd, ro) == 1);
  int other = alos_shm_create(8192);
  CHECK(other >= 0 && alos_shm_same(fd, other) == 0);
  if (other >= 0) close(other);
  CHECK(mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, ro, 0) ==
        MAP_FAILED && errno == EACCES);
  CHECK(mmap(NULL, 12288, PROT_READ, MAP_SHARED, fd, 0) ==
        MAP_FAILED && errno == EINVAL);
  CHECK(mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 1) ==
        MAP_FAILED && errno == EINVAL);
  unsigned char *rw = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, 0);
  unsigned char *read = mmap(NULL, 8192, PROT_READ, MAP_SHARED, ro, 0);
  unsigned char *alias = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
  CHECK(rw != MAP_FAILED && read != MAP_FAILED && alias != MAP_FAILED);
  if (rw == MAP_FAILED || read == MAP_FAILED || alias == MAP_FAILED) return 1;
  rw[0] = 42;
  CHECK(read[0] == 42 && alias[0] == 42);
  CHECK(mprotect(read, 8192, PROT_READ | PROT_WRITE) == -1 && errno == EACCES);
  /* Splits et fusion gardent le plafond, y compris apres PROT_NONE. */
  CHECK(mprotect(read + 4096, 4096, PROT_NONE) == 0);
  CHECK(mprotect(read + 4096, 4096, PROT_READ | PROT_WRITE) == -1 &&
        errno == EACCES);
  CHECK(mprotect(read + 4096, 4096, PROT_READ) == 0);
  close(fd);
  close(ro);
  int child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    if (alos_shm_access(duplicate) != O_RDONLY ||
        mprotect(read, 4096, PROT_WRITE) != -1 || errno != EACCES)
      _exit(1);
    alias[4096] = 93;
    _exit(0);
  }
  if (child > 0) {
    int status = -1;
    CHECK(waitpid(child, &status, 0) == child && status == 0);
    CHECK(read[4096] == 93 && rw[4096] == 93);
  }
  close(duplicate);
  CHECK(munmap(read + 4096, 4096) == 0);
  CHECK(mprotect(read, 4096, PROT_WRITE) == -1 && errno == EACCES);
  CHECK(munmap(read, 4096) == 0);
  CHECK(munmap(rw, 8192) == 0);
  CHECK(munmap(alias, 8192) == 0);
  for (int i = 0; i < 64; ++i) {
    int object = alos_shm_create(4096);
    CHECK(object >= 0);
    if (object >= 0) CHECK(close(object) == 0);
  }
  printf("[shared-memory-test] %s\n", failures ? "FAIL" : "PASS");
  return failures != 0;
}
