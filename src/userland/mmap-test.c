/* Tests VM executes en Ring 3, y compris les fautes attendues des enfants. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/vminfo.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE 4096ULL
#define CHECK(test) do { \
  if (!(test)) { \
    printf("mmap-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
    return 1; \
  } \
} while (0)

static int wait_status(pid_t child, int expected) {
  int status = -1;
  return child > 0 && waitpid(child, &status, 0) == child &&
         status == expected;
}

static void *anonymous(size_t length, int prot, int kind) {
  return mmap(NULL, length, prot, kind | MAP_ANONYMOUS, -1, 0);
}

static int reservation_test(void) {
  vm_info_t before, after;
  CHECK(vminfo(&before) == 0);
  size_t size = 16ULL * 1024 * 1024 * 1024;
  unsigned char *p = mmap(NULL, size, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  CHECK(p != MAP_FAILED && (uint64_t)p >= 0x100000000ULL);
  CHECK(vminfo(&after) == 0);
  CHECK(before.physical_free == after.physical_free);
  CHECK(after.virtual_size == before.virtual_size + size);
  CHECK(after.resident_size == before.resident_size);
  CHECK(mprotect(p + PAGE, PAGE, PROT_READ | PROT_WRITE) == 0);
  CHECK(mprotect(p + size - PAGE, PAGE, PROT_READ | PROT_WRITE) == 0);
  CHECK(p[PAGE] == 0 && p[size - PAGE] == 0);
  p[PAGE] = 17;
  p[size - PAGE] = 31;
  CHECK(vminfo(&after) == 0);
  CHECK(before.physical_free > after.physical_free);
  CHECK(before.physical_free - after.physical_free < 128 * PAGE);
  CHECK(after.resident_size == before.resident_size + 2 * PAGE);
  CHECK(mprotect(p + PAGE, PAGE, PROT_NONE) == 0);
  CHECK(mprotect(p + PAGE, PAGE, PROT_READ) == 0 && p[PAGE] == 17);
  CHECK(munmap(p, size) == 0);
  puts("mmap-test: sparse-reservation PASS");
  return 0;
}

static int protection_test(void) {
  volatile unsigned char *p =
      anonymous(PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  CHECK((void *)p != MAP_FAILED);
  p[0] = 53;
  CHECK(mprotect((void *)p, PAGE, PROT_READ) == 0);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) { p[0] = 1; _exit(1); }
  CHECK(wait_status(child, 139));
  CHECK(p[0] == 53);
  CHECK(mprotect((void *)p, PAGE, PROT_NONE) == 0);
  child = fork();
  CHECK(child >= 0);
  if (!child) { unsigned char value = p[0]; _exit(value); }
  CHECK(wait_status(child, 139));

  child = fork();
  CHECK(child >= 0);
  if (!child) {
    if (mprotect((void *)p, PAGE, PROT_READ | PROT_WRITE) != 0 || p[0] != 53)
      _exit(1);
    p[0] = 62;
    _exit(0);
  }
  CHECK(wait_status(child, 0));
  CHECK(mprotect((void *)p, PAGE, PROT_READ | PROT_WRITE) == 0);
  CHECK(p[0] == 53);
  CHECK(mprotect((void *)p, PAGE, PROT_READ | PROT_WRITE | PROT_EXEC) == -1);
  CHECK(errno == EACCES);
  CHECK(munmap((void *)p, PAGE) == 0);

  unsigned char *code = anonymous(PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  CHECK(code != MAP_FAILED);
  /* mov eax, SYS_GETPID ; int 0x80 ; ret */
  const unsigned char instructions[] = {0xb8, 20, 0, 0, 0, 0xcd, 0x80, 0xc3};
  memcpy(code, instructions, sizeof(instructions));
  child = fork();
  CHECK(child >= 0);
  if (!child) { ((int (*)(void))code)(); _exit(1); }
  CHECK(wait_status(child, 139));
  CHECK(mprotect(code, PAGE, PROT_READ | PROT_EXEC) == 0);
  CHECK(((int (*)(void))code)() == getpid());
  child = fork();
  CHECK(child >= 0);
  if (!child) { *(volatile unsigned char *)code = 0; _exit(1); }
  CHECK(wait_status(child, 139));
  CHECK(munmap(code, PAGE) == 0);
  puts("mmap-test: permissions-and-jit PASS");
  return 0;
}

static int partial_test(void) {
  unsigned char *p = anonymous(3 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  CHECK(p != MAP_FAILED);
  p[0] = 11;
  p[PAGE] = 22;
  p[2 * PAGE] = 33;
  CHECK(madvise(p + PAGE, PAGE, MADV_DONTNEED) == 0);
  CHECK(p[0] == 11 && p[PAGE] == 0 && p[2 * PAGE] == 33);
  CHECK(munmap(p + PAGE, PAGE) == 0);
  CHECK(mprotect(p, 3 * PAGE, PROT_NONE) == -1 && errno == ENOMEM);
  CHECK(p[0] == 11 && p[2 * PAGE] == 33);
  CHECK(mmap(p, PAGE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0)
        == MAP_FAILED && errno == EEXIST);
  CHECK(p[0] == 11);
  CHECK(mmap(p + PAGE, PAGE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0)
        == p + PAGE);
  CHECK(p[PAGE] == 0);
  p[PAGE] = 44;
  CHECK(mmap(p + PAGE, PAGE, PROT_NONE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == p + PAGE);
  CHECK(mprotect(p + PAGE, PAGE, PROT_READ | PROT_WRITE) == 0);
  CHECK(p[0] == 11 && p[PAGE] == 0 && p[2 * PAGE] == 33);
  CHECK(munmap(p, 3 * PAGE) == 0);
  CHECK(munmap(p, 3 * PAGE) == 0);
  CHECK(mmap((void *)0x10000, PAGE, PROT_READ,
              MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        == MAP_FAILED && errno == EINVAL);
  CHECK(mmap(NULL, 0, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        == MAP_FAILED && errno == EINVAL);
  CHECK(mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS,
              -1, 0) == MAP_FAILED && errno == EINVAL);
  CHECK(mprotect(p + 1, PAGE, PROT_NONE) == -1 && errno == EINVAL);
  puts("mmap-test: partial-unmap-fixed-discard PASS");
  return 0;
}

static int shared_test(void) {
  volatile int *shared =
      anonymous(2 * PAGE, PROT_READ | PROT_WRITE, MAP_SHARED);
  volatile int *private =
      anonymous(PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  CHECK((void *)shared != MAP_FAILED && (void *)private != MAP_FAILED);
  private[0] = 10;
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    if (shared[0] != 0) _exit(1);
    shared[0] = 42;
    shared[PAGE / sizeof(int)] = 43;
    private[0] = 20;
    _exit(0);
  }
  CHECK(wait_status(child, 0));
  CHECK(shared[0] == 42 && shared[PAGE / sizeof(int)] == 43 &&
        private[0] == 10);
  CHECK(madvise((void *)shared, PAGE, MADV_DONTNEED) == -1 &&
        errno == ENOTSUP);
  CHECK(munmap((void *)shared, 2 * PAGE) == 0);
  CHECK(munmap((void *)private, PAGE) == 0);

  int fd = shm_create(3 * PAGE);
  CHECK(fd >= 0);
  unsigned char *legacy = shm_map(fd);
  unsigned char *a = mmap(NULL, 2 * PAGE, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, PAGE);
  unsigned char *b = mmap(NULL, PAGE, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 2 * PAGE);
  CHECK(legacy != MAP_FAILED && a != MAP_FAILED && b != MAP_FAILED);
  CHECK(close(fd) == 0);
  a[PAGE] = 77;
  CHECK(b[0] == 77 && legacy[2 * PAGE] == 77);
  CHECK(munmap(a, PAGE) == 0);
  child = fork();
  CHECK(child >= 0);
  if (!child) { b[0] = 88; _exit(0); }
  CHECK(wait_status(child, 0));
  CHECK(a[PAGE] == 88 && b[0] == 88);
  CHECK(shm_unmap(legacy) == 0);
  CHECK(munmap(a + PAGE, PAGE) == 0);
  CHECK(munmap(b, PAGE) == 0);
  puts("mmap-test: fork-private-shared-alias PASS");
  return 0;
}

static int file_test(void) {
  int fd = open("/config/startup.sh", O_RDONLY);
  CHECK(fd >= 0);
  unsigned char expected;
  CHECK(read(fd, &expected, 1) == 1);
  unsigned char *p = mmap(NULL, 2 * PAGE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE, fd, 0);
  CHECK(p != MAP_FAILED);
  unsigned char next;
  CHECK(read(fd, &next, 1) == 1);
  CHECK(close(fd) == 0);
  CHECK(p[0] == expected && p[1] == next);
  fd = open("/config/startup.sh", O_RDONLY);
  CHECK(fd >= 0);
  CHECK(mmap(NULL, PAGE, PROT_READ, MAP_SHARED, fd, 0) == MAP_FAILED &&
        errno == ENOTSUP);
  CHECK(mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE, fd, 1) == MAP_FAILED &&
        errno == EINVAL);
  CHECK(close(fd) == 0);
  p[0] = 'X';
  CHECK(madvise(p, PAGE, MADV_DONTNEED) == 0 && p[0] == expected);
  CHECK(p[PAGE - 1] == 0);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) { unsigned char value = *(volatile unsigned char *)(p + PAGE);
    _exit(value); }
  CHECK(wait_status(child, 135));
  CHECK(munmap(p, 2 * PAGE) == 0);

  /* Une lecture kernel dans un buffer mmap non resident doit aussi fault-in. */
  p = anonymous(PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  CHECK(p != MAP_FAILED);
  fd = open("/config/startup.sh", O_RDONLY);
  CHECK(fd >= 0);
  CHECK(read(fd, p, 1) == 1 && p[0] == expected);
  CHECK(close(fd) == 0);
  CHECK(munmap(p, PAGE) == 0);

  fd = open("/bin/mmap-test", O_RDONLY);
  CHECK(fd >= 0);
  unsigned char chunk[64];
  for (size_t i = 0; i < PAGE / sizeof(chunk); i++) {
    CHECK(read(fd, chunk, sizeof(chunk)) == sizeof(chunk));
  }
  CHECK(read(fd, &expected, 1) == 1);
  p = mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE, fd, PAGE);
  CHECK(p != MAP_FAILED && close(fd) == 0);
  CHECK(p[0] == expected);
  CHECK(munmap(p, PAGE) == 0);
  puts("mmap-test: file-private-and-kernel-copy PASS");
  return 0;
}

static int lifecycle_test(void) {
  void *p = anonymous(PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  CHECK(p != MAP_FAILED);
  /* Adresse transmise sans dependance a strtoull. */
  char text[17];
  uint64_t address = (uint64_t)p;
  for (int i = 0; i < 16; i++) {
    text[15 - i] = "0123456789abcdef"[(address >> (4 * i)) & 15];
  }
  text[16] = 0;
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    char *args[] = {"/bin/mmap-test", "exec-probe", text, NULL};
    execve(args[0], args, NULL);
    _exit(1);
  }
  CHECK(wait_status(child, 139));
  CHECK(munmap(p, PAGE) == 0);
  vm_info_t before, after;
  CHECK(vminfo(&before) == 0);
  for (int i = 0; i < 4; i++) {
    child = fork();
    CHECK(child >= 0);
    if (!child) {
      volatile unsigned char *q =
          anonymous(256 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
      if ((void *)q == MAP_FAILED) _exit(1);
      for (int j = 0; j < 256; j++) q[j * PAGE] = 1;
      if (mprotect((void *)q, 256 * PAGE, PROT_NONE)) _exit(1);
      _exit(0);
    }
    CHECK(wait_status(child, 0));
  }
  CHECK(vminfo(&after) == 0 && before.physical_free == after.physical_free);
  puts("mmap-test: exec-and-exit-cleanup PASS");
  return 0;
}

static int ipc_mapping_test(void) {
  int listener = ipc_listen("vm.mapping.test");
  CHECK(listener >= 0);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    close(listener);
    int connection = ipc_connect("vm.mapping.test");
    if (connection < 0) _exit(1);
    ipc_message_t *message =
        anonymous(PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE);
    if (message == MAP_FAILED || ipc_receive(connection, message, 2000) != 1 ||
        message->attachment_fd < 0) _exit(1);
    volatile int *data = mmap(NULL, PAGE, PROT_READ | PROT_WRITE,
                              MAP_SHARED, message->attachment_fd, 0);
    if ((void *)data == MAP_FAILED || close(message->attachment_fd) != 0 ||
        data[0] != 123) _exit(1);
    data[0] = 456;
    message->length = 1;
    message->data[0] = 1;
    message->attachment_fd = -1;
    if (ipc_send(connection, message) != 0) _exit(1);
    munmap((void *)data, PAGE);
    munmap(message, PAGE);
    close(connection);
    _exit(0);
  }
  int connection = ipc_accept(listener, 2000);
  CHECK(connection >= 0);
  /* Cree apres fork : le fd doit effectivement etre transfere via l'IPC. */
  int fd = shm_create(PAGE);
  CHECK(fd >= 0);
  volatile int *data = mmap(NULL, PAGE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
  CHECK((void *)data != MAP_FAILED);
  data[0] = 123;
  ipc_message_t message;
  memset(&message, 0, sizeof(message));
  message.length = 1;
  message.attachment_fd = fd;
  CHECK(ipc_send(connection, &message) == 0);
  CHECK(close(fd) == 0);
  CHECK(ipc_receive(connection, &message, 2000) == 1 && data[0] == 456);
  CHECK(wait_status(child, 0));
  CHECK(munmap((void *)data, PAGE) == 0);
  CHECK(close(connection) == 0 && close(listener) == 0);
  puts("mmap-test: ipc-shared-fd-transfer PASS");
  return 0;
}

static void errno_worker(void *argument) {
  (void)argument;
  int connection = ipc_connect("vm.errno.test");
  if (connection < 0) _exit(1);
  errno = EINVAL;
  ipc_message_t message;
  memset(&message, 0, sizeof(message));
  int *location = __errno_location();
  message.length = sizeof(location);
  message.attachment_fd = -1;
  memcpy(message.data, &location, sizeof(location));
  if (ipc_send(connection, &message) != 0) _exit(1);
  if (ipc_receive(connection, &message, 2000) != 1 || errno != EINVAL)
    _exit(1);
  message.length = 1;
  message.data[0] = 1;
  if (ipc_send(connection, &message) != 0) _exit(1);
  close(connection);
  _exit(0);
}

static int errno_thread_test(void) {
  extern int thread_create(void (*entry)(void *), void *stack, void *arg);
  int listener = ipc_listen("vm.errno.test");
  CHECK(listener >= 0);
  unsigned char *stack = anonymous(8 * PAGE, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE);
  CHECK(stack != MAP_FAILED);
  errno = EACCES;
  CHECK(thread_create(errno_worker, stack + 8 * PAGE, NULL) > 0);
  int connection = ipc_accept(listener, 2000);
  CHECK(connection >= 0);
  ipc_message_t message;
  CHECK(ipc_receive(connection, &message, 2000) == 1);
  int *worker_location;
  CHECK(message.length == sizeof(worker_location));
  memcpy(&worker_location, message.data, sizeof(worker_location));
  CHECK(worker_location != __errno_location() && errno == EACCES &&
        *worker_location == EINVAL);
  message.length = 1;
  message.attachment_fd = -1;
  CHECK(ipc_send(connection, &message) == 0);
  CHECK(ipc_receive(connection, &message, 2000) == 1 && message.data[0] == 1);
  CHECK(close(connection) == 0 && close(listener) == 0);
  /* La stack reste vivante jusqu'a la sortie effective du worker. */
  puts("mmap-test: native-thread-errno PASS");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "exec-probe") == 0) {
    uint64_t address = 0;
    for (int i = 0; i < 16; i++) {
      char c = argv[2][i];
      address = (address << 4) | (c <= '9' ? c - '0' : c - 'a' + 10);
    }
    unsigned char value = *(volatile unsigned char *)address;
    return value;
  }
  /* Initialiser errno avant de mesurer les allocations physiques. */
  errno = 0;
  CHECK(sysconf(_SC_PAGESIZE) == PAGE);
  if (reservation_test() || protection_test() || partial_test() ||
      shared_test() || file_test() || lifecycle_test() || ipc_mapping_test() ||
      errno_thread_test()) return 1;
  puts("mmap-test: ALL PASS");
  return 0;
}
