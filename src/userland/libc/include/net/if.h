/* <net/if.h> : interfaces reseau. Les informations viennent du noyau
 * (SYS_NET_IFACE_INFO) ; IPv4 uniquement, pas d'ioctl SIOCGIF*. */
#ifndef _NET_IF_H
#define _NET_IF_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IF_NAMESIZE 16
#define IFNAMSIZ IF_NAMESIZE

#define IFF_UP        0x1
#define IFF_BROADCAST 0x2
#define IFF_LOOPBACK  0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING   0x40
#define IFF_MULTICAST 0x1000

struct if_nameindex {
  unsigned int if_index;
  char *if_name;
};

struct ifreq {
  char ifr_name[IFNAMSIZ];
  union {
    struct sockaddr ifru_addr;
    struct sockaddr ifru_netmask;
    short ifru_flags;
    int ifru_ifindex;
    int ifru_mtu;
  } ifr_ifru;
};
#define ifr_addr ifr_ifru.ifru_addr
#define ifr_netmask ifr_ifru.ifru_netmask
#define ifr_flags ifr_ifru.ifru_flags
#define ifr_ifindex ifr_ifru.ifru_ifindex
#define ifr_mtu ifr_ifru.ifru_mtu

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *list);

#ifdef __cplusplus
}
#endif

#endif
