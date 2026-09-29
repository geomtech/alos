#include "internal/syscall.h"
#include <sys/display.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/syscall.h>

int ipc_listen(const char *name) {
  return (int)syscall1(SYS_IPC_LISTEN, (long)name);
}

int ipc_connect(const char *name) {
  return (int)syscall1(SYS_IPC_CONNECT, (long)name);
}

int ipc_accept(int listener_fd, uint32_t timeout_ms) {
  return (int)syscall2(SYS_IPC_ACCEPT, listener_fd, timeout_ms);
}

int ipc_send(int fd, const ipc_message_t *message) {
  return (int)syscall2(SYS_IPC_SEND, fd, (long)message);
}

int ipc_receive(int fd, ipc_message_t *message, uint32_t timeout_ms) {
  return (int)syscall3(SYS_IPC_RECV, fd, (long)message, timeout_ms);
}

int shm_create(size_t size) {
  return (int)syscall1(SYS_SHM_CREATE, (long)size);
}

void *shm_map(int fd) { return (void *)syscall1(SYS_SHM_MAP, fd); }

int shm_unmap(void *address) {
  return (int)syscall1(SYS_SHM_UNMAP, (long)address);
}

long shm_size(int fd) { return syscall1(SYS_SHM_SIZE, fd); }

int display_acquire(void) { return (int)syscall0(SYS_DISPLAY_ACQUIRE); }

int display_release(void) { return (int)syscall0(SYS_DISPLAY_RELEASE); }
