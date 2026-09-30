#include "native_poll.h"
#include "native_socket.h"
#include "native_network_cleanup.h"
#include "process.h"
#include "uaccess.h"
#include "../fs/pipe.h"
#include "../include/errno.h"
#include "../mm/kheap.h"

void native_poll_notify(void) { file_poll_notify(); }
wait_queue_t *native_poll_waitqueue(void) { return file_poll_waitqueue(); }

typedef struct {
    struct alos_pollfd *fds;
    open_file_description_t **files;
    uint64_t count;
    int ready;
    int error;
} poll_context_t;

static bool scan(void *opaque) {
    poll_context_t *ctx = opaque;
    ctx->ready = 0;
    ctx->error = 0;
    for (uint64_t i = 0; i < ctx->count; ++i) {
        struct alos_pollfd *p = &ctx->fds[i];
        open_file_description_t *d = ctx->files[i];
        short events = p->events;
        if (events & 0x40) events |= ALOS_POLLIN; /* POLLRDNORM */
        if (events & 0x100) events |= ALOS_POLLOUT; /* POLLWRNORM */
        p->revents = 0;
        if (p->fd < 0) continue;
        if (!d) p->revents = ALOS_POLLNVAL;
        else {
            int r = file_description_poll(d,events);
            if (r < 0) ctx->error = r;
            else p->revents = (short)r;
        }
        if (p->revents & ALOS_POLLIN) {
            if (p->events & 0x40) p->revents |= 0x40;
            if (!(p->events & ALOS_POLLIN)) p->revents &= ~ALOS_POLLIN;
        }
        if (p->revents & ALOS_POLLOUT) {
            if (p->events & 0x100) p->revents |= 0x100;
            if (!(p->events & ALOS_POLLOUT)) p->revents &= ~ALOS_POLLOUT;
        }
        if (p->revents) ++ctx->ready;
    }
    thread_t *t = thread_current();
    return ctx->ready || ctx->error || (t && t->should_terminate);
}
int64_t native_poll(void *output, uint64_t count, int timeout) {
    /* Limite explicite de ressources ; nfds peut depasser MAX_FD (doublons). */
    if (count > 1024) return -EINVAL;
    if (!user_range_valid(output,count * sizeof(struct alos_pollfd),true)) return -EFAULT;
    process_t *process = process_current();
    if (!process) return -ESRCH;
    size_t bytes = count * sizeof(struct alos_pollfd);
    struct alos_pollfd *fds = count ? kmalloc(bytes) : NULL;
    open_file_description_t **files = count ? kmalloc(count * sizeof(*files)) : NULL;
    if (count && (!fds || !files)) { kfree(fds); kfree(files); return -ENOMEM; }
    if (count && copy_from_user(fds,output,bytes)) { kfree(fds); kfree(files); return -EFAULT; }
    int64_t result = 0;
    uint64_t acquired = 0;
    for (; acquired < count; ++acquired) {
        struct alos_pollfd *p = &fds[acquired];
        open_file_description_t *d = p->fd < 0 ? NULL :
            file_table_acquire(process->fd_table,p->fd);
        files[acquired] = d;
        if (d && d->type != FILE_TYPE_PIPE && d->type != FILE_TYPE_SOCKET &&
            d->type != FILE_TYPE_UNIX_SOCKET &&
            d->type != FILE_TYPE_FILE &&
            !(d->type == FILE_TYPE_CONSOLE && (d->flags & O_ACCMODE) == O_WRONLY)) {
            result = -ENOTSUP; ++acquired; goto cleanup;
        }
    }
    native_network_resources_t held = {
        .descriptions = files, .description_count = acquired,
        .allocation1 = files, .allocation2 = fds, .poll_resources = true
    };
    native_network_resources_register(&held);
    poll_context_t ctx = {fds,files,count,0,0};
    if (!scan(&ctx) && timeout)
        wait_queue_wait_timeout(native_poll_waitqueue(),scan,&ctx,timeout < 0 ? 0 : (uint32_t)timeout);
    scan(&ctx);
    thread_t *t = thread_current();
    if (t && t->should_terminate) result = -EINTR;
    else if (ctx.error) result = ctx.error;
    else if (count && copy_to_user(output,fds,bytes)) result = -EFAULT;
    else result = ctx.ready;
    native_network_resources_unregister(&held);
cleanup:
    for (uint64_t i = 0; i < acquired; ++i) file_description_release(files[i]);
    kfree(files); kfree(fds); return result;
}
