#ifndef ALOS_GUI_PROTOCOL_H
#define ALOS_GUI_PROTOCOL_H

#include <stdint.h>

#define GUI_PROTOCOL_VERSION 1U
#define GUI_SERVICE_NAME "alos.gui"
#define GUI_TITLE_MAX 63
#define GUI_MAX_SURFACE_DIMENSION 4096U

#define GUI_WINDOW_RESIZABLE (1U << 0)
#define GUI_WINDOW_MINIMIZABLE (1U << 1)
#define GUI_WINDOW_MAXIMIZABLE (1U << 2)
#define GUI_WINDOW_DEFAULT                                                   \
  (GUI_WINDOW_RESIZABLE | GUI_WINDOW_MINIMIZABLE | GUI_WINDOW_MAXIMIZABLE)

typedef enum {
  GUI_MSG_HELLO = 1,
  GUI_MSG_HELLO_ACK,
  GUI_MSG_CREATE_WINDOW,
  GUI_MSG_WINDOW_CREATED,
  GUI_MSG_ATTACH_SURFACE,
  GUI_MSG_PRESENT,
  GUI_MSG_DESTROY_WINDOW,
  GUI_MSG_SET_TITLE,
  GUI_MSG_EVENT,
  GUI_MSG_ERROR
} gui_message_type_t;

typedef enum {
  GUI_EVENT_NONE = 0,
  GUI_EVENT_PAINT,
  GUI_EVENT_RESIZE,
  GUI_EVENT_FOCUS,
  GUI_EVENT_MOUSE_MOVE,
  GUI_EVENT_MOUSE_DOWN,
  GUI_EVENT_MOUSE_UP,
  GUI_EVENT_KEY_DOWN,
  GUI_EVENT_KEY_UP,
  GUI_EVENT_CLOSE
} gui_event_type_t;

typedef struct {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
} gui_rect_t;

typedef struct {
  uint32_t version;
  uint32_t type;
  uint32_t size;
  uint32_t request_id;
  uint32_t window_id;
  uint32_t reserved;
} gui_message_header_t;

typedef struct {
  gui_message_header_t header;
  union {
    struct {
      uint32_t width;
      uint32_t height;
      uint32_t flags;
      char title[GUI_TITLE_MAX + 1];
    } create;
    struct {
      uint32_t width;
      uint32_t height;
      uint32_t stride;
      uint32_t format;
    } surface;
    struct {
      gui_rect_t damage;
    } present;
    struct {
      char title[GUI_TITLE_MAX + 1];
    } set_title;
    struct {
      uint32_t event_type;
      int32_t x;
      int32_t y;
      uint32_t width;
      uint32_t height;
      uint32_t key;
      uint32_t scancode;
      uint32_t buttons;
      uint32_t modifiers;
      uint32_t focused;
    } event;
    struct {
      int32_t code;
    } error;
  } body;
} gui_wire_message_t;

#endif
