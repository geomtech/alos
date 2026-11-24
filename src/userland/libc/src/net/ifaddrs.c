/* Interfaces reseau : getifaddrs, if_nametoindex, ... sur SYS_NET_IFACE_INFO. */
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include "../../../../include/net_abi.h"

#define MAX_IFACES 16

/* Remplit info pour l'index base 0 ; 0 si l'interface existe. */
static int iface_get(unsigned index, alos_netif_info_t *info) {
  long result = syscall2(SYS_NET_IFACE_INFO, (long)index, (long)info);
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  return 0;
}

static unsigned iface_flags(const alos_netif_info_t *info) {
  unsigned flags = 0;
  if (info->flags & ALOS_NETIF_UP) flags |= IFF_UP;
  if (info->flags & ALOS_NETIF_RUNNING) flags |= IFF_RUNNING;
  if (info->flags & ALOS_NETIF_BROADCAST) flags |= IFF_BROADCAST | IFF_MULTICAST;
  return flags;
}

unsigned int if_nametoindex(const char *name) {
  alos_netif_info_t info;
  for (unsigned i = 0; i < MAX_IFACES; i++) {
    if (iface_get(i, &info) != 0) break;
    if (strncmp(info.name, name, IF_NAMESIZE) == 0) return info.index;
  }
  errno = ENODEV;
  return 0;
}

char *if_indextoname(unsigned int index, char *name) {
  alos_netif_info_t info;
  if (index == 0 || iface_get(index - 1, &info) != 0) {
    errno = ENXIO;
    return NULL;
  }
  strncpy(name, info.name, IF_NAMESIZE);
  name[IF_NAMESIZE - 1] = 0;
  return name;
}

struct if_nameindex *if_nameindex(void) {
  alos_netif_info_t info;
  unsigned count = 0;
  while (count < MAX_IFACES && iface_get(count, &info) == 0) count++;
  struct if_nameindex *list = calloc(count + 1, sizeof(*list));
  if (!list) return NULL;
  for (unsigned i = 0; i < count; i++) {
    iface_get(i, &info);
    list[i].if_index = info.index;
    list[i].if_name = strdup(info.name);
    if (!list[i].if_name) {
      if_freenameindex(list);
      errno = ENOMEM;
      return NULL;
    }
  }
  return list;
}

void if_freenameindex(struct if_nameindex *list) {
  if (!list) return;
  for (struct if_nameindex *p = list; p->if_index != 0 || p->if_name; p++) free(p->if_name);
  free(list);
}

/* Un seul bloc par entree : [ifaddrs][name][addr][netmask][broadcast]. */
struct entry {
  struct ifaddrs ifa;
  char name[IF_NAMESIZE];
  struct sockaddr_in addr, netmask, broadcast;
};

int getifaddrs(struct ifaddrs **ifap) {
  alos_netif_info_t info;
  struct ifaddrs *head = NULL, **tail = &head;
  for (unsigned i = 0; i < MAX_IFACES && iface_get(i, &info) == 0; i++) {
    if (info.ipv4 == 0) continue; /* sans adresse IPv4 : rien a lister */
    struct entry *e = calloc(1, sizeof(*e));
    if (!e) {
      freeifaddrs(head);
      errno = ENOMEM;
      return -1;
    }
    memcpy(e->name, info.name, IF_NAMESIZE - 1);
    e->ifa.ifa_name = e->name;
    e->ifa.ifa_flags = iface_flags(&info);
    e->addr.sin_family = AF_INET;
    e->addr.sin_addr.s_addr = info.ipv4;
    e->netmask.sin_family = AF_INET;
    e->netmask.sin_addr.s_addr = info.netmask;
    e->broadcast.sin_family = AF_INET;
    e->broadcast.sin_addr.s_addr = (info.ipv4 & info.netmask) | ~info.netmask;
    e->ifa.ifa_addr = (struct sockaddr *)&e->addr;
    e->ifa.ifa_netmask = (struct sockaddr *)&e->netmask;
    e->ifa.ifa_broadaddr = (struct sockaddr *)&e->broadcast;
    *tail = &e->ifa;
    tail = &e->ifa.ifa_next;
  }
  *ifap = head;
  return 0;
}

void freeifaddrs(struct ifaddrs *ifa) {
  while (ifa) {
    struct ifaddrs *next = ifa->ifa_next;
    free(ifa);
    ifa = next;
  }
}
