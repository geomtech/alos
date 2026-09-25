/* src/userland/desktop/wm.c - Politique de fenetrage du desktop */
#include "desktop.h"
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

static bool point_in_rect(int32_t x, int32_t y, gui_rect_t rect) {
  return x >= rect.x && y >= rect.y &&
         x < rect.x + (int32_t)rect.width &&
         y < rect.y + (int32_t)rect.height;
}

uint32_t desktop_work_height(void) {
  return g_desktop.framebuffer.height > DESKTOP_TASKBAR
             ? g_desktop.framebuffer.height - DESKTOP_TASKBAR
             : g_desktop.framebuffer.height;
}

gui_rect_t desktop_client_rect(const desktop_window_t *window) {
  if (window == NULL || window->bounds.width <= DESKTOP_BORDER * 2 ||
      window->bounds.height <= DESKTOP_TITLEBAR + DESKTOP_BORDER * 2) {
    return (gui_rect_t){0, 0, 0, 0};
  }
  return (gui_rect_t){
      window->bounds.x + DESKTOP_BORDER,
      window->bounds.y + DESKTOP_BORDER + DESKTOP_TITLEBAR,
      window->bounds.width - DESKTOP_BORDER * 2,
      window->bounds.height - DESKTOP_TITLEBAR - DESKTOP_BORDER * 2};
}

void desktop_mark_dirty(void) { g_desktop.dirty = true; }

static int send_wire(desktop_client_t *client, gui_wire_message_t *wire) {
  if (client == NULL || !client->used) return -1;
  ipc_message_t message;
  memset(&message, 0, sizeof(message));
  wire->header.version = GUI_PROTOCOL_VERSION;
  wire->header.size = sizeof(*wire);
  message.length = sizeof(*wire);
  message.attachment_fd = -1;
  memcpy(message.data, wire, sizeof(*wire));
  return ipc_send(client->fd, &message);
}

int desktop_send_event(desktop_window_t *window, gui_event_type_t type,
                       int32_t x, int32_t y, uint32_t key,
                       uint32_t scancode, uint32_t buttons) {
  if (window == NULL || !window->used) return -1;
  gui_wire_message_t wire;
  memset(&wire, 0, sizeof(wire));
  wire.header.type = GUI_MSG_EVENT;
  wire.header.window_id = window->id;
  wire.body.event.event_type = type;
  wire.body.event.x = x;
  wire.body.event.y = y;
  gui_rect_t client = desktop_client_rect(window);
  wire.body.event.width = client.width;
  wire.body.event.height = client.height;
  wire.body.event.key = key;
  wire.body.event.scancode = scancode;
  wire.body.event.buttons = buttons;
  wire.body.event.focused = window->focused ? 1U : 0U;
  return send_wire(window->client, &wire);
}

desktop_window_t *desktop_find_window(desktop_client_t *client,
                                      uint32_t id) {
  for (int i = 0; i < DESKTOP_MAX_WINDOWS; i++) {
    desktop_window_t *window = &g_desktop.windows[i];
    if (window->used && window->client == client && window->id == id) {
      return window;
    }
  }
  return NULL;
}

static void remove_from_z_order(desktop_window_t *window) {
  for (uint32_t i = 0; i < g_desktop.z_count; i++) {
    if (g_desktop.z_order[i] != window) continue;
    for (uint32_t j = i + 1; j < g_desktop.z_count; j++) {
      g_desktop.z_order[j - 1] = g_desktop.z_order[j];
    }
    g_desktop.z_count--;
    g_desktop.z_order[g_desktop.z_count] = NULL;
    return;
  }
}

void desktop_focus_window(desktop_window_t *window) {
  if (window == NULL || !window->used) return;
  if (window->minimized) window->minimized = false;

  if (g_desktop.active != window) {
    desktop_window_t *old = g_desktop.active;
    if (old != NULL && old->used) {
      old->focused = false;
      (void)desktop_send_event(old, GUI_EVENT_FOCUS, 0, 0, 0, 0, 0);
    }
    g_desktop.active = window;
    window->focused = true;
    (void)desktop_send_event(window, GUI_EVENT_FOCUS, 0, 0, 0, 0, 0);
  }

  remove_from_z_order(window);
  g_desktop.z_order[g_desktop.z_count++] = window;
  desktop_mark_dirty();
}

static void focus_top_window(void) {
  for (uint32_t i = g_desktop.z_count; i > 0; i--) {
    desktop_window_t *candidate = g_desktop.z_order[i - 1];
    if (candidate != NULL && candidate->used && !candidate->minimized) {
      desktop_focus_window(candidate);
      return;
    }
  }
  g_desktop.active = NULL;
}

desktop_window_t *desktop_create_window(desktop_client_t *client,
                                        const gui_wire_message_t *message) {
  if (client == NULL || message == NULL ||
      message->body.create.width < 80 ||
      message->body.create.height < 40 ||
      message->body.create.width > GUI_MAX_SURFACE_DIMENSION ||
      message->body.create.height > GUI_MAX_SURFACE_DIMENSION) {
    return NULL;
  }

  desktop_window_t *window = NULL;
  for (int i = 0; i < DESKTOP_MAX_WINDOWS; i++) {
    if (!g_desktop.windows[i].used) {
      window = &g_desktop.windows[i];
      break;
    }
  }
  if (window == NULL || g_desktop.z_count >= DESKTOP_MAX_WINDOWS) {
    return NULL;
  }

  memset(window, 0, sizeof(*window));
  window->used = true;
  window->id = g_desktop.next_window_id++;
  if (g_desktop.next_window_id == 0) g_desktop.next_window_id = 1;
  window->client = client;
  window->flags = message->body.create.flags;
  strncpy(window->title, message->body.create.title, GUI_TITLE_MAX);
  window->title[GUI_TITLE_MAX] = '\0';

  uint32_t outer_width =
      message->body.create.width + DESKTOP_BORDER * 2;
  uint32_t outer_height = message->body.create.height + DESKTOP_TITLEBAR +
                          DESKTOP_BORDER * 2;
  if (outer_width > g_desktop.framebuffer.width)
    outer_width = g_desktop.framebuffer.width;
  if (outer_height > desktop_work_height())
    outer_height = desktop_work_height();
  uint32_t cascade = (g_desktop.z_count % 8U) * 24U;
  window->bounds = (gui_rect_t){40 + (int32_t)cascade,
                                30 + (int32_t)cascade, outer_width,
                                outer_height};
  if (window->bounds.x + (int32_t)outer_width >
      (int32_t)g_desktop.framebuffer.width) {
    window->bounds.x =
        (int32_t)g_desktop.framebuffer.width - (int32_t)outer_width;
  }
  if (window->bounds.y + (int32_t)outer_height >
      (int32_t)desktop_work_height()) {
    window->bounds.y =
        (int32_t)desktop_work_height() - (int32_t)outer_height;
  }
  window->restore_bounds = window->bounds;
  window->state = WINDOW_NORMAL;
  g_desktop.z_order[g_desktop.z_count++] = window;
  desktop_focus_window(window);
  return window;
}

void desktop_destroy_window(desktop_window_t *window) {
  if (window == NULL || !window->used) return;
  if (g_desktop.capture == window) {
    g_desktop.capture = NULL;
    g_desktop.dragging = false;
    g_desktop.resizing = false;
  }
  if (window->surface != NULL) {
    shm_unmap(window->surface);
  }
  remove_from_z_order(window);
  bool was_active = g_desktop.active == window;
  memset(window, 0, sizeof(*window));
  if (was_active) {
    g_desktop.active = NULL;
    focus_top_window();
  }
  desktop_mark_dirty();
}

void desktop_destroy_client_windows(desktop_client_t *client) {
  for (int i = 0; i < DESKTOP_MAX_WINDOWS; i++) {
    if (g_desktop.windows[i].used &&
        g_desktop.windows[i].client == client) {
      desktop_destroy_window(&g_desktop.windows[i]);
    }
  }
}

void desktop_configure_window(desktop_window_t *window) {
  if (window == NULL || !window->used) return;
  (void)desktop_send_event(window, GUI_EVENT_RESIZE, 0, 0, 0, 0, 0);
  desktop_mark_dirty();
}

void desktop_minimize_window(desktop_window_t *window) {
  if (window == NULL || !window->used || window->minimized) return;
  if (window->focused) {
    window->focused = false;
    (void)desktop_send_event(window, GUI_EVENT_FOCUS, 0, 0, 0, 0, 0);
  }
  window->minimized = true;
  if (g_desktop.active == window) {
    g_desktop.active = NULL;
    focus_top_window();
  }
  desktop_mark_dirty();
}

static void restore_window(desktop_window_t *window) {
  if (window->state != WINDOW_NORMAL) {
    window->bounds = window->restore_bounds;
    window->state = WINDOW_NORMAL;
    desktop_configure_window(window);
  }
  window->minimized = false;
  desktop_focus_window(window);
}

static void maximize_or_restore(desktop_window_t *window) {
  if (window->state != WINDOW_NORMAL) {
    restore_window(window);
    return;
  }
  window->restore_bounds = window->bounds;
  window->bounds = (gui_rect_t){0, 0, g_desktop.framebuffer.width,
                                desktop_work_height()};
  window->state = WINDOW_MAXIMIZED;
  desktop_focus_window(window);
  desktop_configure_window(window);
}

static void split_window(desktop_window_t *window, bool left) {
  if (window->state == WINDOW_NORMAL) {
    window->restore_bounds = window->bounds;
  }
  uint32_t left_width = g_desktop.framebuffer.width / 2U;
  window->bounds =
      left ? (gui_rect_t){0, 0, left_width, desktop_work_height()}
           : (gui_rect_t){(int32_t)left_width, 0,
                          g_desktop.framebuffer.width - left_width,
                          desktop_work_height()};
  window->state = left ? WINDOW_SPLIT_LEFT : WINDOW_SPLIT_RIGHT;
  desktop_focus_window(window);
  desktop_configure_window(window);
}

static desktop_window_t *window_at(int32_t x, int32_t y) {
  for (uint32_t i = g_desktop.z_count; i > 0; i--) {
    desktop_window_t *window = g_desktop.z_order[i - 1];
    if (window != NULL && window->used && !window->minimized &&
        point_in_rect(x, y, window->bounds)) {
      return window;
    }
  }
  return NULL;
}

static void send_mouse_to_client(desktop_window_t *window,
                                 gui_event_type_t type, int32_t x, int32_t y,
                                 uint32_t buttons) {
  gui_rect_t client = desktop_client_rect(window);
  if (point_in_rect(x, y, client)) {
    (void)desktop_send_event(window, type, x - client.x, y - client.y, 0, 0,
                             buttons);
  }
}

static void handle_mouse_down(int32_t x, int32_t y, uint32_t buttons) {
  if (desktop_handle_shell_click(x, y)) return;

  desktop_window_t *window = window_at(x, y);
  if (window == NULL) {
    if (g_desktop.launcher_open) {
      g_desktop.launcher_open = false;
      desktop_mark_dirty();
    }
    return;
  }
  desktop_focus_window(window);
  int32_t right =
      window->bounds.x + (int32_t)window->bounds.width - DESKTOP_BORDER;
  int32_t title_bottom =
      window->bounds.y + DESKTOP_BORDER + DESKTOP_TITLEBAR;

  if (y >= window->bounds.y && y < title_bottom) {
    if (x >= right - 20) {
      (void)desktop_send_event(window, GUI_EVENT_CLOSE, 0, 0, 0, 0, 0);
      desktop_destroy_window(window);
      return;
    }
    if (x >= right - 40 &&
        (window->flags & GUI_WINDOW_MAXIMIZABLE) != 0) {
      maximize_or_restore(window);
      return;
    }
    if (x >= right - 60 &&
        (window->flags & GUI_WINDOW_MINIMIZABLE) != 0) {
      desktop_minimize_window(window);
      return;
    }
    if (window->state != WINDOW_NORMAL) {
      restore_window(window);
    }
    g_desktop.capture = window;
    g_desktop.dragging = true;
    g_desktop.drag_offset_x = x - window->bounds.x;
    g_desktop.drag_offset_y = y - window->bounds.y;
    return;
  }

  bool resize_hit =
      x >= window->bounds.x + (int32_t)window->bounds.width - 8 &&
      y >= window->bounds.y + (int32_t)window->bounds.height - 8;
  if (resize_hit && (window->flags & GUI_WINDOW_RESIZABLE) != 0 &&
      window->state == WINDOW_NORMAL) {
    g_desktop.capture = window;
    g_desktop.resizing = true;
    return;
  }
  send_mouse_to_client(window, GUI_EVENT_MOUSE_DOWN, x, y, buttons);
}

static void handle_mouse_move(int32_t x, int32_t y, uint32_t buttons) {
  desktop_window_t *window = g_desktop.capture;
  if (window != NULL && g_desktop.dragging) {
    window->bounds.x = x - g_desktop.drag_offset_x;
    window->bounds.y = y - g_desktop.drag_offset_y;
    if (window->bounds.y < 0) window->bounds.y = 0;
    if (window->bounds.y >= (int32_t)desktop_work_height())
      window->bounds.y = (int32_t)desktop_work_height() - 1;
    desktop_mark_dirty();
    return;
  }
  if (window != NULL && g_desktop.resizing) {
    int32_t width = x - window->bounds.x + 1;
    int32_t height = y - window->bounds.y + 1;
    if (width < DESKTOP_MIN_WIDTH) width = DESKTOP_MIN_WIDTH;
    if (height < DESKTOP_MIN_HEIGHT) height = DESKTOP_MIN_HEIGHT;
    if (window->bounds.x + width > (int32_t)g_desktop.framebuffer.width)
      width = (int32_t)g_desktop.framebuffer.width - window->bounds.x;
    if (window->bounds.y + height > (int32_t)desktop_work_height())
      height = (int32_t)desktop_work_height() - window->bounds.y;
    window->bounds.width = (uint32_t)width;
    window->bounds.height = (uint32_t)height;
    desktop_mark_dirty();
    return;
  }

  window = window_at(x, y);
  if (window != NULL) {
    send_mouse_to_client(window, GUI_EVENT_MOUSE_MOVE, x, y, buttons);
  }
}

static void handle_mouse_up(int32_t x, int32_t y, uint32_t buttons) {
  desktop_window_t *window = g_desktop.capture;
  bool resized = g_desktop.resizing;
  bool dragged = g_desktop.dragging;
  g_desktop.capture = NULL;
  g_desktop.dragging = false;
  g_desktop.resizing = false;

  if (window != NULL && dragged) {
    if (x <= 1) {
      split_window(window, true);
    } else if (x >= (int32_t)g_desktop.framebuffer.width - 2) {
      split_window(window, false);
    } else {
      window->restore_bounds = window->bounds;
    }
  } else if (window != NULL && resized) {
    window->restore_bounds = window->bounds;
    desktop_configure_window(window);
  } else {
    window = window_at(x, y);
    if (window != NULL) {
      send_mouse_to_client(window, GUI_EVENT_MOUSE_UP, x, y, buttons);
    }
  }
}

void desktop_handle_mouse(const input_event_t *event) {
  if (event == NULL) return;
  if (event->type == INPUT_EVENT_MOUSE_MOVE) {
    g_desktop.mouse_x = event->data.mouse.x;
    g_desktop.mouse_y = event->data.mouse.y;
    handle_mouse_move(g_desktop.mouse_x, g_desktop.mouse_y,
                      g_desktop.mouse_buttons);
    desktop_mark_dirty();
    return;
  }
  if (event->type != INPUT_EVENT_MOUSE_BUTTON) return;

  uint32_t previous = g_desktop.mouse_buttons;
  uint32_t current = event->data.mouse.buttons;
  g_desktop.mouse_buttons = current;
  bool was_down = (previous & 1U) != 0;
  bool is_down = (current & 1U) != 0;
  if (!was_down && is_down) {
    handle_mouse_down(g_desktop.mouse_x, g_desktop.mouse_y, current);
  } else if (was_down && !is_down) {
    handle_mouse_up(g_desktop.mouse_x, g_desktop.mouse_y, current);
  }
}

void desktop_handle_key(const input_event_t *event) {
  desktop_window_t *window = g_desktop.active;
  if (event == NULL || window == NULL || window->minimized) return;
  gui_event_type_t type =
      event->type == INPUT_EVENT_KEY_PRESS ? GUI_EVENT_KEY_DOWN
                                           : GUI_EVENT_KEY_UP;
  (void)desktop_send_event(window, type, 0, 0, event->data.key.key,
                           event->data.key.scancode, 0);
}
