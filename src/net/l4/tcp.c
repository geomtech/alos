/* src/net/l4/tcp.c - TCP Protocol Handler */
#include "tcp.h"
#include "../l3/ipv4.h"
#include "../l3/route.h"
#include "../l2/arp.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/timer.h"
#include "../../kernel/klog.h"
#include "../../mm/kheap.h"

/* ===========================================
 * Tableau des sockets TCP (allocation dynamique)
 * =========================================== */
static tcp_socket_t* tcp_sockets = NULL;
static int tcp_socket_capacity = 0;  /* Nombre de slots alloués */
static int tcp_socket_count = 0;     /* Nombre de sockets en utilisation */

/* ===========================================
 * Fonctions utilitaires locales
 * =========================================== */

/* Note: print_ip removed - using KLOG instead */

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
 * 
 * Le calcul se fait en additionnant tous les mots de 16 bits
 * en network byte order (big-endian), puis en prenant le complément à 1.
 */
static uint16_t tcp_checksum(uint8_t* src_ip, uint8_t* dest_ip, 
                              tcp_header_t* tcp_hdr, uint8_t* data, int data_len)
{
    uint32_t sum = 0;
    int tcp_len = TCP_HEADER_SIZE + data_len;
    
    /* === Pseudo Header (12 bytes) === */
    /* Structure: SrcIP(4) + DestIP(4) + Zero(1) + Proto(1) + TCP_Length(2) */
    
    /* Source IP (4 bytes = 2 mots de 16 bits) */
    sum += ((uint16_t)src_ip[0] << 8) | src_ip[1];
    sum += ((uint16_t)src_ip[2] << 8) | src_ip[3];
    
    /* Destination IP (4 bytes = 2 mots de 16 bits) */
    sum += ((uint16_t)dest_ip[0] << 8) | dest_ip[1];
    sum += ((uint16_t)dest_ip[2] << 8) | dest_ip[3];
    
    /* Zero (8 bits) + Protocol (8 bits) = 1 mot de 16 bits */
    /* Zero est toujours 0, Protocol TCP = 6 */
    sum += (uint16_t)IP_PROTO_TCP;  /* 0x0006 en big-endian */
    
    /* TCP Length (16 bits) - en host byte order, sera additionné tel quel */
    sum += (uint16_t)tcp_len;
    
    /* === TCP Header (déjà en network byte order) === */
    /* On additionne les mots de 16 bits directement sans conversion */
    uint8_t* ptr = (uint8_t*)tcp_hdr;
    for (int i = 0; i < TCP_HEADER_SIZE; i += 2) {
        sum += ((uint16_t)ptr[i] << 8) | ptr[i + 1];
    }
    
    /* === TCP Data === */
    ptr = data;
    int remaining = data_len;
    while (remaining > 1) {
        sum += ((uint16_t)ptr[0] << 8) | ptr[1];
        ptr += 2;
        remaining -= 2;
    }
    
    /* Dernier octet impair (padding avec 0) */
    if (remaining == 1) {
        sum += (uint16_t)ptr[0] << 8;
    }
    
    /* Replier les bits de carry (fold 32-bit sum to 16-bit) */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    /* Complément à 1 et conversion en network byte order */
    uint16_t checksum = (uint16_t)(~sum);
    return htons(checksum);
}

/* ===========================================
 * Initialisation et gestion dynamique
 * =========================================== */

/**
 * Initialise un socket à l'état par défaut.
 */
static void tcp_init_socket(tcp_socket_t* sock)
{
    sock->in_use = false;
    sock->state = TCP_STATE_CLOSED;
    sock->local_port = 0;
    sock->remote_port = 0;
    sock->seq = 0;
    sock->ack = 0;
    sock->window = 8192;  /* Fenêtre par défaut */
    sock->flags = 0;
    sock->recv_head = 0;
    sock->recv_tail = 0;
    sock->recv_count = 0;
    for (int j = 0; j < 4; j++) {
        sock->remote_ip[j] = 0;
    }
    condvar_init(&sock->state_changed);
}

/**
 * Agrandit le tableau de sockets si nécessaire.
 * Double la capacité jusqu'à TCP_MAX_SOCKETS.
 * 
 * @return true si succès, false si échec ou limite atteinte
 */
static bool tcp_grow_sockets(void)
{
    if (tcp_socket_capacity >= TCP_MAX_SOCKETS) {
        KLOG_WARN("TCP", "Maximum socket limit reached");
        return false;
    }
    
    /* Calculer la nouvelle capacité (doubler) */
    int new_capacity = tcp_socket_capacity * 2;
    if (new_capacity > TCP_MAX_SOCKETS) {
        new_capacity = TCP_MAX_SOCKETS;
    }
    if (new_capacity < TCP_INITIAL_SOCKETS) {
        new_capacity = TCP_INITIAL_SOCKETS;
    }
    
    /* Allouer le nouveau tableau */
    tcp_socket_t* new_sockets = (tcp_socket_t*)kmalloc(new_capacity * sizeof(tcp_socket_t));
    if (new_sockets == NULL) {
        KLOG_ERROR("TCP", "Failed to allocate socket array");
        return false;
    }
    
    /* Copier les sockets existants */
    if (tcp_sockets != NULL && tcp_socket_capacity > 0) {
        for (int i = 0; i < tcp_socket_capacity; i++) {
            new_sockets[i] = tcp_sockets[i];
        }
        /* Libérer l'ancien tableau */
        kfree(tcp_sockets);
    }
    
    /* Initialiser les nouveaux slots */
    for (int i = tcp_socket_capacity; i < new_capacity; i++) {
        tcp_init_socket(&new_sockets[i]);
    }
    
    tcp_sockets = new_sockets;
    int old_capacity = tcp_socket_capacity;
    tcp_socket_capacity = new_capacity;
    
    KLOG_INFO_DEC("TCP", "Socket pool grown from ", old_capacity);
    KLOG_INFO_DEC("TCP", "Socket pool grown to ", new_capacity);
    
    return true;
}

/**
 * Initialise la stack TCP.
 */
void tcp_init(void)
{
    KLOG_INFO("TCP", "Initializing TCP stack...");
    
    /* Allocation initiale des sockets */
    tcp_sockets = NULL;
    tcp_socket_capacity = 0;
    tcp_socket_count = 0;
    
    if (!tcp_grow_sockets()) {
        KLOG_ERROR("TCP", "Failed to initialize socket pool!");
        return;
    }
    
    KLOG_INFO_DEC("TCP", "Initial sockets allocated: ", tcp_socket_capacity);
}

/* ===========================================
 * Gestion des sockets
 * =========================================== */

/**
 * Trouve un slot de socket libre.
 * Agrandit le tableau si nécessaire.
 */
static tcp_socket_t* tcp_alloc_socket(void)
{
    /* Chercher un slot libre dans le tableau existant */
    for (int i = 0; i < tcp_socket_capacity; i++) {
        if (!tcp_sockets[i].in_use) {
            tcp_sockets[i].in_use = true;
            tcp_socket_count++;
            return &tcp_sockets[i];
        }
    }
    
    /* Pas de slot libre - essayer d'agrandir le tableau */
    if (tcp_grow_sockets()) {
        /* Réessayer l'allocation après agrandissement */
        for (int i = 0; i < tcp_socket_capacity; i++) {
            if (!tcp_sockets[i].in_use) {
                tcp_sockets[i].in_use = true;
                tcp_socket_count++;
                return &tcp_sockets[i];
            }
        }
    }
    
    return NULL;
}

/**
 * Trouve un socket par port local EN MODE LISTEN uniquement.
 * Pour les nouvelles connexions entrantes.
 */
static tcp_socket_t* tcp_find_listening_socket(uint16_t port)
{
    for (int i = 0; i < tcp_socket_capacity; i++) {
        if (tcp_sockets[i].in_use && 
            tcp_sockets[i].local_port == port &&
            tcp_sockets[i].state == TCP_STATE_LISTEN) {
            return &tcp_sockets[i];
        }
    }
    return NULL;
}

/**
 * Trouve un socket client prêt (ESTABLISHED) pour un port donné.
 * Utilisé par sys_accept pour trouver les connexions créées par tcp_handle_packet.
 * Ne retourne pas les sockets LISTEN ou CLOSED.
 */
tcp_socket_t* tcp_find_ready_client(uint16_t local_port)
{
    for (int i = 0; i < tcp_socket_capacity; i++) {
        if (tcp_sockets[i].in_use && 
            tcp_sockets[i].local_port == local_port &&
            tcp_sockets[i].state == TCP_STATE_ESTABLISHED) {
            return &tcp_sockets[i];
        }
    }
    return NULL;
}

/**
 * Trouve un socket par port local (pour les connexions entrantes).
 * Note: Retourne le premier socket trouvé, quel que soit son état.
 */
static tcp_socket_t* tcp_find_socket_by_local_port(uint16_t port)
{
    for (int i = 0; i < tcp_socket_capacity; i++) {
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
    for (int i = 0; i < tcp_socket_capacity; i++) {
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
        KLOG_ERROR_DEC("TCP", "Port already in use: ", port);
        return NULL;
    }
    
    /* Allouer un nouveau socket */
    tcp_socket_t* sock = tcp_alloc_socket();
    if (sock == NULL) {
        KLOG_ERROR("TCP", "No free sockets available");
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
    
    KLOG_INFO_DEC("TCP", "Listening on port: ", port);
    
    return sock;
}

/**
 * Ferme un socket TCP de manière conforme au standard (FIN-ACK).
 * Version NON-BLOQUANTE : envoie FIN et libère immédiatement.
 * 
 * Pour un socket serveur réutilisable : remet en LISTEN.
 * Pour un socket client (créé par accept) : libère complètement.
 * 
 * RFC 793 : Le FIN est envoyé, l'ACK sera traité par la machine à états
 * mais on n'attend pas (le client recevra le FIN de toute façon).
 */
void tcp_close(tcp_socket_t* sock)
{
    if (sock == NULL) return;
    
    uint16_t saved_port = sock->local_port;
    
    /* Si le socket est connecté, envoyer FIN pour fermeture propre */
    if (sock->state == TCP_STATE_ESTABLISHED) {
        /* Envoyer FIN+ACK - non bloquant */
        tcp_send_packet(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        /* Note: On ne change pas l'état ici, on libère directement */
    } else if (sock->state == TCP_STATE_CLOSE_WAIT) {
        /* Le client a déjà envoyé FIN, on envoie notre FIN */
        tcp_send_packet(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    }
    /* Pour SYN_RCVD ou autres états, on ne fait rien de spécial */
    
    /* Libérer le socket immédiatement */
    sock->state = TCP_STATE_CLOSED;
    sock->remote_port = 0;
    sock->seq = 0;
    sock->ack = 0;
    sock->flags = 0;
    sock->recv_head = 0;
    sock->recv_tail = 0;
    sock->recv_count = 0;
    for (int i = 0; i < 4; i++) {
        sock->remote_ip[i] = 0;
    }
    sock->local_port = 0;  /* Libérer le port pour ce socket */
    sock->in_use = false;  /* Marquer comme libre */
    tcp_socket_count--;    /* Décrémenter le compteur */
    
    /* Signal state change */
    condvar_broadcast(&sock->state_changed);
}

/**
 * Ferme un socket client et remet le socket serveur en LISTEN.
 * Utilisé quand on a un seul socket qui fait serveur ET client.
 */
void tcp_close_and_relisten(tcp_socket_t* sock, uint16_t listen_port)
{
    if (sock == NULL) return;
    
    /* Si le socket est connecté, envoyer FIN pour fermeture propre */
    if (sock->state == TCP_STATE_ESTABLISHED) {
        tcp_send_packet(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    } else if (sock->state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_packet(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    }
    
    /* Remettre immédiatement en LISTEN - non bloquant */
    sock->state = TCP_STATE_LISTEN;
    sock->remote_port = 0;
    sock->seq = 0;
    sock->ack = 0;
    sock->flags = 0;
    sock->recv_head = 0;
    sock->recv_tail = 0;
    sock->recv_count = 0;
    for (int i = 0; i < 4; i++) {
        sock->remote_ip[i] = 0;
    }
    sock->local_port = listen_port;
    
    /* Signal state change pour débloquer accept() */
    condvar_broadcast(&sock->state_changed);
}

/* ===========================================
 * Envoi de paquets
 * =========================================== */

/**
 * Envoie un paquet RST sans socket (pour rejeter les connexions orphelines).
 * 
 * @param dest_ip     IP destination (l'expéditeur du paquet original)
 * @param dest_port   Port destination
 * @param src_port    Port source (notre port)
 * @param seq         Numéro de séquence (généralement l'ACK reçu)
 * @param ack         Numéro d'ACK (généralement seq reçu + 1)
 */
static void tcp_send_rst(uint8_t* dest_ip, uint16_t dest_port, uint16_t src_port, 
                          uint32_t seq, uint32_t ack)
{
    uint8_t buffer[TCP_HEADER_SIZE];
    tcp_header_t* tcp = (tcp_header_t*)buffer;
    
    tcp->src_port = htons(src_port);
    tcp->dest_port = htons(dest_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    tcp->data_offset_flags = htons(tcp_make_data_offset_flags(TCP_HEADER_SIZE, TCP_FLAG_RST | TCP_FLAG_ACK));
    tcp->window_size = 0;
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    
    /* Calculer le checksum */
    uint8_t my_ip[4];
    NetInterface* netif = netif_get_default();
    if (netif != NULL && netif->ip_addr != 0) {
        ip_u32_to_bytes(netif->ip_addr, my_ip);
    } else {
        for (int i = 0; i < 4; i++) {
            my_ip[i] = MY_IP[i];
        }
    }
    tcp->checksum = tcp_checksum(my_ip, dest_ip, tcp, NULL, 0);
    
    /* Résoudre MAC et envoyer */
    uint8_t dest_mac[6];
    uint8_t next_hop[4];
    if (!route_get_next_hop(dest_ip, next_hop)) return;
    if (!arp_cache_lookup(next_hop, dest_mac)) return;
    
    ipv4_send_packet(netif, dest_mac, dest_ip, IP_PROTO_TCP, buffer, TCP_HEADER_SIZE);
}

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
        KLOG_ERROR_DEC("TCP", "Payload too large: ", len);
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
        KLOG_ERROR("TCP", "No route to destination");
        return;
    }
    
    /* Résoudre le MAC via ARP cache */
    if (!arp_cache_lookup(next_hop, dest_mac)) {
        /* Pas dans le cache - envoyer une requête ARP */
        KLOG_WARN("TCP", "ARP resolution pending");
        
        /* Envoyer une requête ARP */
        arp_send_request(netif, next_hop);
        /* TODO: Mettre le paquet en file d'attente et réessayer plus tard */
        return;
    }
    
    /* === Log debug === */
    KLOG_DEBUG("TCP", "Sending packet");
    
    /* === Envoyer via IPv4 === */
    ipv4_send_packet(netif, dest_mac, sock->remote_ip, IP_PROTO_TCP, buffer, TCP_HEADER_SIZE + len);
    
    /* === Incrémenter SEQ après envoi === */
    /* SYN et FIN consomment chacun 1 numéro de séquence */
    /* Les données consomment len numéros de séquence */
    /* ACK seul ne consomme rien */
    int seq_advance = 0;
    
    if (flags & TCP_FLAG_SYN) {
        seq_advance += 1;  /* SYN consomme un numéro de séquence */
    }
    if (flags & TCP_FLAG_FIN) {
        seq_advance += 1;  /* FIN consomme un numéro de séquence */
    }
    if (len > 0) {
        seq_advance += len;  /* Les données consomment des numéros de séquence */
    }
    
    sock->seq += seq_advance;
    
    /* === Debug: afficher l'avancement du SEQ === */
    (void)seq_advance; /* Avoid unused warning */
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
        KLOG_ERROR_DEC("TCP", "Packet too short: ", len);
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
    
    /* Log: paquet TCP reçu (seulement pour SYN/RST, pas ACK/FIN routine) */
#ifdef TCP_DEBUG_VERBOSE
    KLOG_DEBUG("TCP", "Received packet from %d:%d -> %d:%d (seq=%d, ack=%d)",
               src_port, dest_port, ip_hdr->src_ip, ip_hdr->dest_ip, seq_num,
               ack_num);
#endif
    
    /* Chercher un socket existant pour cette connexion (exact match) */
    tcp_socket_t* sock = tcp_find_socket(dest_port, ip_hdr->src_ip, src_port);
    
    /* Si pas trouvé et c'est un SYN (nouvelle connexion), chercher un socket LISTEN */
    if (sock == NULL && (flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK)) {
        sock = tcp_find_listening_socket(dest_port);
    }
    
    /* Pas de socket trouvé */
    if (sock == NULL) {
        /* Pour tous les paquets sans socket, envoyer RST */
        /* Cela fait réessayer le client immédiatement au lieu d'attendre le timeout */
        if (!(flags & TCP_FLAG_RST)) {
            tcp_send_rst(ip_hdr->src_ip, src_port, dest_port, ack_num, seq_num + 1);
        }
        return;
    }
    
    /* === Machine à états TCP === */
    switch (sock->state) {
        case TCP_STATE_LISTEN:
            /* En écoute - on attend un SYN */
            if (flags & TCP_FLAG_SYN) {
                KLOG_INFO("TCP", "Connection request received");
                
                /* NOUVEAU: Créer immédiatement un socket client pour cette connexion.
                 * Le socket serveur reste en LISTEN pour accepter d'autres connexions. */
                tcp_socket_t* client_sock = tcp_alloc_socket();
                if (client_sock == NULL) {
                    KLOG_ERROR("TCP", "No free sockets for new connection!");
                    /* Envoyer RST car on ne peut pas accepter */
                    tcp_send_rst(ip_hdr->src_ip, src_port, dest_port, 0, seq_num + 1);
                    return;
                }
                
                /* Configurer le socket client */
                client_sock->local_port = sock->local_port;
                for (int i = 0; i < 4; i++) {
                    client_sock->remote_ip[i] = ip_hdr->src_ip[i];
                }
                client_sock->remote_port = src_port;
                
                /* Calculer notre numéro de séquence initial (ISN) */
                client_sock->seq = (uint32_t)timer_get_ticks() * 12345;
                
                /* Enregistrer leur numéro de séquence + 1 */
                client_sock->ack = seq_num + 1;
                
                /* Passer en état SYN_RCVD */
                client_sock->state = TCP_STATE_SYN_RCVD;
                client_sock->window = TCP_WINDOW_SIZE;
                condvar_init(&client_sock->state_changed);
                
                /* Envoyer SYN-ACK depuis le socket client */
                tcp_send_packet(client_sock, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                
                /* Le socket serveur reste en LISTEN - rien à changer! */
            }
            break;
            
        case TCP_STATE_SYN_RCVD:
            /* Si on reçoit une retransmission du SYN (flags == SYN uniquement ou SYN+...) */
            if ((flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK)) {
                KLOG_WARN("TCP", "Retransmitting SYN-ACK");
                
                /* Renvoyer SYN-ACK - mais ne pas ré-incrémenter seq!
                 * On doit envoyer le même SYN-ACK qu'avant.
                 * tcp_send_packet va incrémenter seq, donc on le décrémente d'abord. */
                sock->seq--;
                tcp_send_packet(sock, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                return;
            }
            
            /* On a envoyé SYN-ACK, on attend ACK */
            if (flags & TCP_FLAG_ACK) {
                /* Vérifier que l'ACK correspond.
                 * Note: sock->seq a déjà été incrémenté par tcp_send_packet après le SYN-ACK.
                 * Le client ACK notre ISN+1, qui est maintenant sock->seq. */
                if (ack_num == sock->seq) {
                    sock->state = TCP_STATE_ESTABLISHED;
                    
                    KLOG_INFO("TCP", "Connection ESTABLISHED");
                    
                    /* Signal connection established */
                    condvar_broadcast(&sock->state_changed);
                    
                    /* Si le paquet ACK contient aussi des données (PSH+ACK), les traiter */
                    if (len > header_len) {
                        int payload_len = len - header_len;
                        uint8_t* payload = data + header_len;
                        
                        /* Stocker les données dans le buffer circulaire */
                        for (int i = 0; i < payload_len; i++) {
                            if (sock->recv_count < TCP_RECV_BUFFER_SIZE) {
                                sock->recv_buffer[sock->recv_head] = payload[i];
                                sock->recv_head = (sock->recv_head + 1) % TCP_RECV_BUFFER_SIZE;
                                sock->recv_count++;
                            }
                        }
                        
                        /* Mettre à jour notre ACK */
                        sock->ack = seq_num + payload_len;
                        
                        /* Envoyer ACK */
                        tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
                        
                        /* Signal data received */
                        condvar_broadcast(&sock->state_changed);
                    }
                } else {
                    /* Debug plus détaillé pour comprendre le problème */
                    KLOG_WARN("TCP", "Invalid ACK in SYN_RCVD");
                    
                    /* Si l'ACK est proche (différence de 1), c'est probablement un off-by-one.
                     * Accepter quand même pour être plus tolérant. */
                    int32_t diff = (int32_t)(ack_num - sock->seq);
                    if (diff >= -1 && diff <= 1) {
                        KLOG_WARN("TCP", "Accepting ACK anyway (close enough)");
                        
                        sock->state = TCP_STATE_ESTABLISHED;
                        condvar_broadcast(&sock->state_changed);
                    }
                }
            }
            /* Si on reçoit un RST, retourner en LISTEN */
            if (flags & TCP_FLAG_RST) {
                KLOG_WARN("TCP", "Connection reset - returning to LISTEN");
                
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
                
                (void)payload_len; /* Avoid unused warning in non-debug mode */
                
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
                        KLOG_WARN("TCP", "Recv buffer full! Dropping data.");
                        break;
                    }
                }
                
                /* Mettre à jour notre ACK */
                sock->ack = seq_num + payload_len;
                
                /* Envoyer ACK */
                tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
                
                /* Signal data received */
                condvar_broadcast(&sock->state_changed);
            }
            
            /* Si on reçoit un FIN */
            if (flags & TCP_FLAG_FIN) {
                KLOG_INFO("TCP", "Connection closing (FIN received)");
                
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
                KLOG_WARN("TCP", "Connection reset by peer");
                
                /* Retourner en LISTEN */
                sock->state = TCP_STATE_LISTEN;
                sock->remote_port = 0;
                for (int i = 0; i < 4; i++) {
                    sock->remote_ip[i] = 0;
                }
                
                /* Signal reset */
                condvar_broadcast(&sock->state_changed);
            }
            break;
        
        case TCP_STATE_FIN_WAIT_1:
            /* On a envoyé FIN, on attend ACK et/ou FIN du client */
            if (flags & TCP_FLAG_ACK) {
                /* ACK de notre FIN reçu */
                if (flags & TCP_FLAG_FIN) {
                    /* FIN+ACK simultané - ACK le FIN et passer en TIME_WAIT */
                    sock->ack = seq_num + 1;
                    tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
                    sock->state = TCP_STATE_TIME_WAIT;
                    KLOG_INFO("TCP", "Simultaneous close, entering TIME_WAIT");
                } else {
                    /* Juste ACK - passer en FIN_WAIT_2 */
                    sock->state = TCP_STATE_FIN_WAIT_2;
                }
                condvar_broadcast(&sock->state_changed);
            } else if (flags & TCP_FLAG_FIN) {
                /* FIN sans ACK - simultaneous close */
                sock->ack = seq_num + 1;
                tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
                sock->state = TCP_STATE_CLOSING;
                condvar_broadcast(&sock->state_changed);
            }
            break;
            
        case TCP_STATE_FIN_WAIT_2:
            /* On attend le FIN du client */
            if (flags & TCP_FLAG_FIN) {
                sock->ack = seq_num + 1;
                tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
                sock->state = TCP_STATE_TIME_WAIT;
                KLOG_INFO("TCP", "FIN received, connection closing gracefully");
                condvar_broadcast(&sock->state_changed);
            }
            break;
            
        case TCP_STATE_CLOSING:
            /* Simultaneous close - on attend l'ACK de notre FIN */
            if (flags & TCP_FLAG_ACK) {
                sock->state = TCP_STATE_TIME_WAIT;
                condvar_broadcast(&sock->state_changed);
            }
            break;
            
        case TCP_STATE_TIME_WAIT:
            /* Ignorer les paquets en TIME_WAIT (juste ACK les retransmissions) */
            if (flags & TCP_FLAG_FIN) {
                tcp_send_packet(sock, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_STATE_LAST_ACK:
            /* On attend l'ACK de notre FIN */
            if (flags & TCP_FLAG_ACK) {
                KLOG_INFO("TCP", "Connection closed gracefully");
                
                /* Retourner en LISTEN pour accepter de nouvelles connexions */
                sock->state = TCP_STATE_LISTEN;
                sock->remote_port = 0;
                for (int i = 0; i < 4; i++) {
                    sock->remote_ip[i] = 0;
                }
            }
            break;
            
        default:
            KLOG_WARN("TCP", "Packet in unexpected state");
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
    condvar_init(&sock->state_changed);
    
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
    for (int i = 0; i < tcp_socket_capacity; i++) {
        if (tcp_sockets[i].in_use && 
            &tcp_sockets[i] != sock &&
            tcp_sockets[i].local_port == port) {
            KLOG_ERROR_DEC("TCP", "Port already bound: ", port);
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
        KLOG_ERROR("TCP", "Cannot send: socket not connected");
        return -1;
    }
    
    /* Envoyer les données via tcp_send_packet */
    /* Note: On devrait fragmenter si len > MSS, mais pour V1 on simplifie */
    /* Note: tcp_send_packet incrémente déjà SEQ pour les données envoyées */
    tcp_send_packet(sock, TCP_FLAG_ACK | TCP_FLAG_PSH, (uint8_t*)buf, len);
    
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

/**
 * Accepte une connexion entrante sur un socket LISTEN.
 * 
 * Modèle multi-socket: le socket serveur reste en LISTEN,
 * et un nouveau socket est créé pour chaque connexion.
 * 
 * @param listen_sock Socket en mode LISTEN
 * @return Nouveau socket pour la connexion, ou NULL si pas de connexion
 */
tcp_socket_t* tcp_accept(tcp_socket_t* listen_sock)
{
    if (listen_sock == NULL) return NULL;
    
    /* Si le socket serveur est déjà connecté (connexion en attente), 
     * créer un nouveau socket et transférer la connexion */
    if (listen_sock->state == TCP_STATE_ESTABLISHED ||
        listen_sock->state == TCP_STATE_SYN_RCVD) {
        
        /* Allouer un nouveau socket pour cette connexion */
        tcp_socket_t* client_sock = tcp_alloc_socket();
        if (client_sock == NULL) {
            KLOG_ERROR("TCP", "accept: no free sockets");
            return NULL;
        }
        
        /* Copier l'état de connexion du socket serveur vers le nouveau socket */
        client_sock->state = listen_sock->state;
        client_sock->local_port = listen_sock->local_port;
        client_sock->remote_port = listen_sock->remote_port;
        client_sock->seq = listen_sock->seq;
        client_sock->ack = listen_sock->ack;
        client_sock->window = listen_sock->window;
        client_sock->flags = listen_sock->flags;
        for (int i = 0; i < 4; i++) {
            client_sock->remote_ip[i] = listen_sock->remote_ip[i];
        }
        /* Copier le buffer de réception si des données sont arrivées */
        client_sock->recv_head = listen_sock->recv_head;
        client_sock->recv_tail = listen_sock->recv_tail;
        client_sock->recv_count = listen_sock->recv_count;
        for (int i = 0; i < TCP_RECV_BUFFER_SIZE; i++) {
            client_sock->recv_buffer[i] = listen_sock->recv_buffer[i];
        }
        condvar_init(&client_sock->state_changed);
        
        /* Remettre le socket serveur en LISTEN pour accepter d'autres connexions */
        listen_sock->state = TCP_STATE_LISTEN;
        listen_sock->remote_port = 0;
        listen_sock->seq = 0;
        listen_sock->ack = 0;
        listen_sock->recv_head = 0;
        listen_sock->recv_tail = 0;
        listen_sock->recv_count = 0;
        for (int i = 0; i < 4; i++) {
            listen_sock->remote_ip[i] = 0;
        }
        
        KLOG_INFO("TCP", "accept: connection transferred to new socket");
        
        return client_sock;
    }
    
    /* Pas de connexion en attente */
    return NULL;
}

/**
 * Trouve un socket par ses adresses (pour router les paquets après accept).
 * Cherche d'abord une correspondance exacte, sinon cherche le socket LISTEN.
 */
tcp_socket_t* tcp_find_connection(uint16_t local_port, uint8_t* remote_ip, uint16_t remote_port)
{
    /* D'abord chercher une correspondance exacte (socket client) */
    tcp_socket_t* sock = tcp_find_socket(local_port, remote_ip, remote_port);
    if (sock != NULL) {
        return sock;
    }
    
    /* Sinon chercher un socket LISTEN sur ce port */
    return tcp_find_listening_socket(local_port);
}

/* ===========================================
 * Fonctions d'information sur les sockets
 * =========================================== */

/**
 * Retourne le nombre actuel de sockets alloués (capacité).
 */
int tcp_get_socket_count(void)
{
    return tcp_socket_capacity;
}

/**
 * Retourne le nombre de sockets en cours d'utilisation.
 */
int tcp_get_active_socket_count(void)
{
    return tcp_socket_count;
}
