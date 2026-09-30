#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

uint16_t htons(uint16_t n) { return (uint16_t)((n >> 8) | (n << 8)); }
uint16_t ntohs(uint16_t n) { return htons(n); }
uint32_t htonl(uint32_t n) {
    return (n >> 24) | ((n >> 8) & 0xff00) |
           ((n << 8) & 0xff0000) | (n << 24);
}
uint32_t ntohl(uint32_t n) { return htonl(n); }

static int ipv4(const char *s, unsigned char out[4]) {
    for (int i = 0; i < 4; ++i) {
        unsigned value = 0, digits = 0;
        const char *start = s;
        while (*s >= '0' && *s <= '9') {
            value = value * 10 + (unsigned)(*s++ - '0');
            if (++digits > 3 || value > 255) return 0;
        }
        if (!digits || (digits > 1 && *start == '0')) return 0;
        out[i] = (unsigned char)value;
        if (i < 3) { if (*s++ != '.') return 0; }
    }
    return *s == 0;
}

int inet_pton(int family, const char *text, void *address) {
    unsigned char bytes[16] = {0};
    if (family != AF_INET && family != AF_INET6) {
        errno = EAFNOSUPPORT; return -1;
    }
    if (!text || !address) { errno = EFAULT; return -1; }
    if (family == AF_INET) {
        if (!ipv4(text, bytes)) return 0;
        memcpy(address, bytes, 4); return 1;
    }
    unsigned used = 0;
    int gap = -1;
    const char *s = text;
    if (*s == ':') {
        if (*++s != ':') return 0;
        ++s; gap = 0;
    }
    while (*s) {
        if (used >= 16) return 0;
        const char *start = s;
        unsigned value = 0, digits = 0;
        while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') ||
               (*s >= 'A' && *s <= 'F')) {
            unsigned d = *s <= '9' ? (unsigned)(*s - '0') :
                         (unsigned)((*s | 32) - 'a' + 10);
            value = value * 16 + d; ++s;
            if (++digits > 4) return 0;
        }
        if (*s == '.') {
            if (used > 12 || !ipv4(start, bytes + used)) return 0;
            used += 4; s += strlen(s); break;
        }
        if (!digits) return 0;
        bytes[used++] = (unsigned char)(value >> 8);
        bytes[used++] = (unsigned char)value;
        if (!*s) break;
        if (*s++ != ':') return 0;
        if (*s == ':') {
            if (gap >= 0) return 0;
            gap = (int)used; ++s;
        } else if (!*s) return 0;
    }
    if (gap >= 0) {
        if (used == 16) return 0;
        unsigned missing = 16 - used;
        memmove(bytes + gap + missing, bytes + gap, used - (unsigned)gap);
        memset(bytes + gap, 0, missing);
    } else if (used != 16) return 0;
    memcpy(address, bytes, 16); return 1;
}

const char *inet_ntop(int family, const void *address, char *text, socklen_t size) {
    char buffer[INET6_ADDRSTRLEN];
    const unsigned char *b = address;
    if (family != AF_INET && family != AF_INET6) {
        errno = EAFNOSUPPORT; return NULL;
    }
    if (!address || !text) { errno = EFAULT; return NULL; }
    if (family == AF_INET) {
        snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    } else {
        static const unsigned char mapped[12] = {0,0,0,0,0,0,0,0,0,0,255,255};
        if (!memcmp(b,mapped,12)) {
            snprintf(buffer,sizeof(buffer),"::ffff:%u.%u.%u.%u",b[12],b[13],b[14],b[15]);
            size_t needed = strlen(buffer) + 1;
            if (needed > size) { errno = ENOSPC; return NULL; }
            memcpy(text,buffer,needed); return text;
        }
        unsigned words[8];
        int best = -1, best_len = 1;
        for (int i = 0; i < 8; ++i) words[i] = ((unsigned)b[2*i] << 8) | b[2*i+1];
        for (int i = 0; i < 8;) {
            if (words[i]) { ++i; continue; }
            int start = i;
            while (i < 8 && !words[i]) ++i;
            if (i-start > best_len) { best = start; best_len = i-start; }
        }
        char *p = buffer;
        for (int i = 0; i < 8;) {
            if (i == best) { *p++ = ':'; *p++ = ':'; i += best_len; continue; }
            if (p != buffer && p[-1] != ':') *p++ = ':';
            p += snprintf(p, (size_t)(buffer + sizeof(buffer) - p), "%x", words[i++]);
        }
        *p = 0;
    }
    size_t needed = strlen(buffer) + 1;
    if (needed > size) { errno = ENOSPC; return NULL; }
    memcpy(text, buffer, needed); return text;
}
in_addr_t inet_addr(const char *s) {
    struct in_addr a;
    return inet_pton(AF_INET, s, &a) == 1 ? a.s_addr : INADDR_NONE;
}
int inet_aton(const char *s, struct in_addr *a) { return inet_pton(AF_INET, s, a) == 1; }
char *inet_ntoa(struct in_addr a) {
    static __thread char text[INET_ADDRSTRLEN];
    return (char *)inet_ntop(AF_INET, &a, text, sizeof(text));
}
