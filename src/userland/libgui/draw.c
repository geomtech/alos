/* libgui/draw.c - Primitives de dessin dans une surface cliente */
#include "gui.h"
#include <stddef.h>

void gui_draw_pixel(gui_window_t *window, int32_t x, int32_t y,
                    uint32_t color) {
  uint32_t width = gui_window_width(window);
  uint32_t height = gui_window_height(window);
  uint32_t *pixels = gui_window_pixels(window);
  if (pixels == NULL || x < 0 || y < 0 || (uint32_t)x >= width ||
      (uint32_t)y >= height) {
    return;
  }
  pixels[(uint32_t)y * gui_window_stride(window) + (uint32_t)x] = color;
}

void gui_fill_rect(gui_window_t *window, gui_rect_t rect, uint32_t color) {
  int32_t x0 = rect.x < 0 ? 0 : rect.x;
  int32_t y0 = rect.y < 0 ? 0 : rect.y;
  int32_t x1 = rect.x + (int32_t)rect.width;
  int32_t y1 = rect.y + (int32_t)rect.height;
  uint32_t width = gui_window_width(window);
  uint32_t height = gui_window_height(window);
  if (x1 > (int32_t)width) x1 = (int32_t)width;
  if (y1 > (int32_t)height) y1 = (int32_t)height;
  for (int32_t y = y0; y < y1; y++) {
    for (int32_t x = x0; x < x1; x++) {
      gui_draw_pixel(window, x, y, color);
    }
  }
}

void gui_clear(gui_window_t *window, uint32_t color) {
  gui_rect_t rect = {0, 0, gui_window_width(window),
                     gui_window_height(window)};
  gui_fill_rect(window, rect, color);
}

void gui_draw_rect(gui_window_t *window, gui_rect_t rect, uint32_t color) {
  if (rect.width == 0 || rect.height == 0) {
    return;
  }
  gui_fill_rect(window, (gui_rect_t){rect.x, rect.y, rect.width, 1}, color);
  gui_fill_rect(window,
                (gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                             rect.width, 1},
                color);
  gui_fill_rect(window, (gui_rect_t){rect.x, rect.y, 1, rect.height}, color);
  gui_fill_rect(window,
                (gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y, 1,
                             rect.height},
                color);
}
