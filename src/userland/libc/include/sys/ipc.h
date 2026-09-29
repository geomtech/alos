#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#include <stdint.h>

#define IPC_NAME_MAX 31
#define IPC_MAX_PAYLOAD 2048
#define IPC_NONBLOCK 0xFFFFFFFFU

typedef struct {
  uint32_t length;
  uint32_t flags;
  uint32_t connection_id;
  int32_t attachment_fd;
  uint8_t data[IPC_MAX_PAYLOAD];
} ipc_message_t;

int ipc_listen(const char *name);
int ipc_connect(const char *name);
int ipc_accept(int listener_fd, uint32_t timeout_ms);
int ipc_send(int fd, const ipc_message_t *message);
int ipc_receive(int fd, ipc_message_t *message, uint32_t timeout_ms);

#endif
