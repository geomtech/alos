#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#include <sys/socket.h>
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;
struct in_addr { in_addr_t s_addr; };
struct in6_addr { unsigned char s6_addr[16]; };
struct sockaddr_in {
    sa_family_t sin_family; in_port_t sin_port;
    struct in_addr sin_addr; unsigned char sin_zero[8];
};
struct sockaddr_in6 {
    sa_family_t sin6_family; in_port_t sin6_port; uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr; uint32_t sin6_scope_id;
};
#define INADDR_ANY ((in_addr_t)0)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE INADDR_BROADCAST
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPV6_V6ONLY 26
#define IN6_IS_ADDR_UNSPECIFIED(a)   (((const uint32_t *)(a))[0] == 0 && ((const uint32_t *)(a))[1] == 0 &&    ((const uint32_t *)(a))[2] == 0 && ((const uint32_t *)(a))[3] == 0)
#define IN6_IS_ADDR_LOOPBACK(a)   (((const uint32_t *)(a))[0] == 0 && ((const uint32_t *)(a))[1] == 0 &&    ((const uint32_t *)(a))[2] == 0 && ((const uint8_t *)(a))[12] == 0 &&    ((const uint8_t *)(a))[13] == 0 && ((const uint8_t *)(a))[14] == 0 &&    ((const uint8_t *)(a))[15] == 1)
#define IN6_IS_ADDR_MULTICAST(a) (((const uint8_t *)(a))[0] == 0xff)
#define IN6_IS_ADDR_LINKLOCAL(a)   (((const uint8_t *)(a))[0] == 0xfe && (((const uint8_t *)(a))[1] & 0xc0) == 0x80)
#define IN6_IS_ADDR_V4MAPPED(a)   (((const uint32_t *)(a))[0] == 0 && ((const uint32_t *)(a))[1] == 0 &&    ((const uint8_t *)(a))[8] == 0 && ((const uint8_t *)(a))[9] == 0 &&    ((const uint8_t *)(a))[10] == 0xff && ((const uint8_t *)(a))[11] == 0xff)
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#endif
