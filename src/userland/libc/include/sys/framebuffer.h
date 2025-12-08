#ifndef _SYS_FRAMEBUFFER_H
#define _SYS_FRAMEBUFFER_H

#include <stdint.h>

typedef struct {
  uint64_t addr; /* Adresse virtuelle (mappée en userland) */
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint16_t bpp;
  uint16_t red_mask_size;
  uint16_t red_mask_shift;
  uint16_t green_mask_size;
  uint16_t green_mask_shift;
  uint16_t blue_mask_size;
  uint16_t blue_mask_shift;
} framebuffer_info_t;

#endif
