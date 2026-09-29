/* libgui/gui.c - Client du serveur de fenetres ALOS */
#include "gui.h"
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#define GUI_MAX_WINDOWS 16
#define GUI_PENDING_EVENTS 64

struct gui_window {
  uint32_t id;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  int surface_fd;
  uint32_t *pixels;
};

static int g_connection = -1;
static uint32_t g_next_request = 1;
static gui_window_t *g_windows[GUI_MAX_WINDOWS];
static gui_event_t g_events[GUI_PENDING_EVENTS];
static uint32_t g_event_count;

static gui_window_t *find_window(uint32_t id) {
  for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
    if (g_windows[i] != NULL && g_windows[i]->id == id) {
      return g_windows[i];
    }
  }
  return NULL;
}

static int send_wire(gui_wire_message_t *wire, int attachment_fd) {
  ipc_message_t message;
  memset(&message, 0, sizeof(message));
  wire->header.version = GUI_PROTOCOL_VERSION;
  wire->header.size = sizeof(*wire);
  message.length = sizeof(*wire);
  message.attachment_fd = attachment_fd;
  memcpy(message.data, wire, sizeof(*wire));
  return ipc_send(g_connection, &message);
}

static int connect_desktop(void) {
  if (g_connection >= 0) {
    return 0;
  }
  g_connection = ipc_connect(GUI_SERVICE_NAME);
  if (g_connection < 0) {
    return -1;
  }

  gui_wire_message_t wire;
  memset(&wire, 0, sizeof(wire));
  wire.header.type = GUI_MSG_HELLO;
  wire.header.request_id = g_next_request++;
  if (send_wire(&wire, -1) != 0) {
    close(g_connection);
    g_connection = -1;
    return -1;
  }

  ipc_message_t incoming;
  if (ipc_receive(g_connection, &incoming, 1000) != 1 ||
      incoming.length != sizeof(wire)) {
    close(g_connection);
    g_connection = -1;
    return -1;
  }
  memcpy(&wire, incoming.data, sizeof(wire));
  if (wire.header.version != GUI_PROTOCOL_VERSION ||
      wire.header.type != GUI_MSG_HELLO_ACK) {
    close(g_connection);
    g_connection = -1;
    return -1;
  }
  return 0;
}

static int attach_surface(gui_window_t *window, uint32_t width,
                          uint32_t height) {
  if (window == NULL || width == 0 || height == 0 ||
      width > GUI_MAX_SURFACE_DIMENSION ||
      height > GUI_MAX_SURFACE_DIMENSION ||
      height > (uint32_t)(~(size_t)0) / (width * sizeof(uint32_t))) {
    return -1;
  }

  size_t size = (size_t)width * height * sizeof(uint32_t);
  int fd = shm_create(size);
  if (fd < 0) {
    return -1;
  }
  uint32_t *pixels = (uint32_t *)shm_map(fd);
  if (pixels == (void *)-1) {
    close(fd);
    return -1;
  }

  gui_wire_message_t wire;
  memset(&wire, 0, sizeof(wire));
  wire.header.type = GUI_MSG_ATTACH_SURFACE;
  wire.header.window_id = window->id;
  wire.body.surface.width = width;
  wire.body.surface.height = height;
  wire.body.surface.stride = width;
  wire.body.surface.format = 1;
  if (send_wire(&wire, fd) != 0) {
    shm_unmap(pixels);
    close(fd);
    return -1;
  }

  if (window->pixels != NULL) {
    shm_unmap(window->pixels);
  }
  if (window->surface_fd >= 0) {
    close(window->surface_fd);
  }
  window->pixels = pixels;
  window->surface_fd = fd;
  window->width = width;
  window->height = height;
  window->stride = width;
  return 0;
}

static void queue_event(const gui_wire_message_t *wire) {
  if (g_event_count >= GUI_PENDING_EVENTS) {
    return;
  }
  gui_window_t *window = find_window(wire->header.window_id);
  if (window == NULL) {
    return;
  }

  if (wire->body.event.event_type == GUI_EVENT_RESIZE) {
    if (attach_surface(window, wire->body.event.width,
                       wire->body.event.height) != 0) {
      return;
    }
  }

  gui_event_t *event = &g_events[g_event_count++];
  memset(event, 0, sizeof(*event));
  event->type = (gui_event_type_t)wire->body.event.event_type;
  event->window = window;
  event->x = wire->body.event.x;
  event->y = wire->body.event.y;
  event->width = wire->body.event.width;
  event->height = wire->body.event.height;
  event->key = wire->body.event.key;
  event->scancode = wire->body.event.scancode;
  event->buttons = wire->body.event.buttons;
  event->modifiers = wire->body.event.modifiers;
  event->focused = (int)wire->body.event.focused;
}

static int pump_one(uint32_t timeout_ms) {
  ipc_message_t incoming;
  int result = ipc_receive(g_connection, &incoming, timeout_ms);
  if (result != 1) {
    return result;
  }
  if (incoming.attachment_fd >= 0) {
    close(incoming.attachment_fd);
  }
  if (incoming.length != sizeof(gui_wire_message_t)) {
    return -1;
  }

  gui_wire_message_t wire;
  memcpy(&wire, incoming.data, sizeof(wire));
  if (wire.header.version != GUI_PROTOCOL_VERSION ||
      wire.header.size != sizeof(wire)) {
    return -1;
  }
  if (wire.header.type == GUI_MSG_EVENT) {
    queue_event(&wire);
  }
  return 1;
}

static int take_event(gui_window_t *window, gui_event_t *event) {
  for (uint32_t i = 0; i < g_event_count; i++) {
    if (window != NULL && g_events[i].window != window) {
      continue;
    }
    *event = g_events[i];
    for (uint32_t j = i + 1; j < g_event_count; j++) {
      g_events[j - 1] = g_events[j];
    }
    g_event_count--;
    return 1;
  }
  return 0;
}

gui_window_t *gui_create_window(const char *title, uint32_t width,
                                uint32_t height, uint32_t flags) {
  if (title == NULL || width == 0 || height == 0 ||
      connect_desktop() != 0) {
    return NULL;
  }
  int slot = -1;
  for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
    if (g_windows[i] == NULL) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    return NULL;
  }

  gui_wire_message_t wire;
  memset(&wire, 0, sizeof(wire));
  uint32_t request = g_next_request++;
  wire.header.type = GUI_MSG_CREATE_WINDOW;
  wire.header.request_id = request;
  wire.body.create.width = width;
  wire.body.create.height = height;
  wire.body.create.flags = flags;
  strncpy(wire.body.create.title, title, GUI_TITLE_MAX);
  wire.body.create.title[GUI_TITLE_MAX] = '\0';
  if (send_wire(&wire, -1) != 0) {
    return NULL;
  }

  for (;;) {
    ipc_message_t incoming;
    if (ipc_receive(g_connection, &incoming, 1000) != 1 ||
        incoming.length != sizeof(wire)) {
      return NULL;
    }
    if (incoming.attachment_fd >= 0) {
      close(incoming.attachment_fd);
    }
    memcpy(&wire, incoming.data, sizeof(wire));
    if (wire.header.version != GUI_PROTOCOL_VERSION ||
        wire.header.size != sizeof(wire)) {
      return NULL;
    }
    if (wire.header.type == GUI_MSG_WINDOW_CREATED &&
        wire.header.request_id == request) {
      break;
    }
    if (wire.header.type == GUI_MSG_EVENT) {
      queue_event(&wire);
    }
  }

  gui_window_t *window = (gui_window_t *)calloc(1, sizeof(*window));
  if (window == NULL) {
    return NULL;
  }
  window->id = wire.header.window_id;
  window->surface_fd = -1;
  g_windows[slot] = window;
  if (attach_surface(window, width, height) != 0) {
    gui_destroy_window(window);
    return NULL;
  }
  return window;
}

void gui_destroy_window(gui_window_t *window) {
  if (window == NULL) {
    return;
  }
  gui_wire_message_t wire;
  memset(&wire, 0, sizeof(wire));
  wire.header.type = GUI_MSG_DESTROY_WINDOW;
  wire.header.window_id = window->id;
  (void)send_wire(&wire, -1);

  if (window->pixels != NULL) {
    shm_unmap(window->pixels);
  }
  if (window->surface_fd >= 0) {
    close(window->surface_fd);
  }
  for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
    if (g_windows[i] == window) {
      g_windows[i] = NULL;
      break;
    }
  }
  free(window);
}

int gui_set_title(gui_window_t *window, const char *title) {
  if (window == NULL || title == NULL) {
    return -1;
  }
  gui_wire_message_t wire;
  memset(&wire, 0, sizeof(wire));
  wire.header.type = GUI_MSG_SET_TITLE;
  wire.header.window_id = window->id;
  strncpy(wire.body.set_title.title, title, GUI_TITLE_MAX);
  wire.body.set_title.title[GUI_TITLE_MAX] = '\0';
  return send_wire(&wire, -1);
}

int gui_poll_event(gui_window_t *window, gui_event_t *event) {
  return gui_wait_event(window, event, IPC_NONBLOCK);
}

int gui_wait_event(gui_window_t *window, gui_event_t *event,
                   uint32_t timeout_ms) {
  if (event == NULL || g_connection < 0) {
    return -1;
  }
  if (take_event(window, event) == 1) {
    return 1;
  }
  int result = pump_one(timeout_ms);
  if (result <= 0) {
    return result;
  }
  return take_event(window, event);
}

uint32_t *gui_window_pixels(gui_window_t *window) {
  return window != NULL ? window->pixels : NULL;
}
uint32_t gui_window_width(const gui_window_t *window) {
  return window != NULL ? window->width : 0;
}
uint32_t gui_window_height(const gui_window_t *window) {
  return window != NULL ? window->height : 0;
}
uint32_t gui_window_stride(const gui_window_t *window) {
  return window != NULL ? window->stride : 0;
}

int gui_present(gui_window_t *window, const gui_rect_t *damage) {
  if (window == NULL) {
    return -1;
  }
  gui_wire_message_t wire;
  memset(&wire, 0, sizeof(wire));
  wire.header.type = GUI_MSG_PRESENT;
  wire.header.window_id = window->id;
  if (damage != NULL) {
    wire.body.present.damage = *damage;
  } else {
    wire.body.present.damage.x = 0;
    wire.body.present.damage.y = 0;
    wire.body.present.damage.width = window->width;
    wire.body.present.damage.height = window->height;
  }
  return send_wire(&wire, -1);
}
