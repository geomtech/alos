/* src/net/arp.h - Address Resolution Protocol */
#ifndef NET_ARP_H
#define NET_ARP_H

#include <stdint.h>
#include "ethernet.h"

/**
 * ARP Packet Structure (28 bytes for IPv4 over Ethernet)
 * 
 * RFC 826 - An Ethernet Address Resolution Protocol
 * 
 * +------------------+------------------+
 * | Hardware Type    | Protocol Type    |
 * | (2 bytes)        | (2 bytes)        |
 * +------------------+------------------+
 * | HW Size | Proto  |     Opcode       |
 * | (1)     | Size(1)|    (2 bytes)     |
 * +------------------+------------------+
 * |        Sender Hardware Address      |
 * |            (6 bytes)                |
 * +-------------------------------------+
 * |    Sender Protocol Address          |
 * |            (4 bytes)                |
 * +-------------------------------------+
 * |        Target Hardware Address      |
 * |            (6 bytes)                |
 * +-------------------------------------+
 * |    Target Protocol Address          |
 * |            (4 bytes)                |
 * +-------------------------------------+
 * 
 * Note: Tous les champs 16-bit sont en big-endian (network byte order)
 */
typedef struct __attribute__((packed)) {
    uint16_t hardware_type;     /* Type de hardware (Ethernet = 1) */
    uint16_t protocol_type;     /* Type de protocole (IPv4 = 0x0800) */
    uint8_t  hardware_size;     /* Taille adresse hardware (6 pour MAC) */
    uint8_t  protocol_size;     /* Taille adresse protocole (4 pour IPv4) */
    uint16_t opcode;            /* Opération (Request=1, Reply=2) */
    uint8_t  src_mac[6];        /* Adresse MAC source */
    uint8_t  src_ip[4];         /* Adresse IP source */
    uint8_t  dest_mac[6];       /* Adresse MAC destination */
    uint8_t  dest_ip[4];        /* Adresse IP destination */
} arp_packet_t;

/* Taille du paquet ARP */
#define ARP_PACKET_SIZE         28

/* Hardware Types */
#define ARP_HW_ETHERNET         1

/* Protocol Types */
#define ARP_PROTO_IPV4          0x0800

/* Opcodes */
#define ARP_OP_REQUEST          1
#define ARP_OP_REPLY            2

/**
 * Traite un paquet ARP reçu.
 * 
 * @param eth         Pointeur vers le header Ethernet du paquet
 * @param packet_data Pointeur vers les données ARP (après le header Ethernet)
 * @param len         Longueur des données ARP
 */
void arp_handle_packet(ethernet_header_t* eth, uint8_t* packet_data, int len);

#endif /* NET_ARP_H */
