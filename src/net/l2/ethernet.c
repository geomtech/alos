/* src/net/l2/ethernet.c - Ethernet Frame Handling */
#include "ethernet.h"
#include "arp.h"
#include "../l3/ipv4.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../core/net.h"
#include "../../kernel/klog.h"

/**
 * Traite un paquet Ethernet reçu (nouvelle API avec NetInterface).
 */
void ethernet_handle_packet_netif(NetInterface* netif, uint8_t* data, int len)
{
    /* Vérifier la taille minimale (header Ethernet = 14 bytes) */
    if (data == NULL || len < ETHERNET_HEADER_SIZE) {
        KLOG_ERROR_DEC("ETH", "Packet too short: ", len);
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
    /* NOTE: Pas de net_lock() ici car cette fonction peut être appelée depuis
     * un contexte IRQ. Les mutex ne peuvent pas être utilisés dans les IRQ
     * car ils peuvent causer un deadlock si le thread qui tient le mutex
     * est interrompu par l'IRQ qui essaie de prendre le même mutex.
     * 
     * Les handlers individuels (ARP, IPv4) doivent gérer leur propre
     * synchronisation si nécessaire, en utilisant des spinlocks avec
     * interruptions désactivées.
     */
    
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
