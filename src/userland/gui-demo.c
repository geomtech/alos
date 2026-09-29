/* src/userland/gui-demo.c - Application graphique multiprocessus de test */
#include <gui.h>
#include <stdint.h>

static uint32_t accent = 0xFF2E86C1;
static int focused = 1;
static int32_t marker_x = 40;
static int32_t marker_y = 60;

static void draw_demo(gui_window_t *window) {
  uint32_t width = gui_window_width(window);
  uint32_t height = gui_window_height(window);
  gui_clear(window, focused ? 0xFFF4F7FA : 0xFFE4E8EC);

  gui_fill_rect(window, (gui_rect_t){0, 0, width, 48}, accent);
  gui_fill_rect(window, (gui_rect_t){20, 72, width > 40 ? width - 40 : 0, 54},
                0xFFDCEAF5);
  gui_draw_rect(window,
                (gui_rect_t){20, 72, width > 40 ? width - 40 : 0, 54},
                0xFF416985);

  if (marker_x < 8) marker_x = 8;
  if (marker_y < 8) marker_y = 8;
  if ((uint32_t)marker_x + 24 > width)
    marker_x = width > 32 ? (int32_t)width - 32 : 0;
  if ((uint32_t)marker_y + 24 > height)
    marker_y = height > 32 ? (int32_t)height - 32 : 0;
  gui_fill_rect(window, (gui_rect_t){marker_x, marker_y, 24, 24},
                0xFFE67E22);
  gui_draw_rect(window, (gui_rect_t){marker_x, marker_y, 24, 24},
                0xFF8A4311);

  gui_rect_t full = {0, 0, width, height};
  (void)gui_present(window, &full);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  gui_window_t *window =
      gui_create_window("GUI Demo", 480, 300, GUI_WINDOW_DEFAULT);
  if (window == 0) return 1;

  draw_demo(window);
  for (;;) {
    gui_event_t event;
    int result = gui_wait_event(window, &event, 100);
    if (result < 0) break;
    if (result == 0) continue;

    switch (event.type) {
    case GUI_EVENT_PAINT:
    case GUI_EVENT_RESIZE:
      draw_demo(window);
      break;
    case GUI_EVENT_FOCUS:
      focused = event.focused;
      draw_demo(window);
      break;
    case GUI_EVENT_MOUSE_DOWN:
      marker_x = event.x - 12;
      marker_y = event.y - 12;
      accent = 0xFF27AE60;
      draw_demo(window);
      break;
    case GUI_EVENT_MOUSE_MOVE:
      if ((event.buttons & 1U) != 0) {
        marker_x = event.x - 12;
        marker_y = event.y - 12;
        draw_demo(window);
      }
      break;
    case GUI_EVENT_KEY_DOWN:
      accent = 0xFF000000U | ((event.key * 73U) & 0x00FFFFFFU);
      draw_demo(window);
      break;
    case GUI_EVENT_CLOSE:
      gui_destroy_window(window);
      return 0;
    default:
      break;
    }
  }

  gui_destroy_window(window);
  return 1;
}
