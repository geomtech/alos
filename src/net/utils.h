/* src/net/utils.h - Network Utility Functions */
#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stdint.h>

/**
 * Network to Host byte order (16-bit)
 * Converts big-endian (network) to little-endian (x86)
 */
static inline uint16_t ntohs(uint16_t val)
{
    return (val << 8) | (val >> 8);
}

/**
 * Host to Network byte order (16-bit)
 * Same as ntohs on little-endian systems
 */
static inline uint16_t htons(uint16_t val)
{
    return (val << 8) | (val >> 8);
}

/**
 * Network to Host byte order (32-bit)
 * Converts big-endian (network) to little-endian (x86)
 */
static inline uint32_t ntohl(uint32_t val)
{
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8)  |
           ((val & 0x0000FF00) << 8)  |
           ((val & 0x000000FF) << 24);
}

/**
 * Host to Network byte order (32-bit)
 * Same as ntohl on little-endian systems
 */
static inline uint32_t htonl(uint32_t val)
{
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8)  |
           ((val & 0x0000FF00) << 8)  |
           ((val & 0x000000FF) << 24);
}

/* Common EtherTypes */
#define ETHERTYPE_IPV4      0x0800
#define ETHERTYPE_ARP       0x0806
#define ETHERTYPE_IPV6      0x86DD
#define ETHERTYPE_VLAN      0x8100

#endif /* NET_UTILS_H */
