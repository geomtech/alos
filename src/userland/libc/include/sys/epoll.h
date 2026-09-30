#ifndef _SYS_EPOLL_H
#define _SYS_EPOLL_H

#include <stdint.h>
#include "../../../../include/native_epoll.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct epoll_event {
    uint32_t events;
    epoll_data_t data;
} __attribute__((packed));

#define EPOLLIN ALOS_EPOLLIN
#define EPOLLPRI ALOS_EPOLLPRI
#define EPOLLOUT ALOS_EPOLLOUT
#define EPOLLERR ALOS_EPOLLERR
#define EPOLLHUP ALOS_EPOLLHUP
#define EPOLLRDNORM ALOS_EPOLLRDNORM
#define EPOLLWRNORM ALOS_EPOLLWRNORM
#define EPOLLRDHUP ALOS_EPOLLRDHUP
#define EPOLLEXCLUSIVE ALOS_EPOLLEXCLUSIVE
#define EPOLLWAKEUP ALOS_EPOLLWAKEUP
#define EPOLLONESHOT ALOS_EPOLLONESHOT
#define EPOLLET ALOS_EPOLLET

#define EPOLL_CTL_ADD ALOS_EPOLL_CTL_ADD
#define EPOLL_CTL_DEL ALOS_EPOLL_CTL_DEL
#define EPOLL_CTL_MOD ALOS_EPOLL_CTL_MOD
#define EPOLL_CLOEXEC ALOS_EPOLL_CLOEXEC

int epoll_create(int size);
int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
                const void *sigmask);

#ifdef __cplusplus
}
#endif
#endif
