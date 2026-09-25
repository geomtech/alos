#ifndef ALOS_DESKTOP_H
#define ALOS_DESKTOP_H

#include "gui_protocol.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/framebuffer.h>
#include <sys/syscall.h>

#define DESKTOP_MAX_CLIENTS 12
#define DESKTOP_MAX_WINDOWS 16
#define DESKTOP_MAX_APPS 16

#define DESKTOP_BORDER 2
#define DESKTOP_TITLEBAR 28
#define DESKTOP_TASKBAR 42
#define DESKTOP_MIN_WIDTH 180
#define DESKTOP_MIN_HEIGHT 110

typedef struct desktop_client desktop_client_t;
typedef struct desktop_window desktop_window_t;

typedef enum {
  WINDOW_NORMAL = 0,
  WINDOW_MAXIMIZED,
  WINDOW_SPLIT_LEFT,
  WINDOW_SPLIT_RIGHT
} desktop_window_state_t;

typedef struct {
  char name[64];
  char executable[128];
} desktop_app_t;

struct desktop_client {
  bool used;
  bool hello_complete;
  int fd;
  uint32_t connection_id;
};

struct desktop_window {
  bool used;
  uint32_t id;
  desktop_client_t *client;
  char title[GUI_TITLE_MAX + 1];
  uint32_t flags;
  gui_rect_t bounds;
  gui_rect_t restore_bounds;
  desktop_window_state_t state;
  bool minimized;
  bool focused;

  uint32_t *surface;
  uint32_t surface_width;
  uint32_t surface_height;
  uint32_t surface_stride;
};

typedef struct {
  framebuffer_info_t framebuffer;
  uint32_t *backbuffer;
  int listener_fd;

  desktop_client_t clients[DESKTOP_MAX_CLIENTS];
  desktop_window_t windows[DESKTOP_MAX_WINDOWS];
  desktop_window_t *z_order[DESKTOP_MAX_WINDOWS];
  uint32_t z_count;
  desktop_window_t *active;
  uint32_t next_window_id;

  int32_t mouse_x;
  int32_t mouse_y;
  uint32_t mouse_buttons;
  desktop_window_t *capture;
  bool dragging;
  bool resizing;
  int32_t drag_offset_x;
  int32_t drag_offset_y;

  bool launcher_open;
  desktop_app_t apps[DESKTOP_MAX_APPS];
  uint32_t app_count;
  uint32_t child_count;
  bool dirty;
} desktop_t;

extern desktop_t g_desktop;

uint32_t desktop_work_height(void);
gui_rect_t desktop_client_rect(const desktop_window_t *window);
void desktop_mark_dirty(void);

int desktop_render_init(const framebuffer_info_t *framebuffer);
void desktop_render(void);
void desktop_render_shutdown(void);

desktop_window_t *desktop_create_window(desktop_client_t *client,
                                        const gui_wire_message_t *message);
void desktop_destroy_window(desktop_window_t *window);
void desktop_destroy_client_windows(desktop_client_t *client);
desktop_window_t *desktop_find_window(desktop_client_t *client,
                                      uint32_t id);
void desktop_focus_window(desktop_window_t *window);
void desktop_minimize_window(desktop_window_t *window);
void desktop_handle_mouse(const input_event_t *event);
void desktop_handle_key(const input_event_t *event);
void desktop_configure_window(desktop_window_t *window);
int desktop_send_event(desktop_window_t *window, gui_event_type_t type,
                       int32_t x, int32_t y, uint32_t key,
                       uint32_t scancode, uint32_t buttons);

void desktop_load_apps(void);
void desktop_launch_app(uint32_t index);
bool desktop_handle_shell_click(int32_t x, int32_t y);

#endif
