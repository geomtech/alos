/* src/net/l4/tcp.c - TCP Protocol Handler */
#include "tcp.h"
#include "../l3/ipv4.h"
#include "../l3/route.h"
#include "../l2/arp.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/console.h"
#include "../../kernel/timer.h"
#include "../../mm/kheap.h"

/* ===========================================
 * Tableau des sockets TCP
 * =========================================== */
static tcp_socket_t tcp_sockets[TCP_MAX_SOCKETS];

/* ===========================================
 * Fonctions utilitaires locales
 * =========================================== */

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
 * Retourne le nom d'un état TCP (pour debug).
 */
const char* tcp_state_name(tcp_state_t state)
{
    switch (state) {
        case TCP_STATE_CLOSED:      return "CLOSED";
        case TCP_STATE_LISTEN:      return "LISTEN";
        case TCP_STATE_SYN_SENT:    return "SYN_SENT";
        case TCP_STATE_SYN_RCVD:    return "SYN_RCVD";
        case TCP_STATE_ESTABLISHED: return "ESTABLISHED";
        case TCP_STATE_FIN_WAIT_1:  return "FIN_WAIT_1";
        case TCP_STATE_FIN_WAIT_2:  return "FIN_WAIT_2";
        case TCP_STATE_CLOSE_WAIT:  return "CLOSE_WAIT";
        case TCP_STATE_CLOSING:     return "CLOSING";
        case TCP_STATE_LAST_ACK:    return "LAST_ACK";
        case TCP_STATE_TIME_WAIT:   return "TIME_WAIT";
        default:                    return "UNKNOWN";
    }
}

/**
 * Extrait les flags du champ data_offset_flags.
 */
static inline uint8_t tcp_get_flags(uint16_t data_offset_flags)
{
    return data_offset_flags & 0x3F;  /* 6 bits de poids faible */
}

/**
 * Extrait le data offset (header length) en bytes.
 */
static inline int tcp_get_header_len(uint16_t data_offset_flags)
{
    return ((data_offset_flags >> 12) & 0x0F) * 4;  /* 4 bits de poids fort, en mots de 32 bits */
}

/**
 * Construit le champ data_offset_flags.
 * @param header_len Taille du header en bytes (multiple de 4)
 * @param flags      Flags TCP
 */
static inline uint16_t tcp_make_data_offset_flags(int header_len, uint8_t flags)
{
    uint16_t offset = (header_len / 4) << 12;  /* Data offset en mots de 32 bits */
    return offset | (flags & 0x3F);
}

/**
 * Calcule le checksum TCP avec pseudo-header.
 * Le checksum couvre: pseudo-header + header TCP + data
 */
static uint16_t tcp_checksum(uint8_t* src_ip, uint8_t* dest_ip, 
                              tcp_header_t* tcp_hdr, uint8_t* data, int data_len)
{
    uint32_t sum = 0;
    int tcp_len = TCP_HEADER_SIZE + data_len;
    
    /* === Pseudo Header === */
    /* Source IP */
    sum += ((uint16_t)src_ip[0] << 8) | src_ip[1];
    sum += ((uint16_t)src_ip[2] << 8) | src_ip[3];
    
    /* Destination IP */
    sum += ((uint16_t)dest_ip[0] << 8) | dest_ip[1];
    sum += ((uint16_t)dest_ip[2] << 8) | dest_ip[3];
    
    /* Zero + Protocol */
    sum += IP_PROTO_TCP;
    
    /* TCP Length */
    sum += tcp_len;
    
    /* === TCP Header === */
    uint16_t* ptr = (uint16_t*)tcp_hdr;
    for (int i = 0; i < TCP_HEADER_SIZE / 2; i++) {
        sum += ntohs(ptr[i]);
    }
    
    /* === TCP Data === */
    ptr = (uint16_t*)data;
    while (data_len > 1) {
        sum += ntohs(*ptr++);
        data_len -= 2;
    }
    
    /* Dernier octet impair */
    if (data_len == 1) {
        sum += ((uint8_t*)ptr)[0] << 8;
    }
    
    /* Replier les bits de carry */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return htons(~sum);
}

/* ===========================================
 * Initialisation
 * =========================================== */

/**
 * Initialise la stack TCP.
 */
void tcp_init(void)
{
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[TCP] Initializing TCP stack...\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Initialiser tous les sockets à CLOSED */
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_sockets[i].state = TCP_STATE_CLOSED;
        tcp_sockets[i].in_use = false;
        tcp_sockets[i].local_port = 0;
        tcp_sockets[i].remote_port = 0;
        tcp_sockets[i].seq = 0;
        tcp_sockets[i].ack = 0;
        tcp_sockets[i].window = 8192;  /* 8KB window par défaut */
        tcp_sockets[i].flags = 0;
        tcp_sockets[i].recv_head = 0;
        tcp_sockets[i].recv_tail = 0;
        tcp_sockets[i].recv_count = 0;
        for (int j = 0; j < 4; j++) {
            tcp_sockets[i].remote_ip[j] = 0;
        }
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[TCP] TCP stack initialized (");
    console_put_dec(TCP_MAX_SOCKETS);
    console_puts(" sockets available)\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* ===========================================
 * Gestion des sockets
 * =========================================== */

/**
 * Trouve un slot de socket libre.
 */
static tcp_socket_t* tcp_alloc_socket(void)
{
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!tcp_sockets[i].in_use) {
            tcp_sockets[i].in_use = true;
            return &tcp_sockets[i];
        }
    }
    return NULL;
}

/**
 * Trouve un socket par port local (pour les connexions entrantes).
 */
static tcp_socket_t* tcp_find_socket_by_local_port(uint16_t port)
{
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (tcp_sockets[i].in_use && tcp_sockets[i].local_port == port) {
            return &tcp_sockets[i];
        }
    }
    return NULL;
}

/**
 * Trouve un socket par connexion complète (local_port + remote_ip + remote_port).
 */
static tcp_socket_t* tcp_find_socket(uint16_t local_port, uint8_t* remote_ip, uint16_t remote_port)
{
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!tcp_sockets[i].in_use) continue;
        
        if (tcp_sockets[i].local_port == local_port &&
            tcp_sockets[i].remote_port == remote_port &&
            tcp_sockets[i].remote_ip[0] == remote_ip[0] &&
            tcp_sockets[i].remote_ip[1] == remote_ip[1] &&
            tcp_sockets[i].remote_ip[2] == remote_ip[2] &&
            tcp_sockets[i].remote_ip[3] == remote_ip[3]) {
            return &tcp_sockets[i];
        }
    }
    return NULL;
}

/**
 * Crée un socket en mode LISTEN sur le port spécifié.
 */
tcp_socket_t* tcp_listen(uint16_t port)
{
    /* Vérifier si le port est déjà utilisé */
    if (tcp_find_socket_by_local_port(port) != NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[TCP] Port ");
        console_put_dec(port);
        console_puts(" already in use\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return NULL;
    }
    
    /* Allouer un nouveau socket */
    tcp_socket_t* sock = tcp_alloc_socket();
    if (sock == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[TCP] No free sockets available\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return NULL;
    }
    
    /* Initialiser le socket en mode LISTEN */
    sock->state = TCP_STATE_LISTEN;
    sock->local_port = port;
    sock->remote_port = 0;
    sock->seq = 0;
    sock->ack = 0;
    sock->window = 8192;
    sock->flags = 0;
    for (int i = 0; i < 4; i++) {
        sock->remote_ip[i] = 0;
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[TCP] Listening on port ");
    console_put_dec(port);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return sock;
}

/**
 * Ferme un socket TCP.
 */
void tcp_close(tcp_socket_t* sock)
{
    if (sock == NULL) return;
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[TCP] Closing socket on port ");
    console_put_dec(sock->local_port);
    console_puts(" (state: ");
    console_puts(tcp_state_name(sock->state));
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Réinitialiser le socket */
    sock->state = TCP_STATE_CLOSED;
    sock->in_use = false;
    sock->local_port = 0;
    sock->remote_port = 0;
    sock->seq = 0;
    sock->ack = 0;
    sock->flags = 0;
    for (int i = 0; i < 4; i++) {
        sock->remote_ip[i] = 0;
    }
}

/* ===========================================
 * Envoi de paquets
 * =========================================== */

/**
 * Envoie un paquet TCP.
 */
void tcp_send_packet(tcp_socket_t* sock, uint8_t flags, uint8_t* payload, int len)
{
    if (sock == NULL) return;
    
    /* Buffer pour le paquet TCP (header + payload) */
    uint8_t buffer[1500];
    
    /* Vérifier que le paquet n'est pas trop grand */
    if (len > (int)(sizeof(buffer) - TCP_HEADER_SIZE)) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[TCP] Payload too large: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* === Construire le header TCP === */
    tcp_header_t* tcp = (tcp_header_t*)buffer;
    
    tcp->src_port = htons(sock->local_port);
    tcp->dest_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->seq);
    tcp->ack_num = htonl(sock->ack);
    tcp->data_offset_flags = htons(tcp_make_data_offset_flags(TCP_HEADER_SIZE, flags));
    tcp->window_size = htons(sock->window);
    tcp->checksum = 0;  /* Calculé après */
    tcp->urgent_ptr = 0;
    
    /* === Copier le payload === */
    uint8_t* tcp_payload = buffer + TCP_HEADER_SIZE;
    for (int i = 0; i < len; i++) {
        tcp_payload[i] = payload[i];
    }
    
    /* === Calculer le checksum === */
    /* On a besoin de notre IP source */
    uint8_t my_ip[4];
    NetInterface* netif = netif_get_default();
    if (netif != NULL && netif->ip_addr != 0) {
        ip_u32_to_bytes(netif->ip_addr, my_ip);
    } else {
        for (int i = 0; i < 4; i++) {
            my_ip[i] = MY_IP[i];
        }
    }
    
    tcp->checksum = tcp_checksum(my_ip, sock->remote_ip, tcp, tcp_payload, len);
    
    /* === Résoudre la MAC de destination === */
    uint8_t dest_mac[6];
    uint8_t next_hop[4];
    
    /* Trouver le next hop (gateway si nécessaire) */
    if (!route_get_next_hop(sock->remote_ip, next_hop)) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[TCP] No route to ");
        print_ip(sock->remote_ip);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* Résoudre le MAC via ARP cache */
    if (!arp_cache_lookup(next_hop, dest_mac)) {
        /* Pas dans le cache - envoyer une requête ARP */
        console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        console_puts("[TCP] ARP resolution pending for ");
        print_ip(next_hop);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        /* Envoyer une requête ARP */
        arp_send_request(netif, next_hop);
        /* TODO: Mettre le paquet en file d'attente et réessayer plus tard */
        return;
    }
    
    /* === Log debug === */
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[TCP] Sending ");
    if (flags & TCP_FLAG_SYN) console_puts("SYN ");
    if (flags & TCP_FLAG_ACK) console_puts("ACK ");
    if (flags & TCP_FLAG_FIN) console_puts("FIN ");
    if (flags & TCP_FLAG_RST) console_puts("RST ");
    if (flags & TCP_FLAG_PSH) console_puts("PSH ");
    console_puts("to ");
    print_ip(sock->remote_ip);
    console_puts(":");
    console_put_dec(sock->remote_port);
    console_puts(" (seq=");
    console_put_hex(sock->seq);
    console_puts(", ack=");
    console_put_hex(sock->ack);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* === Envoyer via IPv4 === */
    ipv4_send_packet(netif, dest_mac, sock->remote_ip, IP_PROTO_TCP, buffer, TCP_HEADER_SIZE + len);
    
    /* Si on envoie un SYN ou des données, incrémenter seq */
    if (flags & TCP_FLAG_SYN) {
        sock->seq++;  /* SYN consomme un numéro de séquence */
    }
    if (len > 0) {
        sock->seq += len;  /* Les données consomment des numéros de séquence */
    }
}

/* ===========================================
 * Réception et machine à états
 * =========================================== */

/**
 * Traite un paquet TCP reçu.
 */
void tcp_handle_packet(ipv4_header_t* ip_hdr, uint8_t* data, int len)
{
    /* Vérifier la taille minimale */
    if (data == NULL || len < TCP_HEADER_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[TCP] Packet too short: ");
        console_put_dec(len);
        console_puts(" bytes\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* Caster en header TCP */
    tcp_header_t* tcp = (tcp_header_t*)data;
    
    /* Convertir les champs en host byte order */
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dest_port = ntohs(tcp->dest_port);
    uint32_t seq_num = ntohl(tcp->seq_num);
    uint32_t ack_num = ntohl(tcp->ack_num);
    uint16_t flags_field = ntohs(tcp->data_offset_flags);
    uint8_t flags = tcp_get_flags(flags_field);
    int header_len = tcp_get_header_len(flags_field);
    
    /* Log: paquet TCP reçu */
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[TCP] Received ");
    if (flags & TCP_FLAG_SYN) console_puts("SYN ");
    if (flags & TCP_FLAG_ACK) console_puts("ACK ");
    if (flags & TCP_FLAG_FIN) console_puts("FIN ");
    if (flags & TCP_FLAG_RST) console_puts("RST ");
    if (flags & TCP_FLAG_PSH) console_puts("PSH ");
    console_puts("from ");
    print_ip(ip_hdr->src_ip);
    console_puts(":");
    console_put_dec(src_port);
    console_puts(" -> port ");
    console_put_dec(dest_port);
    console_puts(" (seq=");
    console_put_hex(seq_num);
    console_puts(", ack=");
    console_put_hex(ack_num);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Chercher un socket existant pour cette connexion */
    tcp_socket_t* sock = tcp_find_socket(dest_port, ip_hdr->src_ip, src_port);
    
    /* Si pas trouvé, chercher un socket en LISTEN sur ce port */
    if (sock == NULL) {
        sock = tcp_find_socket_by_local_port(dest_port);
    }
    
    /* Pas de socket trouvé - envoyer RST (pas implémenté pour l'instant) */
    if (sock == NULL) {
        console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        console_puts("[TCP] No socket for port ");
        console_put_dec(dest_port);
        console_puts(" - packet dropped\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* === Machine à états TCP === */
    switch (sock->state) {
        case TCP_STATE_LISTEN:
            /* En écoute - on attend un SYN */
            if (flags & TCP_FLAG_SYN) {
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                console_puts("[TCP] Connection request from ");
                print_ip(ip_hdr->src_ip);
                console_puts(":");
                console_put_dec(src_port);
                console_puts("\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                
                /* Enregistrer l'adresse distante */
                for (int i = 0; i < 4; i++) {
                    sock->remote_ip[i] = ip_hdr->src_ip[i];
                }
                sock->remote_port = src_port;
                
                /* Calculer notre numéro de séquence initial (ISN) */
                /* Utiliser le timer comme source d'aléatoire simple */
                sock->seq = (uint32_t)timer_get_ticks() * 12345;
                
                /* Enregistrer leur numéro de séquence + 1 */
                sock->ack = seq_num + 1;
                
                /* Passer en état SYN_RCVD */
                sock->state = TCP_STATE_SYN_RCVD;
                
                /* Envoyer SYN-ACK */
                tcp_send_packet(sock, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_STATE_SYN_RCVD:
            /* On a envoyé SYN-ACK, on attend ACK */
            if (flags & TCP_FLAG_ACK) {
                /* Vérifier que l'ACK correspond */
                if (ack_num == sock->seq) {
                    sock->state = TCP_STATE_ESTABLISHED;
                    
                    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                    console_puts("[TCP] Connection ESTABLISHED with ");
                    print_ip(sock->remote_ip);
                    console_puts(":");
                    console_put_dec(sock->remote_port);
                    console_puts("!\n");
                    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                } else {
                    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                    console_puts("[TCP] Invalid ACK: expected ");
                    console_put_hex(sock->seq);
                    console_puts(", got ");
                    console_put_hex(ack_num);
                    console_puts("\n");
                    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                }
            }
            /* Si on reçoit un RST, retourner en LISTEN */
            if (flags & TCP_FLAG_RST) {
                console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
                console_puts("[TCP] Connection reset - returning to LISTEN\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                
                sock->state = TCP_STATE_LISTEN;
                sock->remote_port = 0;
                for (int i = 0; i < 4; i++) {
                    sock->remote_ip[i] = 0;
                }
            }
            break;
            
        case TCP_STATE_ESTABLISHED:
            /* Connexion établie - traiter les données ou FIN */
            
            /* Si on reçoit des données */
            if (len > header_len) {
                int payload_len = len - header_len;
                uint8_t* payload = data + header_len;
                
                console_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
                console_puts("[TCP] Received ");
                console_put_dec(payload_len);
                console_puts(" bytes of data\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                
                /* Stocker les données dans le buffer circulaire */
                int stored = 0;
                for (int i = 0; i < payload_len; i++) {
                    if (sock->recv_count < TCP_RECV_BUFFER_SIZE) {
                        sock->recv_buffer[sock->recv_head] = payload[i];
                        sock->recv_head = (sock->recv_head + 1) % TCP_RECV_BUFFER_SIZE;
                        sock->recv_count++;
                        stored++;
                    } else {
                        /* Buffer plein - on perd les données */
                        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                        console_puts("[TCP] Recv buffer full! Dropping data.\n");
                        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                        break;
                    }
                }
                
                /* Mettre à jour notre ACK */
                sock->ack = seq_num + payload_len;
                
                /* Envoyer ACK */
                tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
            }
            
            /* Si on reçoit un FIN */
            if (flags & TCP_FLAG_FIN) {
                console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
                console_puts("[TCP] Connection closing (FIN received)\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                
                /* ACK le FIN */
                sock->ack = seq_num + 1;
                tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
                
                /* Passer en CLOSE_WAIT */
                sock->state = TCP_STATE_CLOSE_WAIT;
                
                /* Pour simplifier, on ferme directement */
                /* Dans une vraie implémentation, on attendrait que l'app ferme */
                tcp_send_packet(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
                sock->state = TCP_STATE_LAST_ACK;
            }
            
            /* Si on reçoit un RST */
            if (flags & TCP_FLAG_RST) {
                console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                console_puts("[TCP] Connection reset by peer\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                
                /* Retourner en LISTEN */
                sock->state = TCP_STATE_LISTEN;
                sock->remote_port = 0;
                for (int i = 0; i < 4; i++) {
                    sock->remote_ip[i] = 0;
                }
            }
            break;
            
        case TCP_STATE_LAST_ACK:
            /* On attend l'ACK de notre FIN */
            if (flags & TCP_FLAG_ACK) {
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                console_puts("[TCP] Connection closed gracefully\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                
                /* Retourner en LISTEN pour accepter de nouvelles connexions */
                sock->state = TCP_STATE_LISTEN;
                sock->remote_port = 0;
                for (int i = 0; i < 4; i++) {
                    sock->remote_ip[i] = 0;
                }
            }
            break;
            
        default:
            console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
            console_puts("[TCP] Packet in unexpected state: ");
            console_puts(tcp_state_name(sock->state));
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            break;
    }
}

/* ===========================================
 * Nouvelles fonctions pour l'API Socket
 * =========================================== */

/**
 * Crée un nouveau socket TCP (non connecté).
 */
tcp_socket_t* tcp_socket_create(void)
{
    tcp_socket_t* sock = tcp_alloc_socket();
    if (sock == NULL) {
        return NULL;
    }
    
    /* Initialiser le socket en état CLOSED */
    sock->state = TCP_STATE_CLOSED;
    sock->local_port = 0;
    sock->remote_port = 0;
    sock->seq = 0;
    sock->ack = 0;
    sock->window = 8192;
    sock->flags = 0;
    sock->recv_head = 0;
    sock->recv_tail = 0;
    sock->recv_count = 0;
    for (int i = 0; i < 4; i++) {
        sock->remote_ip[i] = 0;
    }
    
    return sock;
}

/**
 * Lie un socket à un port local.
 */
int tcp_bind(tcp_socket_t* sock, uint16_t port)
{
    if (sock == NULL) {
        return -1;
    }
    
    /* Vérifier si le port est déjà utilisé par un autre socket */
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (tcp_sockets[i].in_use && 
            &tcp_sockets[i] != sock &&
            tcp_sockets[i].local_port == port) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("[TCP] Port ");
            console_put_dec(port);
            console_puts(" already bound\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return -1;
        }
    }
    
    sock->local_port = port;
    return 0;
}

/**
 * Lit des données depuis le buffer de réception d'un socket.
 * Non-bloquant: retourne immédiatement ce qui est disponible.
 */
int tcp_recv(tcp_socket_t* sock, uint8_t* buf, int len)
{
    if (sock == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    
    /* Vérifier que le socket est dans un état valide pour la lecture */
    if (sock->state != TCP_STATE_ESTABLISHED && 
        sock->state != TCP_STATE_CLOSE_WAIT) {
        /* Pas de données à lire si pas connecté */
        return 0;
    }
    
    /* Lire autant de données que possible (non-bloquant) */
    int read = 0;
    while (read < len && sock->recv_count > 0) {
        buf[read] = sock->recv_buffer[sock->recv_tail];
        sock->recv_tail = (sock->recv_tail + 1) % TCP_RECV_BUFFER_SIZE;
        sock->recv_count--;
        read++;
    }
    
    return read;
}

/**
 * Envoie des données via un socket TCP.
 */
int tcp_send(tcp_socket_t* sock, const uint8_t* buf, int len)
{
    if (sock == NULL || buf == NULL || len <= 0) {
        return -1;
    }
    
    /* Vérifier que le socket est connecté */
    if (sock->state != TCP_STATE_ESTABLISHED) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[TCP] Cannot send: socket not connected\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    /* Envoyer les données via tcp_send_packet */
    /* Note: On devrait fragmenter si len > MSS, mais pour V1 on simplifie */
    tcp_send_packet(sock, TCP_FLAG_ACK | TCP_FLAG_PSH, (uint8_t*)buf, len);
    
    /* Mettre à jour le numéro de séquence */
    sock->seq += len;
    
    return len;
}

/**
 * Vérifie si des données sont disponibles en lecture.
 */
int tcp_available(tcp_socket_t* sock)
{
    if (sock == NULL) {
        return 0;
    }
    return sock->recv_count;
}
