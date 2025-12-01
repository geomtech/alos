/* src/net/net.h - Network Configuration and Identity */
#ifndef NET_NET_H
#define NET_NET_H

#include <stdint.h>

/**
 * Configuration réseau de notre OS.
 * 
 * Dans QEMU avec le mode user networking (SLIRP):
 * - Gateway: 10.0.2.2
 * - DNS Server: 10.0.2.3
 * - Notre IP suggérée: 10.0.2.15
 * - Réseau: 10.0.2.0/24
 * 
 * NOTE: Les variables globales MY_IP, GATEWAY_IP, etc. sont conservées
 * pour compatibilité mais seront supprimées. Utiliser NetInterface à la place.
 */

/* Variables globales legacy - DEPRECATED, utiliser NetInterface */
extern uint8_t MY_IP[4];
extern uint8_t MY_MAC[6];
extern uint8_t GATEWAY_IP[4];
extern uint8_t DNS_IP[4];
extern uint8_t NETMASK[4];

/**
 * Initialise les paramètres réseau.
 * Note: Cette fonction est maintenant simplifiée car la vraie config
 * est dans NetInterface et sera faite par DHCP.
 * 
 * @param mac Adresse MAC de notre interface (6 bytes)
 */
void net_init(uint8_t* mac);

/**
 * Compare deux adresses IP.
 * 
 * @param ip1 Première adresse IP (4 bytes)
 * @param ip2 Deuxième adresse IP (4 bytes)
 * @return 1 si égales, 0 sinon
 */
int ip_equals(const uint8_t* ip1, const uint8_t* ip2);

/**
 * Compare deux adresses MAC.
 * 
 * @param mac1 Première adresse MAC (6 bytes)
 * @param mac2 Deuxième adresse MAC (6 bytes)
 * @return 1 si égales, 0 sinon
 */
int mac_equals(const uint8_t* mac1, const uint8_t* mac2);

/**
 * Vérifie si une adresse MAC est broadcast (FF:FF:FF:FF:FF:FF).
 * 
 * @param mac Adresse MAC à vérifier
 * @return 1 si broadcast, 0 sinon
 */
int mac_is_broadcast(const uint8_t* mac);

/**
 * Traite les paquets réseau en attente (polling).
 * Utile dans les boucles d'attente bloquantes.
 */
void net_poll(void);

#endif /* NET_NET_H */
