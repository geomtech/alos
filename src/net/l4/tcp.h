/* src/net/l4/tcp.h - TCP Protocol Handler */
#ifndef NET_TCP_H
#define NET_TCP_H

#include <stdint.h>
#include <stdbool.h>
#include "../l3/ipv4.h"

/* TCP Header Size (without options) */
#define TCP_HEADER_SIZE     20

/* Maximum number of TCP sockets */
#define TCP_MAX_SOCKETS     16

/* Receive buffer size per socket */
#define TCP_RECV_BUFFER_SIZE    4096

/* Well-known ports */
#define TCP_PORT_HTTP       80
#define TCP_PORT_HTTPS      443
#define TCP_PORT_SSH        22
#define TCP_PORT_TELNET     23
#define TCP_PORT_FTP        21

/* ===========================================
 * TCP Flags (6 bits)
 * =========================================== */
#define TCP_FLAG_FIN    0x01    /* Finish - no more data from sender */
#define TCP_FLAG_SYN    0x02    /* Synchronize sequence numbers */
#define TCP_FLAG_RST    0x04    /* Reset the connection */
#define TCP_FLAG_PSH    0x08    /* Push - deliver data immediately */
#define TCP_FLAG_ACK    0x10    /* Acknowledgment field is significant */
#define TCP_FLAG_URG    0x20    /* Urgent pointer field is significant */

/* ===========================================
 * TCP States (RFC 793 State Machine)
 * =========================================== */
typedef enum {
    TCP_STATE_CLOSED = 0,       /* No connection */
    TCP_STATE_LISTEN,           /* Waiting for connection (server) */
    TCP_STATE_SYN_SENT,         /* SYN sent, waiting for SYN-ACK (client) */
    TCP_STATE_SYN_RCVD,         /* SYN received, SYN-ACK sent (server) */
    TCP_STATE_ESTABLISHED,      /* Connection open, data transfer */
    TCP_STATE_FIN_WAIT_1,       /* FIN sent, waiting for ACK */
    TCP_STATE_FIN_WAIT_2,       /* FIN acknowledged, waiting for FIN */
    TCP_STATE_CLOSE_WAIT,       /* FIN received, waiting for app to close */
    TCP_STATE_CLOSING,          /* Both sides sent FIN simultaneously */
    TCP_STATE_LAST_ACK,         /* Waiting for ACK of FIN */
    TCP_STATE_TIME_WAIT         /* Waiting for packets to expire */
} tcp_state_t;

/**
 * TCP Header (20 bytes minimum, sans options)
 * 
 * Structure du header TCP:
 * +--------+--------+--------+--------+
 * |     Source Port |   Dest Port     |
 * +--------+--------+--------+--------+
 * |           Sequence Number         |
 * +--------+--------+--------+--------+
 * |        Acknowledgment Number      |
 * +--------+--------+--------+--------+
 * |Offset|Res|Flags |   Window Size   |
 * +--------+--------+--------+--------+
 * |    Checksum     |  Urgent Pointer |
 * +--------+--------+--------+--------+
 * 
 * Note: Tous les champs sont en big-endian (network byte order)
 */
typedef struct __attribute__((packed)) {
    uint16_t src_port;          /* Source port */
    uint16_t dest_port;         /* Destination port */
    uint32_t seq_num;           /* Sequence number */
    uint32_t ack_num;           /* Acknowledgment number */
    uint16_t data_offset_flags; /* Data offset (4 bits) + Reserved (6 bits) + Flags (6 bits) */
    uint16_t window_size;       /* Window size */
    uint16_t checksum;          /* Checksum */
    uint16_t urgent_ptr;        /* Urgent pointer */
} tcp_header_t;

/**
 * TCP Pseudo Header (pour le calcul du checksum)
 * 
 * Le checksum TCP est calculé sur:
 * - Pseudo header (12 bytes)
 * - Header TCP (20+ bytes)
 * - Data TCP
 */
typedef struct __attribute__((packed)) {
    uint8_t  src_ip[4];         /* Source IP address */
    uint8_t  dest_ip[4];        /* Destination IP address */
    uint8_t  zero;              /* Reserved (must be 0) */
    uint8_t  protocol;          /* Protocol (always 6 for TCP) */
    uint16_t tcp_length;        /* TCP header + data length */
} tcp_pseudo_header_t;

/**
 * TCP Socket structure
 * Représente une connexion TCP (ou un socket en écoute)
 */
typedef struct tcp_socket {
    /* État du socket */
    tcp_state_t state;          /* Current state (CLOSED, LISTEN, etc.) */
    bool        in_use;         /* Is this socket slot in use? */
    
    /* Addresses */
    uint16_t    local_port;     /* Local port */
    uint16_t    remote_port;    /* Remote port (0 for LISTEN) */
    uint8_t     remote_ip[4];   /* Remote IP (0.0.0.0 for LISTEN) */
    
    /* Sequence numbers */
    uint32_t    seq;            /* Our sequence number (next byte to send) */
    uint32_t    ack;            /* Their sequence number (next byte expected) */
    
    /* Window */
    uint16_t    window;         /* Our receive window size */
    
    /* Flags internes */
    uint8_t     flags;          /* Internal flags (awaiting ACK, etc.) */
    
    /* Receive buffer (circular) */
    uint8_t     recv_buffer[TCP_RECV_BUFFER_SIZE];
    uint16_t    recv_head;      /* Where to write next */
    uint16_t    recv_tail;      /* Where to read next */
    uint16_t    recv_count;     /* Number of bytes in buffer */
} tcp_socket_t;

/* Internal socket flags */
#define TCP_SOCK_AWAITING_ACK   0x01    /* We're waiting for an ACK */

/* ===========================================
 * Fonctions publiques
 * =========================================== */

/**
 * Initialise la stack TCP.
 * Doit être appelé au démarrage.
 */
void tcp_init(void);

/**
 * Crée un socket en mode LISTEN sur le port spécifié.
 * 
 * @param port Port local sur lequel écouter
 * @return Pointeur vers le socket, ou NULL si erreur
 */
tcp_socket_t* tcp_listen(uint16_t port);

/**
 * Ferme un socket TCP.
 * 
 * @param sock Socket à fermer
 */
void tcp_close(tcp_socket_t* sock);

/**
 * Traite un paquet TCP reçu.
 * Appelé par ipv4_handle_packet quand protocol == 6.
 * 
 * @param ip_hdr Header IPv4 du paquet
 * @param data   Pointeur vers le début du paquet TCP
 * @param len    Longueur totale du paquet TCP
 */
void tcp_handle_packet(ipv4_header_t* ip_hdr, uint8_t* data, int len);

/**
 * Envoie un paquet TCP.
 * 
 * @param sock    Socket à utiliser
 * @param flags   Flags TCP (SYN, ACK, FIN, etc.)
 * @param payload Données à envoyer (peut être NULL)
 * @param len     Longueur des données
 */
void tcp_send_packet(tcp_socket_t* sock, uint8_t flags, uint8_t* payload, int len);

/**
 * Retourne le nom d'un état TCP (pour debug).
 * 
 * @param state État TCP
 * @return Chaîne décrivant l'état
 */
const char* tcp_state_name(tcp_state_t state);

/**
 * Crée un nouveau socket TCP (non connecté).
 * 
 * @return Pointeur vers le socket, ou NULL si erreur
 */
tcp_socket_t* tcp_socket_create(void);

/**
 * Lie un socket à un port local.
 * 
 * @param sock Socket à lier
 * @param port Port local (host byte order)
 * @return 0 si succès, -1 si erreur
 */
int tcp_bind(tcp_socket_t* sock, uint16_t port);

/**
 * Lit des données depuis le buffer de réception d'un socket.
 * Non-bloquant: retourne immédiatement ce qui est disponible.
 * 
 * @param sock  Socket TCP
 * @param buf   Buffer destination
 * @param len   Taille maximale à lire
 * @return Nombre de bytes lus, 0 si buffer vide, -1 si erreur
 */
int tcp_recv(tcp_socket_t* sock, uint8_t* buf, int len);

/**
 * Envoie des données via un socket TCP.
 * 
 * @param sock  Socket TCP (doit être ESTABLISHED)
 * @param buf   Buffer source
 * @param len   Nombre de bytes à envoyer
 * @return Nombre de bytes envoyés, -1 si erreur
 */
int tcp_send(tcp_socket_t* sock, const uint8_t* buf, int len);

/**
 * Vérifie si des données sont disponibles en lecture.
 * 
 * @param sock Socket TCP
 * @return Nombre de bytes disponibles
 */
int tcp_available(tcp_socket_t* sock);

#endif /* NET_TCP_H */
