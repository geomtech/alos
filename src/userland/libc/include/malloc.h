#ifndef _ALOS_MALLOC_H
#define _ALOS_MALLOC_H
#include <stdlib.h>

/* Capacites du heap brk, pas une estimation du RSS du processus. */
struct alos_malloc_stats {
  size_t arena_bytes;
  size_t allocated_bytes;
  size_t free_bytes;
  size_t allocated_blocks;
  size_t free_blocks;
  size_t metadata_bytes;
};
#ifdef __cplusplus
extern "C" {
#endif
int alos_malloc_get_stats(struct alos_malloc_stats *stats);
size_t malloc_usable_size(void *allocation);
#ifdef __cplusplus
}
#endif
#endif
