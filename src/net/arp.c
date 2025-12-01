/* src/net/arp.c - Address Resolution Protocol Handler */
#include "arp.h"
#include "net.h"
#include "utils.h"
#include "../console.h"

/**
 * Affiche une adresse MAC au format XX:XX:XX:XX:XX:XX
 */
static void print_mac(const uint8_t* mac)
{
    for (int i = 0; i < 6; i++) {
        if (i > 0) console_putc(':');
        console_put_hex_byte(mac[i]);
    }
}

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
 * Traite un paquet ARP reçu.
 * 
 * Pour l'instant, on ne fait que parser et afficher les infos.
 * Ultérieurement, on répondra aux ARP Requests pour notre IP.
 */
void arp_handle_packet(ethernet_header_t* eth, uint8_t* packet_data, int len)
{
    (void)eth;  /* Utilisé plus tard pour construire la réponse */
    
    /* Vérifier la taille minimale */
    if (len < ARP_PACKET_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[ARP] Packet too short: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Caster les données en structure ARP */
    arp_packet_t* arp = (arp_packet_t*)packet_data;
    
    /* Vérifier que c'est bien de l'ARP pour Ethernet/IPv4 */
    uint16_t hw_type = ntohs(arp->hardware_type);
    uint16_t proto_type = ntohs(arp->protocol_type);
    uint16_t opcode = ntohs(arp->opcode);
    
    if (hw_type != ARP_HW_ETHERNET || proto_type != ARP_PROTO_IPV4) {
        console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLUE);
        console_puts("[ARP] Unsupported HW/Proto type: ");
        console_put_hex(hw_type);
        console_puts("/");
        console_put_hex(proto_type);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Traiter selon l'opcode */
    switch (opcode) {
        case ARP_OP_REQUEST:
            /* ARP Request - quelqu'un cherche une adresse MAC */
            console_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLUE);
            console_puts("[ARP] Request: Who has ");
            print_ip(arp->dest_ip);
            console_puts("? Tell ");
            print_ip(arp->src_ip);
            console_puts(" (");
            print_mac(arp->src_mac);
            console_puts(")\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* Vérifier si c'est pour nous */
            if (ip_equals(arp->dest_ip, MY_IP)) {
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                console_puts("[ARP] >>> That's us! Should reply... <<<\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                /* TODO: Envoyer un ARP Reply */
            }
            break;
            
        case ARP_OP_REPLY:
            /* ARP Reply - quelqu'un nous donne son adresse MAC */
            console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
            console_puts("[ARP] Reply: ");
            print_ip(arp->src_ip);
            console_puts(" is at ");
            print_mac(arp->src_mac);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            /* TODO: Mettre à jour la table ARP */
            break;
            
        default:
            console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLUE);
            console_puts("[ARP] Unknown opcode: ");
            console_put_dec(opcode);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;
    }
}
