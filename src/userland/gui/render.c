/* src/gui/render.c - Implémentation des primitives de rendu graphique
 *
 * Ce module implémente toutes les fonctions de dessin de base pour
 * l'interface graphique ALOS style macOS.
 */

#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/framebuffer.h>

/* ============================================================================
 * VARIABLES GLOBALES
 * ============================================================================
 */

/* Framebuffer principal (écran) */
static framebuffer_t g_main_buffer;

/* Back buffer pour le double buffering */
static framebuffer_t g_back_buffer;

/* Double buffering activé ? */
static bool g_double_buffer_enabled = false;

/* Pile de clipping (max 16 niveaux) */
#define CLIP_STACK_SIZE 16
static rect_t g_clip_stack[CLIP_STACK_SIZE];
static int g_clip_stack_top = -1;

/* Zone de clipping actuelle */
static rect_t g_clip_rect = {0, 0, 0, 0};
static bool g_clip_enabled = false;

/* Dirty Rectangles - Optimisation pour ne redessiner que les zones modifiées */
#define MAX_DIRTY_RECTS 64
static rect_t g_dirty_rects[MAX_DIRTY_RECTS];
static int g_dirty_rect_count = 0;
static bool g_dirty_tracking_enabled = true;

/* Lightweight renderer profiling (reported periodically). */
static uint64_t g_perf_flip_calls = 0;
static uint64_t g_perf_flip_bytes = 0;
static uint64_t g_perf_flip_cycles = 0;

static inline uint64_t render_read_tsc(void) {
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

/* ============================================================================
 * FAST MEMORY OPERATIONS (x86-64 optimized)
 * ============================================================================
 */

/* Fast 32-bit memory fill using 64-bit operations on x86-64 */
static inline void memset32(uint32_t *dest, uint32_t value, size_t count) {
  /* Create 64-bit value with two 32-bit pixels */
  uint64_t value64 = ((uint64_t)value << 32) | value;
  uint64_t *dest64 = (uint64_t *)dest;
  
  /* Fill 2 pixels at a time using 64-bit stores */
  size_t count64 = count / 2;
  
  /* Use rep stosq for large fills (very fast on x86-64) */
  if (count64 >= 8) {
    __asm__ volatile (
      "rep stosq"
      : "+D" (dest64), "+c" (count64)
      : "a" (value64)
      : "memory"
    );
    dest = (uint32_t *)dest64;
    count = count % 2;
  } else {
    /* Small fills: manual unrolled loop */
    while (count64 >= 4) {
      dest64[0] = value64;
      dest64[1] = value64;
      dest64[2] = value64;
      dest64[3] = value64;
      dest64 += 4;
      count64 -= 4;
    }
    while (count64--) {
      *dest64++ = value64;
    }
    dest = (uint32_t *)dest64;
    count = count % 2;
  }
  
  /* Handle odd pixel at end */
  if (count) {
    *dest = value;
  }
}

/* ============================================================================
 * INITIALISATION
 * ============================================================================
 */

int render_init_user(framebuffer_info_t *fb) {
  if (!fb)
    return -1;

  /* On ne supporte que le 32 bits pour l'instant */
  if (fb->bpp != 32) {
    printf("RENDER: Error, unsupported BPP: %d\n", fb->bpp);
    // return -1; // Warning only for now, assume 32
  }

  g_main_buffer.pixels = (uint32_t *)fb->addr;
  g_main_buffer.width = fb->width;
  g_main_buffer.height = fb->height;
  g_main_buffer.pitch = fb->pitch;
  g_main_buffer.owns_memory = false;

  printf("RENDER: Main buffer at %p, size %dx%d\n", g_main_buffer.pixels,
         g_main_buffer.width, g_main_buffer.height);

  // Initialize back buffer struct first
  g_back_buffer.width = g_main_buffer.width;
  g_back_buffer.height = g_main_buffer.height;
  g_back_buffer.pitch = g_main_buffer.pitch;
  g_back_buffer.owns_memory = true;
  g_back_buffer.pixels = NULL; // Initialize to NULL before malloc

  // Allocate back buffer
  size_t buffer_size = g_main_buffer.pitch * g_main_buffer.height;
  g_back_buffer.pixels = (uint32_t *)malloc(buffer_size);

  printf("RENDER: Back buffer allocated at %p, size %lu\n",
         g_back_buffer.pixels, buffer_size);

  if (!g_back_buffer.pixels) {
    printf("RENDER: Failed to allocate back buffer\n");
    return -1;
  }

  // Clear the back buffer
  memset(g_back_buffer.pixels, 0, buffer_size);

  g_double_buffer_enabled = true;

  /* Default clipping (fullscreen) */
  g_clip_rect.x = 0;
  g_clip_rect.y = 0;
  g_clip_rect.width = g_main_buffer.width;
  g_clip_rect.height = g_main_buffer.height;
  g_clip_enabled = true;

  printf("RENDER: Initialized %dx%d %dbpp\n", fb->width, fb->height, fb->bpp);

  return 0;
}

framebuffer_t *render_get_framebuffer(void) { return &g_main_buffer; }

void render_get_screen_size(uint32_t *width, uint32_t *height) {
  if (width)
    *width = g_main_buffer.width;
  if (height)
    *height = g_main_buffer.height;
}

/* ============================================================================
 * DOUBLE BUFFERING
 * ============================================================================
 */

void render_set_double_buffer(bool enabled) {
  g_double_buffer_enabled = enabled && (g_back_buffer.pixels != NULL);
}

void render_flip(void) {
  if (!g_double_buffer_enabled)
    return;

  /* Copie du back buffer vers le front buffer */
  size_t buffer_size = g_main_buffer.pitch * g_main_buffer.height;
  memcpy(g_main_buffer.pixels, g_back_buffer.pixels, buffer_size);
}

void render_flip_region(rect_t region) {
  if (!g_double_buffer_enabled)
    return;

  int32_t x1 = region.x;
  int32_t y1 = region.y;
  int32_t x2 = region.x + (int32_t)region.width;
  int32_t y2 = region.y + (int32_t)region.height;

  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 > (int32_t)g_main_buffer.width) x2 = (int32_t)g_main_buffer.width;
  if (y2 > (int32_t)g_main_buffer.height) y2 = (int32_t)g_main_buffer.height;
  if (x1 >= x2 || y1 >= y2)
    return;

  uint32_t pitch_pixels = g_main_buffer.pitch / 4;
  size_t bytes_per_line = (size_t)(x2 - x1) * sizeof(uint32_t);
  uint64_t start = render_read_tsc();

  for (int32_t y = y1; y < y2; y++) {
    uint32_t *src = g_back_buffer.pixels + y * pitch_pixels + x1;
    uint32_t *dst = g_main_buffer.pixels + y * pitch_pixels + x1;
    memcpy(dst, src, bytes_per_line);
  }

  g_perf_flip_calls++;
  g_perf_flip_bytes += bytes_per_line * (uint64_t)(y2 - y1);
  g_perf_flip_cycles += render_read_tsc() - start;

  if ((g_perf_flip_calls % 240) == 0) {
    printf("GUI PERF: flips=%lu avg_bytes=%lu avg_cycles=%lu\n",
           g_perf_flip_calls,
           g_perf_flip_bytes / g_perf_flip_calls,
           g_perf_flip_cycles / g_perf_flip_calls);
  }
}

/* ============================================================================
 * DIRTY RECTANGLES - Optimisation pour ne redessiner que les zones modifiées
 * ============================================================================
 */

void render_enable_dirty_tracking(bool enabled) {
  g_dirty_tracking_enabled = enabled;
  if (!enabled) {
    g_dirty_rect_count = 0;
  }
}

void render_add_dirty_rect(rect_t rect) {
  if (!g_dirty_tracking_enabled || g_dirty_rect_count >= MAX_DIRTY_RECTS)
    return;

  /* Fusionner avec les rectangles existants si possible */
  for (int i = 0; i < g_dirty_rect_count; i++) {
    if (rect_intersects(g_dirty_rects[i], rect)) {
      /* Fusionner les rectangles */
      g_dirty_rects[i] = rect_union(g_dirty_rects[i], rect);
      return;
    }
  }

  /* Ajouter un nouveau rectangle */
  g_dirty_rects[g_dirty_rect_count++] = rect;
}

void render_clear_dirty_rects(void) {
  g_dirty_rect_count = 0;
}

int render_get_dirty_rect_count(void) {
  return g_dirty_rect_count;
}

rect_t render_get_dirty_rect(int index) {
  if (index < 0 || index >= g_dirty_rect_count) {
    return rect_make(0, 0, 0, 0);
  }
  return g_dirty_rects[index];
}

void render_flip_dirty_regions(void) {
  if (!g_double_buffer_enabled || !g_dirty_tracking_enabled)
    return;

  /* Si aucun rectangle sale, ne rien faire */
  if (g_dirty_rect_count == 0)
    return;

  /* Fusionner tous les rectangles sales en un seul pour optimiser le flip */
  rect_t combined_rect = g_dirty_rects[0];
  for (int i = 1; i < g_dirty_rect_count; i++) {
    combined_rect = rect_union(combined_rect, g_dirty_rects[i]);
  }

  /* Appliquer le flip uniquement sur la zone combinée */
  render_flip_region(combined_rect);

  /* Réinitialiser les rectangles sales */
  g_dirty_rect_count = 0;
}

framebuffer_t *render_get_active_buffer(void) {
  if (g_double_buffer_enabled && g_back_buffer.pixels) {
    return &g_back_buffer;
  }
  return &g_main_buffer;
}

/* ============================================================================
 * CLIPPING
 * ============================================================================
 */

void render_set_clip(const rect_t *clip) {
  if (clip) {
    g_clip_rect = *clip;
    g_clip_enabled = true;
  } else {
    g_clip_rect.x = 0;
    g_clip_rect.y = 0;
    g_clip_rect.width = g_main_buffer.width;
    g_clip_rect.height = g_main_buffer.height;
    g_clip_enabled = false;
  }
}

rect_t render_get_clip(void) { return g_clip_rect; }

void render_push_clip(rect_t clip) {
  if (g_clip_stack_top < CLIP_STACK_SIZE - 1) {
    g_clip_stack_top++;
    g_clip_stack[g_clip_stack_top] = g_clip_rect;

    /* Nouvelle zone = intersection avec l'actuelle */
    if (g_clip_enabled) {
      g_clip_rect = rect_intersect(g_clip_rect, clip);
    } else {
      g_clip_rect = clip;
    }
    g_clip_enabled = true;
  }
}

void render_pop_clip(void) {
  if (g_clip_stack_top >= 0) {
    g_clip_rect = g_clip_stack[g_clip_stack_top];
    g_clip_stack_top--;
    g_clip_enabled = (g_clip_stack_top >= 0);
  }
}

/* Vérifie si un point est dans la zone de clipping */
static inline bool is_clipped(int32_t x, int32_t y) {
  if (!g_clip_enabled) {
    return (x < 0 || y < 0 || x >= (int32_t)g_main_buffer.width ||
            y >= (int32_t)g_main_buffer.height);
  }
  return (x < g_clip_rect.x || y < g_clip_rect.y ||
          x >= g_clip_rect.x + (int32_t)g_clip_rect.width ||
          y >= g_clip_rect.y + (int32_t)g_clip_rect.height);
}

static inline uint32_t blend_pixel_fast(uint32_t bg, uint32_t fg_r,
                                        uint32_t fg_g, uint32_t fg_b,
                                        uint32_t alpha) {
  uint32_t inv_alpha = 255 - alpha;
  uint32_t r = (fg_r * alpha + ((bg >> 16) & 0xFF) * inv_alpha) / 255;
  uint32_t g = (fg_g * alpha + ((bg >> 8) & 0xFF) * inv_alpha) / 255;
  uint32_t b = (fg_b * alpha + (bg & 0xFF) * inv_alpha) / 255;
  return 0xFF000000U | (r << 16) | (g << 8) | b;
}

/* ============================================================================
 * PRIMITIVES DE BASE
 * ============================================================================
 */

void draw_pixel(int32_t x, int32_t y, uint32_t color) {
  if (is_clipped(x, y))
    return;

  framebuffer_t *fb = render_get_active_buffer();
  uint32_t offset = y * (fb->pitch / 4) + x;
  fb->pixels[offset] = color;
}

void draw_pixel_alpha(int32_t x, int32_t y, rgba_t color) {
  if (color.a == 0)
    return;
  if (color.a == 255) {
    draw_pixel(x, y, rgba_to_u32(color));
    return;
  }
  if (is_clipped(x, y))
    return;

  framebuffer_t *fb = render_get_active_buffer();
  uint32_t offset = y * (fb->pitch / 4) + x;
  fb->pixels[offset] =
      blend_pixel_fast(fb->pixels[offset], color.r, color.g, color.b, color.a);
}

uint32_t read_pixel(int32_t x, int32_t y) {
  if (x < 0 || y < 0 || x >= (int32_t)g_main_buffer.width ||
      y >= (int32_t)g_main_buffer.height) {
    return 0;
  }

  framebuffer_t *fb = render_get_active_buffer();
  uint32_t offset = y * (fb->pitch / 4) + x;
  return fb->pixels[offset];
}

void draw_hline(int32_t x1, int32_t x2, int32_t y, uint32_t color) {
  if (x1 > x2) {
    int32_t tmp = x1;
    x1 = x2;
    x2 = tmp;
  }

  framebuffer_t *fb = render_get_active_buffer();

  /* Clipping vertical */
  if (y < 0 || y >= (int32_t)fb->height)
    return;
  if (g_clip_enabled) {
    if (y < g_clip_rect.y || y >= g_clip_rect.y + (int32_t)g_clip_rect.height)
      return;
    if (x1 < g_clip_rect.x)
      x1 = g_clip_rect.x;
    if (x2 >= g_clip_rect.x + (int32_t)g_clip_rect.width)
      x2 = g_clip_rect.x + (int32_t)g_clip_rect.width - 1;
  } else {
    if (x1 < 0)
      x1 = 0;
    if (x2 >= (int32_t)fb->width)
      x2 = (int32_t)fb->width - 1;
  }

  if (x1 > x2)
    return;

  uint32_t *row = fb->pixels + y * (fb->pitch / 4);
  memset32(row + x1, color, (size_t)(x2 - x1 + 1));
}

void draw_vline(int32_t x, int32_t y1, int32_t y2, uint32_t color) {
  if (y1 > y2) {
    int32_t tmp = y1;
    y1 = y2;
    y2 = tmp;
  }

  framebuffer_t *fb = render_get_active_buffer();

  /* Clipping horizontal */
  if (x < 0 || x >= (int32_t)fb->width)
    return;
  if (g_clip_enabled) {
    if (x < g_clip_rect.x || x >= g_clip_rect.x + (int32_t)g_clip_rect.width)
      return;
    if (y1 < g_clip_rect.y)
      y1 = g_clip_rect.y;
    if (y2 >= g_clip_rect.y + (int32_t)g_clip_rect.height)
      y2 = g_clip_rect.y + (int32_t)g_clip_rect.height - 1;
  } else {
    if (y1 < 0)
      y1 = 0;
    if (y2 >= (int32_t)fb->height)
      y2 = (int32_t)fb->height - 1;
  }

  if (y1 > y2)
    return;

  uint32_t pitch_pixels = fb->pitch / 4;
  uint32_t *pixel = fb->pixels + y1 * pitch_pixels + x;
  for (int32_t y = y1; y <= y2; y++) {
    *pixel = color;
    pixel += pitch_pixels;
  }
}

void draw_line(point_t p1, point_t p2, uint32_t color) {
  /* Algorithme de Bresenham */
  int32_t dx = abs_i32(p2.x - p1.x);
  int32_t dy = abs_i32(p2.y - p1.y);
  int32_t sx = (p1.x < p2.x) ? 1 : -1;
  int32_t sy = (p1.y < p2.y) ? 1 : -1;
  int32_t err = dx - dy;

  int32_t x = p1.x;
  int32_t y = p1.y;

  while (1) {
    draw_pixel(x, y, color);

    if (x == p2.x && y == p2.y)
      break;

    int32_t e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
  }
}

void draw_line_aa(point_t p1, point_t p2, rgba_t color) {
  /* Algorithme de Wu pour lignes anti-aliasées */
  int32_t dx = abs_i32(p2.x - p1.x);
  int32_t dy = abs_i32(p2.y - p1.y);

  bool steep = dy > dx;

  if (steep) {
    /* Échange x et y */
    int32_t tmp;
    tmp = p1.x;
    p1.x = p1.y;
    p1.y = tmp;
    tmp = p2.x;
    p2.x = p2.y;
    p2.y = tmp;
  }

  if (p1.x > p2.x) {
    int32_t tmp;
    tmp = p1.x;
    p1.x = p2.x;
    p2.x = tmp;
    tmp = p1.y;
    p1.y = p2.y;
    p2.y = tmp;
  }

  dx = p2.x - p1.x;
  dy = p2.y - p1.y;

  float gradient = (dx == 0) ? 1.0f : (float)dy / (float)dx;

  /* Premier point */
  float xend = (float)p1.x;
  float yend = (float)p1.y;
  float intery = yend + gradient;

  if (steep) {
    draw_pixel_alpha((int32_t)yend, (int32_t)xend, color);
  } else {
    draw_pixel_alpha((int32_t)xend, (int32_t)yend, color);
  }

  /* Points intermédiaires */
  for (int32_t x = p1.x + 1; x < p2.x; x++) {
    int32_t y_int = (int32_t)intery;
    float frac = intery - (float)y_int;

    rgba_t c1 = color;
    rgba_t c2 = color;
    c1.a = (uint8_t)((1.0f - frac) * (float)color.a);
    c2.a = (uint8_t)(frac * (float)color.a);

    if (steep) {
      draw_pixel_alpha(y_int, x, c1);
      draw_pixel_alpha(y_int + 1, x, c2);
    } else {
      draw_pixel_alpha(x, y_int, c1);
      draw_pixel_alpha(x, y_int + 1, c2);
    }

    intery += gradient;
  }

  /* Dernier point */
  if (steep) {
    draw_pixel_alpha(p2.y, p2.x, color);
  } else {
    draw_pixel_alpha(p2.x, p2.y, color);
  }
}

/* ============================================================================
 * RECTANGLES
 * ============================================================================
 */

void draw_rect(rect_t rect, uint32_t color) {
  framebuffer_t *fb = render_get_active_buffer();

  int32_t x1 = rect.x;
  int32_t y1 = rect.y;
  int32_t x2 = rect.x + (int32_t)rect.width;
  int32_t y2 = rect.y + (int32_t)rect.height;

  /* Clipping */
  if (g_clip_enabled) {
    if (x1 < g_clip_rect.x)
      x1 = g_clip_rect.x;
    if (y1 < g_clip_rect.y)
      y1 = g_clip_rect.y;
    if (x2 > g_clip_rect.x + (int32_t)g_clip_rect.width)
      x2 = g_clip_rect.x + (int32_t)g_clip_rect.width;
    if (y2 > g_clip_rect.y + (int32_t)g_clip_rect.height)
      y2 = g_clip_rect.y + (int32_t)g_clip_rect.height;
  } else {
    if (x1 < 0)
      x1 = 0;
    if (y1 < 0)
      y1 = 0;
    if (x2 > (int32_t)fb->width)
      x2 = (int32_t)fb->width;
    if (y2 > (int32_t)fb->height)
      y2 = (int32_t)fb->height;
  }

  if (x1 >= x2 || y1 >= y2)
    return;

  uint32_t pitch_pixels = fb->pitch / 4;
  uint32_t width = (uint32_t)(x2 - x1);
  for (int32_t y = y1; y < y2; y++) {
    memset32(fb->pixels + y * pitch_pixels + x1, color, width);
  }
}

void draw_rect_alpha(rect_t rect, rgba_t color) {
  if (color.a == 0)
    return;
  if (color.a == 255) {
    draw_rect(rect, rgba_to_u32(color));
    return;
  }

  framebuffer_t *fb = render_get_active_buffer();
  int32_t x1 = rect.x;
  int32_t y1 = rect.y;
  int32_t x2 = rect.x + (int32_t)rect.width;
  int32_t y2 = rect.y + (int32_t)rect.height;

  if (g_clip_enabled) {
    if (x1 < g_clip_rect.x) x1 = g_clip_rect.x;
    if (y1 < g_clip_rect.y) y1 = g_clip_rect.y;
    if (x2 > g_clip_rect.x + (int32_t)g_clip_rect.width)
      x2 = g_clip_rect.x + (int32_t)g_clip_rect.width;
    if (y2 > g_clip_rect.y + (int32_t)g_clip_rect.height)
      y2 = g_clip_rect.y + (int32_t)g_clip_rect.height;
  } else {
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int32_t)fb->width) x2 = (int32_t)fb->width;
    if (y2 > (int32_t)fb->height) y2 = (int32_t)fb->height;
  }
  if (x1 >= x2 || y1 >= y2)
    return;

  uint32_t alpha = color.a;
  uint32_t inv_alpha = 255 - alpha;
  uint32_t pre_r = (uint32_t)color.r * alpha;
  uint32_t pre_g = (uint32_t)color.g * alpha;
  uint32_t pre_b = (uint32_t)color.b * alpha;
  uint32_t pitch_pixels = fb->pitch / 4;

  for (int32_t y = y1; y < y2; y++) {
    uint32_t *pixel = fb->pixels + y * pitch_pixels + x1;
    for (int32_t x = x1; x < x2; x++, pixel++) {
      uint32_t bg = *pixel;
      uint32_t r = (pre_r + ((bg >> 16) & 0xFF) * inv_alpha) / 255;
      uint32_t g = (pre_g + ((bg >> 8) & 0xFF) * inv_alpha) / 255;
      uint32_t b = (pre_b + (bg & 0xFF) * inv_alpha) / 255;
      *pixel = 0xFF000000U | (r << 16) | (g << 8) | b;
    }
  }
}

void draw_rect_outline(rect_t rect, uint32_t color, uint32_t thickness) {
  /* Bord supérieur */
  draw_rect(rect_make(rect.x, rect.y, rect.width, thickness), color);
  /* Bord inférieur */
  draw_rect(rect_make(rect.x,
                      rect.y + (int32_t)rect.height - (int32_t)thickness,
                      rect.width, thickness),
            color);
  /* Bord gauche */
  draw_rect(rect_make(rect.x, rect.y + (int32_t)thickness, thickness,
                      rect.height - 2 * thickness),
            color);
  /* Bord droit */
  draw_rect(rect_make(rect.x + (int32_t)rect.width - (int32_t)thickness,
                      rect.y + (int32_t)thickness, thickness,
                      rect.height - 2 * thickness),
            color);
}

/* Dessine un quart de cercle rempli pour les coins arrondis */
static void draw_corner_fill(int32_t cx, int32_t cy, uint32_t radius,
                             int32_t dx_sign, int32_t dy_sign, uint32_t color) {
  int32_t r = (int32_t)radius;
  int32_t r2 = r * r;

  for (int32_t dy = 0; dy <= r; dy++) {
    int32_t dx_max = 0;
    /* Trouve le x maximum pour ce y dans le cercle */
    for (int32_t dx = 0; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r2) {
        dx_max = dx;
      }
    }

    /* Dessine la ligne horizontale pour ce y */
    int32_t y = cy + dy * dy_sign;
    int32_t x_start = (dx_sign > 0) ? cx : cx - dx_max;
    int32_t x_end = (dx_sign > 0) ? cx + dx_max : cx;

    for (int32_t x = x_start; x <= x_end; x++) {
      draw_pixel(x, y, color);
    }
  }
}

/* Dessine un quart de cercle rempli avec alpha */
static void draw_corner_fill_alpha(int32_t cx, int32_t cy, uint32_t radius,
                                   int32_t dx_sign, int32_t dy_sign,
                                   rgba_t color) {
  int32_t r = (int32_t)radius;
  int32_t r2 = r * r;

  for (int32_t dy = 0; dy <= r; dy++) {
    for (int32_t dx = 0; dx <= r; dx++) {
      int32_t dist2 = dx * dx + dy * dy;
      if (dist2 <= r2) {
        int32_t x = cx + dx * dx_sign;
        int32_t y = cy + dy * dy_sign;

        /* Anti-aliasing sur le bord */
        if (dist2 > (r - 1) * (r - 1)) {
          float dist = (float)dist2 / (float)r2;
          rgba_t aa_color = color;
          aa_color.a =
              (uint8_t)((1.0f - (dist - 0.8f) * 5.0f) * (float)color.a);
          if (aa_color.a > 0) {
            draw_pixel_alpha(x, y, aa_color);
          }
        } else {
          draw_pixel_alpha(x, y, color);
        }
      }
    }
  }
}

void draw_rounded_rect(rect_t rect, uint32_t radius, uint32_t color) {
  if (radius == 0) {
    draw_rect(rect, color);
    return;
  }

  uint32_t max_radius =
      (rect.width < rect.height ? rect.width : rect.height) / 2;
  if (radius > max_radius)
    radius = max_radius;
  if (radius == 0) {
    draw_rect(rect, color);
    return;
  }

  int32_t r = (int32_t)radius;
  int32_t r2 = r * r;

  draw_rect(rect_make(rect.x + r, rect.y, rect.width - 2 * radius, rect.height),
            color);
  draw_rect(rect_make(rect.x, rect.y + r, radius, rect.height - 2 * radius),
            color);
  draw_rect(rect_make(rect.x + (int32_t)rect.width - r, rect.y + r, radius,
                      rect.height - 2 * radius), color);

  for (int32_t dy = 0; dy < r; dy++) {
    int32_t cy = r - 1 - dy;
    for (int32_t dx = 0; dx < r; dx++) {
      int32_t cx = r - 1 - dx;
      if (cx * cx + cy * cy > r2)
        continue;

      draw_pixel(rect.x + dx, rect.y + dy, color);
      draw_pixel(rect.x + (int32_t)rect.width - 1 - dx, rect.y + dy, color);
      draw_pixel(rect.x + dx, rect.y + (int32_t)rect.height - 1 - dy, color);
      draw_pixel(rect.x + (int32_t)rect.width - 1 - dx,
                 rect.y + (int32_t)rect.height - 1 - dy, color);
    }
  }
}

void draw_rounded_rect_alpha(rect_t rect, uint32_t radius, rgba_t color) {
  if (color.a == 0)
    return;
  if (color.a == 255) {
    draw_rounded_rect(rect, radius, rgba_to_u32(color));
    return;
  }
  if (radius == 0) {
    draw_rect_alpha(rect, color);
    return;
  }

  uint32_t max_radius =
      (rect.width < rect.height ? rect.width : rect.height) / 2;
  if (radius > max_radius)
    radius = max_radius;
  if (radius == 0) {
    draw_rect_alpha(rect, color);
    return;
  }

  int32_t r = (int32_t)radius;
  int32_t r2 = r * r;
  uint32_t edge_width = (uint32_t)(2 * r);

  draw_rect_alpha(
      rect_make(rect.x + r, rect.y, rect.width - 2 * radius, rect.height),
      color);
  draw_rect_alpha(
      rect_make(rect.x, rect.y + r, radius, rect.height - 2 * radius), color);
  draw_rect_alpha(rect_make(rect.x + (int32_t)rect.width - r, rect.y + r,
                            radius, rect.height - 2 * radius), color);

  for (int32_t dy = 0; dy < r; dy++) {
    int32_t cy = r - 1 - dy;
    for (int32_t dx = 0; dx < r; dx++) {
      int32_t cx = r - 1 - dx;
      int32_t dist2 = cx * cx + cy * cy;
      if (dist2 > r2)
        continue;

      rgba_t pixel = color;
      uint32_t edge = (uint32_t)(r2 - dist2);
      if (edge < edge_width)
        pixel.a = (uint8_t)(((uint32_t)color.a * edge) / edge_width);

      draw_pixel_alpha(rect.x + dx, rect.y + dy, pixel);
      draw_pixel_alpha(rect.x + (int32_t)rect.width - 1 - dx, rect.y + dy,
                       pixel);
      draw_pixel_alpha(rect.x + dx, rect.y + (int32_t)rect.height - 1 - dy,
                       pixel);
      draw_pixel_alpha(rect.x + (int32_t)rect.width - 1 - dx,
                       rect.y + (int32_t)rect.height - 1 - dy, pixel);
    }
  }
}

void draw_rounded_rect_outline(rect_t rect, uint32_t radius, uint32_t color,
                               uint32_t thickness) {
  /* TODO: Implémenter le contour arrondi */
  (void)rect;
  (void)radius;
  (void)color;
  (void)thickness;
}

/* ============================================================================
 * CERCLES
 * ============================================================================
 */

void draw_circle(point_t center, uint32_t radius, uint32_t color) {
  int32_t r = (int32_t)radius;
  int32_t r2 = r * r;

  for (int32_t dy = -r; dy <= r; dy++) {
    for (int32_t dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r2) {
        draw_pixel(center.x + dx, center.y + dy, color);
      }
    }
  }
}

void draw_circle_alpha(point_t center, uint32_t radius, rgba_t color) {
  int32_t r = (int32_t)radius;
  int32_t r2 = r * r;
  float r_f = (float)r;

  for (int32_t dy = -r; dy <= r; dy++) {
    for (int32_t dx = -r; dx <= r; dx++) {
      int32_t dist2 = dx * dx + dy * dy;
      if (dist2 <= r2) {
        rgba_t aa_color = color;

        /* Anti-aliasing sur le bord */
        float edge = r_f * r_f - (float)dist2;
        if (edge < r_f * 2.0f) {
          aa_color.a = (uint8_t)((edge / (r_f * 2.0f)) * (float)color.a);
        }

        draw_pixel_alpha(center.x + dx, center.y + dy, aa_color);
      }
    }
  }
}

void draw_circle_outline(point_t center, uint32_t radius, uint32_t color,
                         uint32_t thickness) {
  int32_t r_outer = (int32_t)radius;
  int32_t r_inner = (int32_t)(radius - thickness);
  int32_t r_outer2 = r_outer * r_outer;
  int32_t r_inner2 = r_inner * r_inner;

  for (int32_t dy = -r_outer; dy <= r_outer; dy++) {
    for (int32_t dx = -r_outer; dx <= r_outer; dx++) {
      int32_t dist2 = dx * dx + dy * dy;
      if (dist2 <= r_outer2 && dist2 >= r_inner2) {
        draw_pixel(center.x + dx, center.y + dy, color);
      }
    }
  }
}

void draw_ellipse(point_t center, uint32_t rx, uint32_t ry, uint32_t color) {
  int32_t a = (int32_t)rx;
  int32_t b = (int32_t)ry;
  int32_t a2 = a * a;
  int32_t b2 = b * b;

  for (int32_t dy = -b; dy <= b; dy++) {
    for (int32_t dx = -a; dx <= a; dx++) {
      /* Équation de l'ellipse: (x/a)² + (y/b)² <= 1 */
      if (dx * dx * b2 + dy * dy * a2 <= a2 * b2) {
        draw_pixel(center.x + dx, center.y + dy, color);
      }
    }
  }
}

/* ============================================================================
 * DÉGRADÉS
 * ============================================================================
 */

void draw_gradient(rect_t rect, rgba_t color1, rgba_t color2,
                   gradient_direction_t dir) {
  for (uint32_t y = 0; y < rect.height; y++) {
    for (uint32_t x = 0; x < rect.width; x++) {
      float t;

      switch (dir) {
      case GRADIENT_HORIZONTAL:
        t = (float)x / (float)(rect.width - 1);
        break;
      case GRADIENT_VERTICAL:
        t = (float)y / (float)(rect.height - 1);
        break;
      case GRADIENT_DIAGONAL_TL:
        t = ((float)x + (float)y) / (float)(rect.width + rect.height - 2);
        break;
      case GRADIENT_DIAGONAL_TR:
        t = ((float)(rect.width - 1 - x) + (float)y) /
            (float)(rect.width + rect.height - 2);
        break;
      default:
        t = 0.0f;
      }

      rgba_t color = lerp_color(color1, color2, t);
      draw_pixel_alpha(rect.x + (int32_t)x, rect.y + (int32_t)y, color);
    }
  }
}

void draw_rounded_gradient(rect_t rect, uint32_t radius, rgba_t color1,
                           rgba_t color2, gradient_direction_t dir) {
  if (radius == 0) {
    draw_gradient(rect, color1, color2, dir);
    return;
  }

  uint32_t max_radius =
      (rect.width < rect.height ? rect.width : rect.height) / 2;
  if (radius > max_radius)
    radius = max_radius;

  int32_t r = (int32_t)radius;
  int32_t r2 = r * r;

  for (uint32_t y = 0; y < rect.height; y++) {
    for (uint32_t x = 0; x < rect.width; x++) {
      /* Vérifie si le pixel est dans le rectangle arrondi */
      bool in_rect = true;
      int32_t ix = (int32_t)x;
      int32_t iy = (int32_t)y;

      /* Coin haut-gauche */
      if (ix < r && iy < r) {
        if ((r - 1 - ix) * (r - 1 - ix) + (r - 1 - iy) * (r - 1 - iy) > r2) {
          in_rect = false;
        }
      }
      /* Coin haut-droit */
      else if (ix >= (int32_t)rect.width - r && iy < r) {
        int32_t dx = ix - ((int32_t)rect.width - r);
        if (dx * dx + (r - 1 - iy) * (r - 1 - iy) > r2) {
          in_rect = false;
        }
      }
      /* Coin bas-gauche */
      else if (ix < r && iy >= (int32_t)rect.height - r) {
        int32_t dy = iy - ((int32_t)rect.height - r);
        if ((r - 1 - ix) * (r - 1 - ix) + dy * dy > r2) {
          in_rect = false;
        }
      }
      /* Coin bas-droit */
      else if (ix >= (int32_t)rect.width - r &&
               iy >= (int32_t)rect.height - r) {
        int32_t dx = ix - ((int32_t)rect.width - r);
        int32_t dy = iy - ((int32_t)rect.height - r);
        if (dx * dx + dy * dy > r2) {
          in_rect = false;
        }
      }

      if (in_rect) {
        float t;
        switch (dir) {
        case GRADIENT_HORIZONTAL:
          t = (float)x / (float)(rect.width - 1);
          break;
        case GRADIENT_VERTICAL:
          t = (float)y / (float)(rect.height - 1);
          break;
        case GRADIENT_DIAGONAL_TL:
          t = ((float)x + (float)y) / (float)(rect.width + rect.height - 2);
          break;
        case GRADIENT_DIAGONAL_TR:
          t = ((float)(rect.width - 1 - x) + (float)y) /
              (float)(rect.width + rect.height - 2);
          break;
        default:
          t = 0.0f;
        }

        rgba_t color = lerp_color(color1, color2, t);
        draw_pixel_alpha(rect.x + ix, rect.y + iy, color);
      }
    }
  }
}

/* ============================================================================
 * EFFETS VISUELS
 * ============================================================================
 */

void draw_shadow(rect_t rect, uint32_t radius, shadow_params_t params) {
  (void)radius;

  int32_t blur = (int32_t)params.blur_radius;
  int32_t spread = (int32_t)params.spread;
  if (blur <= 0 || params.color.a == 0)
    return;

  int32_t pad = blur + spread;
  rect_t shadow_rect = {
      rect.x + params.offset_x - pad,
      rect.y + params.offset_y - pad,
      rect.width + (uint32_t)(2 * pad),
      rect.height + (uint32_t)(2 * pad)};

  framebuffer_t *fb = render_get_active_buffer();
  int32_t vx1 = max_i32(shadow_rect.x, 0);
  int32_t vy1 = max_i32(shadow_rect.y, 0);
  int32_t vx2 = min_i32(shadow_rect.x + (int32_t)shadow_rect.width,
                        (int32_t)fb->width);
  int32_t vy2 = min_i32(shadow_rect.y + (int32_t)shadow_rect.height,
                        (int32_t)fb->height);

  if (g_clip_enabled) {
    vx1 = max_i32(vx1, g_clip_rect.x);
    vy1 = max_i32(vy1, g_clip_rect.y);
    vx2 = min_i32(vx2, g_clip_rect.x + (int32_t)g_clip_rect.width);
    vy2 = min_i32(vy2, g_clip_rect.y + (int32_t)g_clip_rect.height);
  }
  if (vx1 >= vx2 || vy1 >= vy2)
    return;

  uint32_t max_dist = (uint32_t)(blur * blur);
  uint64_t denom = (uint64_t)max_dist * (uint64_t)max_dist;
  if (denom == 0)
    return;

  int32_t local_x1 = vx1 - shadow_rect.x;
  int32_t local_y1 = vy1 - shadow_rect.y;
  int32_t local_x2 = vx2 - shadow_rect.x;
  int32_t local_y2 = vy2 - shadow_rect.y;

  for (int32_t y = local_y1; y < local_y2; y++) {
    int32_t inner_y = y - pad;
    bool inside_y = inner_y >= 0 && inner_y < (int32_t)rect.height;

    for (int32_t x = local_x1; x < local_x2; x++) {
      int32_t inner_x = x - pad;

      /* The window/card paints over this area immediately afterwards. */
      if (inside_y && inner_x >= 0 && inner_x < (int32_t)rect.width)
        continue;

      int32_t dx = 0;
      int32_t dy = 0;
      if (inner_x < 0)
        dx = -inner_x;
      else if (inner_x >= (int32_t)rect.width)
        dx = inner_x - (int32_t)rect.width + 1;
      if (inner_y < 0)
        dy = -inner_y;
      else if (inner_y >= (int32_t)rect.height)
        dy = inner_y - (int32_t)rect.height + 1;

      uint32_t dist2 = (uint32_t)(dx * dx + dy * dy);
      if (dist2 >= max_dist)
        continue;

      uint32_t remaining = max_dist - dist2;
      uint32_t alpha = (uint32_t)(((uint64_t)params.color.a * remaining *
                                   (uint64_t)remaining) /
                                  denom);
      if (alpha == 0)
        continue;

      rgba_t pixel = params.color;
      pixel.a = (uint8_t)alpha;
      draw_pixel_alpha(shadow_rect.x + x, shadow_rect.y + y, pixel);
    }
  }
}

void apply_blur(rect_t region, uint32_t radius) {
  if (radius == 0)
    return;

  framebuffer_t *fb = render_get_active_buffer();

  /* Clipping */
  int32_t x1 = max_i32(region.x, 0);
  int32_t y1 = max_i32(region.y, 0);
  int32_t x2 = min_i32(region.x + (int32_t)region.width, (int32_t)fb->width);
  int32_t y2 = min_i32(region.y + (int32_t)region.height, (int32_t)fb->height);

  if (x1 >= x2 || y1 >= y2)
    return;

  uint32_t w = (uint32_t)(x2 - x1);
  uint32_t h = (uint32_t)(y2 - y1);

  /* Buffer temporaire */
  uint32_t *temp = (uint32_t *)malloc(w * h * sizeof(uint32_t));
  if (!temp)
    return;

  int32_t r = (int32_t)radius;
  uint32_t pitch_pixels = fb->pitch / 4;

  /* Passe horizontale */
  for (int32_t y = y1; y < y2; y++) {
    for (int32_t x = x1; x < x2; x++) {
      uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
      uint32_t count = 0;

      for (int32_t kx = -r; kx <= r; kx++) {
        int32_t sx = x + kx;
        if (sx >= x1 && sx < x2) {
          uint32_t pixel = fb->pixels[y * pitch_pixels + sx];
          sum_a += (pixel >> 24) & 0xFF;
          sum_r += (pixel >> 16) & 0xFF;
          sum_g += (pixel >> 8) & 0xFF;
          sum_b += pixel & 0xFF;
          count++;
        }
      }

      if (count > 0) {
        temp[(y - y1) * w + (x - x1)] =
            ((sum_a / count) << 24) | ((sum_r / count) << 16) |
            ((sum_g / count) << 8) | (sum_b / count);
      }
    }
  }

  /* Passe verticale */
  for (int32_t y = y1; y < y2; y++) {
    for (int32_t x = x1; x < x2; x++) {
      uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
      uint32_t count = 0;

      for (int32_t ky = -r; ky <= r; ky++) {
        int32_t sy = y + ky;
        if (sy >= y1 && sy < y2) {
          uint32_t pixel = temp[(sy - y1) * w + (x - x1)];
          sum_a += (pixel >> 24) & 0xFF;
          sum_r += (pixel >> 16) & 0xFF;
          sum_g += (pixel >> 8) & 0xFF;
          sum_b += pixel & 0xFF;
          count++;
        }
      }

      if (count > 0) {
        fb->pixels[y * pitch_pixels + x] =
            ((sum_a / count) << 24) | ((sum_r / count) << 16) |
            ((sum_g / count) << 8) | (sum_b / count);
      }
    }
  }

  free(temp);
}

void apply_blur_fast(rect_t region, uint32_t radius) {
  /* Box blur itéré 3 fois approxime un gaussian blur */
  for (int i = 0; i < 3; i++) {
    apply_blur(region, radius / 3 + 1);
  }
}

void draw_glass_rect(rect_t rect, uint32_t radius, rgba_t tint,
                     uint32_t blur_radius) {
  /* Applique le flou sur la zone */
  apply_blur_fast(rect, blur_radius);

  /* Dessine le rectangle teinté par-dessus */
  draw_rounded_rect_alpha(rect, radius, tint);

  /* TODO: draw_rounded_rect_outline avec alpha pour bordure subtile */
}

/* ============================================================================
 * IMAGES ET BITMAPS
 * ============================================================================
 */

void draw_bitmap(point_t dest, const uint32_t *src, uint32_t src_width,
                 uint32_t src_height) {
  if (!src || src_width == 0 || src_height == 0)
    return;

  framebuffer_t *fb = render_get_active_buffer();
  int32_t x1 = max_i32(dest.x, 0);
  int32_t y1 = max_i32(dest.y, 0);
  int32_t x2 = min_i32(dest.x + (int32_t)src_width, (int32_t)fb->width);
  int32_t y2 = min_i32(dest.y + (int32_t)src_height, (int32_t)fb->height);

  if (g_clip_enabled) {
    x1 = max_i32(x1, g_clip_rect.x);
    y1 = max_i32(y1, g_clip_rect.y);
    x2 = min_i32(x2, g_clip_rect.x + (int32_t)g_clip_rect.width);
    y2 = min_i32(y2, g_clip_rect.y + (int32_t)g_clip_rect.height);
  }
  if (x1 >= x2 || y1 >= y2)
    return;

  uint32_t pitch_pixels = fb->pitch / 4;
  uint32_t src_x = (uint32_t)(x1 - dest.x);
  uint32_t src_y = (uint32_t)(y1 - dest.y);
  size_t row_bytes = (size_t)(x2 - x1) * sizeof(uint32_t);

  for (int32_t y = y1; y < y2; y++, src_y++) {
    uint32_t *dst_row = fb->pixels + (uint32_t)y * pitch_pixels + (uint32_t)x1;
    const uint32_t *src_row = src + src_y * src_width + src_x;
    memcpy(dst_row, src_row, row_bytes);
  }
}

void draw_bitmap_alpha(point_t dest, const uint32_t *src, uint32_t src_width,
                       uint32_t src_height) {
  if (!src)
    return;

  for (uint32_t y = 0; y < src_height; y++) {
    for (uint32_t x = 0; x < src_width; x++) {
      rgba_t color = u32_to_rgba(src[y * src_width + x]);
      draw_pixel_alpha(dest.x + (int32_t)x, dest.y + (int32_t)y, color);
    }
  }
}

void draw_bitmap_region(point_t dest, const uint32_t *src, uint32_t src_width,
                        rect_t src_rect) {
  if (!src)
    return;

  framebuffer_t *fb = render_get_active_buffer();
  uint32_t pitch_pixels = fb->pitch / 4;

  for (uint32_t y = 0; y < src_rect.height; y++) {
    int32_t dy = dest.y + (int32_t)y;
    if (dy < 0 || dy >= (int32_t)fb->height)
      continue;

    int32_t sy = src_rect.y + (int32_t)y;
    if (sy < 0 || sy >= (int32_t)src_width)
      continue; /* Approximation */

    for (uint32_t x = 0; x < src_rect.width; x++) {
      int32_t dx = dest.x + (int32_t)x;
      if (dx < 0 || dx >= (int32_t)fb->width)
        continue;

      int32_t sx = src_rect.x + (int32_t)x;
      if (sx < 0 || sx >= (int32_t)src_width)
        continue;

      fb->pixels[dy * pitch_pixels + dx] = src[sy * src_width + sx];
    }
  }
}

void draw_bitmap_scaled(rect_t dest_rect, const uint32_t *src,
                        uint32_t src_width, uint32_t src_height) {
  if (!src || src_width == 0 || src_height == 0 ||
      dest_rect.width == 0 || dest_rect.height == 0)
    return;

  framebuffer_t *fb = render_get_active_buffer();
  int32_t x1 = max_i32(dest_rect.x, 0);
  int32_t y1 = max_i32(dest_rect.y, 0);
  int32_t x2 = min_i32(dest_rect.x + (int32_t)dest_rect.width,
                       (int32_t)fb->width);
  int32_t y2 = min_i32(dest_rect.y + (int32_t)dest_rect.height,
                       (int32_t)fb->height);

  if (g_clip_enabled) {
    x1 = max_i32(x1, g_clip_rect.x);
    y1 = max_i32(y1, g_clip_rect.y);
    x2 = min_i32(x2, g_clip_rect.x + (int32_t)g_clip_rect.width);
    y2 = min_i32(y2, g_clip_rect.y + (int32_t)g_clip_rect.height);
  }
  if (x1 >= x2 || y1 >= y2)
    return;

  uint64_t step_x = ((uint64_t)src_width << 32) / dest_rect.width;
  uint64_t step_y = ((uint64_t)src_height << 32) / dest_rect.height;
  uint64_t start_x = (uint64_t)(x1 - dest_rect.x) * step_x;
  uint64_t source_y = (uint64_t)(y1 - dest_rect.y) * step_y;
  uint32_t pitch_pixels = fb->pitch / 4;

  for (int32_t y = y1; y < y2; y++, source_y += step_y) {
    uint32_t sy = (uint32_t)(source_y >> 32);
    if (sy >= src_height) sy = src_height - 1;
    uint64_t source_x = start_x;
    uint32_t *dst = fb->pixels + (uint32_t)y * pitch_pixels + (uint32_t)x1;

    for (int32_t x = x1; x < x2; x++, source_x += step_x, dst++) {
      uint32_t sx = (uint32_t)(source_x >> 32);
      if (sx >= src_width) sx = src_width - 1;
      uint32_t pixel = src[sy * src_width + sx];
      uint8_t alpha = (uint8_t)(pixel >> 24);

      if (alpha == 255) {
        *dst = pixel;
      } else if (alpha != 0) {
        *dst = blend_colors(*dst, u32_to_rgba(pixel));
      }
    }
  }
}

/* ============================================================================
 * UTILITAIRES
 * ============================================================================
 */

void render_clear(uint32_t color) {
  framebuffer_t *fb = render_get_active_buffer();
  uint32_t pitch_pixels = fb->pitch / 4;
  for (uint32_t y = 0; y < fb->height; y++)
    memset32(fb->pixels + y * pitch_pixels, color, fb->width);
}

uint32_t blend_colors(uint32_t bg, rgba_t fg) {
  if (fg.a == 0)
    return bg;
  if (fg.a == 255)
    return rgba_to_u32(fg);
  return blend_pixel_fast(bg, fg.r, fg.g, fg.b, fg.a);
}

rgba_t lerp_color(rgba_t c1, rgba_t c2, float t) {
  if (t <= 0.0f)
    return c1;
  if (t >= 1.0f)
    return c2;

  rgba_t result;
  result.r = (uint8_t)((float)c1.r + t * ((float)c2.r - (float)c1.r));
  result.g = (uint8_t)((float)c1.g + t * ((float)c2.g - (float)c1.g));
  result.b = (uint8_t)((float)c1.b + t * ((float)c2.b - (float)c1.b));
  result.a = (uint8_t)((float)c1.a + t * ((float)c2.a - (float)c1.a));
  return result;
}

shadow_params_t shadow_default(void) {
  shadow_params_t s;
  s.offset_x = 0;
  s.offset_y = 4;
  s.blur_radius = 8;
  s.spread = 0;
  s.color = rgba(0, 0, 0, 80);
  return s;
}

shadow_params_t shadow_card(void) {
  shadow_params_t s;
  s.offset_x = 0;
  s.offset_y = 2;
  s.blur_radius = 6;
  s.spread = 0;
  s.color = rgba(0, 0, 0, 40);
  return s;
}

shadow_params_t shadow_window(void) {
  shadow_params_t s;
  s.offset_x = 0;
  s.offset_y = 20;
  s.blur_radius = 40;
  s.spread = 0;
  s.color = rgba(0, 0, 0, 100);
  return s;
}
