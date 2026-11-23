/* browser/render.c - Rendu logiciel de la liste d'elements dans un tampon ARGB */
#include "web.h"

extern const uint8_t roboto_font_8x16[]; /* police 8x16 ASCII 32..126 */

static void put(uint32_t *px, uint32_t stride, int w, int h, int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  px[(uint32_t)y * stride + (uint32_t)x] = c;
}

static void glyph(uint32_t *px, uint32_t stride, int w, int h, int x, int y,
                  unsigned char ch, uint32_t color, int scale, int bold) {
  if (ch < 32 || ch > 126) ch = '?';
  const uint8_t *g = &roboto_font_8x16[(ch - 32) * 16];
  for (int row = 0; row < 16; row++)
    for (int col = 0; col < 8; col++) {
      if (!(g[row] & (0x80u >> col))) continue;
      for (int sy = 0; sy < scale; sy++)
        for (int sx = 0; sx < scale; sx++) {
          int X = x + col * scale + sx, Y = y + row * scale + sy;
          put(px, stride, w, h, X, Y, color);
          if (bold) put(px, stride, w, h, X + 1, Y, color);
        }
    }
}

void web_draw_text(uint32_t *px, uint32_t stride, int w, int h, int x, int y,
                   const char *text, uint32_t color) {
  for (; *text; text++, x += 8) glyph(px, stride, w, h, x, y, (unsigned char)*text, color, 1, 0);
}

void web_render(const web_page_t *page, uint32_t *px, uint32_t stride, int w,
                int h, int scroll_y, uint32_t background) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) px[(uint32_t)y * stride + (uint32_t)x] = background;
  for (size_t i = 0; i < page->count; i++) {
    const web_item_t *it = &page->items[i];
    int top = it->y - scroll_y;
    if (top + it->h < 0 || top > h) continue;
    if (it->type == WEB_ITEM_RECT) {
      for (int y = 0; y < it->h; y++)
        for (int x = 0; x < it->w; x++) put(px, stride, w, h, it->x + x, top + y, it->color);
      continue;
    }
    int x = it->x;
    for (const char *c = it->text; *c; c++, x += 8 * it->scale)
      glyph(px, stride, w, h, x, top, (unsigned char)*c, it->color, it->scale, it->bold);
    if (it->underline)
      for (int k = 0; k < it->w; k++) put(px, stride, w, h, it->x + k, top + it->h - 2, it->color);
  }
}
