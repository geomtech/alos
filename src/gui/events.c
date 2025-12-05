/* src/gui/events.c - Implémentation du système d'événements */

#include "events.h"
#include "wm.h"
#include "menubar.h"
#include "dock.h"
#include "compositor.h"
#include "../include/string.h"

/* File d'événements circulaire */
static event_t g_event_queue[EVENT_QUEUE_SIZE];
static uint32_t g_queue_head = 0;
static uint32_t g_queue_tail = 0;
static uint32_t g_queue_count = 0;

/* État de la souris */
static point_t g_mouse_pos = {0, 0};
static mouse_button_t g_mouse_buttons = MOUSE_BUTTON_NONE;

/* État du clavier */
static key_modifier_t g_modifiers = MOD_NONE;

/* Timestamp */
static uint32_t g_timestamp = 0;

int events_init(void) {
    g_queue_head = 0;
    g_queue_tail = 0;
    g_queue_count = 0;
    g_mouse_pos.x = 0;
    g_mouse_pos.y = 0;
    g_mouse_buttons = MOUSE_BUTTON_NONE;
    g_modifiers = MOD_NONE;
    g_timestamp = 0;
    return 0;
}

void events_shutdown(void) {
    g_queue_count = 0;
}

void events_push(event_t* event) {
    if (!event || g_queue_count >= EVENT_QUEUE_SIZE) return;
    
    event->timestamp = g_timestamp++;
    g_event_queue[g_queue_tail] = *event;
    g_queue_tail = (g_queue_tail + 1) % EVENT_QUEUE_SIZE;
    g_queue_count++;
}

event_t* events_pop(void) {
    if (g_queue_count == 0) return NULL;
    
    event_t* event = &g_event_queue[g_queue_head];
    g_queue_head = (g_queue_head + 1) % EVENT_QUEUE_SIZE;
    g_queue_count--;
    
    return event;
}

bool events_empty(void) {
    return g_queue_count == 0;
}

void events_process(void) {
    while (!events_empty()) {
        event_t* event = events_pop();
        if (event) {
            events_dispatch(event);
        }
    }
}

void events_dispatch(event_t* event) {
    if (!event) return;
    
    switch (event->type) {
        case EVENT_MOUSE_MOVE:
            g_mouse_pos = event->mouse.position;
            
            /* Dispatch à la menubar d'abord */
            if (event->mouse.position.y < MENUBAR_HEIGHT) {
                menubar_handle_mouse_move(event->mouse.position);
            }
            /* Puis au dock */
            else if (point_in_rect(event->mouse.position, dock_get_bounds())) {
                dock_handle_mouse_move(event->mouse.position);
            }
            /* Puis au window manager */
            else {
                wm_handle_mouse_move(event->mouse.position);
            }
            
            /* Toujours mettre à jour le dock pour l'effet de grossissement */
            dock_handle_mouse_move(event->mouse.position);
            break;
            
        case EVENT_MOUSE_DOWN:
            g_mouse_buttons = (mouse_button_t)(g_mouse_buttons | event->mouse.button);
            
            /* Menubar */
            if (event->mouse.position.y < MENUBAR_HEIGHT) {
                menubar_handle_mouse_down(event->mouse.position);
            }
            /* Dock */
            else if (point_in_rect(event->mouse.position, dock_get_bounds())) {
                dock_handle_mouse_down(event->mouse.position);
            }
            /* Window manager */
            else {
                wm_handle_mouse_down(event->mouse.position, event->mouse.button);
            }
            break;
            
        case EVENT_MOUSE_UP:
            g_mouse_buttons = (mouse_button_t)(g_mouse_buttons & ~event->mouse.button);
            
            /* Menubar */
            menubar_handle_mouse_up(event->mouse.position);
            /* Dock */
            dock_handle_mouse_up(event->mouse.position);
            /* Window manager */
            wm_handle_mouse_up(event->mouse.position, event->mouse.button);
            break;
            
        case EVENT_KEY_DOWN:
            /* Met à jour les modificateurs */
            if (event->key.scancode == 0x2A || event->key.scancode == 0x36) {
                g_modifiers = (key_modifier_t)(g_modifiers | MOD_SHIFT);
            } else if (event->key.scancode == 0x1D) {
                g_modifiers = (key_modifier_t)(g_modifiers | MOD_CTRL);
            } else if (event->key.scancode == 0x38) {
                g_modifiers = (key_modifier_t)(g_modifiers | MOD_ALT);
            }
            
            /* TODO: dispatch aux fenêtres focusées */
            break;
            
        case EVENT_KEY_UP:
            /* Met à jour les modificateurs */
            if (event->key.scancode == 0x2A || event->key.scancode == 0x36) {
                g_modifiers = (key_modifier_t)(g_modifiers & ~MOD_SHIFT);
            } else if (event->key.scancode == 0x1D) {
                g_modifiers = (key_modifier_t)(g_modifiers & ~MOD_CTRL);
            } else if (event->key.scancode == 0x38) {
                g_modifiers = (key_modifier_t)(g_modifiers & ~MOD_ALT);
            }
            break;
            
        default:
            break;
    }
}

void events_mouse_move(int32_t x, int32_t y) {
    event_t event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_MOUSE_MOVE;
    event.mouse.position.x = x;
    event.mouse.position.y = y;
    event.mouse.button = MOUSE_BUTTON_NONE;
    events_push(&event);
}

void events_mouse_button(mouse_button_t button, bool pressed) {
    event_t event;
    memset(&event, 0, sizeof(event));
    event.type = pressed ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP;
    event.mouse.position = g_mouse_pos;
    event.mouse.button = button;
    events_push(&event);
}

void events_mouse_scroll(int32_t delta) {
    event_t event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_MOUSE_SCROLL;
    event.mouse.position = g_mouse_pos;
    event.mouse.scroll_delta = delta;
    events_push(&event);
}

void events_key(uint8_t scancode, char character, bool pressed, key_modifier_t mods) {
    event_t event;
    memset(&event, 0, sizeof(event));
    event.type = pressed ? EVENT_KEY_DOWN : EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.character = character;
    event.key.mods = mods;
    events_push(&event);
}

point_t events_get_mouse_pos(void) {
    return g_mouse_pos;
}

key_modifier_t events_get_modifiers(void) {
    return g_modifiers;
}
