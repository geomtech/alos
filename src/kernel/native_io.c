#include "native_io.h"
#include "native_socket.h"
#include "native_network_cleanup.h"
#include "process.h"
#include "uaccess.h"
#include "../include/native_io.h"
#include "../include/errno.h"
#include "../fs/pipe.h"
#include "../mm/kheap.h"
#include "../net/l4/tcp.h"
#include "../net/core/net.h"

int64_t native_vector_io(int fd, const void *vectors, int count, int write) {
    if (count < 0 || count > ALOS_IOV_MAX) return -EINVAL;
    process_t *process = process_current();
    if (!process) return -ESRCH;
    open_file_description_t *d = file_table_acquire(process->fd_table, fd);
    if (!d) return -EBADF;
    if ((write && (d->flags & O_ACCMODE) == O_RDONLY) ||
        (!write && (d->flags & O_ACCMODE) == O_WRONLY)) {
        file_description_release(d);
        return -EBADF;
    }
    int64_t result = 0;
    alos_iovec_t *iov = NULL;
    if (!count) goto release;
    iov = kmalloc((size_t)count * sizeof(*iov));
    if (!iov) { result = -ENOMEM; goto release; }
    if (copy_from_user(iov, vectors, (size_t)count * sizeof(*iov))) {
        result = -EFAULT; goto release;
    }
    uint64_t total = 0;
    for (int i = 0; i < count; ++i) {
        if (iov[i].iov_len > INT64_MAX - total) {
            result = -EINVAL; goto release;
        }
        total += iov[i].iov_len;
    }
    for (int i = 0; i < count; ++i) {
        if (iov[i].iov_len &&
            !user_range_valid(iov[i].iov_base, iov[i].iov_len, !write)) {
            result = -EFAULT; goto release;
        }
    }
    if (!total) goto release;
    /* Les transferts pipe ont leur propre reference abandonnee; les autres
     * transferts partagent cette reference avec les attentes socket imbriquees. */
    native_network_resources_t held = {
        .description = d->type == FILE_TYPE_PIPE ? NULL : d,
        .allocation1 = iov
    };
    native_network_resources_register(&held);
    if (d->type == FILE_TYPE_PIPE) {
        result = pipe_transfer_vector(d, iov, count, total, write);
    } else {
        for (int i = 0; i < count; ++i) {
            if (!iov[i].iov_len) continue;
            int64_t transferred;
            if (!write && d->type == FILE_TYPE_SOCKET) {
                transferred = native_socket_read(d, iov[i].iov_base,
                    iov[i].iov_len, result ? ALOS_MSG_DONTWAIT : 0);
            } else {
                transferred = write
                    ? file_description_write_user(d, iov[i].iov_base, iov[i].iov_len)
                    : file_description_read_user(d, iov[i].iov_base, iov[i].iov_len);
            }
            if (transferred <= 0) {
                if (!result) result = transferred;
                break;
            }
            result += transferred;
            if ((uint64_t)transferred < iov[i].iov_len ||
                (!write && d->type == FILE_TYPE_CONSOLE)) break;
        }
    }
    native_network_resources_unregister(&held);
release:
    kfree(iov);
    file_description_release(d);
    return result;
}

int64_t native_ioctl(int fd, uint64_t request, void *argument) {
    process_t *process = process_current();
    if (!process) return -ESRCH;
    open_file_description_t *d = file_table_acquire(process->fd_table, fd);
    if (!d) return -EBADF;
    int result = -ENOTTY;
    if (request == ALOS_FIONREAD) {
        int available;
        if (d->type == FILE_TYPE_PIPE) available = pipe_bytes_available(d);
        else if (d->type == FILE_TYPE_SOCKET) {
            net_lock();
            available = d->socket->recv_count;
            net_unlock();
        } else goto release;
        result = copy_to_user(argument, &available, sizeof(available)) ? -EFAULT : 0;
    }
release:
    file_description_release(d);
    return result;
}
