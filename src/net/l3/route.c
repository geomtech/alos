/* src/net/l3/route.c - Simple Routing Table */
#include "route.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/console.h"

/* Table de routage statique */
static route_entry_t routes[MAX_ROUTES];
static int route_count = 0;

/**
 * Affiche une adresse IP au format X.X.X.X
 */
static void print_ip(const uint8_t* ip)
{
    for (int i = 0; i < 4; i++) {
        if (i > 0) console_putc('.');
        console_put_dec(ip[i]);
    }
}

/**
 * Vérifie si une IP est dans un réseau donné.
 */
static bool ip_in_network(uint8_t* ip, uint8_t* network, uint8_t* netmask)
{
    for (int i = 0; i < 4; i++) {
        if ((ip[i] & netmask[i]) != (network[i] & netmask[i])) {
            return false;
        }
    }
    return true;
}

/**
 * Vérifie si une adresse IP est 0.0.0.0
 */
static bool ip_is_zero(uint8_t* ip)
{
    return (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

/**
 * Copie une adresse IP.
 */
static void ip_copy(uint8_t* dest, const uint8_t* src)
{
    for (int i = 0; i < 4; i++) {
        dest[i] = src[i];
    }
}

/**
 * Compte le nombre de bits à 1 dans le masque (pour longest prefix match).
 */
static int netmask_length(uint8_t* netmask)
{
    int len = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = netmask[i];
        while (byte) {
            len += (byte & 1);
            byte >>= 1;
        }
    }
    return len;
}

/**
 * Initialise la table de routage.
 */
void route_init(void)
{
    /* Initialiser toutes les entrées comme inactives */
    for (int i = 0; i < MAX_ROUTES; i++) {
        routes[i].active = false;
    }
    route_count = 0;
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[ROUTE] Initializing routing table...\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Obtenir l'interface par défaut */
    netdev_t* default_iface = netdev_get_default();
    if (default_iface == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[ROUTE] No network interface available!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* Note: On n'ajoute pas de routes statiques ici.
     * Les routes seront ajoutées dynamiquement par DHCP via route_update_from_netif().
     * Cela évite le problème de gateway 0.0.0.0 avant DHCP.
     */
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[ROUTE] Routing table initialized (waiting for DHCP)\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/**
 * Met à jour les routes depuis la configuration d'une interface.
 * Appelé après DHCP pour configurer les routes avec les vraies valeurs.
 */
void route_update_from_netif(NetInterface* netif)
{
    if (netif == NULL || netif->ip_addr == 0) {
        return;
    }
    
    /* Obtenir l'interface legacy */
    netdev_t* iface = netdev_get_default();
    if (iface == NULL) {
        return;
    }
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[ROUTE] Updating routes from DHCP...\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Calculer l'adresse réseau à partir de l'IP et du masque */
    uint8_t network[4], netmask[4], gateway[4], no_gw[4] = {0, 0, 0, 0};
    ip_u32_to_bytes(netif->ip_addr & netif->netmask, network);
    ip_u32_to_bytes(netif->netmask, netmask);
    ip_u32_to_bytes(netif->gateway, gateway);
    
    /* Route pour le réseau local - accès direct */
    route_add(network, netmask, no_gw, iface);
    
    /* Route par défaut via la gateway (si gateway configurée) */
    if (netif->gateway != 0) {
        uint8_t default_net[4] = {0, 0, 0, 0};
        uint8_t default_mask[4] = {0, 0, 0, 0};
        route_add(default_net, default_mask, gateway, iface);
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[ROUTE] Routes updated (");
    console_put_dec(route_count);
    console_puts(" routes)\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/**
 * Ajoute une route à la table.
 */
bool route_add(uint8_t* network, uint8_t* netmask, uint8_t* gateway, netdev_t* iface)
{
    if (route_count >= MAX_ROUTES) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[ROUTE] Table full!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return false;
    }
    
    /* Trouver une entrée libre */
    int idx = -1;
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (!routes[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        return false;
    }
    
    /* Remplir l'entrée */
    route_entry_t* r = &routes[idx];
    ip_copy(r->network, network);
    ip_copy(r->netmask, netmask);
    ip_copy(r->gateway, gateway);
    r->interface = iface;
    r->active = true;
    route_count++;
    
    /* Log */
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[ROUTE] Added: ");
    print_ip(network);
    console_puts("/");
    console_put_dec(netmask_length(netmask));
    if (!ip_is_zero(gateway)) {
        console_puts(" via ");
        print_ip(gateway);
    } else {
        console_puts(" (direct)");
    }
    console_puts(" dev ");
    console_puts(iface->name);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return true;
}

/**
 * Trouve la route pour une destination donnée.
 * Utilise longest prefix match.
 */
route_entry_t* route_lookup(uint8_t* dest_ip)
{
    route_entry_t* best_route = NULL;
    int best_prefix_len = -1;
    
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (!routes[i].active) {
            continue;
        }
        
        if (ip_in_network(dest_ip, routes[i].network, routes[i].netmask)) {
            int prefix_len = netmask_length(routes[i].netmask);
            if (prefix_len > best_prefix_len) {
                best_prefix_len = prefix_len;
                best_route = &routes[i];
            }
        }
    }
    
    return best_route;
}

/**
 * Retourne l'interface à utiliser pour atteindre une destination.
 */
netdev_t* route_get_interface(uint8_t* dest_ip)
{
    route_entry_t* route = route_lookup(dest_ip);
    if (route != NULL) {
        return route->interface;
    }
    
    /* Fallback: interface par défaut */
    return netdev_get_default();
}

/**
 * Retourne le next-hop pour une IP.
 */
bool route_get_next_hop(uint8_t* dest_ip, uint8_t* next_hop)
{
    route_entry_t* route = route_lookup(dest_ip);
    if (route == NULL) {
        return false;
    }
    
    /* Si gateway == 0.0.0.0, le next-hop est la destination elle-même */
    if (ip_is_zero(route->gateway)) {
        ip_copy(next_hop, dest_ip);
    } else {
        ip_copy(next_hop, route->gateway);
    }
    
    return true;
}

/**
 * Affiche la table de routage.
 */
void route_print_table(void)
{
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("\n=== Routing Table ===\n");
    console_puts("Destination      Gateway          Iface\n");
    console_puts("-----------------------------------------\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (!routes[i].active) {
            continue;
        }
        
        route_entry_t* r = &routes[i];
        
        /* Destination/masque */
        print_ip(r->network);
        console_puts("/");
        console_put_dec(netmask_length(r->netmask));
        console_puts("\t");
        
        /* Gateway */
        if (ip_is_zero(r->gateway)) {
            console_puts("*\t\t");
        } else {
            print_ip(r->gateway);
            console_puts("\t");
        }
        
        /* Interface */
        console_puts(r->interface->name);
        console_puts("\n");
    }
    
    console_puts("-----------------------------------------\n");
}
