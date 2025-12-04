/* src/net/l4/udp.c - UDP Protocol Handler */
#include "udp.h"
#include "dhcp.h"
#include "dns.h"
#include "../l3/ipv4.h"
#include "../l3/route.h"
#include "../l2/arp.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/klog.h"

/* Note: print_ip removed - using KLOG instead */

/**
 * Traite un paquet UDP reçu.
 */
void udp_handle_packet(ipv4_header_t* ip_hdr, uint8_t* data, int len)
{
    /* Vérifier la taille minimale */
    if (data == NULL || len < UDP_HEADER_SIZE) {
        KLOG_ERROR_DEC("UDP", "Packet too short: ", len);
        return;
    }

    /* Caster en header UDP */
    udp_header_t* udp = (udp_header_t*)data;

    /* Convertir les ports en host byte order */
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t udp_len = ntohs(udp->length);

    /* Vérifier la longueur */
    if (udp_len < UDP_HEADER_SIZE || udp_len > len) {
        KLOG_ERROR_DEC("UDP", "Invalid length: ", udp_len);
        return;
    }

    /* Calculer le pointeur vers le payload UDP */
    uint8_t* payload = data + UDP_HEADER_SIZE;
    int payload_len = udp_len - UDP_HEADER_SIZE;
    (void)payload;      /* Éviter warning unused */
    (void)payload_len;  /* Éviter warning unused */

    /* Dispatcher selon le port de destination OU le port source pour les réponses */
    switch (dest_port) {
        case UDP_PORT_DHCP_CLIENT:
            /* DHCP response - router vers le handler DHCP */
            dhcp_handle_packet(NULL, payload, payload_len);
            return;

        case UDP_PORT_DNS:
            /* DNS query (rare, on est client) */
            dns_handle_packet(payload, payload_len);
            return;

        default:
            break;
    }
    
    /* Vérifier aussi le port SOURCE pour les réponses */
    switch (src_port) {
        case UDP_PORT_DNS:
            /* DNS response (serveur répond depuis port 53) */
            dns_handle_packet(payload, payload_len);
            return;

        default:
            /* Pas de handler pour les autres ports, en clair, ils ne sont pas en écoute. */
            break;
    }
}

/**
 * Envoie un paquet UDP.
 */
void udp_send_packet(uint8_t* dest_ip, uint16_t src_port, uint16_t dest_port,
                     uint8_t* data, int len)
{
    /* Buffer pour le paquet UDP (header + payload) */
    uint8_t buffer[1500];  /* MTU - headers */
    
    /* Vérifier que le paquet n'est pas trop grand */
    if (len > (int)(sizeof(buffer) - UDP_HEADER_SIZE)) {
        KLOG_ERROR_DEC("UDP", "Payload too large: ", len);
        return;
    }

    /* === Construire le header UDP === */
    udp_header_t* udp = (udp_header_t*)buffer;
    
    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(UDP_HEADER_SIZE + len);
    udp->checksum = 0;  /* Optionnel en IPv4, on le laisse à 0 */

    /* === Copier le payload === */
    uint8_t* payload = buffer + UDP_HEADER_SIZE;
    for (int i = 0; i < len; i++) {
        payload[i] = data[i];
    }

    /* === Résoudre la MAC de destination === */
    uint8_t dest_mac[6];
    uint8_t next_hop[4];
    
    /* Trouver le next hop (gateway si nécessaire) */
    if (!route_get_next_hop(dest_ip, next_hop)) {
        KLOG_ERROR("UDP", "No route to destination");
        return;
    }
    
    /* Obtenir l'interface par défaut */
    NetInterface* netif = netif_get_default();
    
    /* Résoudre la MAC via ARP cache */
    if (!arp_cache_lookup(next_hop, dest_mac)) {
        /* MAC pas dans le cache, envoyer une requête ARP */
        KLOG_WARN("UDP", "MAC unknown, sending ARP request...");
        
        arp_send_request(netif, next_hop);
        
        /* Pour l'instant, on abandonne - dans un vrai OS, on mettrait
         * le paquet en queue et on réessaierait après la réponse ARP */
        KLOG_ERROR("UDP", "Packet dropped (ARP pending)");
        return;
    }

    /* === Envoyer via IPv4 === */
    ipv4_send_packet(netif, dest_mac, dest_ip, IP_PROTO_UDP, buffer, UDP_HEADER_SIZE + len);
}
