#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include "../../../../include/native_socket.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_storage {
    sa_family_t ss_family;
    char __padding[118];
    uint64_t __alignment;
};
struct msghdr {
    void *msg_name; socklen_t msg_namelen;
    struct iovec *msg_iov; size_t msg_iovlen;
    void *msg_control; size_t msg_controllen; int msg_flags;
};
struct cmsghdr { size_t cmsg_len; int cmsg_level; int cmsg_type; };
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_LOCAL AF_UNIX
#define AF_INET 2
#define AF_INET6 10
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_SEQPACKET 5
#define SOCK_NONBLOCK ALOS_SOCK_NONBLOCK
#define SOCK_CLOEXEC ALOS_SOCK_CLOEXEC
#define SOL_SOCKET ALOS_SOL_SOCKET
#define SO_REUSEADDR 2
#define SO_TYPE ALOS_SO_TYPE
#define SO_ERROR ALOS_SO_ERROR
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE ALOS_SO_KEEPALIVE
#define SO_LINGER 13
#define SO_REUSEPORT 15
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_ACCEPTCONN ALOS_SO_ACCEPTCONN
#define MSG_OOB 1
#define MSG_PEEK ALOS_MSG_PEEK
#define MSG_DONTWAIT ALOS_MSG_DONTWAIT
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL ALOS_MSG_NOSIGNAL
#define MSG_CTRUNC 8
#define MSG_TRUNC 0x20
#define MSG_CMSG_CLOEXEC 0x40000000
#define SCM_RIGHTS 1
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#define SOMAXCONN 128
struct linger { int l_onoff, l_linger; };
#define CMSG_ALIGN(n) (((n) + sizeof(size_t)-1) & ~(sizeof(size_t)-1))
#define CMSG_SPACE(n) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(n))
#define CMSG_LEN(n) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (n))
#define CMSG_DATA(c) ((unsigned char *)(c) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_FIRSTHDR(m) ((m)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(m)->msg_control : (struct cmsghdr *)0)
static inline struct cmsghdr *__alos_cmsg_next(const struct msghdr *m,
                                              const struct cmsghdr *c) {
    size_t off = (size_t)((const char *)c - (const char *)m->msg_control);
    if (c->cmsg_len < sizeof(*c) || off > m->msg_controllen ||
        c->cmsg_len > m->msg_controllen - off) return NULL;
    size_t step = CMSG_ALIGN(c->cmsg_len);
    if (step < c->cmsg_len || step > m->msg_controllen - off ||
        m->msg_controllen - off - step < sizeof(*c)) return NULL;
    return (struct cmsghdr *)((char *)m->msg_control + off + step);
}
#define CMSG_NXTHDR(m,c) __alos_cmsg_next((m),(c))
int socket(int, int, int);
int socketpair(int, int, int, int[2]);
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int connect(int, const struct sockaddr *, socklen_t);
int accept(int, struct sockaddr *, socklen_t *);
int accept4(int, struct sockaddr *, socklen_t *, int);
int getsockname(int, struct sockaddr *, socklen_t *);
int getpeername(int, struct sockaddr *, socklen_t *);
int getsockopt(int, int, int, void *, socklen_t *);
int setsockopt(int, int, int, const void *, socklen_t);
int shutdown(int, int);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
ssize_t sendmsg(int, const struct msghdr *, int);
ssize_t recvmsg(int, struct msghdr *, int);
#ifdef __cplusplus
}
#endif
#endif
