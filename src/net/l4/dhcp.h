/* src/net/l4/dhcp.h - DHCP Client Protocol */
#ifndef NET_L4_DHCP_H
#define NET_L4_DHCP_H

#include <stdint.h>
#include <stdbool.h>
#include "../core/netdev.h"

/* ========================================
 * Constantes DHCP (RFC 2131)
 * ======================================== */

/* Ports UDP */
#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68

/* Types de message (op) */
#define DHCP_BOOTREQUEST    1
#define DHCP_BOOTREPLY      2

/* Types de hardware */
#define DHCP_HTYPE_ETH      1   /* Ethernet */
#define DHCP_HLEN_ETH       6   /* 6 bytes MAC */

/* DHCP Magic Cookie (RFC 1533) */
#define DHCP_MAGIC_COOKIE   0x63825363

/* Options DHCP */
#define DHCP_OPT_PAD             0
#define DHCP_OPT_SUBNET_MASK     1
#define DHCP_OPT_ROUTER          3
#define DHCP_OPT_DNS             6
#define DHCP_OPT_HOSTNAME        12
#define DHCP_OPT_DOMAIN_NAME     15
#define DHCP_OPT_BROADCAST       28
#define DHCP_OPT_REQUESTED_IP    50
#define DHCP_OPT_LEASE_TIME      51
#define DHCP_OPT_MESSAGE_TYPE    53
#define DHCP_OPT_SERVER_ID       54
#define DHCP_OPT_PARAM_REQUEST   55
#define DHCP_OPT_RENEWAL_TIME    58
#define DHCP_OPT_REBIND_TIME     59
#define DHCP_OPT_CLIENT_ID       61
#define DHCP_OPT_END             255

/* Types de messages DHCP (option 53) */
#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPDECLINE     4
#define DHCPACK         5
#define DHCPNAK         6
#define DHCPRELEASE     7
#define DHCPINFORM      8

/* États du client DHCP */
typedef enum {
    DHCP_STATE_INIT,        /* État initial, pas de bail */
    DHCP_STATE_SELECTING,   /* DISCOVER envoyé, attend OFFER */
    DHCP_STATE_REQUESTING,  /* REQUEST envoyé, attend ACK */
    DHCP_STATE_BOUND,       /* Bail actif */
    DHCP_STATE_RENEWING,    /* Renouvellement en cours */
    DHCP_STATE_REBINDING    /* Rebinding en cours */
} dhcp_state_t;

/* ========================================
 * Structures DHCP
 * ======================================== */

/**
 * En-tête DHCP (RFC 2131).
 * Format fixe de 236 bytes + magic cookie (4) + options (variable).
 */
typedef struct __attribute__((packed)) {
    uint8_t  op;            /* 1=Request, 2=Reply */
    uint8_t  htype;         /* Hardware type (1=Ethernet) */
    uint8_t  hlen;          /* Hardware address length (6 for Ethernet) */
    uint8_t  hops;          /* Hops (0 for client) */
    uint32_t xid;           /* Transaction ID */
    uint16_t secs;          /* Seconds since start */
    uint16_t flags;         /* Flags (0x8000 = broadcast) */
    uint32_t ciaddr;        /* Client IP (if known) */
    uint32_t yiaddr;        /* 'Your' IP (offered by server) */
    uint32_t siaddr;        /* Server IP */
    uint32_t giaddr;        /* Gateway IP (relay agent) */
    uint8_t  chaddr[16];    /* Client hardware address */
    uint8_t  sname[64];     /* Server hostname (optional) */
    uint8_t  file[128];     /* Boot filename (optional) */
    /* Followed by: magic cookie (4 bytes) + options */
} dhcp_header_t;

#define DHCP_HEADER_SIZE    236
#define DHCP_OPTIONS_OFFSET (DHCP_HEADER_SIZE + 4)  /* After magic cookie */

/**
 * Contexte DHCP pour une interface.
 */
typedef struct {
    NetInterface*  netif;           /* Interface réseau */
    dhcp_state_t   state;           /* État courant */
    uint32_t       xid;             /* Transaction ID */
    uint32_t       offered_ip;      /* IP offerte par le serveur */
    uint32_t       server_ip;       /* IP du serveur DHCP */
    uint32_t       lease_time;      /* Durée du bail (secondes) */
    uint32_t       renewal_time;    /* T1: temps avant renouvellement */
    uint32_t       rebind_time;     /* T2: temps avant rebind */
    /* Statistiques */
    uint32_t       discover_count;  /* Nombre de DISCOVER envoyés */
    uint32_t       request_count;   /* Nombre de REQUEST envoyés */
} dhcp_context_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le client DHCP pour une interface.
 * 
 * @param netif Interface réseau à configurer
 */
void dhcp_init(NetInterface* netif);

/**
 * Démarre la découverte DHCP (envoie DHCPDISCOVER).
 * Non bloquant: le callback sera appelé quand l'IP sera obtenue.
 * 
 * @param netif Interface réseau
 * @return 0 si DISCOVER envoyé, -1 en cas d'erreur
 */
int dhcp_discover(NetInterface* netif);

/**
 * Traite un paquet DHCP reçu.
 * Appelé par UDP quand un paquet arrive sur le port 68.
 * 
 * @param netif Interface réseau (peut être NULL, on utilise default)
 * @param data Données du paquet DHCP
 * @param len Taille des données
 */
void dhcp_handle_packet(NetInterface* netif, uint8_t* data, int len);

/**
 * Libère le bail DHCP.
 * 
 * @param netif Interface réseau
 */
void dhcp_release(NetInterface* netif);

/**
 * Retourne l'état courant du DHCP.
 * 
 * @param netif Interface réseau
 * @return État DHCP
 */
dhcp_state_t dhcp_get_state(NetInterface* netif);

/**
 * Vérifie si l'interface a une IP valide via DHCP.
 * 
 * @param netif Interface réseau
 * @return true si l'interface a une IP DHCP valide
 */
bool dhcp_is_bound(NetInterface* netif);

#endif /* NET_L4_DHCP_H */
