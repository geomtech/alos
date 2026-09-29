/* src/kernel/ipc.c - Canaux IPC locaux bornes */
#include "ipc.h"
#include "../include/string.h"
#include "../mm/kheap.h"
#include "shared_memory.h"
#include "thread.h"

#define IPC_MAX_SERVICES 8

typedef struct ipc_packet {
  uint32_t length;
  shm_object_t *attachment;
  struct ipc_packet *next;
  uint8_t data[IPC_MAX_PAYLOAD];
} ipc_packet_t;

struct ipc_endpoint {
  volatile int ref_count;
  uint32_t connection_id;
  bool listener;
  bool closed;
  bool peer_closed;
  char service_name[IPC_NAME_MAX + 1];
  struct ipc_endpoint *peer;

  spinlock_t queue_lock;
  wait_queue_t wait_queue;
  ipc_packet_t *queue_head;
  ipc_packet_t *queue_tail;
  uint32_t queue_depth;

  struct ipc_endpoint *pending_head;
  struct ipc_endpoint *pending_tail;
  struct ipc_endpoint *accept_next;
};

typedef struct {
  char name[IPC_NAME_MAX + 1];
  ipc_endpoint_t *listener;
} ipc_service_t;

static ipc_service_t g_services[IPC_MAX_SERVICES];
static spinlock_t g_ipc_lock;
static uint32_t g_next_connection_id = 1;

static ipc_endpoint_t *endpoint_create(bool listener) {
  ipc_endpoint_t *endpoint = (ipc_endpoint_t *)kmalloc(sizeof(*endpoint));
  if (endpoint == NULL) {
    return NULL;
  }
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->ref_count = 1;
  endpoint->listener = listener;
  spinlock_init(&endpoint->queue_lock);
  wait_queue_init(&endpoint->wait_queue);
  return endpoint;
}

void ipc_init(void) {
  memset(g_services, 0, sizeof(g_services));
  spinlock_init(&g_ipc_lock);
  g_next_connection_id = 1;
}

void ipc_endpoint_retain(ipc_endpoint_t *endpoint) {
  if (endpoint != NULL) {
    __atomic_add_fetch(&endpoint->ref_count, 1, __ATOMIC_SEQ_CST);
  }
}

static void packet_destroy(ipc_packet_t *packet) {
  if (packet == NULL) {
    return;
  }
  if (packet->attachment != NULL) {
    shm_release(packet->attachment);
  }
  kfree(packet);
}

void ipc_endpoint_release(ipc_endpoint_t *endpoint) {
  if (endpoint == NULL ||
      __atomic_sub_fetch(&endpoint->ref_count, 1, __ATOMIC_SEQ_CST) > 0) {
    return;
  }

  uint64_t flags = spinlock_irqsave(&g_ipc_lock);
  endpoint->closed = true;

  if (endpoint->listener) {
    for (int i = 0; i < IPC_MAX_SERVICES; i++) {
      if (g_services[i].listener == endpoint) {
        memset(&g_services[i], 0, sizeof(g_services[i]));
        break;
      }
    }
  }

  ipc_endpoint_t *peer = endpoint->peer;
  endpoint->peer = NULL;
  if (peer != NULL) {
    peer->peer = NULL;
    peer->peer_closed = true;
    wait_queue_wake_all(&peer->wait_queue);
  }
  spinlock_irqrestore(&g_ipc_lock, flags);

  ipc_endpoint_t *pending = endpoint->pending_head;
  while (pending != NULL) {
    ipc_endpoint_t *next = pending->accept_next;
    pending->accept_next = NULL;
    ipc_endpoint_release(pending);
    pending = next;
  }

  ipc_packet_t *packet = endpoint->queue_head;
  while (packet != NULL) {
    ipc_packet_t *next = packet->next;
    packet_destroy(packet);
    packet = next;
  }
  kfree(endpoint);
}

ipc_endpoint_t *ipc_listen(const char *name) {
  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  ipc_endpoint_t *listener = endpoint_create(true);
  if (listener == NULL) {
    return NULL;
  }
  strncpy(listener->service_name, name, IPC_NAME_MAX);
  listener->service_name[IPC_NAME_MAX] = '\0';

  uint64_t flags = spinlock_irqsave(&g_ipc_lock);
  int free_slot = -1;
  for (int i = 0; i < IPC_MAX_SERVICES; i++) {
    if (g_services[i].listener != NULL &&
        strcmp(g_services[i].name, listener->service_name) == 0) {
      spinlock_irqrestore(&g_ipc_lock, flags);
      ipc_endpoint_release(listener);
      return NULL;
    }
    if (free_slot < 0 && g_services[i].listener == NULL) {
      free_slot = i;
    }
  }
  if (free_slot < 0) {
    spinlock_irqrestore(&g_ipc_lock, flags);
    ipc_endpoint_release(listener);
    return NULL;
  }

  strncpy(g_services[free_slot].name, listener->service_name, IPC_NAME_MAX);
  g_services[free_slot].name[IPC_NAME_MAX] = '\0';
  g_services[free_slot].listener = listener;
  spinlock_irqrestore(&g_ipc_lock, flags);
  return listener;
}

ipc_endpoint_t *ipc_connect(const char *name) {
  if (name == NULL) {
    return NULL;
  }

  ipc_endpoint_t *client = endpoint_create(false);
  ipc_endpoint_t *server = endpoint_create(false);
  if (client == NULL || server == NULL) {
    ipc_endpoint_release(client);
    ipc_endpoint_release(server);
    return NULL;
  }

  uint64_t flags = spinlock_irqsave(&g_ipc_lock);
  ipc_endpoint_t *listener = NULL;
  for (int i = 0; i < IPC_MAX_SERVICES; i++) {
    if (g_services[i].listener != NULL &&
        strcmp(g_services[i].name, name) == 0) {
      listener = g_services[i].listener;
      break;
    }
  }
  if (listener == NULL || listener->closed) {
    spinlock_irqrestore(&g_ipc_lock, flags);
    ipc_endpoint_release(client);
    ipc_endpoint_release(server);
    return NULL;
  }

  uint32_t connection_id = g_next_connection_id++;
  if (g_next_connection_id == 0) {
    g_next_connection_id = 1;
  }
  client->connection_id = connection_id;
  server->connection_id = connection_id;
  client->peer = server;
  server->peer = client;

  if (listener->pending_tail != NULL) {
    listener->pending_tail->accept_next = server;
  } else {
    listener->pending_head = server;
  }
  listener->pending_tail = server;
  wait_queue_wake_all(&listener->wait_queue);
  spinlock_irqrestore(&g_ipc_lock, flags);
  return client;
}

static bool listener_has_pending(void *context) {
  ipc_endpoint_t *listener = (ipc_endpoint_t *)context;
  return listener->pending_head != NULL || listener->closed;
}

ipc_endpoint_t *ipc_accept(ipc_endpoint_t *listener, uint32_t timeout_ms) {
  if (listener == NULL || !listener->listener) {
    return NULL;
  }
  if (timeout_ms == IPC_NONBLOCK && !listener_has_pending(listener)) {
    return NULL;
  }
  if (!wait_queue_wait_timeout(&listener->wait_queue, listener_has_pending,
                               listener,
                               timeout_ms == IPC_NONBLOCK ? 1 : timeout_ms)) {
    return NULL;
  }

  uint64_t flags = spinlock_irqsave(&g_ipc_lock);
  ipc_endpoint_t *endpoint = listener->pending_head;
  if (endpoint != NULL) {
    listener->pending_head = endpoint->accept_next;
    if (listener->pending_head == NULL) {
      listener->pending_tail = NULL;
    }
    endpoint->accept_next = NULL;
  }
  spinlock_irqrestore(&g_ipc_lock, flags);
  return endpoint;
}

int ipc_send(ipc_endpoint_t *endpoint, const void *data, uint32_t length,
             shm_object_t *attachment) {
  if (endpoint == NULL || endpoint->listener || data == NULL ||
      length == 0 || length > IPC_MAX_PAYLOAD) {
    return -1;
  }

  ipc_packet_t *packet = (ipc_packet_t *)kmalloc(sizeof(*packet));
  if (packet == NULL) {
    return -1;
  }
  memset(packet, 0, sizeof(*packet));
  packet->length = length;
  memcpy(packet->data, data, length);
  packet->attachment = attachment;
  if (attachment != NULL) {
    shm_retain(attachment);
  }

  uint64_t global_flags = spinlock_irqsave(&g_ipc_lock);
  ipc_endpoint_t *peer = endpoint->peer;
  if (endpoint->closed || peer == NULL || peer->closed) {
    spinlock_irqrestore(&g_ipc_lock, global_flags);
    packet_destroy(packet);
    return -1;
  }
  ipc_endpoint_retain(peer);
  spinlock_irqrestore(&g_ipc_lock, global_flags);

  uint64_t queue_flags = spinlock_irqsave(&peer->queue_lock);
  if (peer->queue_depth >= IPC_QUEUE_DEPTH) {
    spinlock_irqrestore(&peer->queue_lock, queue_flags);
    ipc_endpoint_release(peer);
    packet_destroy(packet);
    return -1;
  }
  if (peer->queue_tail != NULL) {
    peer->queue_tail->next = packet;
  } else {
    peer->queue_head = packet;
  }
  peer->queue_tail = packet;
  peer->queue_depth++;
  spinlock_irqrestore(&peer->queue_lock, queue_flags);
  wait_queue_wake_all(&peer->wait_queue);
  ipc_endpoint_release(peer);
  return 0;
}

static bool endpoint_has_message(void *context) {
  ipc_endpoint_t *endpoint = (ipc_endpoint_t *)context;
  return endpoint->queue_head != NULL || endpoint->peer_closed ||
         endpoint->closed;
}

int ipc_receive(ipc_endpoint_t *endpoint, void *data, uint32_t capacity,
                uint32_t *length, shm_object_t **attachment,
                uint32_t timeout_ms) {
  if (endpoint == NULL || endpoint->listener || data == NULL ||
      length == NULL || attachment == NULL) {
    return -1;
  }
  if (timeout_ms == IPC_NONBLOCK && !endpoint_has_message(endpoint)) {
    return 0;
  }
  if (!wait_queue_wait_timeout(&endpoint->wait_queue, endpoint_has_message,
                               endpoint,
                               timeout_ms == IPC_NONBLOCK ? 1 : timeout_ms)) {
    return 0;
  }

  uint64_t flags = spinlock_irqsave(&endpoint->queue_lock);
  ipc_packet_t *packet = endpoint->queue_head;
  if (packet == NULL) {
    bool disconnected = endpoint->peer_closed || endpoint->closed;
    spinlock_irqrestore(&endpoint->queue_lock, flags);
    return disconnected ? -1 : 0;
  }
  if (packet->length > capacity) {
    spinlock_irqrestore(&endpoint->queue_lock, flags);
    return -1;
  }

  endpoint->queue_head = packet->next;
  if (endpoint->queue_head == NULL) {
    endpoint->queue_tail = NULL;
  }
  endpoint->queue_depth--;
  spinlock_irqrestore(&endpoint->queue_lock, flags);

  memcpy(data, packet->data, packet->length);
  *length = packet->length;
  *attachment = packet->attachment;
  packet->attachment = NULL;
  packet_destroy(packet);
  return 1;
}

uint32_t ipc_endpoint_id(ipc_endpoint_t *endpoint) {
  return endpoint != NULL ? endpoint->connection_id : 0;
}
