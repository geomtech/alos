/* src/net/l2/arp.c - Address Resolution Protocol Handler */
#include "arp.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "ethernet.h"
#include "../utils.h"
#include "../../kernel/console.h"

/* ========================================
 * Cache ARP
 * ======================================== */

/* Taille maximale du cache ARP */
#define ARP_CACHE_SIZE  16

/* Entrée du cache ARP */
typedef struct {
    uint8_t ip[4];          /* Adresse IP */
    uint8_t mac[6];         /* Adresse MAC */
    bool    valid;          /* Entrée valide? */
} arp_cache_entry_t;

/* Table du cache ARP */
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static int arp_cache_count = 0;

/**
 * Compare deux adresses IP.
 */
static bool arp_ip_equals(const uint8_t* ip1, const uint8_t* ip2)
{
    return (ip1[0] == ip2[0] && ip1[1] == ip2[1] &&
            ip1[2] == ip2[2] && ip1[3] == ip2[3]);
}

/**
 * Copie une adresse MAC.
 */
static void mac_copy(uint8_t* dest, const uint8_t* src)
{
    for (int i = 0; i < 6; i++) {
        dest[i] = src[i];
    }
}

/**
 * Copie une adresse IP.
 */
static void arp_ip_copy(uint8_t* dest, const uint8_t* src)
{
    for (int i = 0; i < 4; i++) {
        dest[i] = src[i];
    }
}

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

/* ========================================
 * Fonctions du cache ARP
 * ======================================== */

/**
 * Ajoute ou met à jour une entrée dans le cache ARP.
 */
void arp_cache_add(uint8_t* ip, uint8_t* mac)
{
    /* Chercher si l'entrée existe déjà */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_ip_equals(arp_cache[i].ip, ip)) {
            /* Mettre à jour la MAC */
            mac_copy(arp_cache[i].mac, mac);
            return;
        }
    }
    
    /* Chercher une entrée libre */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_ip_copy(arp_cache[i].ip, ip);
            mac_copy(arp_cache[i].mac, mac);
            arp_cache[i].valid = true;
            arp_cache_count++;
            
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            console_puts("[ARP] Cache: Added ");
            print_ip(ip);
            console_puts(" -> ");
            print_mac(mac);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return;
        }
    }
    
    /* Cache plein, écraser la première entrée (simple LRU) */
    arp_ip_copy(arp_cache[0].ip, ip);
    mac_copy(arp_cache[0].mac, mac);
    arp_cache[0].valid = true;
    
    console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    console_puts("[ARP] Cache full, replaced entry 0\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/**
 * Cherche une adresse MAC dans le cache ARP.
 */
bool arp_cache_lookup(uint8_t* ip, uint8_t* mac_out)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_ip_equals(arp_cache[i].ip, ip)) {
            mac_copy(mac_out, arp_cache[i].mac);
            return true;
        }
    }
    return false;
}

/**
 * Envoie une requête ARP.
 */
void arp_send_request(NetInterface* netif, uint8_t* target_ip)
{
    /* Buffer de 60 octets (taille min Ethernet) */
    uint8_t buffer[60];
    
    /* Utiliser la MAC de l'interface si fournie, sinon fallback sur l'ancienne API */
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
    
    /* Initialiser à zéro */
    for (int i = 0; i < 60; i++) {
        buffer[i] = 0;
    }
    
    /* === Ethernet Header === */
    ethernet_header_t* eth = (ethernet_header_t*)buffer;
    
    /* Destination = Broadcast */
    for (int i = 0; i < 6; i++) {
        eth->dest_mac[i] = 0xFF;
    }
    
    /* Source = notre MAC */
    for (int i = 0; i < 6; i++) {
        eth->src_mac[i] = my_mac[i];
    }
    
    eth->ethertype = htons(ETH_TYPE_ARP);
    
    /* === ARP Packet === */
    arp_packet_t* arp = (arp_packet_t*)(buffer + ETHERNET_HEADER_SIZE);
    
    arp->hardware_type = htons(ARP_HW_ETHERNET);
    arp->protocol_type = htons(ARP_PROTO_IPV4);
    arp->hardware_size = 6;
    arp->protocol_size = 4;
    arp->opcode = htons(ARP_OP_REQUEST);
    
    /* Sender = nous */
    for (int i = 0; i < 6; i++) {
        arp->src_mac[i] = my_mac[i];
    }
    for (int i = 0; i < 4; i++) {
        arp->src_ip[i] = my_ip[i];
    }
    
    /* Target = MAC inconnue (0), IP cible */
    for (int i = 0; i < 6; i++) {
        arp->dest_mac[i] = 0x00;
    }
    for (int i = 0; i < 4; i++) {
        arp->dest_ip[i] = target_ip[i];
    }
    
    /* Envoyer */
    bool sent = false;
    if (netif != NULL && netif->send != NULL) {
        sent = (netif->send(netif, buffer, 60) >= 0);
    } else {
        sent = netdev_send(buffer, 60);
    }
    
    if (sent) {
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts("[ARP] Request: Who has ");
        print_ip(target_ip);
        console_puts("?\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[ARP] Error sending request!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

/**
 * Envoie une réponse ARP.
 * 
 * @param netif      Interface réseau à utiliser pour l'envoi
 * @param target_mac MAC de la cible (celui qui a envoyé la requête)
 * @param target_ip  IP de la cible
 */
void arp_send_reply(NetInterface* netif, uint8_t* target_mac, uint8_t* target_ip)
{
    /* Buffer de 60 octets (taille min Ethernet) sur la stack */
    uint8_t buffer[60];
    
    /* Utiliser la MAC/IP de l'interface si fournie, sinon fallback */
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
    
    /* Initialiser à zéro (padding) */
    for (int i = 0; i < 60; i++) {
        buffer[i] = 0;
    }
    
    /* === Ethernet Header (14 bytes) === */
    ethernet_header_t* eth = (ethernet_header_t*)buffer;
    
    /* Destination MAC = celui qui a fait la requête */
    for (int i = 0; i < 6; i++) {
        eth->dest_mac[i] = target_mac[i];
    }
    
    /* Source MAC = notre MAC */
    for (int i = 0; i < 6; i++) {
        eth->src_mac[i] = my_mac[i];
    }
    
    /* EtherType = ARP (0x0806) en big-endian */
    eth->ethertype = htons(ETH_TYPE_ARP);
    
    /* === ARP Packet (28 bytes) === */
    arp_packet_t* arp = (arp_packet_t*)(buffer + ETHERNET_HEADER_SIZE);
    
    /* Hardware Type = Ethernet (1) */
    arp->hardware_type = htons(ARP_HW_ETHERNET);
    
    /* Protocol Type = IPv4 (0x0800) */
    arp->protocol_type = htons(ARP_PROTO_IPV4);
    
    /* Hardware Size = 6 (MAC address) */
    arp->hardware_size = 6;
    
    /* Protocol Size = 4 (IPv4 address) */
    arp->protocol_size = 4;
    
    /* Opcode = Reply (2) */
    arp->opcode = htons(ARP_OP_REPLY);
    
    /* Sender MAC = notre MAC */
    for (int i = 0; i < 6; i++) {
        arp->src_mac[i] = my_mac[i];
    }
    
    /* Sender IP = notre IP */
    for (int i = 0; i < 4; i++) {
        arp->src_ip[i] = my_ip[i];
    }
    
    /* Target MAC = MAC de celui qui a demandé */
    for (int i = 0; i < 6; i++) {
        arp->dest_mac[i] = target_mac[i];
    }
    
    /* Target IP = IP de celui qui a demandé */
    for (int i = 0; i < 4; i++) {
        arp->dest_ip[i] = target_ip[i];
    }
    
    /* Envoyer le paquet via l'interface ou l'ancienne API */
    bool sent = false;
    if (netif != NULL && netif->send != NULL) {
        sent = (netif->send(netif, buffer, 60) >= 0);
    } else {
        sent = netdev_send(buffer, 60);
    }
    
    if (sent) {
        /* Log */
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("[ARP] Sent Reply: ");
        print_ip(my_ip);
        console_puts(" is at ");
        print_mac(my_mac);
        console_puts(" -> ");
        print_mac(target_mac);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[ARP] Error: No network device!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

/**
 * Traite un paquet ARP reçu.
 */
void arp_handle_packet(NetInterface* netif, ethernet_header_t* eth, uint8_t* packet_data, int len)
{
    (void)eth;  /* Utilisé plus tard pour construire la réponse */
    
    /* Vérifier la taille minimale */
    if (len < ARP_PACKET_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[ARP] Packet too short: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* Caster les données en structure ARP */
    arp_packet_t* arp = (arp_packet_t*)packet_data;
    
    /* Vérifier que c'est bien de l'ARP pour Ethernet/IPv4 */
    uint16_t hw_type = ntohs(arp->hardware_type);
    uint16_t proto_type = ntohs(arp->protocol_type);
    uint16_t opcode = ntohs(arp->opcode);
    
    if (hw_type != ARP_HW_ETHERNET || proto_type != ARP_PROTO_IPV4) {
        console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        console_puts("[ARP] Unsupported HW/Proto type: ");
        console_put_hex(hw_type);
        console_puts("/");
        console_put_hex(proto_type);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
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
    
    /* Traiter selon l'opcode */
    switch (opcode) {
        case ARP_OP_REQUEST:
            /* ARP Request - quelqu'un cherche une adresse MAC */
            console_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
            console_puts("[ARP] Request: Who has ");
            print_ip(arp->dest_ip);
            console_puts("? Tell ");
            print_ip(arp->src_ip);
            console_puts(" (");
            print_mac(arp->src_mac);
            console_puts(")\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            
            /* Ajouter l'émetteur au cache ARP (on apprend de chaque request) */
            arp_cache_add(arp->src_ip, arp->src_mac);
            
            /* Vérifier si c'est pour nous */
            if (arp_ip_equals(arp->dest_ip, my_ip)) {
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                console_puts("[ARP] >>> That's us! Sending reply... <<<\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                
                /* Envoyer la réponse ARP via l'interface */
                arp_send_reply(netif, arp->src_mac, arp->src_ip);
            }
            break;
            
        case ARP_OP_REPLY:
            /* ARP Reply - quelqu'un nous donne son adresse MAC */
            console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            console_puts("[ARP] Reply: ");
            print_ip(arp->src_ip);
            console_puts(" is at ");
            print_mac(arp->src_mac);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            
            /* Ajouter au cache ARP */
            arp_cache_add(arp->src_ip, arp->src_mac);
            break;
            
        default:
            console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
            console_puts("[ARP] Unknown opcode: ");
            console_put_dec(opcode);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            break;
    }
}
