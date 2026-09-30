#ifndef ALOS_NATIVE_SOCKET_ABI_H
#define ALOS_NATIVE_SOCKET_ABI_H

#include <stdint.h>
#include <stddef.h>
#include "native_io.h"

/* ABI ALOS, distincte des anciens appels socket 41-55. */
#define ALOS_SYS_SOCKET 240
#define ALOS_SYS_POLL 241
enum {
    ALOS_SOCKET_CREATE, ALOS_SOCKET_BIND, ALOS_SOCKET_LISTEN,
    ALOS_SOCKET_ACCEPT, ALOS_SOCKET_CONNECT, ALOS_SOCKET_SEND,
    ALOS_SOCKET_RECV, ALOS_SOCKET_GETNAME, ALOS_SOCKET_GETPEER,
    ALOS_SOCKET_GETOPT, ALOS_SOCKET_SETOPT, ALOS_SOCKET_SHUTDOWN,
    ALOS_SOCKET_PAIR, ALOS_SOCKET_SENDMSG, ALOS_SOCKET_RECVMSG
};

#define ALOS_UNIX_MAX_RIGHTS 16
#define ALOS_MSG_CTRUNC 8
#define ALOS_MSG_CMSG_CLOEXEC 0x40000000
struct alos_socket_message {
    void *name;
    uint32_t name_length;
    struct alos_iovec *vectors;
    size_t vector_count;
    void *control;
    size_t control_length;
    int flags;
};
struct alos_socket_control {
    size_t length;
    int level;
    int type;
};

#define ALOS_SOCK_NONBLOCK 0x800
#define ALOS_SOCK_CLOEXEC 0x80000
#define ALOS_MSG_PEEK 2
#define ALOS_MSG_DONTWAIT 0x40
#define ALOS_MSG_NOSIGNAL 0x4000
#define ALOS_SOL_SOCKET 1
#define ALOS_SO_TYPE 3
#define ALOS_SO_ERROR 4
#define ALOS_SO_KEEPALIVE 9
#define ALOS_SO_ACCEPTCONN 30
#define ALOS_POLLIN 1
#define ALOS_POLLOUT 4
#define ALOS_POLLERR 8
#define ALOS_POLLHUP 16
#define ALOS_POLLNVAL 32
struct alos_pollfd { int fd; short events, revents; };

#endif
