#include "path_ops.h"
#include "process.h"
#include "uaccess.h"
#include "native_network_cleanup.h"
#include "../fs/file.h"
#include "../fs/vfs.h"
#include "../include/errno.h"
#include "../include/fcntl.h"
#include "../include/path_abi.h"
#include "../include/string.h"
#include "../mm/kheap.h"

static int copy_path(char* output, const char* input) {
  if (!input) return -EFAULT;
  for (size_t i = 0; i < VFS_MAX_PATH; ++i) {
    if (copy_from_user(output + i, input + i, 1)) return -EFAULT;
    if (!output[i]) return i ? 0 : -ENOENT;
  }
  return -ENAMETOOLONG;
}

static int canonicalize(const char* input, char* output) {
  size_t used = 1;
  output[0] = '/';
  while (*input) {
    while (*input == '/') ++input;
    const char* start = input;
    while (*input && *input != '/') ++input;
    size_t size = (size_t)(input - start);
    if (!size || (size == 1 && start[0] == '.')) continue;
    if (size == 2 && start[0] == '.' && start[1] == '.') {
      while (used > 1 && output[used - 1] != '/') --used;
      if (used > 1) --used;
      continue;
    }
    if (used + (used > 1) + size >= VFS_MAX_PATH) return -ENAMETOOLONG;
    if (used > 1) output[used++] = '/';
    memcpy(output + used, start, size);
    used += size;
  }
  output[used] = 0;
  return 0;
}

int64_t native_path_call(int op, int dirfd, const char* user_path, uint64_t flags,
                         uint64_t argument, uint64_t count) {
  if (op < ALOS_PATH_OPEN || op > ALOS_PATH_NAME_MAX) return -EINVAL;
  if ((op == ALOS_PATH_OPEN || op == ALOS_PATH_STAT ||
       op == ALOS_PATH_UNLINK || op == ALOS_PATH_CHMOD) && flags > UINT32_MAX)
    return -EINVAL;
  process_t* process = process_current();
  if (!process) return -ESRCH;
  char* storage = kmalloc(4 * VFS_MAX_PATH);
  if (!storage) return -ENOMEM;
  char* path = storage;
  char* second = storage + VFS_MAX_PATH;
  char* output = storage + 2 * VFS_MAX_PATH;
  char* cwd = storage + 3 * VFS_MAX_PATH;
  int64_t result = copy_path(path, user_path);
  open_file_description_t* directory = NULL;
  vfs_node_t* anchor = NULL;
  if (result) goto release;
  if (op == ALOS_PATH_RENAME || op == ALOS_PATH_SYMLINK) {
    result = copy_path(second, (const char*)argument);
    if (result) goto release;
  }
  size_t cwd_length = 0;
  while (cwd_length < sizeof(process->cwd) && process->cwd[cwd_length]) ++cwd_length;
  if (cwd_length == sizeof(process->cwd)) { result = -EIO; goto release; }
  memcpy(cwd, process->cwd, cwd_length + 1);
  bool relative = path[0] != '/' ||
      (op == ALOS_PATH_RENAME && second[0] != '/');
  if (relative) {
    if (dirfd == AT_FDCWD) {
      vfs_node_t* node = NULL;
      result = vfs_open_checked(cwd, O_RDONLY, &node);
      if (result) goto release;
      directory = file_description_create(FILE_TYPE_FILE, O_RDONLY, node);
      if (!directory) { vfs_close(node); result = -ENOMEM; goto release; }
    } else directory = file_table_acquire(process->fd_table, dirfd);
    if (!directory) { result = -EBADF; goto release; }
    if (directory->type != FILE_TYPE_FILE ||
        ((vfs_node_t*)directory->vfs_node)->type != VFS_DIRECTORY) {
      result = -ENOTDIR;
      goto release;
    }
    anchor = directory->vfs_node;
  }
  native_network_resources_t held = {.description = directory, .allocation1 = storage};
  native_network_resources_register(&held);
  switch (op) {
  case ALOS_PATH_OPEN:
    result = file_table_open_at_mode(process->fd_table, anchor, path,
                                     (uint32_t)flags, (uint32_t)argument);
    break;
  case ALOS_PATH_STAT: {
    if (flags & ~AT_SYMLINK_NOFOLLOW) { result = -EINVAL; break; }
    struct stat metadata;
    result = vfs_stat_at(anchor, path, &metadata,
                         (flags & AT_SYMLINK_NOFOLLOW) ? ALOS_STAT_NOFOLLOW : 0);
    if (!result && copy_to_user((void*)argument, &metadata, sizeof(metadata))) result = -EFAULT;
    break;
  }
  case ALOS_PATH_UNLINK:
    result = vfs_unlink_at(anchor, path, (int)flags);
    break;
  case ALOS_PATH_REALPATH: {
    struct stat metadata;
    result = vfs_stat_at(anchor, path, &metadata, 0);
    if (result) break;
    const char* raw = path;
    if (path[0] != '/') {
      size_t path_length = strlen(path);
      if (cwd_length + 1 + path_length >= VFS_MAX_PATH) { result = -ENAMETOOLONG; break; }
      memcpy(second, cwd, cwd_length);
      second[cwd_length] = '/';
      memcpy(second + cwd_length + 1, path, path_length + 1);
      raw = second;
    }
    result = canonicalize(raw, output);
    if (!result && copy_to_user((void*)argument, output, strlen(output) + 1)) result = -EFAULT;
    break;
  }
  case ALOS_PATH_CHMOD: {
    struct stat metadata;
    result = vfs_stat_at(anchor, path, &metadata, 0);
    /* L'identite et les autorisations ne sont pas enforcees : ne pas vendre
     * une protection POSIX en ne changeant que des bits sur disque. */
    if (!result) result = -ENOTSUP;
    break;
  }
  case ALOS_PATH_SYMLINK:
    result = vfs_symlink_at(anchor, path, second);
    break;
  case ALOS_PATH_READLINK:
    if (!count) { result = -EINVAL; break; }
    result = vfs_readlink_at(anchor, path, output,
                             count < VFS_MAX_PATH ? (uint32_t)count : VFS_MAX_PATH);
    if (result >= 0 && copy_to_user((void*)argument, output, (size_t)result)) result = -EFAULT;
    break;
  case ALOS_PATH_RENAME:
    result = vfs_rename_at(anchor, path, second);
    break;
  case ALOS_PATH_NAME_MAX: {
    struct stat metadata;
    result = vfs_stat_at(anchor, path, &metadata, 0);
    if (!result) result = VFS_MAX_NAME;
    break;
  }
  }
  native_network_resources_unregister(&held);
release:
  file_description_release(directory);
  kfree(storage);
  return result;
}
