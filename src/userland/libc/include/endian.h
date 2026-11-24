#ifndef _ENDIAN_H
#define _ENDIAN_H
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __PDP_ENDIAN 3412
#define __BYTE_ORDER __LITTLE_ENDIAN
#define LITTLE_ENDIAN __LITTLE_ENDIAN
#define BIG_ENDIAN __BIG_ENDIAN
#define PDP_ENDIAN __PDP_ENDIAN
#define BYTE_ORDER __BYTE_ORDER

/* Conversions d'endianness (x86-64 : little-endian). */
#include <stdint.h>
static inline uint16_t htobe16(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t htobe32(uint32_t v) { return __builtin_bswap32(v); }
static inline uint64_t htobe64(uint64_t v) { return __builtin_bswap64(v); }
static inline uint16_t be16toh(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t be32toh(uint32_t v) { return __builtin_bswap32(v); }
static inline uint64_t be64toh(uint64_t v) { return __builtin_bswap64(v); }
static inline uint16_t htole16(uint16_t v) { return v; }
static inline uint32_t htole32(uint32_t v) { return v; }
static inline uint64_t htole64(uint64_t v) { return v; }
static inline uint16_t le16toh(uint16_t v) { return v; }
static inline uint32_t le32toh(uint32_t v) { return v; }
static inline uint64_t le64toh(uint64_t v) { return v; }
#endif
