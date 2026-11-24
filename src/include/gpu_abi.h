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

/* Capacites annoncees par le pilote actif (alos_gpu_caps_t.caps). */
#define ALOS_GPU_CAP_EXPLICIT_FLUSH (1u << 0) /* SYS_GPU_FLUSH requis apres dessin */
#define ALOS_GPU_CAP_MODESET        (1u << 1) /* changement de mode supporte */
#define ALOS_GPU_CAP_CURSOR         (1u << 2) /* curseur materiel */
#define ALOS_GPU_CAP_VBLANK         (1u << 3) /* evenement vblank */
#define ALOS_GPU_CAP_3D             (1u << 4) /* acceleration 3D (virgl/venus/...) */

/* SYS_GPU_QUERY : independant du pilote. */
typedef struct {
  char driver[16];  /* nom du pilote actif (ex. "virtio-gpu") */
  uint32_t caps;    /* ALOS_GPU_CAP_* */
  uint16_t vendor;
  uint16_t device;  /* IDs PCI */
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t format;  /* ALOS_GPU_FORMAT_* */
} alos_gpu_caps_t;

#endif
