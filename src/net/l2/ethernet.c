/* src/net/l2/ethernet.c - Ethernet Frame Handling */
#include "ethernet.h"
#include "arp.h"
#include "../l3/ipv4.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/console.h"

/**
 * Traite un paquet Ethernet reçu (nouvelle API avec NetInterface).
 */
void ethernet_handle_packet_netif(NetInterface* netif, uint8_t* data, int len)
{
    /* Vérifier la taille minimale (header Ethernet = 14 bytes) */
    if (data == NULL || len < ETHERNET_HEADER_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[ETH] Packet too short: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* Caster le début du buffer en header Ethernet */
    ethernet_header_t* eth = (ethernet_header_t*)data;
    
    /* Lire l'EtherType (conversion big-endian -> little-endian) */
    uint16_t ethertype = ntohs(eth->ethertype);
    
    /* Calculer le pointeur vers les données (payload) */
    uint8_t* payload = data + ETHERNET_HEADER_SIZE;
    int payload_len = len - ETHERNET_HEADER_SIZE;
    
    /* Dispatcher selon le type de protocole */
    switch (ethertype) {
        case ETH_TYPE_ARP:
            /* Paquet ARP */
            arp_handle_packet(netif, eth, payload, payload_len);
            break;
            
        case ETH_TYPE_IPV4:
            /* Paquet IPv4 */
            ipv4_handle_packet(netif, eth, payload, payload_len);
            break;
            
        case ETH_TYPE_IPV6:
            /* Paquet IPv6 - non supporté, ignorer silencieusement */
            break;
            
        default:
            /* Type inconnu - ignorer silencieusement */
            break;
    }
}

/**
 * Traite un paquet Ethernet reçu (ancienne API pour compatibilité).
 * 
 * Cette fonction est le point d'entrée pour tous les paquets reçus
 * par le driver réseau. Elle parse le header Ethernet et dispatch
 * vers le handler approprié selon l'EtherType.
 */
void ethernet_handle_packet(uint8_t* data, int len)
{
    /* Utiliser l'interface par défaut */
    NetInterface* netif = netif_get_default();
    ethernet_handle_packet_netif(netif, data, len);
}
