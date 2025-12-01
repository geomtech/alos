/* src/net/l4/dns.h - DNS Resolver Client */
#ifndef NET_L4_DNS_H
#define NET_L4_DNS_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================
 * Constantes DNS (RFC 1035)
 * ======================================== */

#define DNS_PORT            53
#define DNS_HEADER_SIZE     12
#define DNS_MAX_NAME_LEN    255
#define DNS_MAX_PACKET_SIZE 512

/* Cache DNS */
#define DNS_CACHE_SIZE      16
#define DNS_CACHE_DEFAULT_TTL 300   /* 5 minutes */

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

/* Flags DNS */
#define DNS_FLAG_QR         0x8000
#define DNS_FLAG_OPCODE     0x7800
#define DNS_FLAG_AA         0x0400
#define DNS_FLAG_TC         0x0200
#define DNS_FLAG_RD         0x0100
#define DNS_FLAG_RA         0x0080
#define DNS_FLAG_Z          0x0070
#define DNS_FLAG_RCODE      0x000F

/* Codes de réponse */
#define DNS_RCODE_OK        0
#define DNS_RCODE_FORMAT    1
#define DNS_RCODE_SERVFAIL  2
#define DNS_RCODE_NXDOMAIN  3
#define DNS_RCODE_NOTIMP    4
#define DNS_RCODE_REFUSED   5

/* Types de requête interne */
typedef enum {
    DNS_QUERY_A,        /* hostname -> IP */
    DNS_QUERY_PTR,      /* IP -> hostname (reverse) */
    DNS_QUERY_CNAME
} dns_query_type_t;

/* ========================================
 * Structures DNS
 * ======================================== */

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

typedef struct {
    char hostname[64];
    uint8_t ip[4];
    char cname[64];
    uint32_t ttl;
    uint32_t timestamp;
    uint8_t record_type;
    bool valid;
} dns_cache_entry_t;

typedef struct {
    uint16_t id;
    char hostname[64];
    uint8_t resolved_ip[4];
    char resolved_name[64];     /* Pour PTR */
    char cname[64];
    dns_query_type_t type;
    bool completed;
    bool success;
    bool has_cname;
} dns_pending_query_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

void dns_init(uint32_t dns_server);
int dns_encode_name(uint8_t* buffer, const char* hostname);
void dns_send_query(const char* hostname);
void dns_send_reverse_query(const uint8_t* ip);
void dns_handle_packet(uint8_t* data, int len);
bool dns_is_pending(void);
bool dns_get_result(uint8_t* out_ip);
bool dns_get_reverse_result(char* out_name, int max_len);
bool dns_get_cname(char* out_cname, int max_len);

/* Cache DNS */
bool dns_cache_lookup(const char* hostname, uint8_t* out_ip);
bool dns_cache_reverse_lookup(const uint8_t* ip, char* out_name, int max_len);
void dns_cache_add(const char* hostname, const uint8_t* ip, uint32_t ttl);
void dns_cache_add_ptr(const uint8_t* ip, const char* hostname, uint32_t ttl);
void dns_cache_flush(void);
void dns_cache_stats(void);

#endif /* NET_L4_DNS_H */
