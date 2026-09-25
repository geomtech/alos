/* src/userland/desktop/main.c - Desktop officiel ALOS (/bin/gui) */
#include "desktop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/display.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

desktop_t g_desktop;

static int send_wire(desktop_client_t *client, gui_wire_message_t *wire) {
  ipc_message_t message;
  memset(&message, 0, sizeof(message));
  wire->header.version = GUI_PROTOCOL_VERSION;
  wire->header.size = sizeof(*wire);
  message.length = sizeof(*wire);
  message.attachment_fd = -1;
  memcpy(message.data, wire, sizeof(*wire));
  return ipc_send(client->fd, &message);
}

static void send_error(desktop_client_t *client, uint32_t request_id,
                       int code) {
  gui_wire_message_t response;
  memset(&response, 0, sizeof(response));
  response.header.type = GUI_MSG_ERROR;
  response.header.request_id = request_id;
  response.body.error.code = code;
  (void)send_wire(client, &response);
}

static void disconnect_client(desktop_client_t *client) {
  if (client == NULL || !client->used) return;
  desktop_destroy_client_windows(client);
  close(client->fd);
  memset(client, 0, sizeof(*client));
  client->fd = -1;
}

static bool damage_valid(desktop_window_t *window, gui_rect_t damage) {
  if (damage.x < 0 || damage.y < 0 ||
      (uint32_t)damage.x > window->surface_width ||
      (uint32_t)damage.y > window->surface_height) {
    return false;
  }
  return damage.width <= window->surface_width - (uint32_t)damage.x &&
         damage.height <= window->surface_height - (uint32_t)damage.y;
}

static void handle_message(desktop_client_t *client,
                           const ipc_message_t *incoming) {
  if (incoming->length != sizeof(gui_wire_message_t)) {
    send_error(client, 0, -1);
    return;
  }
  gui_wire_message_t wire;
  memcpy(&wire, incoming->data, sizeof(wire));
  if (wire.header.version != GUI_PROTOCOL_VERSION ||
      wire.header.size != sizeof(wire)) {
    send_error(client, wire.header.request_id, -2);
    return;
  }

  if (!client->hello_complete) {
    if (wire.header.type != GUI_MSG_HELLO ||
        incoming->attachment_fd >= 0) {
      send_error(client, wire.header.request_id, -3);
      return;
    }
    client->hello_complete = true;
    gui_wire_message_t response;
    memset(&response, 0, sizeof(response));
    response.header.type = GUI_MSG_HELLO_ACK;
    response.header.request_id = wire.header.request_id;
    (void)send_wire(client, &response);
    return;
  }

  desktop_window_t *window = NULL;
  if (wire.header.window_id != 0) {
    window =
        desktop_find_window(client, wire.header.window_id);
    if (window == NULL) {
      send_error(client, wire.header.request_id, -4);
      return;
    }
  }

  switch ((gui_message_type_t)wire.header.type) {
  case GUI_MSG_CREATE_WINDOW: {
    if (incoming->attachment_fd >= 0) {
      send_error(client, wire.header.request_id, -5);
      return;
    }
    window = desktop_create_window(client, &wire);
    if (window == NULL) {
      send_error(client, wire.header.request_id, -6);
      return;
    }
    gui_wire_message_t response;
    memset(&response, 0, sizeof(response));
    response.header.type = GUI_MSG_WINDOW_CREATED;
    response.header.request_id = wire.header.request_id;
    response.header.window_id = window->id;
    (void)send_wire(client, &response);
    break;
  }

  case GUI_MSG_ATTACH_SURFACE: {
    if (window == NULL || incoming->attachment_fd < 0 ||
        wire.body.surface.format != 1 ||
        wire.body.surface.stride != wire.body.surface.width) {
      send_error(client, wire.header.request_id, -7);
      return;
    }
    gui_rect_t client_rect = desktop_client_rect(window);
    if (wire.body.surface.width != client_rect.width ||
        wire.body.surface.height != client_rect.height ||
        wire.body.surface.width > GUI_MAX_SURFACE_DIMENSION ||
        wire.body.surface.height > GUI_MAX_SURFACE_DIMENSION) {
      send_error(client, wire.header.request_id, -8);
      return;
    }
    uint64_t required = (uint64_t)wire.body.surface.stride *
                        wire.body.surface.height * sizeof(uint32_t);
    long available = shm_size(incoming->attachment_fd);
    if (available < 0 || (uint64_t)available < required) {
      send_error(client, wire.header.request_id, -9);
      return;
    }
    uint32_t *surface = (uint32_t *)shm_map(incoming->attachment_fd);
    if (surface == (void *)-1) {
      send_error(client, wire.header.request_id, -10);
      return;
    }
    if (window->surface != NULL) {
      shm_unmap(window->surface);
    }
    window->surface = surface;
    window->surface_width = wire.body.surface.width;
    window->surface_height = wire.body.surface.height;
    window->surface_stride = wire.body.surface.stride;
    desktop_mark_dirty();
    (void)desktop_send_event(window, GUI_EVENT_PAINT, 0, 0, 0, 0, 0);
    break;
  }

  case GUI_MSG_PRESENT:
    if (window == NULL || window->surface == NULL ||
        incoming->attachment_fd >= 0 ||
        !damage_valid(window, wire.body.present.damage)) {
      send_error(client, wire.header.request_id, -11);
      return;
    }
    desktop_mark_dirty();
    break;

  case GUI_MSG_DESTROY_WINDOW:
    if (incoming->attachment_fd >= 0) {
      send_error(client, wire.header.request_id, -12);
      return;
    }
    desktop_destroy_window(window);
    break;

  case GUI_MSG_SET_TITLE:
    if (incoming->attachment_fd >= 0) {
      send_error(client, wire.header.request_id, -13);
      return;
    }
    strncpy(window->title, wire.body.set_title.title, GUI_TITLE_MAX);
    window->title[GUI_TITLE_MAX] = '\0';
    desktop_mark_dirty();
    break;

  default:
    send_error(client, wire.header.request_id, -14);
    break;
  }
}

static void accept_clients(void) {
  for (;;) {
    int fd = ipc_accept(g_desktop.listener_fd, IPC_NONBLOCK);
    if (fd < 0) break;

    desktop_client_t *slot = NULL;
    for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
      if (!g_desktop.clients[i].used) {
        slot = &g_desktop.clients[i];
        break;
      }
    }
    if (slot == NULL) {
      close(fd);
      continue;
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->fd = fd;
  }
}

static void process_clients(void) {
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    desktop_client_t *client = &g_desktop.clients[i];
    if (!client->used) continue;

    for (int count = 0; count < 8; count++) {
      ipc_message_t incoming;
      int result = ipc_receive(client->fd, &incoming, IPC_NONBLOCK);
      if (result == 0) break;
      if (result < 0) {
        disconnect_client(client);
        break;
      }
      if (client->connection_id == 0) {
        client->connection_id = incoming.connection_id;
      } else if (client->connection_id != incoming.connection_id) {
        if (incoming.attachment_fd >= 0) close(incoming.attachment_fd);
        disconnect_client(client);
        break;
      }
      handle_message(client, &incoming);
      if (incoming.attachment_fd >= 0) {
        close(incoming.attachment_fd);
      }
    }
  }
}

static void process_input(void) {
  input_event_t event;
  int result = (int)syscall2(SYS_WAIT_EVENT, (long)&event, 8);
  if (result == 1) {
    if (event.type == INPUT_EVENT_MOUSE_MOVE ||
        event.type == INPUT_EVENT_MOUSE_BUTTON ||
        event.type == INPUT_EVENT_MOUSE_SCROLL) {
      desktop_handle_mouse(&event);
    } else {
      desktop_handle_key(&event);
    }
  }
  for (int i = 0; i < 31; i++) {
    result = (int)syscall1(SYS_GET_EVENT, (long)&event);
    if (result != 1) break;
    if (event.type == INPUT_EVENT_MOUSE_MOVE ||
        event.type == INPUT_EVENT_MOUSE_BUTTON ||
        event.type == INPUT_EVENT_MOUSE_SCROLL) {
      desktop_handle_mouse(&event);
    } else {
      desktop_handle_key(&event);
    }
  }
}

static void reap_children(void) {
  if (g_desktop.child_count == 0) return;
  int status;
  for (;;) {
    pid_t child = waitpid(-1, &status, WNOHANG);
    if (child > 0) {
      g_desktop.child_count--;
      if (g_desktop.child_count == 0) break;
      continue;
    }
    if (child < 0) g_desktop.child_count = 0;
    break;
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  memset(&g_desktop, 0, sizeof(g_desktop));
  g_desktop.listener_fd = -1;
  g_desktop.next_window_id = 1;
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    g_desktop.clients[i].fd = -1;
  }

  if (display_acquire() != 0) {
    printf("gui: another display server already owns the display\n");
    return 1;
  }

  framebuffer_info_t framebuffer;
  if (syscall1(SYS_GET_FRAMEBUFFER, (long)&framebuffer) != 0 ||
      desktop_render_init(&framebuffer) != 0) {
    display_release();
    return 1;
  }

  g_desktop.listener_fd = ipc_listen(GUI_SERVICE_NAME);
  if (g_desktop.listener_fd < 0) {
    desktop_render_shutdown();
    display_release();
    return 1;
  }

  desktop_load_apps();
  g_desktop.mouse_x = (int32_t)framebuffer.width / 2;
  g_desktop.mouse_y = (int32_t)framebuffer.height / 2;
  desktop_mark_dirty();
  desktop_render();

  for (;;) {
    process_input();
    accept_clients();
    process_clients();
    reap_children();
    desktop_render();
  }
}
