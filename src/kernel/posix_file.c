#include "posix_file.h"
#include "process.h"
#include "uaccess.h"
#include "../fs/pipe.h"
#include "../fs/vfs.h"
#include "../mm/kheap.h"
#include "../include/errno.h"

int64_t posix_fsync(int fd) {
    process_t* process = process_current();
    if (!process) return -ESRCH;
    open_file_description_t* d = file_table_acquire(process->fd_table, fd);
    int result = file_description_sync(d);
    file_description_release(d);
    return result;
}

int64_t posix_pread(int fd, void* buffer, uint64_t count, int64_t offset) {
    process_t* process = process_current();
    if (!process) return -ESRCH;
    open_file_description_t* d = file_table_acquire(process->fd_table, fd);
    int64_t result = file_description_pread_user(d, buffer, count, offset);
    file_description_release(d);
    return result;
}

int64_t posix_pwrite(int fd, const void* buffer, uint64_t count, int64_t offset) {
    process_t* process = process_current();
    if (!process) return -ESRCH;
    open_file_description_t* d = file_table_acquire(process->fd_table, fd);
    int64_t result = file_description_pwrite_user(d, buffer, count, offset);
    file_description_release(d);
    return result;
}

int64_t posix_ftruncate(int fd, int64_t length) {
    process_t* process = process_current();
    if (!process) return -ESRCH;
    open_file_description_t* d = file_table_acquire(process->fd_table, fd);
    int64_t result = file_description_resize(d, length);
    file_description_release(d);
    return result;
}

int64_t posix_pipe2(int* output, int flags) {
    if (!user_range_valid(output, 2 * sizeof(int), true)) return -EFAULT;
    process_t* process = process_current();
    if (!process) return -ESRCH;
    int fds[2];
    int error = file_table_pipe(process->fd_table, fds, flags);
    if (error) return error;
    if (copy_to_user(output, fds, sizeof(fds))) {
        file_table_close(process->fd_table, fds[0]);
        file_table_close(process->fd_table, fds[1]);
        return -EFAULT;
    }
    return 0;
}

static int path_operation(const char* path, int mode, int directory) {
    char* paths = kmalloc(2 * VFS_MAX_PATH);
    if (!paths) return -ENOMEM;
    int error = 0;
    size_t length;
    for (length = 0; length < VFS_MAX_PATH; length++) {
        if (!path || copy_from_user(paths + length, path + length, 1)) {
            error = -EFAULT; break;
        }
        if (!paths[length]) break;
    }
    if (!error && length == VFS_MAX_PATH) error = -ENAMETOOLONG;
    if (!error && !paths[0]) error = -ENOENT;
    const char* absolute = paths;
    if (!error && paths[0] != '/') {
        if (process_resolve_path(paths, paths + VFS_MAX_PATH, VFS_MAX_PATH))
            error = -ENAMETOOLONG;
        else absolute = paths + VFS_MAX_PATH;
    }
    if (!error) error = directory
        ? vfs_create_checked(absolute, VFS_DIRECTORY, mode)
        : vfs_access(absolute, mode);
    kfree(paths);
    return error;
}

int64_t posix_access(const char* path, int mode) {
    return path_operation(path, mode, 0);
}

int64_t posix_mkdir(const char* path, uint32_t mode) {
    return path_operation(path, mode & 0777, 1);
}
