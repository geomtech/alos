/* src/fs/file.c - Tables de descripteurs et descriptions ouvertes */
#include "file.h"
#include "vfs.h"
#include "pipe.h"
#include "../mm/kheap.h"
#include "../net/core/net.h"
#include "../net/l4/tcp.h"
#include "../include/string.h"
#include "../kernel/ipc.h"
#include "../kernel/shared_memory.h"
#include "../kernel/sync.h"
#include "../kernel/uaccess.h"
#include "../kernel/native_socket.h"
#include "../kernel/native_network_cleanup.h"
#include "../kernel/unix_socket.h"
#include "../kernel/native_epoll.h"
#include "../include/errno.h"

static spinlock_t table_lock;
/* Serialise aussi les ecritures append entre descriptions distinctes. */
static mutex_t file_io_lock = MUTEX_INIT;
static wait_queue_t poll_waiters;

wait_queue_t *file_poll_waitqueue(void) {
  return &poll_waiters;
}

void file_poll_notify(void) {
  wait_queue_wake_all(&poll_waiters);
}

int file_description_poll(open_file_description_t *description, short events) {
  if (!description) return 32; /* POLLNVAL */
  if (description->type == FILE_TYPE_PIPE) return pipe_poll(description, events);
  if (description->type == FILE_TYPE_SOCKET)
    return native_socket_poll(description, events);
  if (description->type == FILE_TYPE_UNIX_SOCKET)
    return unix_socket_poll(description, events);
  if (description->type == FILE_TYPE_EPOLL)
    return native_epoll_poll(description, events);
  if (description->type == FILE_TYPE_IPC)
    return ipc_poll(description->ipc_endpoint, events);
  if (description->type == FILE_TYPE_CONSOLE &&
      (description->flags & O_ACCMODE) == O_WRONLY)
    return events & 4;
  if (description->type != FILE_TYPE_FILE) return -ENOTSUP;
  uint32_t mode = description->flags & O_ACCMODE;
  int ready = 0;
  if (mode != O_WRONLY) ready |= events & 1;
  if (mode != O_RDONLY) ready |= events & 4;
  return ready;
}

static open_file_description_t console_stdin = {
    FILE_TYPE_CONSOLE, O_RDONLY, 0, {.vfs_node = NULL}, 0};
static open_file_description_t console_stdout = {
    FILE_TYPE_CONSOLE, O_WRONLY, 0, {.vfs_node = NULL}, 0};
static open_file_description_t console_stderr = {
    FILE_TYPE_CONSOLE, O_WRONLY, 0, {.vfs_node = NULL}, 0};

void file_description_retain(open_file_description_t *description) {
  if (description != NULL) {
    __atomic_add_fetch(&description->ref_count, 1, __ATOMIC_SEQ_CST);
  }
}

void file_description_release(open_file_description_t *description) {
  if (description == NULL) {
    return;
  }

  int refs =
      __atomic_sub_fetch(&description->ref_count, 1, __ATOMIC_SEQ_CST);
  if (refs > 0) {
    return;
  }

  if (description->type == FILE_TYPE_FILE &&
      description->vfs_node != NULL) {
    vfs_close((vfs_node_t *)description->vfs_node);
  } else if (description->type == FILE_TYPE_PIPE) {
    pipe_release(description);
  } else if (description->type == FILE_TYPE_SOCKET &&
             description->socket != NULL) {
    net_lock();
    tcp_close(description->socket);
    net_unlock();
  } else if (description->type == FILE_TYPE_IPC &&
             description->ipc_endpoint != NULL) {
    ipc_endpoint_release(description->ipc_endpoint);
  } else if (description->type == FILE_TYPE_UNIX_SOCKET) {
    unix_socket_release(description->unix_socket);
  } else if (description->type == FILE_TYPE_SHM &&
             description->shm_object != NULL) {
    shm_release(description->shm_object);
  } else if (description->type == FILE_TYPE_EPOLL &&
             description->epoll_set != NULL) {
    native_epoll_release(description->epoll_set);
  }

  if (description->type != FILE_TYPE_CONSOLE) {
    kfree(description);
  }
}

open_file_description_t *file_description_create(file_type_t type,
                                                 uint32_t flags,
                                                 void *resource) {
  open_file_description_t *description =
      (open_file_description_t *)kmalloc(sizeof(open_file_description_t));
  if (description == NULL) {
    return NULL;
  }

  description->type = type;
  description->flags = flags;
  description->position = 0;
  description->vfs_node = resource;
  description->ref_count = 1;
  return description;
}

void file_table_init(file_descriptor_t table[MAX_FD],
                     const file_descriptor_t parent[MAX_FD]) {
  memset(table, 0, sizeof(file_descriptor_t) * MAX_FD);

  if (parent != NULL) {
    uint64_t flags = spinlock_irqsave(&table_lock);
    for (int fd = 0; fd < MAX_FD; fd++) {
      table[fd] = parent[fd];
      file_description_retain(table[fd].description);
    }
    spinlock_irqrestore(&table_lock, flags);
    return;
  }

  table[FD_STDIN].description = &console_stdin;
  table[FD_STDOUT].description = &console_stdout;
  table[FD_STDERR].description = &console_stderr;
  file_description_retain(&console_stdin);
  file_description_retain(&console_stdout);
  file_description_retain(&console_stderr);
}

void file_table_destroy(file_descriptor_t table[MAX_FD]) {
  for (int fd = 0; fd < MAX_FD; fd++) {
    file_table_close(table, fd);
  }
}

void file_table_close_on_exec(file_descriptor_t table[MAX_FD]) {
  open_file_description_t *closed[MAX_FD] = {0};
  uint64_t flags = spinlock_irqsave(&table_lock);
  for (int fd = 0; fd < MAX_FD; fd++) {
    if ((table[fd].descriptor_flags & FD_CLOEXEC) != 0) {
      closed[fd] = table[fd].description;
      table[fd].description = NULL;
      table[fd].descriptor_flags = 0;
    }
  }
  spinlock_irqrestore(&table_lock, flags);
  for (int fd = 0; fd < MAX_FD; fd++) file_description_release(closed[fd]);
}

int file_table_install(file_descriptor_t table[MAX_FD],
                       open_file_description_t *description) {
  return file_table_install_flags(table, description, 0);
}

int file_table_install_flags(file_descriptor_t table[MAX_FD],
                             open_file_description_t *description,
                             uint32_t descriptor_flags) {
  if (table == NULL || description == NULL) return -EINVAL;
  if (descriptor_flags & ~FD_CLOEXEC) return -EINVAL;
  uint64_t flags = spinlock_irqsave(&table_lock);
  for (int fd = 0; fd < MAX_FD; fd++) {
    if (table[fd].description == NULL) {
      table[fd].description = description;
      table[fd].descriptor_flags = descriptor_flags & FD_CLOEXEC;
      spinlock_irqrestore(&table_lock, flags);
      file_poll_notify();
      return fd;
    }
  }
  spinlock_irqrestore(&table_lock, flags);
  return -EMFILE;
}

open_file_description_t *file_table_get(file_descriptor_t table[MAX_FD],
                                        int fd) {
  if (table == NULL || fd < 0 || fd >= MAX_FD) {
    return NULL;
  }
  return table[fd].description;
}

open_file_description_t *file_table_acquire(file_descriptor_t table[MAX_FD],
                                            int fd) {
  uint64_t flags = spinlock_irqsave(&table_lock);
  open_file_description_t *description = file_table_get(table, fd);
  file_description_retain(description);
  spinlock_irqrestore(&table_lock, flags);
  return description;
}

int file_table_close(file_descriptor_t table[MAX_FD], int fd) {
  uint64_t flags = spinlock_irqsave(&table_lock);
  if (file_table_get(table, fd) == NULL) {
    spinlock_irqrestore(&table_lock, flags);
    return -EBADF;
  }

  open_file_description_t *description = table[fd].description;
  table[fd].description = NULL;
  table[fd].descriptor_flags = 0;
  spinlock_irqrestore(&table_lock, flags);
  file_description_release(description);
  file_poll_notify();
  return 0;
}

static int duplicate_locked(file_descriptor_t table[MAX_FD], int fd,
                             int minimum, uint32_t descriptor_flags) {
  if (minimum < 0 || minimum >= MAX_FD) return -EINVAL;
  for (int target = minimum; target < MAX_FD; target++) {
    if (table[target].description == NULL) {
      file_description_retain(table[fd].description);
      table[target].description = table[fd].description;
      table[target].descriptor_flags = descriptor_flags;
      return target;
    }
  }
  return -EMFILE;
}

int file_table_dup(file_descriptor_t table[MAX_FD], int fd) {
  return file_table_fcntl(table, fd, F_DUPFD, 0);
}

int file_table_dup2(file_descriptor_t table[MAX_FD], int fd, int target) {
  uint64_t flags = spinlock_irqsave(&table_lock);
  open_file_description_t *description = file_table_get(table, fd);
  if (description == NULL || target < 0 || target >= MAX_FD) {
    spinlock_irqrestore(&table_lock, flags);
    return -EBADF;
  }
  if (fd == target) {
    spinlock_irqrestore(&table_lock, flags);
    return target;
  }
  open_file_description_t *previous = table[target].description;
  file_description_retain(description);
  table[target].description = description;
  table[target].descriptor_flags = 0;
  spinlock_irqrestore(&table_lock, flags);
  file_description_release(previous);
  return target;
}

int file_table_fcntl(file_descriptor_t table[MAX_FD], int fd, int command,
                     int argument) {
  uint64_t flags = spinlock_irqsave(&table_lock);
  open_file_description_t *description = file_table_get(table, fd);
  int result;
  if (description == NULL) {
    result = -EBADF;
  } else {
    switch (command) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
      result = duplicate_locked(table, fd, argument,
                                 command == F_DUPFD ? 0 : FD_CLOEXEC);
      break;
    case F_GETFD:
      result = (int)table[fd].descriptor_flags;
      break;
    case F_SETFD:
      if (argument & ~FD_CLOEXEC) result = -EINVAL;
      else {
        table[fd].descriptor_flags = (uint32_t)argument;
        result = 0;
      }
      break;
    case F_GETFL:
      result = (int)__atomic_load_n(&description->flags, __ATOMIC_RELAXED);
      break;
    case F_SETLK:
    case F_GETLK:
    case F_SETLKW:
      result = -ENOTSUP;
      break;
    case F_SETFL:
      if (argument & ~(O_ACCMODE | O_APPEND | O_NONBLOCK | O_SYNC)) {
        result = -ENOTSUP;
      } else if (description->type != FILE_TYPE_FILE &&
                 description->type != FILE_TYPE_PIPE &&
                 description->type != FILE_TYPE_UNIX_SOCKET) {
        result = -ENOTSUP;
      } else if (description->type == FILE_TYPE_UNIX_SOCKET &&
                 (argument & (O_APPEND | O_SYNC))) {
        result = -ENOTSUP;
      } else {
        uint32_t access = description->flags & (O_ACCMODE | O_SYNC);
        __atomic_store_n(&description->flags,
                           access | ((uint32_t)argument & (O_APPEND | O_NONBLOCK)),
                           __ATOMIC_RELAXED);
        result = 0;
      }
      break;
    default:
      result = -EINVAL;
      break;
    }
  }
  spinlock_irqrestore(&table_lock, flags);
  return result;
}

int file_table_open(file_descriptor_t table[MAX_FD], const char *path,
                    uint32_t flags) {
  return file_table_open_mode(table, path, flags, 0644);
}

int file_table_open_mode(file_descriptor_t table[MAX_FD], const char *path,
                         uint32_t flags, uint32_t mode) {
  return file_table_open_at_mode(table, NULL, path, flags, mode);
}

int file_table_open_at_mode(file_descriptor_t table[MAX_FD], vfs_node_t *anchor,
                            const char *path, uint32_t flags, uint32_t mode) {
  uint32_t supported = O_ACCMODE | O_APPEND | O_NONBLOCK | O_CLOEXEC |
                       O_DIRECTORY | O_CREAT | O_EXCL | O_TRUNC | O_SYNC | O_NOFOLLOW;
  if ((flags & O_ACCMODE) == O_ACCMODE) return -EINVAL;
  if (flags & ~supported) return -ENOTSUP;
  if ((flags & O_EXCL) && !(flags & O_CREAT)) return -EINVAL;
  if ((flags & O_CREAT) && (flags & O_DIRECTORY)) return -EINVAL;
  if ((flags & O_TRUNC) && (flags & O_ACCMODE) == O_RDONLY) return -EINVAL;
  if (flags & O_CREAT) {
    int created = vfs_create_at(anchor, path, VFS_FILE, mode);
    if (created && (created != -EEXIST || (flags & O_EXCL))) return created;
  }
  uint32_t vfs_flags = flags & O_ACCMODE;
  if (flags & O_APPEND) vfs_flags |= VFS_O_APPEND;
  if (flags & O_NOFOLLOW) vfs_flags |= O_NOFOLLOW;
  vfs_node_t *node = NULL;
  int error = vfs_open_at_checked(anchor, path, vfs_flags, &node);
  if (error) return error;
  if ((flags & O_DIRECTORY) && node->type != VFS_DIRECTORY)
    error = -ENOTDIR;
  else if (node->type == VFS_DIRECTORY && (flags & O_ACCMODE) != O_RDONLY)
    error = -EISDIR;
  if (error) { vfs_close(node); return error; }
  open_file_description_t *description = file_description_create(
      FILE_TYPE_FILE, flags & (O_ACCMODE | O_APPEND | O_NONBLOCK | O_SYNC), node);
  if (!description) { vfs_close(node); return -ENOMEM; }
  native_network_resources_t held = {.description = description};
  native_network_resources_register(&held);
  if (!error && (flags & O_TRUNC)) {
    if (!node->truncate) error = -ENOTSUP;
    else if (mutex_lock(&file_io_lock)) error = -EIO;
    else {
      error = node->truncate(node);
      mutex_unlock(&file_io_lock);
    }
  }
  if (error) {
    native_network_resources_unregister(&held);
    file_description_release(description);
    return error;
  }
  native_network_resources_unregister(&held);
  int fd = file_table_install_flags(table, description,
                                    (flags & O_CLOEXEC) ? FD_CLOEXEC : 0);
  if (fd < 0) file_description_release(description);
  return fd;
}

int file_description_sync(open_file_description_t *description) {
  if (!description) return -EBADF;
  if (description->type != FILE_TYPE_FILE) return -EINVAL;
  vfs_node_t *node = description->vfs_node;
  if (!node || !node->sync) return -ENOTSUP;
  if (mutex_lock(&file_io_lock)) return -EIO;
  int result = node->sync(node);
  mutex_unlock(&file_io_lock);
  return result;
}

static int file_access_error(open_file_description_t *description, bool write) {
  if (description == NULL) return -EBADF;
  uint32_t mode = __atomic_load_n(&description->flags, __ATOMIC_RELAXED) &
                  O_ACCMODE;
  if (write ? mode == O_RDONLY : mode == O_WRONLY) return -EBADF;
  if (description->type != FILE_TYPE_FILE) return -ENOTSUP;
  vfs_node_t *node = description->vfs_node;
  if (node == NULL) return -EIO;
  if (node->type == VFS_DIRECTORY) return -EISDIR;
  if (node->type != VFS_FILE) return -ENOTSUP;
  return 0;
}

int file_statvfs_path(const char *path, struct statvfs *information) {
  if (mutex_lock(&file_io_lock)) return -EIO;
  int result = vfs_statvfs_path(path, information);
  mutex_unlock(&file_io_lock);
  return result;
}

int file_description_set_times(open_file_description_t *description,
                                uint32_t atime, uint32_t mtime, uint32_t ctime) {
  if (!description) return -EBADF;
  if (description->type != FILE_TYPE_FILE) return -EINVAL;
  vfs_node_t *node = description->vfs_node;
  if (!node || !node->set_times) return -ENOTSUP;
  if (mutex_lock(&file_io_lock)) return -EIO;
  int result = node->set_times(node, atime, mtime, ctime);
  mutex_unlock(&file_io_lock);
  return result;
}

static int64_t file_transfer_user(open_file_description_t *description,
                                  void *buffer, uint64_t count, bool write,
                                  bool positional, int64_t offset) {
  if (positional) {
    if (!description) return -EBADF;
    if (description->type != FILE_TYPE_FILE) return -ESPIPE;
    if (offset < 0) return -EINVAL;
    if (offset > UINT32_MAX) return -EOVERFLOW;
    if (write && count > UINT32_MAX - (uint64_t)offset) return -EOVERFLOW;
  }
  int error = file_access_error(description, write);
  if (error) return error;
  if (count > INT64_MAX) return -EINVAL;
  if (count == 0) return 0;
  if (!user_range_valid(buffer, (size_t)count, !write)) return -EFAULT;
  uint8_t *bounce = kmalloc(4096);
  if (bounce == NULL) return -ENOMEM;
  if (mutex_lock(&file_io_lock) != 0) {
    kfree(bounce);
    return -EIO;
  }
  vfs_node_t *node = description->vfs_node;
  uint32_t position = positional ? (uint32_t)offset : description->position;
  if (!positional && write &&
      (__atomic_load_n(&description->flags, __ATOMIC_RELAXED) & O_APPEND)) {
    struct stat metadata;
    error = vfs_stat_node(node, &metadata);
    if (!error && (metadata.st_size < 0 || metadata.st_size > UINT32_MAX))
      error = -EOVERFLOW;
    if (error) {
      mutex_unlock(&file_io_lock);
      kfree(bounce);
      return error;
    }
    position = (uint32_t)metadata.st_size;
  }
  uint64_t transferred = 0;
  if (count > INT32_MAX) count = INT32_MAX;
  while (transferred < count) {
    uint32_t length = (uint32_t)(count - transferred);
    if (length > 4096) length = 4096;
    if (write && length > UINT32_MAX - position) {
      length = UINT32_MAX - position;
      if (length == 0) { error = -EFBIG; break; }
    }
    uint8_t *user = (uint8_t *)buffer + transferred;
    if (write && copy_from_user(bounce, user, length) != 0) {
      error = -EFAULT;
      break;
    }
    int result = write
        ? vfs_write(node, position, length, bounce)
        : vfs_read(node, position, length, bounce);
    if (result < 0) {
      error = result == -1 ? -EIO : result;
      break;
    }
    if ((uint32_t)result > length) {
      error = -EIO;
      break;
    }
    if (write && result == 0) {
      error = -EIO;
      break;
    }
    if (!write && result > 0 &&
        copy_to_user(user, bounce, (size_t)result) != 0) {
      error = -EFAULT;
      break;
    }
    position += (uint32_t)result;
    transferred += (uint32_t)result;
    if ((uint32_t)result < length) break;
  }
  if (!positional && transferred) description->position = position;
  if (write && transferred && (description->flags & O_SYNC)) {
    int sync_error = node->sync ? node->sync(node) : -ENOTSUP;
    if (sync_error) { error = sync_error; transferred = 0; }
  }
  mutex_unlock(&file_io_lock);
  kfree(bounce);
  return transferred ? (int64_t)transferred : error;
}

int64_t file_description_read_user(open_file_description_t *description,
                                   void *buffer, uint64_t count) {
  if (description && description->type == FILE_TYPE_PIPE)
    return pipe_transfer_user(description, buffer, count, 0);
  if (description && description->type == FILE_TYPE_SOCKET)
    return native_socket_read(description, buffer, count, 0);
  if (description && description->type == FILE_TYPE_UNIX_SOCKET)
    return unix_socket_read(description, buffer, count, 0);
  return file_transfer_user(description, buffer, count, false, false, 0);
}

int64_t file_description_write_user(open_file_description_t *description,
                                    const void *buffer, uint64_t count) {
  if (description && description->type == FILE_TYPE_PIPE)
    return pipe_transfer_user(description, (void *)buffer, count, 1);
  if (description && description->type == FILE_TYPE_SOCKET)
    return native_socket_write(description, buffer, count, 0);
  if (description && description->type == FILE_TYPE_UNIX_SOCKET)
    return unix_socket_write(description, buffer, count, 0);
  return file_transfer_user(description, (void *)buffer, count, true, false, 0);
}

int64_t file_description_pread_user(open_file_description_t *description,
                                    void *buffer, uint64_t count, int64_t offset) {
  return file_transfer_user(description, buffer, count, false, true, offset);
}

int64_t file_description_pwrite_user(open_file_description_t *description,
                                     const void *buffer, uint64_t count,
                                     int64_t offset) {
  return file_transfer_user(description, (void *)buffer, count, true, true, offset);
}

int file_description_resize(open_file_description_t *description, int64_t length) {
  if (!description) return -EBADF;
  if (description->type != FILE_TYPE_FILE) return -EINVAL;
  int error = file_access_error(description, true);
  if (error) return error;
  if (length < 0) return -EINVAL;
  if (length > UINT32_MAX) return -EOVERFLOW;
  vfs_node_t *node = description->vfs_node;
  if (!node->resize) return -ENOTSUP;
  if (mutex_lock(&file_io_lock)) return -EIO;
  error = node->resize(node, (uint32_t)length);
  if (!error && (description->flags & O_SYNC))
    error = node->sync ? node->sync(node) : -ENOTSUP;
  mutex_unlock(&file_io_lock);
  return error;
}

int64_t file_description_seek(open_file_description_t *description,
                              int64_t offset, int whence) {
  if (description == NULL) return -EBADF;
  if (description->type != FILE_TYPE_FILE) return -ESPIPE;
  vfs_node_t *node = description->vfs_node;
  if (node == NULL) return -EIO;
  if (node->type == VFS_DIRECTORY) return -EISDIR;
  if (mutex_lock(&file_io_lock) != 0) return -EIO;
  int64_t base;
  if (whence == SEEK_SET) base = 0;
  else if (whence == SEEK_CUR) base = description->position;
  else if (whence == SEEK_END) {
    struct stat metadata;
    int error = vfs_stat_node(node, &metadata);
    if (!error && (metadata.st_size < 0 || metadata.st_size > UINT32_MAX))
      error = -EOVERFLOW;
    if (error) {
      mutex_unlock(&file_io_lock);
      return error;
    }
    base = metadata.st_size;
  }
  else {
    mutex_unlock(&file_io_lock);
    return -EINVAL;
  }
  int64_t result;
  if (offset < -base) result = -EINVAL;
  else if (offset > (int64_t)UINT32_MAX - base) result = -EOVERFLOW;
  else {
    result = base + offset;
    description->position = (uint32_t)result;
  }
  mutex_unlock(&file_io_lock);
  return result;
}

int file_description_readdir_user(open_file_description_t *description,
                                  void *output) {
  if (description == NULL) return -EBADF;
  vfs_node_t *node = description->type == FILE_TYPE_FILE
                         ? description->vfs_node : NULL;
  if (node == NULL || node->type != VFS_DIRECTORY) return -ENOTDIR;
  if ((description->flags & O_ACCMODE) == O_WRONLY) return -EBADF;
  if (!user_range_valid(output, sizeof(alos_dir_record_t), true)) return -EFAULT;
  if (mutex_lock(&file_io_lock) != 0) return -EIO;
  int result;
  if (description->position >= UINT32_MAX) {
    result = -EOVERFLOW;
  } else {
    vfs_dirent_t entry;
    result = vfs_readdir_checked(node, description->position, &entry);
    if (result == 1) {
      alos_dir_record_t record = {0};
      record.inode = entry.inode;
      record.next_offset = (uint64_t)description->position + 1;
      record.type = entry.type;
      memcpy(record.name, entry.name, sizeof(record.name));
      if (copy_to_user(output, &record, sizeof(record)) != 0)
        result = -EFAULT;
      else
        description->position++;
    }
  }
  mutex_unlock(&file_io_lock);
  return result;
}
