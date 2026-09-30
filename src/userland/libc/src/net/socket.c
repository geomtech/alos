#include <sys/socket.h>
#include <poll.h>
#include <errno.h>
#include "../internal/syscall.h"

static long call(int op, long a, long b, long c, long d, long e) {
    long r = syscall6(ALOS_SYS_SOCKET, op, a, b, c, d, e);
    if (r < 0) { errno = (int)-r; return -1; }
    return r;
}
int socket(int f, int t, int p) { return (int)call(ALOS_SOCKET_CREATE,f,t,p,0,0); }
int socketpair(int f, int t, int p, int pair[2]) {
    return (int)call(ALOS_SOCKET_PAIR,f,t,p,(long)pair,0);
}
int bind(int fd, const struct sockaddr *a, socklen_t n) {
    return (int)call(ALOS_SOCKET_BIND,fd,(long)a,n,0,0);
}
int connect(int fd, const struct sockaddr *a, socklen_t n) {
    return (int)call(ALOS_SOCKET_CONNECT,fd,(long)a,n,0,0);
}
int listen(int fd, int n) { return (int)call(ALOS_SOCKET_LISTEN,fd,n,0,0,0); }
int accept4(int fd, struct sockaddr *a, socklen_t *n, int flags) {
    return (int)call(ALOS_SOCKET_ACCEPT,fd,(long)a,(long)n,flags,0);
}
int accept(int fd, struct sockaddr *a, socklen_t *n) { return accept4(fd,a,n,0); }
int getsockname(int fd, struct sockaddr *a, socklen_t *n) {
    return (int)call(ALOS_SOCKET_GETNAME,fd,(long)a,(long)n,0,0);
}
int getpeername(int fd, struct sockaddr *a, socklen_t *n) {
    return (int)call(ALOS_SOCKET_GETPEER,fd,(long)a,(long)n,0,0);
}
int getsockopt(int fd, int l, int o, void *v, socklen_t *n) {
    return (int)call(ALOS_SOCKET_GETOPT,fd,l,o,(long)v,(long)n);
}
int setsockopt(int fd, int l, int o, const void *v, socklen_t n) {
    return (int)call(ALOS_SOCKET_SETOPT,fd,l,o,(long)v,n);
}
int shutdown(int fd, int how) { return (int)call(ALOS_SOCKET_SHUTDOWN,fd,how,0,0,0); }
ssize_t send(int fd, const void *b, size_t n, int flags) {
    return call(ALOS_SOCKET_SEND,fd,(long)b,(long)n,flags,0);
}
ssize_t recv(int fd, void *b, size_t n, int flags) {
    return call(ALOS_SOCKET_RECV,fd,(long)b,(long)n,flags,0);
}
ssize_t sendto(int fd, const void *b, size_t n, int flags,
                const struct sockaddr *a, socklen_t len) {
    (void)len;
    if (a) { errno = EOPNOTSUPP; return -1; }
    return send(fd,b,n,flags);
}
ssize_t recvfrom(int fd, void *b, size_t n, int flags,
                  struct sockaddr *a, socklen_t *len) {
    if (a && getpeername(fd,a,len)) return -1;
    return recv(fd,b,n,flags);
}
ssize_t sendmsg(int fd, const struct msghdr *m, int flags) {
    typedef char message_size_check[
        sizeof(struct msghdr) == sizeof(struct alos_socket_message) ? 1 : -1];
    (void)sizeof(message_size_check);
    return call(ALOS_SOCKET_SENDMSG,fd,(long)m,flags,0,0);
}
ssize_t recvmsg(int fd, struct msghdr *m, int flags) {
    return call(ALOS_SOCKET_RECVMSG,fd,(long)m,flags,0,0);
}
int poll(struct pollfd *fds, nfds_t n, int timeout) {
    long r = syscall3(ALOS_SYS_POLL,(long)fds,(long)n,timeout);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
