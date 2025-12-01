/* src/net/l3/route.h - Simple Routing Table */
#ifndef NET_ROUTE_H
#define NET_ROUTE_H

#include <stdint.h>
#include <stdbool.h>
#include "../core/netdev.h"

/**
 * Structure d'une entrée de la table de routage.
 * 
 * Pour l'instant, routage simplifié:
 * - Si dest est sur le même réseau -> envoi direct
 * - Sinon -> envoi via gateway
 */
typedef struct {
    uint8_t     network[4];     /* Adresse réseau (ex: 10.0.2.0) */
    uint8_t     netmask[4];     /* Masque (ex: 255.255.255.0) */
    uint8_t     gateway[4];     /* Gateway (0.0.0.0 si direct) */
    netdev_t*   interface;      /* Interface de sortie */
    bool        active;         /* Entrée active? */
} route_entry_t;

/* Nombre maximum de routes */
#define MAX_ROUTES  8

/**
 * Initialise la table de routage.
 * Crée les routes par défaut basées sur les interfaces disponibles.
 */
void route_init(void);

/**
 * Ajoute une route à la table.
 * 
 * @param network  Adresse réseau (4 bytes)
 * @param netmask  Masque de sous-réseau (4 bytes)
 * @param gateway  Adresse gateway (4 bytes, 0.0.0.0 si direct)
 * @param iface    Interface de sortie
 * @return true si succès, false si table pleine
 */
bool route_add(uint8_t* network, uint8_t* netmask, uint8_t* gateway, netdev_t* iface);

/**
 * Trouve la route pour une destination donnée.
 * 
 * @param dest_ip  Adresse IP de destination (4 bytes)
 * @return Pointeur vers l'entrée de route ou NULL si pas de route
 */
route_entry_t* route_lookup(uint8_t* dest_ip);

/**
 * Retourne l'interface à utiliser pour atteindre une destination.
 * 
 * @param dest_ip  Adresse IP de destination (4 bytes)
 * @return Pointeur vers le netdev ou NULL si pas de route
 */
netdev_t* route_get_interface(uint8_t* dest_ip);

/**
 * Retourne le next-hop (gateway ou destination directe) pour une IP.
 * 
 * @param dest_ip   Adresse IP de destination (4 bytes)
 * @param next_hop  Buffer pour stocker le next-hop (4 bytes)
 * @return true si route trouvée, false sinon
 */
bool route_get_next_hop(uint8_t* dest_ip, uint8_t* next_hop);

/**
 * Affiche la table de routage (debug).
 */
void route_print_table(void);

#endif /* NET_ROUTE_H */
