#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../internal/syscall.h"

/* Profil IPv4: numerique/localhost localement, DNS A via le resolver noyau. */
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hint,
                struct addrinfo **result) {
    if (!result) return EAI_FAIL;
    *result = NULL;
    int family = hint ? hint->ai_family : AF_UNSPEC;
    int type = hint ? hint->ai_socktype : 0;
    int protocol = hint ? hint->ai_protocol : 0;
    int flags = hint ? hint->ai_flags : 0;
    if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
                  AI_NUMERICSERV | AI_ADDRCONFIG))
        return EAI_BADFLAGS;
    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
    if (type && type != SOCK_STREAM && type != SOCK_DGRAM) return EAI_SOCKTYPE;
    if (protocol && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) return EAI_SERVICE;
    if ((type == SOCK_STREAM && protocol == IPPROTO_UDP) ||
        (type == SOCK_DGRAM && protocol == IPPROTO_TCP)) return EAI_SOCKTYPE;
    if (!node && !service) return EAI_NONAME;
    unsigned port = 0;
    if (service) {
        if (!*service) return EAI_SERVICE;
        for (const char *s = service; *s; ++s) {
            if (*s < '0' || *s > '9') return EAI_SERVICE;
            port = port * 10 + (unsigned)(*s - '0');
            if (port > 65535) return EAI_SERVICE;
        }
    }
    struct in_addr ip;
    if (!node) ip.s_addr = htonl(flags & AI_PASSIVE ? INADDR_ANY : INADDR_LOOPBACK);
    else if (inet_pton(AF_INET, node, &ip) != 1) {
        if (!(flags & AI_NUMERICHOST) && !strcmp(node, "localhost"))
            ip.s_addr = htonl(INADDR_LOOPBACK);
        else {
            if (flags & AI_NUMERICHOST) return EAI_NONAME;
            unsigned char resolved[4];
            long status = syscall2(SYS_RESOLVE_IPV4, (long)node, (long)resolved);
            if (status < 0) {
                int error = (int)-status;
                if (error == ENOENT || error == EINVAL || error == ENAMETOOLONG)
                    return EAI_NONAME;
                if (error == ETIMEDOUT || error == EAGAIN)
                    return EAI_AGAIN;
                errno = error;
                return EAI_SYSTEM;
            }
            memcpy(&ip, resolved, sizeof(resolved));
        }
    }
    struct addrinfo **tail = result;
    int first = type ? type : protocol == IPPROTO_UDP ? SOCK_DGRAM : SOCK_STREAM;
    int last = type || protocol ? first : SOCK_DGRAM;
    for (int t = first; t <= last; ++t) {
        struct addrinfo *a = calloc(1, sizeof(*a) + sizeof(struct sockaddr_in));
        if (!a) { freeaddrinfo(*result); *result = NULL; return EAI_MEMORY; }
        struct sockaddr_in *sin = (struct sockaddr_in *)(a + 1);
        sin->sin_family = AF_INET; sin->sin_port = htons((uint16_t)port); sin->sin_addr = ip;
        a->ai_family = AF_INET; a->ai_socktype = t;
        a->ai_protocol = t == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
        a->ai_addrlen = sizeof(*sin); a->ai_addr = (struct sockaddr *)sin;
        a->ai_flags = flags;
        if (flags & AI_CANONNAME && node && tail == result) {
            a->ai_canonname = malloc(strlen(node) + 1);
            if (!a->ai_canonname) {
                free(a); freeaddrinfo(*result); *result = NULL; return EAI_MEMORY;
            }
            strcpy(a->ai_canonname, node);
        }
        *tail = a; tail = &a->ai_next;
    }
    return 0;
}
void freeaddrinfo(struct addrinfo *a) {
    while (a) { struct addrinfo *next = a->ai_next; free(a->ai_canonname); free(a); a = next; }
}
const char *gai_strerror(int e) {
    switch (e) {
    case 0: return "Success";
    case EAI_BADFLAGS: return "Unsupported address flags";
    case EAI_NONAME: return "Name or service not known";
    case EAI_AGAIN: return "Temporary name resolution failure";
    case EAI_FAIL: return "Name resolution failed";
    case EAI_FAMILY: return "Address family not supported";
    case EAI_SOCKTYPE: return "Socket type not supported";
    case EAI_SERVICE: return "Service not supported";
    case EAI_MEMORY: return "Out of memory";
    case EAI_SYSTEM: return "System error";
    case EAI_OVERFLOW: return "Buffer too small";
    default: return "Unknown address error";
    }
}
int getnameinfo(const struct sockaddr *address, socklen_t length,
                char *host, socklen_t hostlen, char *service, socklen_t servicelen,
                int flags) {
    if (flags & ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_NAMEREQD | NI_DGRAM))
        return EAI_BADFLAGS;
    if (!address || length < sizeof(struct sockaddr_in) || address->sa_family != AF_INET)
        return EAI_FAMILY;
    if (host && (flags & NI_NAMEREQD) && !(flags & NI_NUMERICHOST)) {
        errno = ENOTSUP; return EAI_SYSTEM;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in *)address;
    char h[INET_ADDRSTRLEN], s[6];
    inet_ntop(AF_INET, &sin->sin_addr, h, sizeof(h));
    snprintf(s, sizeof(s), "%u", ntohs(sin->sin_port));
    if ((host && strlen(h) + 1 > hostlen) || (service && strlen(s) + 1 > servicelen))
        return EAI_OVERFLOW;
    if (host) strcpy(host, h);
    if (service) strcpy(service, s);
    return 0;
}
