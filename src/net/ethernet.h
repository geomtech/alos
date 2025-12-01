/* src/net/ethernet.h - Ethernet Frame Handling */
#ifndef NET_ETHERNET_H
#define NET_ETHERNET_H

#include <stdint.h>

/**
 * Ethernet Frame Header (14 bytes)
 * 
 * Structure du header Ethernet II:
 * +------------------+------------------+------------+
 * | Destination MAC  |    Source MAC    | EtherType  |
 * |    (6 bytes)     |    (6 bytes)     | (2 bytes)  |
 * +------------------+------------------+------------+
 * 
 * Note: EtherType est en big-endian (network byte order)
 */
typedef struct __attribute__((packed)) {
    uint8_t  dest_mac[6];   /* Adresse MAC destination */
    uint8_t  src_mac[6];    /* Adresse MAC source */
    uint16_t ethertype;     /* Type de protocole (big-endian!) */
} ethernet_header_t;

/* Taille du header Ethernet */
#define ETHERNET_HEADER_SIZE    14

/* EtherTypes courants (en host byte order pour comparaison après ntohs) */
#define ETH_TYPE_IPV4           0x0800
#define ETH_TYPE_ARP            0x0806
#define ETH_TYPE_IPV6           0x86DD
#define ETH_TYPE_VLAN           0x8100

/**
 * Traite un paquet Ethernet reçu.
 * 
 * @param data Pointeur vers le début du paquet (header Ethernet inclus)
 * @param len  Longueur totale du paquet en bytes
 */
void ethernet_handle_packet(uint8_t* data, int len);

#endif /* NET_ETHERNET_H */
