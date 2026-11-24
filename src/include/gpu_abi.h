/* ABI GPU partagee noyau/libc : virtio-gpu 2D expose au display owner. */
#ifndef ALOS_GPU_ABI_H
#define ALOS_GPU_ABI_H

#include <stdint.h>

#define ALOS_GPU_FORMAT_B8G8R8X8 1 /* pixel 32 bits little-endian : 0x00RRGGBB */

typedef struct {
  uint64_t addr; /* surface mappee en userland (lecture/ecriture) */
  uint64_t size; /* octets mappes */
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t format; /* ALOS_GPU_FORMAT_* */
} alos_gpu_info_t;

#endif
