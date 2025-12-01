/* src/net/l3/ipv4.c - IPv4 Protocol Handler */
#include "ipv4.h"
#include "icmp.h"
#include "../l4/udp.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "../l2/ethernet.h"
#include "../utils.h"
#include "../../kernel/console.h"

/* Compteur d'identification pour les paquets sortants */
static uint16_t ip_id_counter = 0;

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
 * Compare deux adresses IP.
 */
static bool ip_addr_equals(const uint8_t* ip1, const uint8_t* ip2)
{
    return (ip1[0] == ip2[0] && ip1[1] == ip2[1] &&
            ip1[2] == ip2[2] && ip1[3] == ip2[3]);
}

/**
 * Calcule le checksum Internet (RFC 1071).
 * Somme complémentée à 1 sur 16 bits.
 */
uint16_t ip_checksum(void* data, int len)
{
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)data;
    
    /* Sommer tous les mots de 16 bits */
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    /* Ajouter le dernier octet s'il reste */
    if (len == 1) {
        sum += *((uint8_t*)ptr);
    }
    
    /* Replier les bits de carry */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    /* Retourner le complément à 1 */
    return (uint16_t)(~sum);
}

/**
 * Traite un paquet IPv4 reçu.
 */
void ipv4_handle_packet(NetInterface* netif, ethernet_header_t* eth, uint8_t* data, int len)
{
    /* Vérifier la taille minimale */
    if (data == NULL || len < IPV4_HEADER_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[IPv4] Packet too short: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Caster en header IPv4 */
    ipv4_header_t* ip = (ipv4_header_t*)data;
    
    /* Extraire la version (4 bits de poids fort) */
    uint8_t version = (ip->version_ihl >> 4) & 0x0F;
    
    /* Extraire IHL (Internet Header Length) en mots de 32 bits */
    uint8_t ihl = ip->version_ihl & 0x0F;
    int header_len = ihl * 4;  /* Longueur en bytes */
    
    /* Vérifier que c'est bien IPv4 */
    if (version != 4) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[IPv4] Invalid version: ");
        console_put_dec(version);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Vérifier que le header est assez grand */
    if (header_len < IPV4_HEADER_SIZE || header_len > len) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[IPv4] Invalid header length: ");
        console_put_dec(header_len);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Obtenir notre IP depuis l'interface ou les globales */
    uint8_t my_ip[4];
    if (netif != NULL && netif->ip_addr != 0) {
        ip_u32_to_bytes(netif->ip_addr, my_ip);
    } else {
        for (int i = 0; i < 4; i++) {
            my_ip[i] = MY_IP[i];
        }
    }
    
    /* Vérifier l'IP de destination:
     * - Accepter si c'est notre IP
     * - Accepter si c'est broadcast (255.255.255.255) - nécessaire pour DHCP
     * - Accepter si notre IP est 0.0.0.0 (pendant DHCP on n'a pas d'IP)
     * - Accepter les paquets UDP port 68 (DHCP client) pendant DHCP
     */
    bool is_broadcast = (ip->dest_ip[0] == 255 && ip->dest_ip[1] == 255 &&
                         ip->dest_ip[2] == 255 && ip->dest_ip[3] == 255);
    bool we_have_no_ip = (my_ip[0] == 0 && my_ip[1] == 0 &&
                          my_ip[2] == 0 && my_ip[3] == 0);
    bool is_for_us = ip_addr_equals(ip->dest_ip, my_ip);
    
    /* Pendant DHCP, accepter aussi les paquets unicast destinés à l'IP offerte */
    bool is_dhcp_response = (ip->protocol == IP_PROTO_UDP && we_have_no_ip);
    
    if (!is_for_us && !is_broadcast && !we_have_no_ip && !is_dhcp_response) {
        /* Debug: afficher les paquets rejetés */
        console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLUE);
        console_puts("[IPv4] REJECTED: ");
        print_ip(ip->src_ip);
        console_puts(" -> ");
        print_ip(ip->dest_ip);
        console_puts(" (our IP: ");
        print_ip(my_ip);
        console_puts(")\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Log: paquet IPv4 reçu pour nous */
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    console_puts("[IPv4] Received from ");
    print_ip(ip->src_ip);
    console_puts(" -> ");
    print_ip(ip->dest_ip);
    console_puts(" (Proto=");
    console_put_dec(ip->protocol);
    console_puts(", TTL=");
    console_put_dec(ip->ttl);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Calculer le pointeur vers le payload */
    uint8_t* payload = data + header_len;
    int payload_len = ntohs(ip->total_length) - header_len;
    
    /* Dispatcher selon le protocole */
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            icmp_handle_packet(netif, eth, ip, payload, payload_len);
            break;
            
        case IP_PROTO_TCP:
            console_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
            console_puts("[IPv4] TCP packet (not implemented)\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;
            
        case IP_PROTO_UDP:
            udp_handle_packet(ip, payload, payload_len);
            break;
            
        default:
            console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLUE);
            console_puts("[IPv4] Unknown protocol: ");
            console_put_dec(ip->protocol);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            break;
    }
}

/**
 * Envoie un paquet IPv4.
 */
void ipv4_send_packet(NetInterface* netif, uint8_t* dest_mac, uint8_t* dest_ip, 
                      uint8_t protocol, uint8_t* payload, int payload_len)
{
    /* Buffer pour le paquet complet (Ethernet + IPv4 + payload) */
    /* Taille max: 14 (Eth) + 20 (IP) + payload, min 60 bytes */
    uint8_t buffer[1518];  /* MTU standard */
    int total_len = ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE + payload_len;
    
    /* Padding à 60 bytes minimum (Ethernet) */
    if (total_len < 60) {
        /* Initialiser à zéro pour le padding */
        for (int i = 0; i < 60; i++) {
            buffer[i] = 0;
        }
        total_len = 60;
    }
    
    /* Obtenir notre MAC et IP depuis l'interface ou les globales */
    uint8_t my_mac[6];
    uint8_t my_ip[4];
    
    if (netif != NULL) {
        for (int i = 0; i < 6; i++) {
            my_mac[i] = netif->mac_addr[i];
        }
        ip_u32_to_bytes(netif->ip_addr, my_ip);
    } else {
        netdev_get_mac(my_mac);
        for (int i = 0; i < 4; i++) {
            my_ip[i] = MY_IP[i];
        }
    }
    
    /* === Construire le header Ethernet === */
    ethernet_header_t* eth = (ethernet_header_t*)buffer;
    
    for (int i = 0; i < 6; i++) {
        eth->dest_mac[i] = dest_mac[i];
        eth->src_mac[i] = my_mac[i];
    }
    eth->ethertype = htons(ETH_TYPE_IPV4);
    
    /* === Construire le header IPv4 === */
    ipv4_header_t* ip = (ipv4_header_t*)(buffer + ETHERNET_HEADER_SIZE);
    
    /* Version 4 + IHL 5 (pas d'options) */
    ip->version_ihl = (4 << 4) | 5;
    
    /* Type of Service: 0 (normal) */
    ip->tos = 0;
    
    /* Total Length (header + payload) */
    ip->total_length = htons(IPV4_HEADER_SIZE + payload_len);
    
    /* Identification (compteur incrémental) */
    ip->identification = htons(ip_id_counter++);
    
    /* Flags: Don't Fragment (0x4000), Fragment Offset: 0 */
    ip->flags_fragment = htons(0x4000);
    
    /* TTL */
    ip->ttl = IP_DEFAULT_TTL;
    
    /* Protocol */
    ip->protocol = protocol;
    
    /* Checksum: d'abord à zéro pour le calcul */
    ip->checksum = 0;
    
    /* Adresses IP */
    for (int i = 0; i < 4; i++) {
        ip->src_ip[i] = my_ip[i];
        ip->dest_ip[i] = dest_ip[i];
    }
    
    /* Calculer et écrire le checksum du header IP */
    ip->checksum = ip_checksum(ip, IPV4_HEADER_SIZE);
    
    /* === Copier le payload === */
    uint8_t* data = buffer + ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE;
    for (int i = 0; i < payload_len; i++) {
        data[i] = payload[i];
    }
    
    /* === Envoyer le paquet via l'interface ou l'ancienne API === */
    bool sent = false;
    if (netif != NULL && netif->send != NULL) {
        sent = (netif->send(netif, buffer, total_len) >= 0);
    } else {
        sent = netdev_send(buffer, total_len);
    }
    
    if (sent) {
        /* Log */
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
        console_puts("[IPv4] Sent to ");
        print_ip(dest_ip);
        console_puts(" (Proto=");
        console_put_dec(protocol);
        console_puts(", ");
        console_put_dec(payload_len);
        console_puts(" bytes)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[IPv4] Error: No network device!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    }
}
