/* src/net/ipv4.h - IPv4 Protocol Handler */
#ifndef NET_IPV4_H
#define NET_IPV4_H

#include <stdint.h>
#include "ethernet.h"

/* IP Protocol Numbers */
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

/* Default TTL */
#define IP_DEFAULT_TTL  64

/**
 * IPv4 Header (20 bytes minimum, sans options)
 * 
 * Structure du header IPv4:
 * +--------+--------+--------+--------+
 * |Ver|IHL |  ToS   |   Total Length  |
 * +--------+--------+--------+--------+
 * |  Identification |Flags|Frag Offset|
 * +--------+--------+--------+--------+
 * |  TTL   |Protocol|  Header Checksum|
 * +--------+--------+--------+--------+
 * |         Source IP Address         |
 * +--------+--------+--------+--------+
 * |       Destination IP Address      |
 * +--------+--------+--------+--------+
 * 
 * Note: Tous les champs multi-octets sont en big-endian
 */
typedef struct __attribute__((packed)) {
    uint8_t  version_ihl;       /* Version (4 bits) + IHL (4 bits) */
    uint8_t  tos;               /* Type of Service */
    uint16_t total_length;      /* Total Length (header + data) */
    uint16_t identification;    /* Identification */
    uint16_t flags_fragment;    /* Flags (3 bits) + Fragment Offset (13 bits) */
    uint8_t  ttl;               /* Time To Live */
    uint8_t  protocol;          /* Protocol (1=ICMP, 6=TCP, 17=UDP) */
    uint16_t checksum;          /* Header Checksum */
    uint8_t  src_ip[4];         /* Source IP Address */
    uint8_t  dest_ip[4];        /* Destination IP Address */
} ipv4_header_t;

/* Taille minimale du header IPv4 (sans options) */
#define IPV4_HEADER_SIZE    20

/**
 * Traite un paquet IPv4 reçu.
 * 
 * @param eth  Header Ethernet du paquet (pour récupérer la MAC source)
 * @param data Pointeur vers le début du paquet IPv4 (payload Ethernet)
 * @param len  Longueur totale du paquet IPv4 en bytes
 */
void ipv4_handle_packet(ethernet_header_t* eth, uint8_t* data, int len);

/**
 * Envoie un paquet IPv4.
 * 
 * @param dest_mac   MAC de destination (6 bytes)
 * @param dest_ip    IP de destination (4 bytes)
 * @param protocol   Protocole (1=ICMP, 6=TCP, 17=UDP)
 * @param payload    Données à envoyer
 * @param payload_len Longueur des données
 */
void ipv4_send_packet(uint8_t* dest_mac, uint8_t* dest_ip, uint8_t protocol,
                      uint8_t* payload, int payload_len);

/**
 * Calcule le checksum Internet (RFC 1071).
 * Somme complémentée à 1 sur 16 bits.
 * 
 * @param data Pointeur vers les données
 * @param len  Longueur en bytes
 * @return Checksum (déjà en network byte order)
 */
uint16_t ip_checksum(void* data, int len);

#endif /* NET_IPV4_H */
