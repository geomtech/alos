/* src/net/core/net.c - Network Configuration */
#include "net.h"
#include "../../drivers/net/pcnet.h"
#include "../../drivers/net/virtio_net.h"
#include "../../drivers/net/e1000e.h"
#include "netdev.h"
#include "../../kernel/klog.h"

/*
 * Variables globales LEGACY - DEPRECATED
 * Ces variables sont conservées pour compatibilité mais seront supprimées.
 * La vraie configuration est maintenant dans NetInterface.
 *
 * Pour DHCP: l'IP sera 0.0.0.0 jusqu'à réception d'un bail.
 */
uint8_t MY_IP[4] = {0, 0, 0, 0}; /* Pas d'IP statique par défaut */
uint8_t MY_MAC[6] = {0, 0, 0, 0, 0, 0};
uint8_t GATEWAY_IP[4] = {0, 0, 0, 0}; /* Sera configuré par DHCP */
uint8_t DNS_IP[4] = {0, 0, 0, 0};     /* Sera configuré par DHCP */
uint8_t NETMASK[4] = {0, 0, 0, 0};    /* Sera configuré par DHCP */

/**
 * Initialise les paramètres réseau.
 * Note: La vraie configuration IP est dans NetInterface et sera faite par DHCP.
 */
void net_init(uint8_t *mac) {
  /* Initialize global network lock */
  mutex_init(&net_mutex, MUTEX_TYPE_NORMAL);

  /* Copier notre adresse MAC dans la variable legacy */
  for (int i = 0; i < 6; i++) {
    MY_MAC[i] = mac[i];
  }

  /* Afficher l'état initial (en attente de DHCP) */
  KLOG_INFO("NET", "Network layer initialized");
  KLOG_INFO("NET", "Status: Waiting for DHCP or static configuration");
}

/**
 * Compare deux adresses IP.
 */
int ip_equals(const uint8_t *ip1, const uint8_t *ip2) {
  for (int i = 0; i < 4; i++) {
    if (ip1[i] != ip2[i])
      return 0;
  }
  return 1;
}

/**
 * Compare deux adresses MAC.
 */
int mac_equals(const uint8_t *mac1, const uint8_t *mac2) {
  for (int i = 0; i < 6; i++) {
    if (mac1[i] != mac2[i])
      return 0;
  }
  return 1;
}

/**
 * Vérifie si une adresse MAC est broadcast.
 */
int mac_is_broadcast(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0xFF)
      return 0;
  }
  return 1;
}

/**
 * Traite les paquets réseau en attente (polling mode).
 * Cette fonction traite directement les paquets sans passer par le worker
 * thread. Utilisée pendant le boot (DHCP) et dans les boucles d'attente
 * syscall.
 */
void net_poll(void) {
  /* Appeler le polling des drivers */
  /* Poll PCnet if available */
  PCNetDevice *pcnet_dev = pcnet_get_device();
  if (pcnet_dev != NULL) {
    pcnet_poll();
  }

  /* Poll Virtio if available */
  VirtIONetDevice *virtio_dev = virtio_net_get_device();
  if (virtio_dev != NULL) {
    virtio_net_poll();
  }

  /* Poll e1000e if available */
  E1000Device *e1000_dev = e1000e_get_device();
  if (e1000_dev != NULL) {
    e1000e_poll();
  }
}

/* ========================================
 * Global Network Lock Implementation
 * ======================================== */

mutex_t net_mutex;

void net_lock(void) { mutex_lock(&net_mutex); }

void net_unlock(void) { mutex_unlock(&net_mutex); }
