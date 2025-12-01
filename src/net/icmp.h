/* src/net/icmp.h - ICMP Protocol Handler */
#ifndef NET_ICMP_H
#define NET_ICMP_H

#include <stdint.h>
#include "ipv4.h"
#include "ethernet.h"

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

/**
 * Traite un paquet ICMP reçu.
 * 
 * @param eth       Header Ethernet (pour récupérer la MAC source)
 * @param ip_hdr    Header IPv4 du paquet
 * @param icmp_data Pointeur vers le début du paquet ICMP
 * @param len       Longueur du paquet ICMP en bytes
 */
void icmp_handle_packet(ethernet_header_t* eth, ipv4_header_t* ip_hdr,
                        uint8_t* icmp_data, int len);

#endif /* NET_ICMP_H */
