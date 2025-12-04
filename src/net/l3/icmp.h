/* src/net/l3/icmp.h - ICMP Protocol Handler */
#ifndef NET_ICMP_H
#define NET_ICMP_H

#include <stdint.h>
#include <stdbool.h>
#include "ipv4.h"
#include "../l2/ethernet.h"

/* Forward declaration */
struct NetInterface;

/* ICMP Types */
#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_DEST_UNREACH  3
#define ICMP_TYPE_ECHO_REQUEST  8
#define ICMP_TYPE_TIME_EXCEEDED 11

/* ICMP Codes for Destination Unreachable */
#define ICMP_CODE_NET_UNREACH   0
#define ICMP_CODE_HOST_UNREACH  1
#define ICMP_CODE_PORT_UNREACH  3

/**
 * ICMP Header (8 bytes minimum)
 * 
 * Structure du header ICMP:
 * +--------+--------+--------+--------+
 * |  Type  |  Code  |    Checksum     |
 * +--------+--------+--------+--------+
 * |         Identifier (opt)          |
 * |       Sequence Number (opt)       |
 * +--------+--------+--------+--------+
 * 
 * Pour Echo Request/Reply:
 * - Type: 8 (Request) ou 0 (Reply)
 * - Code: 0
 * - Identifier et Sequence Number sont présents
 */
typedef struct __attribute__((packed)) {
    uint8_t  type;          /* Message Type */
    uint8_t  code;          /* Message Code */
    uint16_t checksum;      /* Checksum (sur tout le message ICMP) */
    uint16_t identifier;    /* Identifier (pour Echo) */
    uint16_t sequence;      /* Sequence Number (pour Echo) */
} icmp_header_t;

/* Taille du header ICMP Echo */
#define ICMP_HEADER_SIZE    8

/* Ping default data size */
#define PING_DATA_SIZE      56
#define PING_DEFAULT_COUNT  4
#define PING_TIMEOUT_MS     2000

/* État du ping en cours */
typedef struct {
    uint8_t dest_ip[4];         /* IP de destination */
    char hostname[64];          /* Hostname (si résolu via DNS) */
    uint16_t identifier;        /* ID du ping */
    uint16_t sequence;          /* Numéro de séquence actuel */
    uint16_t sent;              /* Nombre envoyés */
    uint16_t received;          /* Nombre reçus */
    uint8_t ttl;                /* TTL de la réponse */
    uint32_t time;              /* Temps de réponse en ms */
    uint64_t send_time;         /* Timestamp d'envoi (pour calcul RTT) */
    uint32_t min_time;          /* Temps min */
    uint32_t max_time;          /* Temps max */
    uint32_t total_time;        /* Temps total (pour moyenne) */
    bool waiting;               /* Attente d'une réponse */
    bool active;                /* Ping en cours */
} ping_state_t;

/**
 * Traite un paquet ICMP reçu.
 * 
 * @param netif     Interface réseau sur laquelle le paquet a été reçu
 * @param eth       Header Ethernet (pour récupérer la MAC source)
 * @param ip_hdr    Header IPv4 du paquet
 * @param icmp_data Pointeur vers le début du paquet ICMP
 * @param len       Longueur du paquet ICMP en bytes
 */
void icmp_handle_packet(struct NetInterface* netif, ethernet_header_t* eth, 
                        ipv4_header_t* ip_hdr, uint8_t* icmp_data, int len);

/**
 * Envoie un Echo Request (ping) vers une adresse IP.
 * 
 * @param dest_ip Adresse IP de destination (4 bytes)
 */
void icmp_send_echo_request(const uint8_t* dest_ip);

/**
 * Ping une adresse IP (envoie un Echo Request et attend la réponse).
 * 
 * @param dest_ip Adresse IP de destination (4 bytes)
 * @return 0 si succès, -1 si erreur
 */
int ping_ip(const uint8_t* dest_ip);

/**
 * Ping un hostname (résolution DNS puis ping).
 * 
 * @param hostname Nom d'hôte à pinger (ex: "google.com")
 * @return 0 si succès, -1 si erreur DNS, -2 si pas de réponse
 */
int ping(const char* hostname);

/**
 * Ping une adresse IP de manière continue.
 * 
 * @param dest_ip Adresse IP de destination (4 bytes)
 * @return 0 si succès, -1 si erreur
 */
int ping_ip_continuous(const uint8_t* dest_ip);

/**
 * Ping un hostname de manière continue (résolution DNS puis ping).
 * 
 * @param hostname Nom d'hôte à pinger (ex: "google.com")
 * @return 0 si succès, -1 si erreur DNS, -2 si pas de réponse
 */
int ping_continuous(const char* hostname);

/**
 * Vérifie si un ping est en attente de réponse.
 * 
 * @return true si en attente
 */
bool ping_is_waiting(void);

/**
 * Récupère les statistiques du dernier ping.
 * 
 * @param sent     Nombre de paquets envoyés (peut être NULL)
 * @param received Nombre de paquets reçus (peut être NULL)
 */
void ping_get_stats(uint16_t* sent, uint16_t* received);

#endif /* NET_ICMP_H */
