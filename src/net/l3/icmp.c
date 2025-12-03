/* src/net/l3/icmp.c - ICMP Protocol Handler */
#include "icmp.h"
#include "ipv4.h"
#include "route.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "../l2/arp.h"
#include "../l4/dns.h"
#include "../utils.h"
#include "../netlog.h"

/* ========================================
 * Variables globales pour le ping
 * ======================================== */

static ping_state_t g_ping = {0};
static uint16_t g_ping_id = 0x1234;  /* ID unique pour nos pings */

/**
 * Affiche une adresse IP au format X.X.X.X
 */
static void print_ip(const uint8_t* ip)
{
    for (int i = 0; i < 4; i++) {
        if (i > 0) net_putc('.');
        net_put_dec(ip[i]);
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
        net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        net_puts("[ICMP] Packet too short: ");
        net_put_dec(len);
        net_puts(" bytes\n");
        net_reset_color();
        return;
    }
    
    /* Caster en header ICMP */
    icmp_header_t* icmp = (icmp_header_t*)icmp_data;
    
    /* Log du paquet reçu */
    net_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
    net_puts("[ICMP] Type=");
    net_put_dec(icmp->type);
    net_puts(" Code=");
    net_put_dec(icmp->code);
    
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST || icmp->type == ICMP_TYPE_ECHO_REPLY) {
        net_puts(" ID=");
        net_put_hex(ntohs(icmp->identifier));
        net_puts(" Seq=");
        net_put_dec(ntohs(icmp->sequence));
    }
    net_puts("\n");
    net_reset_color();
    
    /* Traiter selon le type */
    switch (icmp->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            /* Ping Request - on doit répondre! */
            net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            net_puts("[ICMP] Echo Request from ");
            print_ip(ip_hdr->src_ip);
            net_puts(" - Sending Reply!\n");
            net_reset_color();
            
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
            /* Ping Reply - vérifier si c'est notre ping */
            if (g_ping.active && g_ping.waiting) {
                uint16_t reply_id = ntohs(icmp->identifier);
                uint16_t reply_seq = ntohs(icmp->sequence);
                
                if (reply_id == g_ping.identifier) {
                    g_ping.received++;
                    g_ping.waiting = false;
                    
                    net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                    net_puts("[PING] Reply from ");
                    print_ip(ip_hdr->src_ip);
                    net_puts(": seq=");
                    net_put_dec(reply_seq);
                    net_puts("\n");
                    net_reset_color();
                } else {
                    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
                    net_puts("[ICMP] Echo Reply from ");
                    print_ip(ip_hdr->src_ip);
                    net_puts(" (not our ping)\n");
                    net_reset_color();
                }
            } else {
                net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
                net_puts("[ICMP] Echo Reply from ");
                print_ip(ip_hdr->src_ip);
                net_puts("\n");
                net_reset_color();
            }
            break;
            
        case ICMP_TYPE_DEST_UNREACH:
            net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            net_puts("[ICMP] Destination Unreachable\n");
            net_reset_color();
            break;
            
        case ICMP_TYPE_TIME_EXCEEDED:
            net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            net_puts("[ICMP] Time Exceeded\n");
            net_reset_color();
            break;
            
        default:
            net_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
            net_puts("[ICMP] Unknown type: ");
            net_put_dec(icmp->type);
            net_puts("\n");
            net_reset_color();
            break;
    }
}

/* ========================================
 * Fonctions d'envoi de ping
 * ======================================== */

/**
 * Copie une chaîne
 */
static void icmp_str_copy(char* dest, const char* src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/**
 * Envoie un Echo Request vers une IP
 */
void icmp_send_echo_request(const uint8_t* dest_ip)
{
    NetInterface* netif = netif_get_default();
    if (netif == NULL) {
        net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        net_puts("[PING] No network interface!\n");
        net_reset_color();
        return;
    }
    
    /* Buffer pour le paquet ICMP */
    uint8_t buffer[ICMP_HEADER_SIZE + PING_DATA_SIZE];
    
    /* Construire le header ICMP */
    icmp_header_t* icmp = (icmp_header_t*)buffer;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->identifier = htons(g_ping.identifier);
    icmp->sequence = htons(g_ping.sequence);
    
    /* Remplir les données avec un pattern */
    for (int i = 0; i < PING_DATA_SIZE; i++) {
        buffer[ICMP_HEADER_SIZE + i] = (uint8_t)(i & 0xFF);
    }
    
    /* Calculer le checksum */
    icmp->checksum = ip_checksum(buffer, ICMP_HEADER_SIZE + PING_DATA_SIZE);
    
    /* Trouver le next-hop et la MAC */
    uint8_t next_hop[4];
    uint8_t dest_mac[6];
    
    if (!route_get_next_hop((uint8_t*)dest_ip, next_hop)) {
        net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        net_puts("[PING] No route to ");
        print_ip(dest_ip);
        net_puts("\n");
        net_reset_color();
        return;
    }
    
    /* Chercher la MAC dans le cache ARP */
    if (!arp_cache_lookup(next_hop, dest_mac)) {
        /* Envoyer une requête ARP */
        net_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        net_puts("[PING] Resolving MAC for ");
        print_ip(next_hop);
        net_puts("...\n");
        net_reset_color();
        
        arp_send_request(netif, next_hop);
        return;  /* Le ping sera réessayé */
    }
    
    /* Envoyer le paquet */
    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    net_puts("[PING] Sending to ");
    print_ip(dest_ip);
    net_puts(" seq=");
    net_put_dec(g_ping.sequence);
    net_puts("\n");
    net_reset_color();
    
    /* Marquer comme en attente AVANT l'envoi (la réponse peut arriver très vite) */
    g_ping.sent++;
    g_ping.waiting = true;
    
    ipv4_send_packet(netif, dest_mac, (uint8_t*)dest_ip, IP_PROTO_ICMP,
                     buffer, ICMP_HEADER_SIZE + PING_DATA_SIZE);
}

/**
 * Ping une adresse IP
 */
int ping_ip(const uint8_t* dest_ip)
{
    /* Initialiser l'état du ping */
    g_ping_id++;
    g_ping.identifier = g_ping_id;
    g_ping.sequence = 1;
    g_ping.sent = 0;
    g_ping.received = 0;
    g_ping.waiting = false;
    g_ping.active = true;
    g_ping.dest_ip[0] = dest_ip[0];
    g_ping.dest_ip[1] = dest_ip[1];
    g_ping.dest_ip[2] = dest_ip[2];
    g_ping.dest_ip[3] = dest_ip[3];
    
    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    net_puts("[PING] ");
    print_ip(dest_ip);
    if (g_ping.hostname[0]) {
        net_puts(" (");
        net_puts(g_ping.hostname);
        net_puts(")");
    }
    net_puts("\n");
    net_reset_color();
    
    /* Envoyer le premier ping (peut échouer si ARP pending) */
    icmp_send_echo_request(dest_ip);
    
    /* Attendre la réponse avec timeout et retry */
    int timeout_count = 0;
    int retry_count = 0;
    
    while (timeout_count < 60) {
        for (volatile int w = 0; w < 500000; w++);
        asm volatile("sti");
        asm volatile("hlt");
        timeout_count++;
        
        /* Si pas encore envoyé (ARP était pending), réessayer */
        if (g_ping.sent == 0 && retry_count < 3) {
            if (timeout_count == 5 || timeout_count == 15 || timeout_count == 25) {
                icmp_send_echo_request(dest_ip);
                retry_count++;
            }
        }
        
        /* Si envoyé et réponse reçue, on a fini */
        if (g_ping.sent > 0 && !g_ping.waiting) {
            break;
        }
    }
    
    /* Afficher les statistiques */
    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    net_puts("[PING] ");
    net_put_dec(g_ping.sent);
    net_puts(" sent, ");
    net_put_dec(g_ping.received);
    net_puts(" received");
    if (g_ping.sent > 0 && g_ping.received == 0) {
        net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        net_puts(" (100% loss)");
    } else if (g_ping.received > 0) {
        net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        net_puts(" (0% loss)");
    }
    net_puts("\n");
    net_reset_color();
    
    g_ping.active = false;
    
    return (g_ping.received > 0) ? 0 : -2;
}

/**
 * Ping un hostname (résolution DNS puis ping)
 */
int ping(const char* hostname)
{
    uint8_t ip[4];
    
    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    net_puts("[PING] Resolving ");
    net_puts(hostname);
    net_puts("...\n");
    net_reset_color();
    
    /* Vérifier le cache DNS d'abord */
    if (dns_cache_lookup(hostname, ip)) {
        icmp_str_copy(g_ping.hostname, hostname, sizeof(g_ping.hostname));
        return ping_ip(ip);
    }
    
    /* Envoyer la requête DNS */
    dns_send_query(hostname);
    
    /* Attendre la résolution */
    int timeout = 0;
    while (dns_is_pending() && timeout < 50) {
        for (volatile int w = 0; w < 500000; w++);
        asm volatile("sti");
        asm volatile("hlt");
        timeout++;
        
        /* Réessayer après quelques itérations (ARP pour le serveur DNS) */
        if (timeout == 5 && dns_is_pending()) {
            dns_send_query(hostname);
        }
    }
    
    /* Récupérer le résultat */
    if (!dns_get_result(ip)) {
        net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        net_puts("[PING] DNS resolution failed for ");
        net_puts(hostname);
        net_puts("\n");
        net_reset_color();
        return -1;
    }
    
    /* Stocker le hostname pour l'affichage */
    icmp_str_copy(g_ping.hostname, hostname, sizeof(g_ping.hostname));
    
    /* Pinger l'IP résolue */
    return ping_ip(ip);
}

/**
 * Vérifie si un ping est en attente
 */
bool ping_is_waiting(void)
{
    return g_ping.active && g_ping.waiting;
}

/**
 * Récupère les statistiques du ping
 */
void ping_get_stats(uint16_t* sent, uint16_t* received)
{
    if (sent) *sent = g_ping.sent;
    if (received) *received = g_ping.received;
}
