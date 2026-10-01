#ifndef ALOS_GUI_H
#define ALOS_GUI_H

#include "gui_protocol.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gui_window gui_window_t;

typedef struct {
  gui_event_type_t type;
  gui_window_t *window;
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  uint32_t key;
  uint32_t scancode;
  uint32_t buttons;
  uint32_t modifiers;
  int focused;
} gui_event_t;

gui_window_t *gui_create_window(const char *title, uint32_t width,
                                uint32_t height, uint32_t flags);
void gui_destroy_window(gui_window_t *window);
int gui_set_title(gui_window_t *window, const char *title);

int gui_poll_event(gui_window_t *window, gui_event_t *event);
int gui_wait_event(gui_window_t *window, gui_event_t *event,
                   uint32_t timeout_ms);
/* Descripteur IPC du desktop (pollable, POLLIN) ou -1 avant la premiere
 * fenetre. libgui n'est pas thread-safe : appeler toutes les fonctions
 * gui_* depuis un seul thread, par exemple apres poll() sur ce descripteur. */
int gui_connection_fd(void);

uint32_t *gui_window_pixels(gui_window_t *window);
uint32_t gui_window_width(const gui_window_t *window);
uint32_t gui_window_height(const gui_window_t *window);
uint32_t gui_window_stride(const gui_window_t *window);
int gui_present(gui_window_t *window, const gui_rect_t *damage);

void gui_clear(gui_window_t *window, uint32_t color);
void gui_draw_pixel(gui_window_t *window, int32_t x, int32_t y,
                    uint32_t color);
void gui_fill_rect(gui_window_t *window, gui_rect_t rect, uint32_t color);
void gui_draw_rect(gui_window_t *window, gui_rect_t rect, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif
