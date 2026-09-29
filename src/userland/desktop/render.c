/* src/userland/desktop/render.c - Compositeur logiciel du desktop */
#include "desktop.h"
#include <stdlib.h>
#include <string.h>

extern const uint8_t roboto_font_8x16[];

#define COLOR_DESKTOP_TOP 0xFF163B5C
#define COLOR_DESKTOP_BOTTOM 0xFF315F82
#define COLOR_WINDOW 0xFFF7F8FA
#define COLOR_TITLE_ACTIVE 0xFF2E6EA6
#define COLOR_TITLE_INACTIVE 0xFF536372
#define COLOR_BORDER_ACTIVE 0xFF9BC7EA
#define COLOR_BORDER_INACTIVE 0xFF71808D
#define COLOR_TASKBAR 0xFF18232D
#define COLOR_TASK_ACTIVE 0xFF327FC0
#define COLOR_LAUNCHER 0xFF202D38
#define COLOR_TEXT 0xFFF5F7FA

static void put_pixel(int32_t x, int32_t y, uint32_t color) {
  if (x < 0 || y < 0 || (uint32_t)x >= g_desktop.framebuffer.width ||
      (uint32_t)y >= g_desktop.framebuffer.height) {
    return;
  }
  g_desktop.backbuffer[(uint32_t)y * g_desktop.framebuffer.width +
                       (uint32_t)x] = color;
}

static void fill_rect(gui_rect_t rect, uint32_t color) {
  int32_t x0 = rect.x < 0 ? 0 : rect.x;
  int32_t y0 = rect.y < 0 ? 0 : rect.y;
  int32_t x1 = rect.x + (int32_t)rect.width;
  int32_t y1 = rect.y + (int32_t)rect.height;
  if (x1 > (int32_t)g_desktop.framebuffer.width)
    x1 = (int32_t)g_desktop.framebuffer.width;
  if (y1 > (int32_t)g_desktop.framebuffer.height)
    y1 = (int32_t)g_desktop.framebuffer.height;
  for (int32_t y = y0; y < y1; y++) {
    uint32_t *row = g_desktop.backbuffer +
                    (uint32_t)y * g_desktop.framebuffer.width;
    for (int32_t x = x0; x < x1; x++) {
      row[x] = color;
    }
  }
}

static void outline_rect(gui_rect_t rect, uint32_t color) {
  if (rect.width == 0 || rect.height == 0) return;
  fill_rect((gui_rect_t){rect.x, rect.y, rect.width, 1}, color);
  fill_rect((gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                         rect.width, 1},
            color);
  fill_rect((gui_rect_t){rect.x, rect.y, 1, rect.height}, color);
  fill_rect((gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y, 1,
                         rect.height},
            color);
}

static void draw_char(int32_t x, int32_t y, char character, uint32_t color) {
  unsigned char c = (unsigned char)character;
  if (c < 32 || c > 126) c = '?';
  const uint8_t *glyph = &roboto_font_8x16[(c - 32) * 16];
  for (int row = 0; row < 16; row++) {
    for (int column = 0; column < 8; column++) {
      if ((glyph[row] & (0x80U >> column)) != 0) {
        put_pixel(x + column, y + row, color);
      }
    }
  }
}

static void draw_text(int32_t x, int32_t y, const char *text,
                      uint32_t color, uint32_t max_chars) {
  if (text == NULL) return;
  for (uint32_t i = 0; text[i] != '\0' && i < max_chars; i++) {
    draw_char(x + (int32_t)i * 8, y, text[i], color);
  }
}

static void draw_desktop(void) {
  uint32_t height = desktop_work_height();
  for (uint32_t y = 0; y < height; y++) {
    uint32_t mix = height > 1 ? (y * 255U) / (height - 1) : 0;
    uint32_t r = (0x16U * (255U - mix) + 0x31U * mix) / 255U;
    uint32_t g = (0x3BU * (255U - mix) + 0x5FU * mix) / 255U;
    uint32_t b = (0x5CU * (255U - mix) + 0x82U * mix) / 255U;
    fill_rect((gui_rect_t){0, (int32_t)y, g_desktop.framebuffer.width, 1},
              0xFF000000U | (r << 16) | (g << 8) | b);
  }
}

static void draw_window(desktop_window_t *window) {
  if (window == NULL || !window->used || window->minimized) return;

  uint32_t border = window->focused ? COLOR_BORDER_ACTIVE
                                    : COLOR_BORDER_INACTIVE;
  uint32_t title = window->focused ? COLOR_TITLE_ACTIVE
                                   : COLOR_TITLE_INACTIVE;
  fill_rect(window->bounds, COLOR_WINDOW);
  outline_rect(window->bounds, border);
  fill_rect((gui_rect_t){window->bounds.x + DESKTOP_BORDER,
                         window->bounds.y + DESKTOP_BORDER,
                         window->bounds.width - DESKTOP_BORDER * 2,
                         DESKTOP_TITLEBAR},
            title);
  draw_text(window->bounds.x + 10, window->bounds.y + 7, window->title,
            COLOR_TEXT, (window->bounds.width - 90) / 8);

  int32_t right =
      window->bounds.x + (int32_t)window->bounds.width - DESKTOP_BORDER;
  fill_rect((gui_rect_t){right - 20, window->bounds.y + 7, 13, 13},
            0xFFE35555);
  fill_rect((gui_rect_t){right - 40, window->bounds.y + 7, 13, 13},
            0xFF52B96A);
  fill_rect((gui_rect_t){right - 60, window->bounds.y + 7, 13, 13},
            0xFFE7B84E);

  gui_rect_t client = desktop_client_rect(window);
  if (window->surface == NULL) {
    fill_rect(client, 0xFFE2E7EC);
    draw_text(client.x + 12, client.y + 12, "Waiting for application...",
              0xFF34404A, 30);
    return;
  }

  uint32_t copy_width = client.width < window->surface_width
                            ? client.width
                            : window->surface_width;
  uint32_t copy_height = client.height < window->surface_height
                             ? client.height
                             : window->surface_height;
  for (uint32_t y = 0; y < copy_height; y++) {
    int32_t destination_y = client.y + (int32_t)y;
    if (destination_y < 0 ||
        (uint32_t)destination_y >= g_desktop.framebuffer.height) {
      continue;
    }
    for (uint32_t x = 0; x < copy_width; x++) {
      int32_t destination_x = client.x + (int32_t)x;
      if (destination_x >= 0 &&
          (uint32_t)destination_x < g_desktop.framebuffer.width) {
        put_pixel(destination_x, destination_y,
                  window->surface[y * window->surface_stride + x]);
      }
    }
  }
}

static void draw_taskbar(void) {
  int32_t y =
      (int32_t)g_desktop.framebuffer.height - DESKTOP_TASKBAR;
  fill_rect((gui_rect_t){0, y, g_desktop.framebuffer.width, DESKTOP_TASKBAR},
            COLOR_TASKBAR);
  fill_rect((gui_rect_t){8, y + 6, 82, DESKTOP_TASKBAR - 12},
            g_desktop.launcher_open ? COLOR_TASK_ACTIVE : 0xFF2B3945);
  draw_text(20, y + 13, "Apps", COLOR_TEXT, 8);

  int32_t x = 100;
  for (uint32_t i = 0; i < g_desktop.z_count && x < (int32_t)g_desktop.framebuffer.width - 100;
       i++) {
    desktop_window_t *window = g_desktop.z_order[i];
    if (window == NULL || !window->used) continue;
    uint32_t color = window == g_desktop.active && !window->minimized
                         ? COLOR_TASK_ACTIVE
                         : 0xFF2B3945;
    fill_rect((gui_rect_t){x, y + 6, 120, DESKTOP_TASKBAR - 12}, color);
    draw_text(x + 8, y + 13, window->title, COLOR_TEXT, 13);
    x += 126;
  }
}

static gui_rect_t launcher_rect(void) {
  uint32_t item_height = 32;
  uint32_t height = 48 + g_desktop.app_count * item_height;
  if (height < 80) height = 80;
  return (gui_rect_t){8,
                      (int32_t)g_desktop.framebuffer.height -
                          DESKTOP_TASKBAR - (int32_t)height - 4,
                      300, height};
}

static void draw_launcher(void) {
  if (!g_desktop.launcher_open) return;
  gui_rect_t panel = launcher_rect();
  fill_rect(panel, COLOR_LAUNCHER);
  outline_rect(panel, 0xFF536879);
  draw_text(panel.x + 14, panel.y + 12, "Applications", COLOR_TEXT, 24);
  for (uint32_t i = 0; i < g_desktop.app_count; i++) {
    int32_t y = panel.y + 42 + (int32_t)i * 32;
    fill_rect((gui_rect_t){panel.x + 8, y, panel.width - 16, 27},
              0xFF2B3C49);
    draw_text(panel.x + 18, y + 6, g_desktop.apps[i].name, COLOR_TEXT, 31);
  }
  if (g_desktop.app_count == 0) {
    draw_text(panel.x + 14, panel.y + 48, "No applications installed",
              0xFFB9C3CB, 32);
  }
}

static void draw_cursor(void) {
  int32_t x = g_desktop.mouse_x;
  int32_t y = g_desktop.mouse_y;
  for (int row = 0; row < 16; row++) {
    int width = row / 2 + 1;
    for (int column = 0; column < width; column++) {
      put_pixel(x + column, y + row,
                column == 0 || column == width - 1 ? 0xFF000000
                                                   : 0xFFFFFFFF);
    }
  }
}

int desktop_render_init(const framebuffer_info_t *framebuffer) {
  if (framebuffer == NULL || framebuffer->addr == 0 ||
      framebuffer->width == 0 || framebuffer->height == 0) {
    return -1;
  }
  g_desktop.framebuffer = *framebuffer;
  size_t pixels = (size_t)framebuffer->width * framebuffer->height;
  g_desktop.backbuffer = (uint32_t *)malloc(pixels * sizeof(uint32_t));
  return g_desktop.backbuffer != NULL ? 0 : -1;
}

void desktop_render(void) {
  if (!g_desktop.dirty || g_desktop.backbuffer == NULL) return;
  draw_desktop();
  for (uint32_t i = 0; i < g_desktop.z_count; i++) {
    draw_window(g_desktop.z_order[i]);
  }
  draw_taskbar();
  draw_launcher();
  draw_cursor();

  uint32_t *front = (uint32_t *)(uintptr_t)g_desktop.framebuffer.addr;
  uint32_t front_stride = g_desktop.framebuffer.pitch / sizeof(uint32_t);
  for (uint32_t y = 0; y < g_desktop.framebuffer.height; y++) {
    memcpy(front + y * front_stride,
           g_desktop.backbuffer + y * g_desktop.framebuffer.width,
           g_desktop.framebuffer.width * sizeof(uint32_t));
  }
  g_desktop.dirty = false;
}

void desktop_render_shutdown(void) {
  free(g_desktop.backbuffer);
  g_desktop.backbuffer = NULL;
}
