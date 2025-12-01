/* src/net/l4/dns.h - DNS Resolver Client */
#ifndef NET_L4_DNS_H
#define NET_L4_DNS_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================
 * Constantes DNS (RFC 1035)
 * ======================================== */

/* Port DNS */
#define DNS_PORT            53

/* Tailles */
#define DNS_HEADER_SIZE     12
#define DNS_MAX_NAME_LEN    255
#define DNS_MAX_PACKET_SIZE 512

/* Types d'enregistrement DNS */
#define DNS_TYPE_A          1       /* Host address (IPv4) */
#define DNS_TYPE_NS         2       /* Authoritative name server */
#define DNS_TYPE_CNAME      5       /* Canonical name */
#define DNS_TYPE_SOA        6       /* Start of authority */
#define DNS_TYPE_PTR        12      /* Domain name pointer */
#define DNS_TYPE_MX         15      /* Mail exchange */
#define DNS_TYPE_TXT        16      /* Text strings */
#define DNS_TYPE_AAAA       28      /* IPv6 address */

/* Classes DNS */
#define DNS_CLASS_IN        1       /* Internet */

/* Flags DNS (dans le header) */
#define DNS_FLAG_QR         0x8000  /* Query/Response: 0=Query, 1=Response */
#define DNS_FLAG_OPCODE     0x7800  /* Opcode (0=standard query) */
#define DNS_FLAG_AA         0x0400  /* Authoritative Answer */
#define DNS_FLAG_TC         0x0200  /* Truncation */
#define DNS_FLAG_RD         0x0100  /* Recursion Desired */
#define DNS_FLAG_RA         0x0080  /* Recursion Available */
#define DNS_FLAG_Z          0x0070  /* Reserved */
#define DNS_FLAG_RCODE      0x000F  /* Response code */

/* Codes de réponse DNS */
#define DNS_RCODE_OK        0       /* No error */
#define DNS_RCODE_FORMAT    1       /* Format error */
#define DNS_RCODE_SERVFAIL  2       /* Server failure */
#define DNS_RCODE_NXDOMAIN  3       /* Name does not exist */
#define DNS_RCODE_NOTIMP    4       /* Not implemented */
#define DNS_RCODE_REFUSED   5       /* Query refused */

/* ========================================
 * Structures DNS
 * ======================================== */

/**
 * En-tête DNS (RFC 1035, Section 4.1.1)
 * 
 *                                  1  1  1  1  1  1
 *    0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                      ID                       |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    QDCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    ANCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    NSCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    ARCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 */
typedef struct __attribute__((packed)) {
    uint16_t id;            /* Transaction ID */
    uint16_t flags;         /* Flags (QR, Opcode, AA, TC, RD, RA, Z, RCODE) */
    uint16_t qd_count;      /* Number of questions */
    uint16_t an_count;      /* Number of answer RRs */
    uint16_t ns_count;      /* Number of authority RRs */
    uint16_t ar_count;      /* Number of additional RRs */
} dns_header_t;

/**
 * Entrée de cache DNS (pour un éventuel cache)
 */
typedef struct {
    char hostname[64];      /* Nom d'hôte */
    uint8_t ip[4];          /* Adresse IP résolue */
    uint32_t ttl;           /* Time to live */
    bool valid;             /* Entrée valide */
} dns_cache_entry_t;

/**
 * Requête DNS en attente
 */
typedef struct {
    uint16_t id;            /* Transaction ID */
    char hostname[64];      /* Hostname demandé */
    uint8_t resolved_ip[4]; /* IP résolue (si succès) */
    bool completed;         /* Résolution terminée */
    bool success;           /* Résolution réussie */
} dns_pending_query_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le résolveur DNS avec l'adresse du serveur DNS.
 * 
 * @param dns_server Adresse IP du serveur DNS (format uint32_t)
 */
void dns_init(uint32_t dns_server);

/**
 * Encode un nom de domaine au format DNS.
 * Ex: "google.com" -> "\x06google\x03com\x00"
 * 
 * @param buffer Buffer de destination (doit être assez grand)
 * @param hostname Nom de domaine à encoder
 * @return Nombre de bytes écrits
 */
int dns_encode_name(uint8_t* buffer, const char* hostname);

/**
 * Envoie une requête DNS pour résoudre un nom d'hôte.
 * 
 * @param hostname Nom d'hôte à résoudre (ex: "google.com")
 */
void dns_send_query(const char* hostname);

/**
 * Traite une réponse DNS reçue.
 * Appelé par le dispatcher UDP quand un paquet arrive sur le port 53.
 * 
 * @param data    Payload UDP (données DNS)
 * @param len     Longueur des données
 */
void dns_handle_packet(uint8_t* data, int len);

/**
 * Vérifie si une résolution est en attente.
 * 
 * @return true si une requête DNS est en cours
 */
bool dns_is_pending(void);

/**
 * Récupère le résultat de la dernière résolution.
 * 
 * @param out_ip Buffer pour stocker l'IP résolue (4 bytes)
 * @return true si une IP a été résolue, false sinon
 */
bool dns_get_result(uint8_t* out_ip);

#endif /* NET_L4_DNS_H */
