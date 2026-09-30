#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do { if (!(x)) { printf("inet-test FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while (0)
int main(void) {
    const char *valid[] = {"::", "::1", "2001:db8::1", "1:2:3:4:5:6:7:8",
                           "::ffff:192.0.2.1", "1::", "1:2:3:4:5:6:192.0.2.1"};
    const char *invalid[] = {"", ":", "1:", "1::2::3", "12345::", "1:2:3:4:5:6:7",
                             "1:2:3:4:5:6:7:8:9", "::192.0.2.256", "1:2:3:4:5:6:7::8"};
    for (size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); ++i) {
        struct in6_addr a, b; char text[INET6_ADDRSTRLEN];
        CHECK(inet_pton(AF_INET6,valid[i],&a) == 1);
        CHECK(inet_ntop(AF_INET6,&a,text,sizeof(text)) != NULL);
        CHECK(inet_pton(AF_INET6,text,&b) == 1 && !memcmp(&a,&b,sizeof(a)));
    }
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        struct in6_addr a, before;
        memset(&a,0xab,sizeof(a)); before = a;
        CHECK(inet_pton(AF_INET6,invalid[i],&a) == 0 && !memcmp(&a,&before,sizeof(a)));
    }
    struct in_addr a;
    CHECK(inet_pton(AF_INET,"0.0.0.0",&a) == 1 && a.s_addr == 0);
    CHECK(inet_pton(AF_INET,"255.255.255.255",&a) == 1 && a.s_addr == INADDR_BROADCAST);
    CHECK(inet_pton(AF_INET,"1.2.3.4.",&a) == 0);
    CHECK(inet_pton(AF_INET,"1.2.3",&a) == 0);
    CHECK(ntohs(htons(0x1234)) == 0x1234 && ntohl(htonl(0x12345678)) == 0x12345678);
    struct addrinfo h = {0}, *r = NULL;
    h.ai_socktype = SOCK_STREAM; h.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    CHECK(getaddrinfo("192.0.2.1","65535",&h,&r) == 0);
    if (r) {
        char host[INET_ADDRSTRLEN], service[6];
        CHECK(getnameinfo(r->ai_addr,r->ai_addrlen,host,sizeof(host),service,sizeof(service),
                         NI_NUMERICHOST | NI_NUMERICSERV) == 0);
        CHECK(!strcmp(host,"192.0.2.1") && !strcmp(service,"65535"));
        CHECK(getnameinfo(r->ai_addr,r->ai_addrlen,host,1,NULL,0,NI_NUMERICHOST) == EAI_OVERFLOW);
        freeaddrinfo(r);
    }
    CHECK(getaddrinfo("192.0.2.1","65536",&h,&r) == EAI_SERVICE && !r);
    CHECK(getaddrinfo("host.invalid","80",&h,&r) == EAI_NONAME && !r);
    h.ai_flags = 0;
    CHECK(getaddrinfo("host.invalid","80",&h,&r) == EAI_SYSTEM && errno == ENOTSUP && !r);
    CHECK(getaddrinfo(NULL,"80",&h,&r) == 0);
    if (r) {
        CHECK(((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr == htonl(INADDR_LOOPBACK));
        freeaddrinfo(r);
    }
    printf("inet-test: %s (%d failures)\n",failures ? "FAIL" : "PASS",failures);
    return failures ? 1 : 0;
}
