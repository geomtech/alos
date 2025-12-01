/* src/net/l3/icmp.c - ICMP Protocol Handler */
#include "icmp.h"
#include "ipv4.h"
#include "../core/net.h"
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
 * Traite un paquet ICMP reçu.
 */
void icmp_handle_packet(NetInterface* netif, ethernet_header_t* eth, 
                        ipv4_header_t* ip_hdr, uint8_t* icmp_data, int len)
{
    /* Vérifier la taille minimale */
    if (icmp_data == NULL || len < ICMP_HEADER_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[ICMP] Packet too short: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Caster en header ICMP */
    icmp_header_t* icmp = (icmp_header_t*)icmp_data;
    
    /* Log du paquet reçu */
    console_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLUE);
    console_puts("[ICMP] Type=");
    console_put_dec(icmp->type);
    console_puts(" Code=");
    console_put_dec(icmp->code);
    
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST || icmp->type == ICMP_TYPE_ECHO_REPLY) {
        console_puts(" ID=");
        console_put_hex(ntohs(icmp->identifier));
        console_puts(" Seq=");
        console_put_dec(ntohs(icmp->sequence));
    }
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Traiter selon le type */
    switch (icmp->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            /* Ping Request - on doit répondre! */
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
            console_puts("[ICMP] Echo Request from ");
            print_ip(ip_hdr->src_ip);
            console_puts(" - Sending Reply!\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* === Construire la réponse ICMP === */
            
            /* Buffer pour le paquet ICMP de réponse */
            /* On réutilise la même taille que le paquet reçu */
            uint8_t reply_buffer[1500];
            
            /* Copier tout le paquet ICMP (header + données) */
            for (int i = 0; i < len && i < 1500; i++) {
                reply_buffer[i] = icmp_data[i];
            }
            
            /* Transformer en Echo Reply */
            icmp_header_t* reply = (icmp_header_t*)reply_buffer;
            reply->type = ICMP_TYPE_ECHO_REPLY;
            reply->code = 0;
            
            /* Recalculer le checksum */
            /* 1. Mettre le checksum à 0 */
            reply->checksum = 0;
            
            /* 2. Calculer le nouveau checksum sur tout le paquet ICMP */
            reply->checksum = ip_checksum(reply_buffer, len);
            
            /* Envoyer la réponse via IPv4 en utilisant l'interface */
            /* On utilise la MAC source du paquet reçu comme destination */
            ipv4_send_packet(netif, eth->src_mac, ip_hdr->src_ip, IP_PROTO_ICMP,
                           reply_buffer, len);
            break;
            
        case ICMP_TYPE_ECHO_REPLY:
            /* Ping Reply - on l'affiche juste */
            console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
            console_puts("[ICMP] Echo Reply from ");
            print_ip(ip_hdr->src_ip);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;
            
        case ICMP_TYPE_DEST_UNREACH:
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            console_puts("[ICMP] Destination Unreachable\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;
            
        case ICMP_TYPE_TIME_EXCEEDED:
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            console_puts("[ICMP] Time Exceeded\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;
            
        default:
            console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLUE);
            console_puts("[ICMP] Unknown type: ");
            console_put_dec(icmp->type);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;
    }
}
