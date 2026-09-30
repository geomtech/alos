/* Paires locales sans namespace : flux borne sur les files IPC existantes. */
#include "unix_socket.h"
#include "ipc.h"
#include "native_network_cleanup.h"
#include "process.h"
#include "uaccess.h"
#include "../include/errno.h"
#include "../include/string.h"
#include "../mm/kheap.h"

typedef struct {
  uint32_t count;
  open_file_description_t *files[ALOS_UNIX_MAX_RIGHTS];
} rights_t;

struct unix_socket {
  ipc_endpoint_t *endpoint;
  uint32_t length, offset;
  rights_t *rights;
  uint8_t data[IPC_MAX_PAYLOAD];
  bool walk_mark;
  struct unix_socket *walk_next, *visited_next;
  struct unix_socket *release_next;
};

static struct unix_socket *release_queue;
static bool releasing;

typedef struct {
  struct alos_socket_message message;
  alos_iovec_t vectors[ALOS_IOV_MAX];
  uint8_t data[IPC_MAX_PAYLOAD];
} transfer_t;

static file_descriptor_t *current_table(void) {
  process_t *process = process_current();
  return process ? process->fd_table : NULL;
}

static void rights_destroy(void *opaque) {
  rights_t *rights = opaque;
  if (!rights) return;
  for (uint32_t i = 0; i < rights->count; ++i)
    file_description_release(rights->files[i]);
  kfree(rights);
}

void unix_socket_release(struct unix_socket *socket) {
  if (!socket) return;
  preempt_disable();
  socket->release_next = release_queue;
  release_queue = socket;
  if (releasing) { preempt_enable(); return; }
  releasing = true;
  preempt_enable();
  /* Un DAG de droits peut etre profond : drainer sans recursion sur les
   * petites piles kernel. Les appels imbriques ne font qu'enfiler. */
  for (;;) {
    preempt_disable();
    socket = release_queue;
    if (!socket) {
      releasing = false;
      preempt_enable();
      break;
    }
    release_queue = socket->release_next;
    preempt_enable();
    rights_destroy(socket->rights);
    ipc_endpoint_release(socket->endpoint);
    kfree(socket);
  }
}

int64_t unix_socket_pair(int family, int type, int protocol, void *output) {
  if (family != 1) return -EAFNOSUPPORT;
  if ((type & ~(ALOS_SOCK_NONBLOCK | ALOS_SOCK_CLOEXEC)) != 1 || protocol)
    return -EPROTONOSUPPORT;
  if (!user_range_valid(output, 2 * sizeof(int), true)) return -EFAULT;
  file_descriptor_t *table = current_table();
  if (!table) return -ESRCH;
  struct unix_socket *a = kmalloc(sizeof(*a)), *b = kmalloc(sizeof(*b));
  if (!a || !b) { kfree(a); kfree(b); return -ENOMEM; }
  memset(a, 0, sizeof(*a));
  memset(b, 0, sizeof(*b));
  int error = ipc_pair(&a->endpoint, &b->endpoint);
  if (error) { kfree(a); kfree(b); return error; }
  uint32_t flags = O_RDWR | (type & ALOS_SOCK_NONBLOCK ? O_NONBLOCK : 0);
  open_file_description_t *first = file_description_create(FILE_TYPE_UNIX_SOCKET, flags, a);
  open_file_description_t *second = file_description_create(FILE_TYPE_UNIX_SOCKET, flags, b);
  if (!first || !second) {
    if (first) file_description_release(first); else unix_socket_release(a);
    if (second) file_description_release(second); else unix_socket_release(b);
    return -ENOMEM;
  }
  uint32_t descriptor_flags = type & ALOS_SOCK_CLOEXEC ? FD_CLOEXEC : 0;
  int pair[2];
  preempt_disable();
  pair[0] = file_table_install_flags(table, first, descriptor_flags);
  pair[1] = pair[0] < 0 ? -1 :
      file_table_install_flags(table, second, descriptor_flags);
  if (pair[0] < 0 || pair[1] < 0) error = -EMFILE;
  else if (copy_to_user(output, pair, sizeof(pair))) error = -EFAULT;
  if (error) {
    if (pair[0] >= 0) file_table_close(table, pair[0]);
    else file_description_release(first);
    if (pair[1] >= 0) file_table_close(table, pair[1]);
    else file_description_release(second);
  }
  preempt_enable();
  return error;
}

static int parse_message(transfer_t *transfer, const void *message, bool receiving,
                         uint64_t *length) {
  if (copy_from_user(&transfer->message, message, sizeof(transfer->message)))
    return -EFAULT;
  struct alos_socket_message *m = &transfer->message;
  if (receiving && !user_range_valid(message, sizeof(*m), true)) return -EFAULT;
  if (m->name || m->name_length) return -EOPNOTSUPP;
  if (m->vector_count > ALOS_IOV_MAX) return -EMSGSIZE;
  if (copy_from_user(transfer->vectors, m->vectors,
                     m->vector_count * sizeof(alos_iovec_t))) return -EFAULT;
  *length = 0;
  for (size_t i = 0; i < m->vector_count; ++i) {
    if (transfer->vectors[i].iov_len > INT64_MAX - *length) return -EINVAL;
    *length += transfer->vectors[i].iov_len;
  }
  return 0;
}

static int prepare_vectors(transfer_t *transfer, size_t length, bool receiving) {
  size_t position = 0;
  for (size_t i = 0; i < transfer->message.vector_count && position < length; ++i) {
    alos_iovec_t *v = &transfer->vectors[i];
    size_t n = v->iov_len < length - position ? v->iov_len : length - position;
    if (!user_range_valid(v->iov_base, n, receiving)) return -EFAULT;
    if (!receiving && copy_from_user(transfer->data + position, v->iov_base, n))
      return -EFAULT;
    position += n;
  }
  return 0;
}

static int parse_rights(transfer_t *transfer, rights_t *rights,
                        native_network_resources_t *held) {
  struct alos_socket_message *m = &transfer->message;
  if (!m->control_length) return 0;
  struct alos_socket_control control;
  if (m->control_length < sizeof(control)) return -EINVAL;
  if (copy_from_user(&control, m->control, sizeof(control))) return -EFAULT;
  if (control.level != ALOS_SOL_SOCKET || control.type != 1) return -EOPNOTSUPP;
  if (control.length < sizeof(control) || control.length > m->control_length ||
      (control.length - sizeof(control)) % sizeof(int)) return -EINVAL;
  if (m->control_length > ((control.length + 7) & ~(size_t)7)) return -EOPNOTSUPP;
  size_t count = (control.length - sizeof(control)) / sizeof(int);
  if (count > ALOS_UNIX_MAX_RIGHTS) return -EMSGSIZE;
  int files[ALOS_UNIX_MAX_RIGHTS];
  if (copy_from_user(files, (uint8_t *)m->control + sizeof(control),
                     count * sizeof(int))) return -EFAULT;
  file_descriptor_t *table = current_table();
  if (!table) return -ESRCH;
  for (size_t i = 0; i < count; ++i) {
    open_file_description_t *file = file_table_acquire(table, files[i]);
    if (!file) return -EBADF;
    rights->files[rights->count++] = file;
    held->description_count = rights->count;
  }
  return 0;
}

typedef struct {
  struct unix_socket *first, *last, *visited;
} rights_walk_t;

static void visit_rights(void *opaque, void *context) {
  rights_t *rights = opaque;
  rights_walk_t *walk = context;
  if (!rights) return;
  for (uint32_t i = 0; i < rights->count; ++i) {
    open_file_description_t *file = rights->files[i];
    if (file->type != FILE_TYPE_UNIX_SOCKET) continue;
    struct unix_socket *socket = file->unix_socket;
    if (socket->walk_mark) continue;
    socket->walk_mark = true;
    socket->visited_next = walk->visited;
    walk->visited = socket;
    socket->walk_next = NULL;
    if (walk->last) walk->last->walk_next = socket;
    else walk->first = socket;
    walk->last = socket;
  }
}

/* UP, preemption desactivee : seules les references fortes de droits comptent,
 * pas les pointeurs peer. Refuser les cycles evite un faux GC de sockets. */
static int check_rights_cycle(struct unix_socket *sender, rights_t *rights) {
  ipc_endpoint_t *receiver = ipc_peer_endpoint(sender->endpoint);
  if (!receiver) return -EPIPE;
  rights_walk_t walk = {0};
  visit_rights(rights, &walk);
  int error = 0;
  while (walk.first) {
    struct unix_socket *socket = walk.first;
    walk.first = socket->walk_next;
    if (!walk.first) walk.last = NULL;
    if (socket->endpoint == receiver) { error = -EOPNOTSUPP; break; }
    visit_rights(socket->rights, &walk);
    ipc_visit_owned(socket->endpoint, visit_rights, &walk);
  }
  while (walk.visited) {
    struct unix_socket *socket = walk.visited;
    walk.visited = socket->visited_next;
    socket->walk_mark = false;
    socket->walk_next = socket->visited_next = NULL;
  }
  return error;
}

static int64_t transfer_message(open_file_description_t *description,
                                void *message, int flags, bool receiving) {
  int allowed = receiving ? ALOS_MSG_DONTWAIT | ALOS_MSG_CMSG_CLOEXEC :
                            ALOS_MSG_DONTWAIT | ALOS_MSG_NOSIGNAL;
  if (flags & ~allowed) return -EOPNOTSUPP;
  transfer_t *transfer = kmalloc(sizeof(*transfer));
  rights_t *rights = receiving ? NULL : kmalloc(sizeof(*rights));
  if (!transfer || (!receiving && !rights)) {
    kfree(transfer); kfree(rights); return -ENOMEM;
  }
  if (rights) memset(rights, 0, sizeof(*rights));
  native_network_resources_t held = {
      .description = description, .allocation1 = transfer, .allocation2 = rights,
      .descriptions = rights ? rights->files : NULL
  };
  native_network_resources_register(&held);
  uint64_t length = 0;
  int64_t result = parse_message(transfer, message, receiving, &length);
  if (result) goto done;
  if (!receiving) {
    result = parse_rights(transfer, rights, &held);
    if (result) goto done;
  }
  if (!length) {
    result = rights && rights->count ? -EINVAL : 0;
    goto done;
  }
  size_t capacity = length < IPC_MAX_PAYLOAD ? length : IPC_MAX_PAYLOAD;
  result = prepare_vectors(transfer, capacity, receiving);
  if (result) goto done;
  struct alos_socket_message *m = &transfer->message;
  if (receiving && m->control_length &&
      !user_range_valid(m->control, m->control_length < 80 ? m->control_length : 80, true)) {
    result = -EFAULT; goto done;
  }
  struct unix_socket *socket = description->unix_socket;
  for (;;) {
    if (thread_current()->should_terminate) { result = -EINTR; break; }
    if (receiving) {
      result = prepare_vectors(transfer, capacity, true);
      if (result || !user_range_valid(message, sizeof(*m), true) ||
          (m->control_length && !user_range_valid(m->control,
              m->control_length < 80 ? m->control_length : 80, true))) {
        result = -EFAULT;
        break;
      }
    }
    preempt_disable();
    if (!receiving) {
      result = check_rights_cycle(socket, rights);
      if (!result)
        result = ipc_send_owned(socket->endpoint, transfer->data, capacity, rights, rights_destroy);
      if (!result) {
        held.description_count = 0;
        held.allocation2 = NULL;
        rights = NULL;
        result = capacity;
        preempt_enable();
        break;
      }
      preempt_enable();
      if (result != -EAGAIN || (flags & ALOS_MSG_DONTWAIT) ||
          (description->flags & O_NONBLOCK)) break;
      wait_queue_wait(ipc_waitqueue(socket->endpoint), ipc_write_ready, socket->endpoint);
      continue;
    }
    if (socket->offset == socket->length) {
      void *context;
      result = ipc_receive_owned(socket->endpoint, socket->data, IPC_MAX_PAYLOAD,
                                  &socket->length, &context);
      if (result == 1) {
        socket->offset = 0;
        socket->rights = context;
      } else {
        preempt_enable();
        if (result == -EPIPE) { result = 0; break; }
        if (result < 0) break;
        if ((flags & ALOS_MSG_DONTWAIT) || (description->flags & O_NONBLOCK)) {
          result = -EAGAIN; break;
        }
        wait_queue_wait(ipc_waitqueue(socket->endpoint), ipc_read_ready, socket->endpoint);
        continue;
      }
    }
    size_t n = socket->length - socket->offset;
    if (n > capacity) n = capacity;
    rights_t *incoming = socket->rights;
    size_t deliver = incoming ? incoming->count : 0;
    if (m->control_length < sizeof(struct alos_socket_control)) deliver = 0;
    else if (deliver > (m->control_length - sizeof(struct alos_socket_control)) / sizeof(int))
      deliver = (m->control_length - sizeof(struct alos_socket_control)) / sizeof(int);
    struct {
      struct alos_socket_control header;
      int files[ALOS_UNIX_MAX_RIGHTS];
    } output;
    size_t installed = 0;
    rights_t *consumed = NULL;
    file_descriptor_t *table = current_table();
    result = 0;
    for (; installed < deliver; ++installed) {
      file_description_retain(incoming->files[installed]);
      int fd = file_table_install_flags(table, incoming->files[installed],
          flags & ALOS_MSG_CMSG_CLOEXEC ? FD_CLOEXEC : 0);
      if (fd < 0) {
        file_description_release(incoming->files[installed]);
        result = -EMFILE; break;
      }
      output.files[installed] = fd;
    }
    size_t position = 0;
    for (size_t i = 0; !result && i < m->vector_count && position < n; ++i) {
      size_t bytes = transfer->vectors[i].iov_len;
      if (bytes > n - position) bytes = n - position;
      if (copy_to_user(transfer->vectors[i].iov_base,
                       socket->data + socket->offset + position, bytes)) result = -EFAULT;
      position += bytes;
    }
    m->control_length = deliver ? sizeof(output.header) + deliver * sizeof(int) : 0;
    m->flags = incoming && deliver < incoming->count ? ALOS_MSG_CTRUNC : 0;
    output.header = (struct alos_socket_control){m->control_length, ALOS_SOL_SOCKET, 1};
    if (!result && deliver && copy_to_user(m->control, &output, m->control_length))
      result = -EFAULT;
    if (!result && copy_to_user(message, m, sizeof(*m))) result = -EFAULT;
    if (result) {
      for (size_t i = 0; i < installed; ++i) file_table_close(table, output.files[i]);
    } else {
      socket->offset += n;
      consumed = socket->rights;
      socket->rights = NULL;
      result = n;
    }
    preempt_enable();
    rights_destroy(consumed);
    file_poll_notify();
    break;
  }
done:
  native_network_resources_unregister(&held);
  rights_destroy(rights);
  kfree(transfer);
  return result;
}

static int64_t plain_transfer(open_file_description_t *description, void *buffer,
                              uint64_t count, int flags, bool receiving) {
  /* read/write ne fournissent pas d'enveloppe msghdr userland. */
  if (flags & ~(receiving ? ALOS_MSG_DONTWAIT : ALOS_MSG_DONTWAIT | ALOS_MSG_NOSIGNAL))
    return -EOPNOTSUPP;
  if (count > INT64_MAX) return -EINVAL;
  if (!count) return 0;
  size_t capacity = count < IPC_MAX_PAYLOAD ? count : IPC_MAX_PAYLOAD;
  if (!user_range_valid(buffer, capacity, receiving)) return -EFAULT;
  uint8_t *data = receiving ? NULL : kmalloc(capacity);
  if (!receiving && !data) return -ENOMEM;
  native_network_resources_t held = {.description = description, .allocation1 = data};
  native_network_resources_register(&held);
  int64_t result;
  if (!receiving && copy_from_user(data, buffer, capacity)) { result = -EFAULT; goto done; }
  struct unix_socket *socket = description->unix_socket;
  for (;;) {
    if (thread_current()->should_terminate) { result = -EINTR; break; }
    if (receiving && !user_range_valid(buffer, capacity, true)) {
      result = -EFAULT;
      break;
    }
    preempt_disable();
    if (!receiving) result = ipc_send_owned(socket->endpoint, data, capacity, NULL, NULL);
    else if (socket->offset == socket->length) {
      void *context;
      result = ipc_receive_owned(socket->endpoint, socket->data, IPC_MAX_PAYLOAD,
                                  &socket->length, &context);
      if (result == 1) { socket->offset = 0; socket->rights = context; }
    } else result = 1;
    if ((!receiving && !result) || (receiving && result == 1)) {
      rights_t *consumed = NULL;
      if (!receiving) result = capacity;
      else {
        size_t n = socket->length - socket->offset;
        if (n > capacity) n = capacity;
        if (copy_to_user(buffer, socket->data + socket->offset, n)) result = -EFAULT;
        else {
          socket->offset += n;
          consumed = socket->rights;
          socket->rights = NULL;
          result = n;
        }
      }
      preempt_enable();
      rights_destroy(consumed);
      file_poll_notify();
      break;
    }
    preempt_enable();
    if (receiving && result == -EPIPE) { result = 0; break; }
    if (receiving && !result) result = -EAGAIN;
    if (result != -EAGAIN || (flags & ALOS_MSG_DONTWAIT) ||
        (description->flags & O_NONBLOCK)) break;
    wait_queue_wait(ipc_waitqueue(socket->endpoint),
        receiving ? ipc_read_ready : ipc_write_ready, socket->endpoint);
  }
done:
  native_network_resources_unregister(&held);
  kfree(data);
  return result;
}

int64_t unix_socket_read(open_file_description_t *d, void *p, uint64_t n, int f) {
  return plain_transfer(d, p, n, f, true);
}
int64_t unix_socket_write(open_file_description_t *d, const void *p, uint64_t n, int f) {
  return plain_transfer(d, (void *)p, n, f, false);
}

int unix_socket_poll(open_file_description_t *description, short events) {
  struct unix_socket *socket = description->unix_socket;
  int ready = 0;
  if (socket->length != socket->offset || ipc_read_ready(socket->endpoint))
    ready |= events & ALOS_POLLIN;
  if (ipc_peer_closed(socket->endpoint)) ready |= ALOS_POLLHUP;
  else if (ipc_write_ready(socket->endpoint)) ready |= events & ALOS_POLLOUT;
  return ready;
}

int64_t unix_socket_call(open_file_description_t *description, uint64_t operation,
                         uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
  switch (operation) {
  case ALOS_SOCKET_SEND: return unix_socket_write(description, (void *)b, c, (int)d);
  case ALOS_SOCKET_RECV: return unix_socket_read(description, (void *)b, c, (int)d);
  case ALOS_SOCKET_SENDMSG: return transfer_message(description, (void *)b, (int)c, false);
  case ALOS_SOCKET_RECVMSG: return transfer_message(description, (void *)b, (int)c, true);
  case ALOS_SOCKET_GETOPT: {
    if (b != ALOS_SOL_SOCKET || (c != ALOS_SO_TYPE && c != ALOS_SO_ERROR))
      return -EOPNOTSUPP;
    uint32_t size;
    if (copy_from_user(&size, (void *)e, sizeof(size))) return -EFAULT;
    if (size < sizeof(int)) return -EINVAL;
    int value = c == ALOS_SO_TYPE ? 1 : 0;
    size = sizeof(value);
    if (copy_to_user((void *)d, &value, sizeof(value)) ||
        copy_to_user((void *)e, &size, sizeof(size))) return -EFAULT;
    return 0;
  }
  default: return -EOPNOTSUPP;
  }
}
