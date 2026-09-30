/* src/kernel/ipc.h - Canaux IPC locaux bornes */
#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct shm_object;
typedef struct ipc_endpoint ipc_endpoint_t;

#define IPC_NAME_MAX 31
#define IPC_MAX_PAYLOAD 2048
#define IPC_QUEUE_DEPTH 32
#define IPC_NONBLOCK 0xFFFFFFFFU

typedef struct {
  uint32_t length;
  uint32_t flags;
  uint32_t connection_id;
  int32_t attachment_fd;
  uint8_t data[IPC_MAX_PAYLOAD];
} ipc_user_message_t;

void ipc_init(void);
ipc_endpoint_t *ipc_listen(const char *name);
ipc_endpoint_t *ipc_connect(const char *name);
ipc_endpoint_t *ipc_accept(ipc_endpoint_t *listener, uint32_t timeout_ms);
int ipc_send(ipc_endpoint_t *endpoint, const void *data, uint32_t length,
             struct shm_object *attachment);
int ipc_receive(ipc_endpoint_t *endpoint, void *data, uint32_t capacity,
                uint32_t *length, struct shm_object **attachment,
                uint32_t timeout_ms);
uint32_t ipc_endpoint_id(ipc_endpoint_t *endpoint);
int ipc_pair(ipc_endpoint_t **first, ipc_endpoint_t **second);
int ipc_send_owned(ipc_endpoint_t *, const void *, uint32_t,
                   void *, void (*)(void *));
int ipc_receive_owned(ipc_endpoint_t *, void *, uint32_t, uint32_t *, void **);
bool ipc_read_ready(void *);
bool ipc_write_ready(void *);
bool ipc_peer_closed(ipc_endpoint_t *);
ipc_endpoint_t *ipc_peer_endpoint(ipc_endpoint_t *);
void ipc_visit_owned(ipc_endpoint_t *, void (*)(void *, void *), void *);
struct wait_queue *ipc_waitqueue(ipc_endpoint_t *);
void ipc_endpoint_retain(ipc_endpoint_t *endpoint);
void ipc_endpoint_release(ipc_endpoint_t *endpoint);

#endif
