/* Test des API reseau POSIX de la libc ALOS : getifaddrs, if_nametoindex,
 * if_indextoname, res_ninit, gethostname, getpeereid (non supporte). */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("net-info-test: FAIL line=%d errno=%d\n", __LINE__, errno); return 1; } } while (0)

int main(void) {
  struct ifaddrs *list = NULL;
  CHECK(getifaddrs(&list) == 0 && list != NULL);
  int count = 0;
  char ip[INET_ADDRSTRLEN], mask[INET_ADDRSTRLEN];
  for (struct ifaddrs *i = list; i; i = i->ifa_next) {
    CHECK(i->ifa_addr && i->ifa_addr->sa_family == AF_INET);
    struct sockaddr_in *a = (struct sockaddr_in *)i->ifa_addr;
    struct sockaddr_in *m = (struct sockaddr_in *)i->ifa_netmask;
    CHECK(inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip)) != NULL);
    CHECK(inet_ntop(AF_INET, &m->sin_addr, mask, sizeof(mask)) != NULL);
    printf("net-info-test: iface %s addr=%s mask=%s flags=0x%x\n", i->ifa_name, ip, mask, i->ifa_flags);
    CHECK(i->ifa_flags & IFF_UP);
    unsigned index = if_nametoindex(i->ifa_name);
    CHECK(index != 0);
    char name[IF_NAMESIZE];
    CHECK(if_indextoname(index, name) != NULL && strcmp(name, i->ifa_name) == 0);
    count++;
  }
  freeifaddrs(list);
  CHECK(count >= 1);
  CHECK(if_nametoindex("nonexistent0") == 0 && errno == ENODEV);

  struct if_nameindex *names = if_nameindex();
  CHECK(names != NULL && names[0].if_index != 0);
  if_freenameindex(names);

  struct __res_state state;
  CHECK(res_ninit(&state) == 0 && (state.options & RES_INIT));
  CHECK(state.nscount >= 1 && state.nsaddr_list[0].sin_family == AF_INET);
  CHECK(inet_ntop(AF_INET, &state.nsaddr_list[0].sin_addr, ip, sizeof(ip)) != NULL);
  printf("net-info-test: nameserver0=%s port=%d retrans=%d retry=%d\n", ip,
         ntohs(state.nsaddr_list[0].sin_port), state.retrans, state.retry);
  res_nclose(&state);

  char host[64];
  CHECK(gethostname(host, sizeof(host)) == 0 && host[0]);
  printf("net-info-test: hostname=%s\n", host);

  /* socket TCP non bloquant : requis par //net (SocketPosix::Open). */
  int tcp = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(tcp >= 0);
  CHECK(fcntl(tcp, F_SETFL, O_NONBLOCK) == 0);
  CHECK(fcntl(tcp, F_GETFL) & O_NONBLOCK);
  close(tcp);

  uid_t uid; gid_t gid;
  CHECK(getpeereid(0, &uid, &gid) == -1 && errno == ENOTSUP);
  puts("net-info-test: PASS");
  return 0;
}
