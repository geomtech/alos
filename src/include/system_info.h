#ifndef ALOS_SYSTEM_INFO_H
#define ALOS_SYSTEM_INFO_H

#include <stddef.h>
#include <stdint.h>

#define ALOS_SYSTEM_INFO_VERSION 1U
#define ALOS_SYSTEM_INFO_TOTAL (1ULL << 0)
#define ALOS_SYSTEM_INFO_FREE (1ULL << 1)
#define ALOS_SYSTEM_INFO_SWAP (1ULL << 2)
#define ALOS_SYSTEM_INFO_MEMORY_FIELDS (ALOS_SYSTEM_INFO_TOTAL | ALOS_SYSTEM_INFO_FREE | ALOS_SYSTEM_INFO_SWAP)

typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint64_t valid_fields;
  /* Pages USABLE gerees au boot, apres reservations PMM, pas RAM installee. */
  uint64_t managed_total_bytes;
  uint64_t allocatable_free_bytes;
  uint64_t swap_total_bytes;
  uint64_t swap_free_bytes;
} alos_system_info_t;

typedef char alos_system_info_size_check[sizeof(alos_system_info_t) == 48 ? 1 : -1];
typedef char alos_system_info_offset_check[offsetof(alos_system_info_t, managed_total_bytes) == 16 ? 1 : -1];

#endif
