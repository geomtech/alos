#include <sys/epoll.h>
#include <errno.h>
#include "../internal/syscall.h"

static long epoll_call(long operation, long a, long b, long c, long d) {
    long result = syscall5(ALOS_SYS_EPOLL, operation, a, b, c, d);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return result;
}

int epoll_create1(int flags) {
    return (int)epoll_call(ALOS_EPOLL_CREATE1, flags, 0, 0, 0);
}

int epoll_create(int size) {
    if (size <= 0) {
        errno = EINVAL;
        return -1;
    }
    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    typedef char epoll_event_size_check[
        sizeof(struct epoll_event) == sizeof(struct alos_epoll_event) ? 1 : -1];
    (void)sizeof(epoll_event_size_check);
    return (int)epoll_call(ALOS_EPOLL_CTL, epfd, op, fd, (long)event);
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    return (int)epoll_call(ALOS_EPOLL_WAIT, epfd, (long)events, maxevents,
                           timeout);
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
                const void *sigmask) {
    if (sigmask) {
        errno = ENOTSUP;
        return -1;
    }
    return epoll_wait(epfd, events, maxevents, timeout);
}
