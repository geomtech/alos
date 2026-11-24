/* ABI reseau partagee noyau/libc : informations d'interface (SYS_NET_IFACE_INFO). */
#ifndef ALOS_NET_ABI_H
#define ALOS_NET_ABI_H

#include <stdint.h>

#define ALOS_NETIF_UP        (1u << 0)
#define ALOS_NETIF_RUNNING   (1u << 1)
#define ALOS_NETIF_BROADCAST (1u << 2)
#define ALOS_NETIF_DHCP      (1u << 3)

/* Les adresses IPv4 sont en ordre octets reseau (comme struct in_addr). */
typedef struct {
  char name[16];
  uint8_t mac[6];
  uint16_t mtu;
  uint32_t index;   /* 1-based, stable tant que la liste ne change pas */
  uint32_t flags;   /* ALOS_NETIF_* */
  uint32_t ipv4;
  uint32_t netmask;
  uint32_t gateway;
  uint32_t dns;     /* serveur DNS configure (DHCP), 0 si aucun */
} alos_netif_info_t;

#endif
