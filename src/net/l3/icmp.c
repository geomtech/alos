/* src/net/l3/icmp.c - ICMP Protocol Handler */
#include "icmp.h"
#include "ipv4.h"
#include "route.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "../l2/arp.h"
#include "../l4/dns.h"
#include "../utils.h"
#include "../../kernel/klog.h"
#include "../../kernel/console.h"
#include "../../kernel/keyboard.h"
#include "../../kernel/timer.h"

/* ========================================
 * Variables globales pour le ping
 * ======================================== */

static ping_state_t g_ping = {0};
static uint16_t g_ping_id = 0x1234;  /* ID unique pour nos pings */
static volatile bool g_ping_stop = false;  /* Flag pour arrêter le ping (CTRL+C) */

/**
 * Affiche une adresse IP en décimal (x.x.x.x)
 */
static void print_ip(const uint8_t* ip)
{
    console_put_dec(ip[0]);
    console_putc('.');
    console_put_dec(ip[1]);
    console_putc('.');
    console_put_dec(ip[2]);
    console_putc('.');
    console_put_dec(ip[3]);
}

/**
 * Traite un paquet ICMP reçu.
 */
void icmp_handle_packet(NetInterface* netif, ethernet_header_t* eth, 
                        ipv4_header_t* ip_hdr, uint8_t* icmp_data, int len)
{
    /* Vérifier la taille minimale */
    if (icmp_data == NULL || len < ICMP_HEADER_SIZE) {
        KLOG_ERROR_DEC("ICMP", "Packet too short: ", len);
        return;
    }
    
    /* Caster en header ICMP */
    icmp_header_t* icmp = (icmp_header_t*)icmp_data;
    
    /* Log du paquet reçu */
    KLOG_DEBUG_DEC("ICMP", "Type: ", icmp->type);
    
    /* Traiter selon le type */
    switch (icmp->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            /* Ping Request - on doit répondre! */
            KLOG_INFO("ICMP", "Echo Request received, sending reply");
            
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
                    /* Calculer le temps de réponse */
                    uint64_t now = timer_get_uptime_ms();
                    g_ping.time = (uint32_t)(now - g_ping.send_time);
                    g_ping.ttl = ip_hdr->ttl;
                    g_ping.received++;
                    g_ping.waiting = false;
                    
                    KLOG_INFO_DEC("PING", "Reply received, seq: ", reply_seq);
                } else {
                    KLOG_DEBUG("ICMP", "Echo Reply (not our ping)");
                }
            } else {
                KLOG_DEBUG("ICMP", "Echo Reply received");
            }
            break;
            
        case ICMP_TYPE_DEST_UNREACH:
            KLOG_WARN("ICMP", "Destination Unreachable");
            break;
            
        case ICMP_TYPE_TIME_EXCEEDED:
            KLOG_WARN("ICMP", "Time Exceeded");
            break;
            
        default:
            KLOG_WARN_DEC("ICMP", "Unknown type: ", icmp->type);
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
        KLOG_ERROR("PING", "No network interface!");
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
        KLOG_ERROR("PING", "No route to destination");
        return;
    }
    
    /* Chercher la MAC dans le cache ARP */
    if (!arp_cache_lookup(next_hop, dest_mac)) {
        /* Envoyer une requête ARP */
        KLOG_INFO("PING", "Resolving MAC...");
        
        arp_send_request(netif, next_hop);
        return;  /* Le ping sera réessayé */
    }
    
    /* Envoyer le paquet */
    KLOG_INFO_DEC("PING", "Sending, seq: ", g_ping.sequence);
    
    /* Marquer comme en attente AVANT l'envoi (la réponse peut arriver très vite) */
    g_ping.sent++;
    g_ping.waiting = true;
    g_ping.send_time = timer_get_uptime_ms();  /* Enregistrer le timestamp d'envoi */
    
    ipv4_send_packet(netif, dest_mac, (uint8_t*)dest_ip, IP_PROTO_ICMP,
                     buffer, ICMP_HEADER_SIZE + PING_DATA_SIZE);
}

/**
 * Affiche une ligne de réponse ping
 */
static void print_ping_reply(void)
{
    console_puts("64 bytes from ");
    print_ip(g_ping.dest_ip);
    if (g_ping.hostname[0] != '\0') {
        console_puts(" (");
        console_puts(g_ping.hostname);
        console_puts(")");
    }
    console_puts(": icmp_seq=");
    console_put_dec(g_ping.sequence);
    console_puts(" ttl=");
    console_put_dec(g_ping.ttl);
    console_puts(" time=");
    console_put_dec(g_ping.time);
    console_puts(" ms\n");
}

/**
 * Affiche les statistiques finales du ping
 */
static void print_ping_stats(void)
{
    console_puts("\n--- ");
    print_ip(g_ping.dest_ip);
    console_puts(" ping statistics ---\n");
    console_put_dec(g_ping.sent);
    console_puts(" packets transmitted, ");
    console_put_dec(g_ping.received);
    console_puts(" received, ");
    
    /* Calculer le pourcentage de perte */
    if (g_ping.sent > 0) {
        uint32_t loss = ((g_ping.sent - g_ping.received) * 100) / g_ping.sent;
        console_put_dec(loss);
    } else {
        console_puts("0");
    }
    console_puts("% packet loss\n");
    
    /* Afficher les temps si on a reçu des réponses */
    if (g_ping.received > 0) {
        console_puts("rtt min/avg/max = ");
        console_put_dec(g_ping.min_time);
        console_puts("/");
        console_put_dec(g_ping.total_time / g_ping.received);
        console_puts("/");
        console_put_dec(g_ping.max_time);
        console_puts(" ms\n");
    }
}

/**
 * Vérifie si l'utilisateur veut arrêter (CTRL+C ou 'q')
 */
static bool check_stop_request(void)
{
    char c = keyboard_getchar_nonblock();
    if (c == 0) {
        return false;  /* Pas de touche pressée */
    }
    /* CTRL+C = 0x03, ou 'q' pour quitter */
    if (c == 0x03 || c == 'q' || c == 'Q') {
        return true;
    }
    return false;
}

/**
 * Ping une adresse IP (une seule fois)
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
    g_ping.min_time = 0xFFFFFFFF;
    g_ping.max_time = 0;
    g_ping.total_time = 0;
    g_ping.dest_ip[0] = dest_ip[0];
    g_ping.dest_ip[1] = dest_ip[1];
    g_ping.dest_ip[2] = dest_ip[2];
    g_ping.dest_ip[3] = dest_ip[3];
    g_ping_stop = false;
    
    /* Afficher l'en-tête */
    console_puts("PING ");
    print_ip(dest_ip);
    if (g_ping.hostname[0] != '\0') {
        console_puts(" (");
        console_puts(g_ping.hostname);
        console_puts(")");
    }
    console_puts(" ");
    console_put_dec(PING_DATA_SIZE);
    console_puts(" bytes of data.\n");
    
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
    
    /* Mettre à jour les stats */
    if (g_ping.received > 0) {
        if (g_ping.time < g_ping.min_time) g_ping.min_time = g_ping.time;
        if (g_ping.time > g_ping.max_time) g_ping.max_time = g_ping.time;
        g_ping.total_time += g_ping.time;
        print_ping_reply();
    } else {
        console_puts("Request timeout for icmp_seq ");
        console_put_dec(g_ping.sequence);
        console_puts("\n");
    }
    
    /* Afficher les statistiques */
    print_ping_stats();
    
    g_ping.active = false;
    
    return (g_ping.received > 0) ? 0 : -2;
}

/**
 * Ping continu une adresse IP (jusqu'à CTRL+C ou 'q')
 */
int ping_ip_continuous(const uint8_t* dest_ip)
{
    /* Initialiser l'état du ping */
    g_ping_id++;
    g_ping.identifier = g_ping_id;
    g_ping.sequence = 0;
    g_ping.sent = 0;
    g_ping.received = 0;
    g_ping.waiting = false;
    g_ping.active = true;
    g_ping.min_time = 0xFFFFFFFF;
    g_ping.max_time = 0;
    g_ping.total_time = 0;
    g_ping.dest_ip[0] = dest_ip[0];
    g_ping.dest_ip[1] = dest_ip[1];
    g_ping.dest_ip[2] = dest_ip[2];
    g_ping.dest_ip[3] = dest_ip[3];
    g_ping_stop = false;
    
    /* Vider le buffer clavier */
    keyboard_clear_buffer();
    
    /* Afficher l'en-tête */
    console_puts("PING ");
    print_ip(dest_ip);
    if (g_ping.hostname[0] != '\0') {
        console_puts(" (");
        console_puts(g_ping.hostname);
        console_puts(")");
    }
    console_puts(" ");
    console_put_dec(PING_DATA_SIZE);
    console_puts(" bytes of data.\n");
    console_puts("Press 'q' or CTRL+C to stop.\n");
    
    /* Boucle de ping continue */
    while (!g_ping_stop) {
        g_ping.sequence++;
        g_ping.waiting = false;
        
        /* Envoyer le ping */
        icmp_send_echo_request(dest_ip);
        
        /* Attendre la réponse avec timeout */
        int timeout_count = 0;
        int retry_count = 0;
        uint16_t seq_before = g_ping.sent;
        
        while (timeout_count < 30) {  /* ~3 secondes de timeout */
            /* Vérifier CTRL+C */
            if (check_stop_request()) {
                g_ping_stop = true;
                break;
            }
            
            for (volatile int w = 0; w < 100000; w++);
            asm volatile("sti");
            asm volatile("hlt");
            timeout_count++;
            
            /* Si pas encore envoyé (ARP était pending), réessayer */
            if (g_ping.sent == seq_before && retry_count < 3) {
                if (timeout_count == 5 || timeout_count == 15) {
                    icmp_send_echo_request(dest_ip);
                    retry_count++;
                }
            }
            
            /* Si envoyé et réponse reçue, on a fini */
            if (g_ping.sent > seq_before && !g_ping.waiting) {
                break;
            }
        }
        
        if (g_ping_stop) break;
        
        /* Afficher le résultat */
        if (g_ping.sent > seq_before && !g_ping.waiting) {
            /* Réponse reçue */
            if (g_ping.time < g_ping.min_time) g_ping.min_time = g_ping.time;
            if (g_ping.time > g_ping.max_time) g_ping.max_time = g_ping.time;
            g_ping.total_time += g_ping.time;
            print_ping_reply();
        } else {
            /* Timeout */
            console_puts("Request timeout for icmp_seq ");
            console_put_dec(g_ping.sequence);
            console_puts("\n");
        }
        
        /* Attendre 1 seconde avant le prochain ping */
        for (int i = 0; i < 10 && !g_ping_stop; i++) {
            if (check_stop_request()) {
                g_ping_stop = true;
                break;
            }
            for (volatile int w = 0; w < 500000; w++);
            asm volatile("sti");
            asm volatile("hlt");
        }
    }
    
    /* Afficher les statistiques finales */
    print_ping_stats();
    
    g_ping.active = false;
    
    /* Vider le buffer clavier pour éviter les caractères parasites */
    keyboard_clear_buffer();
    
    return (g_ping.received > 0) ? 0 : -2;
}

/**
 * Résout un hostname et retourne l'IP
 * @return true si résolu, false sinon
 */
static bool resolve_hostname(const char* hostname, uint8_t* ip)
{
    KLOG_INFO("PING", "Resolving hostname...");
    
    /* Vérifier le cache DNS d'abord */
    if (dns_cache_lookup(hostname, ip)) {
        return true;
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
        KLOG_ERROR("PING", "DNS resolution failed");
        return false;
    }
    
    return true;
}

/**
 * Ping un hostname (résolution DNS puis ping, une seule fois)
 */
int ping(const char* hostname)
{
    uint8_t ip[4];
    
    if (!resolve_hostname(hostname, ip)) {
        return -1;
    }
    
    /* Stocker le hostname pour l'affichage */
    icmp_str_copy(g_ping.hostname, hostname, sizeof(g_ping.hostname));
    
    /* Pinger l'IP résolue */
    return ping_ip(ip);
}

/**
 * Ping continu un hostname (résolution DNS puis ping continu jusqu'à CTRL+C)
 */
int ping_continuous(const char* hostname)
{
    uint8_t ip[4];
    
    if (!resolve_hostname(hostname, ip)) {
        return -1;
    }
    
    /* Stocker le hostname pour l'affichage */
    icmp_str_copy(g_ping.hostname, hostname, sizeof(g_ping.hostname));
    
    /* Pinger l'IP résolue en continu */
    return ping_ip_continuous(ip);
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
