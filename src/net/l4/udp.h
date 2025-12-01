/* src/net/l4/udp.h - UDP Protocol Handler */
#ifndef NET_UDP_H
#define NET_UDP_H

#include <stdint.h>
#include "../l3/ipv4.h"

/* UDP Header Size */
#define UDP_HEADER_SIZE     8

/* Well-known ports */
#define UDP_PORT_DHCP_CLIENT    68
#define UDP_PORT_DHCP_SERVER    67
#define UDP_PORT_DNS            53

/**
 * UDP Header (8 bytes)
 * 
 * Structure du header UDP:
 * +--------+--------+--------+--------+
 * |     Source Port |   Dest Port     |
 * +--------+--------+--------+--------+
 * |      Length     |    Checksum     |
 * +--------+--------+--------+--------+
 * 
 * Note: Tous les champs sont en big-endian (network byte order)
 */
typedef struct __attribute__((packed)) {
    uint16_t src_port;      /* Source port */
    uint16_t dest_port;     /* Destination port */
    uint16_t length;        /* Length (header + data) */
    uint16_t checksum;      /* Checksum (optional in IPv4, set to 0) */
} udp_header_t;

/**
 * Traite un paquet UDP reçu.
 * 
 * @param ip_hdr Header IPv4 du paquet (pour récupérer les IPs)
 * @param data   Pointeur vers le début du paquet UDP (payload IPv4)
 * @param len    Longueur totale du paquet UDP en bytes
 */
void udp_handle_packet(ipv4_header_t* ip_hdr, uint8_t* data, int len);

/**
 * Envoie un paquet UDP.
 * 
 * @param dest_ip   IP de destination (4 bytes)
 * @param src_port  Port source
 * @param dest_port Port de destination
 * @param data      Données à envoyer
 * @param len       Longueur des données
 */
void udp_send_packet(uint8_t* dest_ip, uint16_t src_port, uint16_t dest_port,
                     uint8_t* data, int len);

#endif /* NET_UDP_H */
