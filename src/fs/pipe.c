#include "pipe.h"
#include "../kernel/thread.h"
#include "../kernel/uaccess.h"
#include "../mm/kheap.h"
#include "../include/string.h"
#include "../include/errno.h"

#define PIPE_CAPACITY 4096
typedef struct pipe_state {
    spinlock_t lock;
    wait_queue_t changed;
    unsigned head, size, readers, writers;
    unsigned char bytes[PIPE_CAPACITY];
} pipe_state_t;

typedef struct pipe_wait {
    pipe_state_t *pipe;
    unsigned required;
    int write;
} pipe_wait_t;

typedef struct pipe_transfer {
    struct pipe_transfer *next;
    thread_t *thread;
    open_file_description_t *description;
    unsigned char bytes[PIPE_CAPACITY];
} pipe_transfer_t;
static pipe_transfer_t *transfers;
static spinlock_t transfers_lock;

static void transfer_remove(pipe_transfer_t *transfer) {
    uint64_t flags = spinlock_irqsave(&transfers_lock);
    pipe_transfer_t **link = &transfers;
    while (*link && *link != transfer) link = &(*link)->next;
    if (*link) *link = transfer->next;
    spinlock_irqrestore(&transfers_lock, flags);
}

void pipe_thread_cleanup(thread_t *thread) {
    uint64_t flags = spinlock_irqsave(&transfers_lock);
    pipe_transfer_t **link = &transfers;
    while (*link && (*link)->thread != thread) link = &(*link)->next;
    pipe_transfer_t *transfer = *link;
    if (transfer) *link = transfer->next;
    spinlock_irqrestore(&transfers_lock, flags);
    if (!transfer) return;
    pipe_state_t *p = transfer->description->vfs_node;
    wait_queue_remove(&p->changed, thread);
    if (thread->current_wait_queue == &p->changed)
        thread->current_wait_queue = NULL;
    /* La reference acquise par sys_read/sys_write ne sera plus restituee
     * lorsque la pile syscall est definitivement abandonnee. */
    file_description_release(transfer->description);
    kfree(transfer);
}

static bool pipe_ready(void *context) {
    pipe_wait_t *wait = context;
    pipe_state_t *p = wait->pipe;
    uint64_t flags = spinlock_irqsave(&p->lock);
    bool ready = wait->write ? (!p->readers ||
                   PIPE_CAPACITY - p->size >= wait->required)
                             : (p->size || !p->writers);
    spinlock_irqrestore(&p->lock, flags);
    return ready;
}

int file_table_pipe(file_descriptor_t table[MAX_FD], int output[2], int flags) {
    if (flags & ~(O_NONBLOCK | O_CLOEXEC)) return -EINVAL;
    pipe_state_t *p = kmalloc(sizeof(*p));
    if (!p) return -ENOMEM;
    memset(p, 0, sizeof(*p));
    wait_queue_init(&p->changed);
    p->readers = p->writers = 1;
    open_file_description_t *rd = file_description_create(
        FILE_TYPE_PIPE, O_RDONLY | (flags & O_NONBLOCK), p);
    open_file_description_t *wr = file_description_create(
        FILE_TYPE_PIPE, O_WRONLY | (flags & O_NONBLOCK), p);
    if (!rd || !wr) {
        kfree(rd); kfree(wr); kfree(p);
        return -ENOMEM;
    }
    int a = file_table_install_flags(table, rd, flags & O_CLOEXEC ? FD_CLOEXEC : 0);
    if (a < 0) {
        file_description_release(rd); file_description_release(wr);
        return a;
    }
    int b = file_table_install_flags(table, wr, flags & O_CLOEXEC ? FD_CLOEXEC : 0);
    if (b < 0) {
        file_table_close(table, a); file_description_release(wr);
        return b;
    }
    output[0] = a; output[1] = b;
    return 0;
}

void pipe_release(open_file_description_t *description) {
    pipe_state_t *p = description->vfs_node;
    uint64_t flags = spinlock_irqsave(&p->lock);
    if ((description->flags & O_ACCMODE) == O_WRONLY) p->writers--;
    else p->readers--;
    bool dead = !p->readers && !p->writers;
    spinlock_irqrestore(&p->lock, flags);
    wait_queue_wake_all(&p->changed);
    file_poll_notify();
    if (dead) kfree(p);
}

int pipe_poll(open_file_description_t *description, int events) {
    pipe_state_t *p = description->vfs_node;
    uint64_t flags = spinlock_irqsave(&p->lock);
    int result = 0;
    if ((description->flags & O_ACCMODE) == O_WRONLY) {
        if (!p->readers) result |= 8;
        else if (p->size < PIPE_CAPACITY) result |= events & 4;
    } else {
        if (p->size) result |= events & 1;
        if (!p->writers) result |= 16;
    }
    spinlock_irqrestore(&p->lock, flags);
    return result;
}

int pipe_bytes_available(open_file_description_t *description) {
    pipe_state_t *p = description->vfs_node;
    uint64_t flags = spinlock_irqsave(&p->lock);
    int count = p->size;
    spinlock_irqrestore(&p->lock, flags);
    return count;
}

static int vector_copy(const alos_iovec_t *vectors, int n, uint64_t offset,
                       unsigned char *bytes, unsigned count, int write) {
    for (int i = 0; i < n && count; ++i) {
        if (offset >= vectors[i].iov_len) {
            offset -= vectors[i].iov_len;
            continue;
        }
        uint64_t available = vectors[i].iov_len - offset;
        unsigned length = available < count ? available : count;
        void *buffer = (char *)vectors[i].iov_base + offset;
        int error = write ? copy_from_user(bytes, buffer, length)
                          : copy_to_user(buffer, bytes, length);
        if (error) return -EFAULT;
        bytes += length;
        count -= length;
        offset = 0;
    }
    return count ? -EINVAL : 0;
}

int64_t pipe_transfer_vector(open_file_description_t *description,
                             const alos_iovec_t *vectors, int n,
                             uint64_t count, int write) {
    if (((description->flags & O_ACCMODE) == O_WRONLY) != !!write) return -EBADF;
    if (count > INT64_MAX) return -EINVAL;
    if (!count) return 0;
    pipe_transfer_t *transfer = kmalloc(sizeof(*transfer));
    if (!transfer) return -ENOMEM;
    transfer->thread = thread_current();
    transfer->description = description;
    uint64_t registration_flags = spinlock_irqsave(&transfers_lock);
    transfer->next = transfers;
    transfers = transfer;
    spinlock_irqrestore(&transfers_lock, registration_flags);
    unsigned char *bounce = transfer->bytes;
    pipe_state_t *p = description->vfs_node;
    uint64_t done = 0;
    int error = 0;
    while (done < count) {
        unsigned length = count - done > PIPE_CAPACITY ? PIPE_CAPACITY : count - done;
        if (write && vector_copy(vectors, n, done, bounce, length, 1)) {
            error = -EFAULT; break;
        }
        pipe_wait_t wait = {p, write && count <= PIPE_CAPACITY ? length : 1, write};
        uint64_t flags = spinlock_irqsave(&p->lock);
        unsigned available = write ? PIPE_CAPACITY - p->size : p->size;
        if (write && !p->readers) {
            spinlock_irqrestore(&p->lock, flags);
            error = -EPIPE; break;
        }
        if (!available || (write && available < wait.required)) {
            bool eof = !write && !p->writers;
            spinlock_irqrestore(&p->lock, flags);
            if (eof) break;
            if (__atomic_load_n(&description->flags, __ATOMIC_RELAXED) & O_NONBLOCK) {
                error = -EAGAIN; break;
            }
            wait_queue_wait(&p->changed, pipe_ready, &wait);
            if (thread_current()->should_terminate) {
                wait_queue_remove(&p->changed, thread_current());
                error = -EINTR; break;
            }
            continue;
        }
        if (length > available) length = available;
        if (!write) {
            for (unsigned i = 0; i < length; i++)
                bounce[i] = p->bytes[(p->head + i) % PIPE_CAPACITY];
            /* La copie est faite avant de consommer les octets. */
            if (vector_copy(vectors, n, done, bounce, length, 0)) {
                spinlock_irqrestore(&p->lock, flags);
                error = -EFAULT; break;
            }
            p->head = (p->head + length) % PIPE_CAPACITY;
            p->size -= length;
        } else {
            for (unsigned i = 0; i < length; i++)
                p->bytes[(p->head + p->size + i) % PIPE_CAPACITY] = bounce[i];
            p->size += length;
        }
        spinlock_irqrestore(&p->lock, flags);
        done += length;
        wait_queue_wake_all(&p->changed);
        file_poll_notify();
        if (!write) break;
    }
    transfer_remove(transfer);
    kfree(transfer);
    return done ? (int64_t)done : error;
}

int64_t pipe_transfer_user(open_file_description_t *description, void *buffer,
                           uint64_t count, int write) {
    if (((description->flags & O_ACCMODE) == O_WRONLY) != !!write) return -EBADF;
    if (count > INT64_MAX) return -EINVAL;
    if (!count) return 0;
    if (!user_range_valid(buffer, count, !write)) return -EFAULT;
    alos_iovec_t vector = {buffer, count};
    return pipe_transfer_vector(description, &vector, 1, count, write);
}
