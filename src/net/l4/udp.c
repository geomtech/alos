/* src/net/l4/udp.c - UDP Protocol Handler */
#include "udp.h"
#include "dhcp.h"
#include "../l3/ipv4.h"
#include "../l3/route.h"
#include "../l2/arp.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/console.h"

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
 * Traite un paquet UDP reçu.
 */
void udp_handle_packet(ipv4_header_t* ip_hdr, uint8_t* data, int len)
{
    /* Vérifier la taille minimale */
    if (data == NULL || len < UDP_HEADER_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[UDP] Packet too short: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
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
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[UDP] Invalid length: ");
        console_put_dec(udp_len);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }

    /* Log: paquet UDP reçu */
    console_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLUE);
    console_puts("[UDP] Packet received on Port ");
    console_put_dec(dest_port);
    console_puts(" from ");
    print_ip(ip_hdr->src_ip);
    console_puts(":");
    console_put_dec(src_port);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    /* Calculer le pointeur vers le payload UDP */
    uint8_t* payload = data + UDP_HEADER_SIZE;
    int payload_len = udp_len - UDP_HEADER_SIZE;
    (void)payload;      /* Éviter warning unused */
    (void)payload_len;  /* Éviter warning unused */

    /* Dispatcher selon le port de destination */
    switch (dest_port) {
        case UDP_PORT_DHCP_CLIENT:
            /* DHCP response - router vers le handler DHCP */
            dhcp_handle_packet(NULL, payload, payload_len);
            break;

        case UDP_PORT_DNS:
            /* DNS response (à implémenter) */
            console_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
            console_puts("[UDP] DNS packet (not implemented)\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;

        default:
            /* Port non géré */
            console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLUE);
            console_puts("[UDP] No handler for port ");
            console_put_dec(dest_port);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
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
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[UDP] Payload too large: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
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
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[UDP] No route to ");
        print_ip(dest_ip);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Obtenir l'interface par défaut */
    NetInterface* netif = netif_get_default();
    
    /* Résoudre la MAC via ARP cache */
    if (!arp_cache_lookup(next_hop, dest_mac)) {
        /* MAC pas dans le cache, envoyer une requête ARP */
        console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLUE);
        console_puts("[UDP] MAC unknown for ");
        print_ip(next_hop);
        console_puts(", sending ARP request...\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        
        arp_send_request(netif, next_hop);
        
        /* Pour l'instant, on abandonne - dans un vrai OS, on mettrait
         * le paquet en queue et on réessaierait après la réponse ARP */
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[UDP] Packet dropped (ARP pending)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }

    /* === Envoyer via IPv4 === */
    ipv4_send_packet(netif, dest_mac, dest_ip, IP_PROTO_UDP, buffer, UDP_HEADER_SIZE + len);

    /* Log */
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[UDP] Sent ");
    console_put_dec(len);
    console_puts(" bytes to ");
    print_ip(dest_ip);
    console_puts(":");
    console_put_dec(dest_port);
    console_puts(" from port ");
    console_put_dec(src_port);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}
