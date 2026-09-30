#ifndef ALOS_NATIVE_EPOLL_ABI_H
#define ALOS_NATIVE_EPOLL_ABI_H

#include <stdint.h>

#define ALOS_SYS_EPOLL 242

enum {
    ALOS_EPOLL_CREATE1 = 0,
    ALOS_EPOLL_CTL = 1,
    ALOS_EPOLL_WAIT = 2
};

#define ALOS_EPOLL_CLOEXEC 0x80000

#define ALOS_EPOLL_CTL_ADD 1
#define ALOS_EPOLL_CTL_DEL 2
#define ALOS_EPOLL_CTL_MOD 3

#define ALOS_EPOLLIN        0x00000001u
#define ALOS_EPOLLPRI       0x00000002u
#define ALOS_EPOLLOUT       0x00000004u
#define ALOS_EPOLLERR       0x00000008u
#define ALOS_EPOLLHUP       0x00000010u
#define ALOS_EPOLLRDNORM    0x00000040u
#define ALOS_EPOLLWRNORM    0x00000100u
#define ALOS_EPOLLRDHUP     0x00002000u
#define ALOS_EPOLLEXCLUSIVE 0x10000000u
#define ALOS_EPOLLWAKEUP    0x20000000u
#define ALOS_EPOLLONESHOT   0x40000000u
#define ALOS_EPOLLET        0x80000000u

struct alos_epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

#endif
