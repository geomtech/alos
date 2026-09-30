#include "native_epoll.h"
#include "process.h"
#include "thread.h"
#include "uaccess.h"
#include "../include/errno.h"
#include "../include/string.h"
#include "../mm/kheap.h"

#define EPOLL_MAX_WATCHES MAX_FD

typedef struct {
    int used;
    int fd;
    uint32_t events;
    uint32_t last_ready;
    uint64_t data;
    int disabled;
    open_file_description_t *description;
} epoll_entry_t;

struct epoll_set {
    spinlock_t lock;
    epoll_entry_t entries[EPOLL_MAX_WATCHES];
};

typedef struct {
    struct epoll_set *set;
    int maxevents;
} epoll_wait_context_t;

static int terminating(void) {
    thread_t *thread = thread_current();
    return thread && thread->should_terminate;
}

static int watchable(open_file_description_t *description) {
    if (!description) return 0;
    return description->type == FILE_TYPE_PIPE ||
           description->type == FILE_TYPE_SOCKET ||
           description->type == FILE_TYPE_UNIX_SOCKET;
}

static uint32_t ready_mask(epoll_entry_t *entry) {
    uint32_t interest = entry->events;
    short requested = 0;
    if (interest & (ALOS_EPOLLIN | ALOS_EPOLLRDNORM | ALOS_EPOLLRDHUP))
        requested |= 1;
    if (interest & (ALOS_EPOLLOUT | ALOS_EPOLLWRNORM))
        requested |= 4;
    if (interest & ALOS_EPOLLPRI) requested |= 2;

    int polled = file_description_poll(entry->description, requested);
    if (polled < 0) return ALOS_EPOLLERR;

    uint32_t ready = 0;
    if (polled & 1) {
        if (interest & ALOS_EPOLLIN) ready |= ALOS_EPOLLIN;
        if (interest & ALOS_EPOLLRDNORM) ready |= ALOS_EPOLLRDNORM;
    }
    if (polled & 4) {
        if (interest & ALOS_EPOLLOUT) ready |= ALOS_EPOLLOUT;
        if (interest & ALOS_EPOLLWRNORM) ready |= ALOS_EPOLLWRNORM;
    }
    if (polled & 2) {
        if (interest & ALOS_EPOLLPRI) ready |= ALOS_EPOLLPRI;
    }
    if (polled & 8) ready |= ALOS_EPOLLERR;
    if (polled & 16) {
        ready |= ALOS_EPOLLHUP;
        if (interest & ALOS_EPOLLRDHUP) ready |= ALOS_EPOLLRDHUP;
    }
    return ready;
}

static int scan_set(struct epoll_set *set, struct alos_epoll_event *output,
                    int maxevents, int consume) {
    int count = 0;
    uint64_t flags = spinlock_irqsave(&set->lock);
    for (int i = 0; i < EPOLL_MAX_WATCHES; ++i) {
        epoll_entry_t *entry = &set->entries[i];
        if (!entry->used) continue;
        if (count >= maxevents) break;

        uint32_t current = ready_mask(entry);
        uint32_t deliver = entry->disabled ? 0 : current;
        if (entry->events & ALOS_EPOLLET)
            deliver &= ~entry->last_ready;

        if (deliver) {
            if (output) {
                output[count].events = deliver;
                output[count].data = entry->data;
            }
            ++count;
            if (consume && (entry->events & ALOS_EPOLLONESHOT))
                entry->disabled = 1;
        }
        if (consume) entry->last_ready = current;
    }
    spinlock_irqrestore(&set->lock, flags);
    return count;
}

static bool epoll_ready(void *opaque) {
    epoll_wait_context_t *context = opaque;
    return scan_set(context->set, NULL, context->maxevents, 0) > 0 ||
           terminating();
}

static int create_epoll(int flags) {
    if (flags & ~ALOS_EPOLL_CLOEXEC) return -EINVAL;
    process_t *process = process_current();
    if (!process) return -ESRCH;

    struct epoll_set *set = kmalloc(sizeof(*set));
    if (!set) return -ENOMEM;
    memset(set, 0, sizeof(*set));
    spinlock_init(&set->lock);

    open_file_description_t *description =
        file_description_create(FILE_TYPE_EPOLL, O_RDONLY, set);
    if (!description) {
        kfree(set);
        return -ENOMEM;
    }
    int fd = file_table_install_flags(
        process->fd_table, description,
        flags & ALOS_EPOLL_CLOEXEC ? FD_CLOEXEC : 0);
    if (fd < 0) {
        file_description_release(description);
        return fd;
    }
    return fd;
}

static int acquire_epoll(int fd, open_file_description_t **holding,
                         struct epoll_set **set) {
    process_t *process = process_current();
    if (!process) return -ESRCH;
    open_file_description_t *description =
        file_table_acquire(process->fd_table, fd);
    if (!description) return -EBADF;
    if (description->type != FILE_TYPE_EPOLL || !description->epoll_set) {
        file_description_release(description);
        return -EINVAL;
    }
    *holding = description;
    *set = description->epoll_set;
    return 0;
}

static int ctl_epoll(int epfd, int operation, int fd,
                     const struct alos_epoll_event *user_event) {
    if (epfd == fd) return -EINVAL;
    if (operation != ALOS_EPOLL_CTL_ADD &&
        operation != ALOS_EPOLL_CTL_DEL &&
        operation != ALOS_EPOLL_CTL_MOD)
        return -EINVAL;

    open_file_description_t *epoll_description = NULL;
    struct epoll_set *set = NULL;
    int result = acquire_epoll(epfd, &epoll_description, &set);
    if (result) return result;

    process_t *process = process_current();
    open_file_description_t *target =
        file_table_acquire(process->fd_table, fd);
    if (!target) {
        file_description_release(epoll_description);
        return -EBADF;
    }
    if (!watchable(target)) {
        file_description_release(target);
        file_description_release(epoll_description);
        return target->type == FILE_TYPE_EPOLL ? -EINVAL : -EPERM;
    }

    struct alos_epoll_event event = {0};
    if (operation != ALOS_EPOLL_CTL_DEL) {
        if (!user_event || copy_from_user(&event, user_event, sizeof(event))) {
            file_description_release(target);
            file_description_release(epoll_description);
            return -EFAULT;
        }
        uint32_t unsupported = ALOS_EPOLLWAKEUP | ALOS_EPOLLEXCLUSIVE;
        uint32_t valid = ALOS_EPOLLIN | ALOS_EPOLLPRI | ALOS_EPOLLOUT |
                         ALOS_EPOLLERR | ALOS_EPOLLHUP | ALOS_EPOLLRDNORM |
                         ALOS_EPOLLWRNORM | ALOS_EPOLLRDHUP |
                         ALOS_EPOLLONESHOT | ALOS_EPOLLET | unsupported;
        if (event.events & ~valid) result = -EINVAL;
        else if (event.events & unsupported) result = -EOPNOTSUPP;
        if (result) {
            file_description_release(target);
            file_description_release(epoll_description);
            return result;
        }
    }

    open_file_description_t *release_after = NULL;
    uint64_t flags = spinlock_irqsave(&set->lock);
    int found = -1, free_slot = -1;
    for (int i = 0; i < EPOLL_MAX_WATCHES; ++i) {
        epoll_entry_t *entry = &set->entries[i];
        if (!entry->used) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (entry->fd == fd && entry->description == target) {
            found = i;
            break;
        }
    }

    if (operation == ALOS_EPOLL_CTL_ADD) {
        if (found >= 0) result = -EEXIST;
        else if (free_slot < 0) result = -ENOSPC;
        else {
            epoll_entry_t *entry = &set->entries[free_slot];
            entry->used = 1;
            entry->fd = fd;
            entry->events = event.events;
            entry->last_ready = 0;
            entry->data = event.data;
            entry->disabled = 0;
            entry->description = target;
            target = NULL; /* reference transferred to the set */
            result = 0;
        }
    } else if (operation == ALOS_EPOLL_CTL_MOD) {
        if (found < 0) result = -ENOENT;
        else {
            epoll_entry_t *entry = &set->entries[found];
            entry->events = event.events;
            entry->data = event.data;
            entry->last_ready = 0;
            entry->disabled = 0;
            result = 0;
        }
    } else {
        if (found < 0) result = -ENOENT;
        else {
            epoll_entry_t *entry = &set->entries[found];
            release_after = entry->description;
            memset(entry, 0, sizeof(*entry));
            result = 0;
        }
    }
    spinlock_irqrestore(&set->lock, flags);

    file_description_release(target);
    file_description_release(release_after);
    file_description_release(epoll_description);
    file_poll_notify();
    return result;
}

static int wait_epoll(int epfd, struct alos_epoll_event *user_events,
                      int maxevents, int timeout) {
    if (maxevents <= 0 || maxevents > 1024) return -EINVAL;
    if (!user_range_valid(user_events,
                          (size_t)maxevents * sizeof(struct alos_epoll_event),
                          true))
        return -EFAULT;

    open_file_description_t *epoll_description = NULL;
    struct epoll_set *set = NULL;
    int result = acquire_epoll(epfd, &epoll_description, &set);
    if (result) return result;

    struct alos_epoll_event *events =
        kmalloc((size_t)maxevents * sizeof(*events));
    if (!events) {
        file_description_release(epoll_description);
        return -ENOMEM;
    }

    epoll_wait_context_t context = {set, maxevents};
    if (!epoll_ready(&context) && timeout)
        wait_queue_wait_timeout(file_poll_waitqueue(), epoll_ready, &context,
                                timeout < 0 ? 0 : (uint32_t)timeout);

    if (terminating()) result = -EINTR;
    else {
        result = scan_set(set, events, maxevents, 1);
        if (result > 0 &&
            copy_to_user(user_events, events,
                         (size_t)result * sizeof(*events)))
            result = -EFAULT;
    }

    kfree(events);
    file_description_release(epoll_description);
    return result;
}

int64_t native_epoll_call(uint64_t operation, uint64_t a, uint64_t b,
                          uint64_t c, uint64_t d) {
    switch (operation) {
    case ALOS_EPOLL_CREATE1:
        return create_epoll((int)a);
    case ALOS_EPOLL_CTL:
        return ctl_epoll((int)a, (int)b, (int)c,
                         (const struct alos_epoll_event *)d);
    case ALOS_EPOLL_WAIT:
        return wait_epoll((int)a, (struct alos_epoll_event *)b,
                          (int)c, (int)d);
    default:
        return -EINVAL;
    }
}

int native_epoll_poll(open_file_description_t *description, short events) {
    if (!description || description->type != FILE_TYPE_EPOLL ||
        !description->epoll_set)
        return -EINVAL;
    return scan_set(description->epoll_set, NULL, 1, 0) > 0
               ? (events & 1)
               : 0;
}

void native_epoll_release(struct epoll_set *set) {
    if (!set) return;
    open_file_description_t *release[EPOLL_MAX_WATCHES] = {0};
    int count = 0;
    uint64_t flags = spinlock_irqsave(&set->lock);
    for (int i = 0; i < EPOLL_MAX_WATCHES; ++i) {
        if (!set->entries[i].used) continue;
        release[count++] = set->entries[i].description;
        memset(&set->entries[i], 0, sizeof(set->entries[i]));
    }
    spinlock_irqrestore(&set->lock, flags);
    for (int i = 0; i < count; ++i) file_description_release(release[i]);
    kfree(set);
}
