#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("socket-test FAIL line %d: %s errno=%d\n", __LINE__, #x, errno); ++failures; } } while (0)
int main(void) {
    struct in_addr ip;
    char text[INET6_ADDRSTRLEN];
    CHECK(inet_pton(AF_INET,"10.0.2.2",&ip) == 1);
    CHECK(inet_ntop(AF_INET,&ip,text,sizeof(text)) && !strcmp(text,"10.0.2.2"));
    struct in_addr unchanged = ip;
    CHECK(inet_pton(AF_INET,"256.0.0.1",&ip) == 0 && ip.s_addr == unchanged.s_addr);
    CHECK(inet_pton(AF_INET,"01.2.3.4",&ip) == 0);
    CHECK(inet_pton(AF_INET,"1.2.3.4x",&ip) == 0);
    CHECK(inet_ntop(AF_INET,&ip,text,2) == NULL && errno == ENOSPC);
    struct in6_addr ipv6;
    CHECK(inet_pton(AF_INET6,"2001:db8::1",&ipv6) == 1);
    CHECK(inet_ntop(AF_INET6,&ipv6,text,sizeof(text)) && !strcmp(text,"2001:db8::1"));
    CHECK(inet_pton(AF_INET6,"1::2::3",&ipv6) == 0);
    struct addrinfo hint = {0}, *list = NULL;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    CHECK(getaddrinfo("10.0.2.2","443",&hint,&list) == 0);
    if (list) {
        struct sockaddr_in *a = (struct sockaddr_in *)list->ai_addr;
        CHECK(list->ai_family == AF_INET && list->ai_protocol == IPPROTO_TCP);
        CHECK(ntohs(a->sin_port) == 443 && list->ai_next == NULL);
        freeaddrinfo(list);
    }
    CHECK(getaddrinfo("invalid.host","443",&hint,&list) == EAI_NONAME && !list);
    CHECK(socket(AF_UNIX,SOCK_STREAM,0) == -1 && errno == EAFNOSUPPORT);
    CHECK(socket(AF_INET,SOCK_DGRAM,0) == -1 && errno == EPROTONOSUPPORT);
    int fd = socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,0);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(fcntl(fd,F_GETFL,0) & O_NONBLOCK);
        CHECK(fcntl(fd,F_GETFD,0) & FD_CLOEXEC);
        int type = 0; socklen_t n = sizeof(type);
        CHECK(getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&n) == 0 && type == SOCK_STREAM);
        CHECK(connect(fd,(const struct sockaddr *)1,sizeof(struct sockaddr_in)) == -1 && errno == EFAULT);
        struct sockaddr_in target = {0};
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = htonl(0x0a000202);
        CHECK(connect(fd,(struct sockaddr *)&target,sizeof(target)) == -1 && errno == EINVAL);
        target.sin_port = htons(80);
        target.sin_addr.s_addr = 0;
        CHECK(connect(fd,(struct sockaddr *)&target,sizeof(target)) == -1 && errno == EADDRNOTAVAIL);
        CHECK(bind(fd,(const struct sockaddr *)1,sizeof(struct sockaddr_in)) == -1 && errno == EFAULT);
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        CHECK(bind(fd,(struct sockaddr *)&a,sizeof(a)) == 0);
        n = sizeof(a);
        CHECK(getsockname(fd,(struct sockaddr *)&a,&n) == 0 && a.sin_port != 0 && n == sizeof(a));
        CHECK(listen(fd,4) == 0);
        CHECK(accept(fd,NULL,NULL) == -1 && errno == EAGAIN);
        struct pollfd p = {fd,POLLIN,123};
        CHECK(poll(&p,1,0) == 0 && p.revents == 0);
        CHECK(recv(fd,text,1,0) == -1 && errno == ENOTCONN);
        CHECK(close(fd) == 0);
        CHECK(poll(&p,1,0) == 1 && p.revents == POLLNVAL);
    }
    struct pollfd ignored = {-1,POLLIN,123};
    CHECK(poll(&ignored,1,1) == 0 && ignored.revents == 0);
    CHECK(poll((struct pollfd *)1,1,0) == -1 && errno == EFAULT);
    printf("socket-test: %s (%d failures)\n",failures ? "FAIL" : "PASS",failures);
    return failures ? 1 : 0;
}
