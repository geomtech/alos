/* src/gui/gui.c - Implémentation du point d'entrée GUI */

#include "gui.h"
#include "ssfn_render.h"
#include <stdlib.h>
#include <string.h>

/* État global */
static gui_state_t g_state = GUI_STATE_UNINITIALIZED;
static bool g_quit_requested = false;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/framebuffer.h>
#include <sys/syscall.h>

#include "compositor.h"
#include "dock.h"
#include "events.h"
#include "gui.h"
#include "menubar.h"
#include "render.h"
#include "wm.h"
#include "components/component.h"
#include "components/label.h"
#include "components/button.h"
#include "components/panel.h"

/* Globals */
uint32_t g_screen_width = 0;
uint32_t g_screen_height = 0;
bool g_gui_running = true;

/* Position du curseur souris */
static int32_t g_mouse_x = 0;
static int32_t g_mouse_y = 0;
static bool g_mouse_visible = true;
static bool g_needs_redraw = false;

/* Forward declarations */
void gui_process_event(input_event_t *event);
window_t *gui_create_components_test_window(void);

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  /* Direct write to see if we reach main() - no malloc involved */
  const char *msg = "GUI: Entered main()\n";
  syscall3(SYS_WRITE, 1, (long)msg, 20); /* SYS_WRITE=4, stdout, msg, len */

  /* Test heap/brk before doing anything else */
  void *current_brk = (void *)syscall1(120, 0); /* SYS_BRK = 120 */

  /* Try to expand heap by one page */
  void *new_brk = (void *)((uint64_t)current_brk + 4096);
  void *result_brk = (void *)syscall1(120, (long)new_brk);

  /* Direct write to confirm brk worked */
  if (result_brk == new_brk) {
    const char *msg2 = "GUI: Heap test OK\n";
    syscall3(SYS_WRITE, 1, (long)msg2, 18);
  } else {
    const char *msg2 = "GUI: Heap test FAILED\n";
    syscall3(SYS_WRITE, 1, (long)msg2, 22);
  }

  printf("Starting ALOS GUI (Userland)...\n");

  /* 1. Get Framebuffer Info */
  const char *msg3 = "GUI: Getting framebuffer...\n";
  syscall3(SYS_WRITE, 1, (long)msg3, 28);

  framebuffer_info_t fb_info;
  long fb_result = syscall1(SYS_GET_FRAMEBUFFER, (long)&fb_info);

  if (fb_result != 0) {
    const char *msg4 = "GUI: Framebuffer syscall FAILED\n";
    syscall3(SYS_WRITE, 1, (long)msg4, 32);
    return 1;
  }

  const char *msg5 = "GUI: Framebuffer OK\n";
  syscall3(SYS_WRITE, 1, (long)msg5, 20);

  printf("Framebuffer: %dx%d %d bpp at %lx\n", fb_info.width, fb_info.height,
         fb_info.bpp, fb_info.addr);

  g_screen_width = fb_info.width;
  g_screen_height = fb_info.height;

  /* 2. Initialize Renderer */
  /* Note: render_init now takes framebuffer_info_t* or we adapt it */
  /* Original render_init took struct limine_framebuffer* */
  /* We expect render.c to be refactored to accept raw userland pointers */

  printf("Calling render_init_user...\n");
  int render_result = render_init_user(&fb_info);
  printf("render_init_user returned: %d\n", render_result);

  if (render_result != 0) {
    printf("Error: Failed to initialize renderer.\n");
    return 1;
  }

  printf("Renderer initialized successfully\n");

  /* Initialise les polices */
  printf("Initializing fonts...\n");
  font_init();

  /* Initialise SSFN avec Unifont (support UTF-8) */
  printf("Initializing SSFN...\n");
  ssfn_init();

  /* Initialise le compositeur AVANT de l'utiliser */
  printf("Getting active framebuffer...\n");
  framebuffer_t *active_fb = render_get_active_buffer();
  printf("Active FB: %p\n", active_fb);

  if (active_fb == NULL) {
    printf("Error: active_fb is NULL!\n");
    return 1;
  }

  printf("Active FB pixels: %p, width: %u, height: %u\n", active_fb->pixels,
         active_fb->width, active_fb->height);

  printf("Calling compositor_init...\n");
  if (compositor_init(active_fb) != 0) {
    printf("Error: Failed to initialize compositor.\\n");
    return 1;
  }

  printf("Compositor initialized successfully\n");

  /* 3. Setup WM and UI */
  compositor_set_background_gradient(rgba(30, 80, 140, 255),   /* Bleu foncé */
                                     rgba(100, 160, 220, 255), /* Bleu clair */
                                     GRADIENT_VERTICAL);

  if (wm_init() != 0) {
    printf("Error: Failed to init WM.\n");
    return 1;
  }

  if (menubar_init() != 0) {
    printf("Error: Failed to init menubar.\n");
    return 1;
  }

  if (dock_init() != 0) {
    printf("Error: Failed to init dock.\n");
    return 1;
  }

  if (events_init() != 0) {
    printf("Error: Failed to init events.\n");
    return 1;
  }

  g_state = GUI_STATE_RUNNING;
  g_quit_requested = false;

  gui_setup_demo_menus();
  gui_setup_demo_dock();

  /* Fenêtre de test des composants */
  gui_create_components_test_window();

  /* Initial render */
  gui_render_full();

  /* 4. Event Loop */
  input_event_t event;
  while (g_gui_running && !g_quit_requested) {
    /* Poll for events */
    int res = syscall1(SYS_GET_EVENT, (long)&event);
    if (res == 1) {
      /* Process event */
      gui_process_event(&event);
      
      /* Cursor is now drawn directly to front buffer in gui_render(),
       * so NO render_flip() needed for mouse moves! */
      gui_render();
    } else {
      /* No event, yield CPU */
      syscall1(SYS_SLEEP, 1);
    }
  }

  gui_shutdown();
  return 0;
}

/* Traite un événement système et l'injecte dans le système GUI */
void gui_process_event(input_event_t *event) {
  if (!event)
    return;

  switch (event->type) {
  case INPUT_EVENT_MOUSE_MOVE:
    /* Use data.mouse.x and data.mouse.y for absolute position */
    events_mouse_move(event->data.mouse.x, event->data.mouse.y);
    /* Update local cursor pos for drawing */
    g_mouse_x = event->data.mouse.x;
    g_mouse_y = event->data.mouse.y;
    break;

  case INPUT_EVENT_MOUSE_BUTTON:
    /* data.mouse.buttons = button mask (1=L, 2=R, 4=M) */
    /* Ignore special buttons (like forward/back on gaming mice) */
    {
      uint32_t buttons = event->data.mouse.buttons & 0x07; /* Only L, R, M */
      if (buttons != 0) {
        events_mouse_button((mouse_button_t)buttons,
                            event->data.mouse.dx != 0); /* dx used as pressed flag */
      }
    }
    break;

  case INPUT_EVENT_MOUSE_SCROLL:
    /* data.mouse.dy = scroll delta */
    events_mouse_scroll(event->data.mouse.dy);
    break;

  case INPUT_EVENT_KEY_PRESS:
    /* data.key.scancode = scancode, data.key.key = character */
    events_key((uint8_t)event->data.key.scancode, (char)event->data.key.key, true,
               MOD_NONE /* TODO: track mods */);
    break;

  case INPUT_EVENT_KEY_RELEASE:
    events_key((uint8_t)event->data.key.scancode, (char)event->data.key.key, false,
               MOD_NONE);
    break;
  }
}

/* Dimensions du curseur */
#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19

/* Déclaration forward du curseur */
static void draw_cursor(int32_t x, int32_t y);

void gui_shutdown(void) {
  g_state = GUI_STATE_SHUTDOWN;

  events_shutdown();
  dock_shutdown();
  menubar_shutdown();
  wm_shutdown();
  compositor_shutdown();

  g_state = GUI_STATE_UNINITIALIZED;
}

gui_state_t gui_get_state(void) { return g_state; }

void gui_pause(void) {
  if (g_state == GUI_STATE_RUNNING) {
    g_state = GUI_STATE_PAUSED;
  }
}

void gui_resume(void) {
  if (g_state == GUI_STATE_PAUSED) {
    g_state = GUI_STATE_RUNNING;
  }
}

void gui_process_events(void) { events_process(); }

void gui_update(float delta_time) {
  dock_update(delta_time);
  /* TODO: autres animations */
}

/* Sauvegarde de la zone sous le curseur pour restauration rapide */
static uint32_t g_cursor_save[CURSOR_WIDTH * CURSOR_HEIGHT];
static int32_t g_cursor_save_x = -1;
static int32_t g_cursor_save_y = -1;

/* Helper: inline min/max for cursor ops */
static inline int32_t cursor_max(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t cursor_min(int32_t a, int32_t b) { return a < b ? a : b; }

/* IMPORTANT: Cursor operations work directly on FRONT buffer
 * to avoid full 3MB memcpy on every mouse move */

/* Sauvegarde les pixels sous le curseur (depuis le FRONT buffer) */
static void save_cursor_background(int32_t x, int32_t y) {
  framebuffer_t *fb = render_get_framebuffer(); /* Front buffer! */
  if (!fb || !fb->pixels) return;
  
  uint32_t pitch_pixels = fb->pitch / 4;
  
  for (int32_t cy = 0; cy < CURSOR_HEIGHT; cy++) {
    int32_t py = y + cy;
    if (py < 0 || py >= (int32_t)g_screen_height) {
      /* Clear this row in save buffer */
      memset(&g_cursor_save[cy * CURSOR_WIDTH], 0, CURSOR_WIDTH * sizeof(uint32_t));
      continue;
    }
    
    int32_t x_start = cursor_max(x, 0);
    int32_t x_end = cursor_min(x + CURSOR_WIDTH, (int32_t)g_screen_width);
    
    if (x_start >= x_end) {
      memset(&g_cursor_save[cy * CURSOR_WIDTH], 0, CURSOR_WIDTH * sizeof(uint32_t));
      continue;
    }
    
    /* Copy row from framebuffer to save buffer */
    uint32_t *src = fb->pixels + py * pitch_pixels + x_start;
    uint32_t *dst = &g_cursor_save[cy * CURSOR_WIDTH + (x_start - x)];
    memcpy(dst, src, (x_end - x_start) * sizeof(uint32_t));
  }
  g_cursor_save_x = x;
  g_cursor_save_y = y;
}

/* Restaure les pixels sous le curseur (vers le FRONT buffer) */
static void restore_cursor_background(void) {
  if (g_cursor_save_x < 0)
    return;

  framebuffer_t *fb = render_get_framebuffer(); /* Front buffer! */
  if (!fb || !fb->pixels) return;
  
  uint32_t pitch_pixels = fb->pitch / 4;
  
  for (int32_t cy = 0; cy < CURSOR_HEIGHT; cy++) {
    int32_t py = g_cursor_save_y + cy;
    if (py < 0 || py >= (int32_t)g_screen_height) continue;
    
    int32_t x_start = cursor_max(g_cursor_save_x, 0);
    int32_t x_end = cursor_min(g_cursor_save_x + CURSOR_WIDTH, (int32_t)g_screen_width);
    
    if (x_start >= x_end) continue;
    
    /* Copy row from save buffer back to framebuffer */
    uint32_t *src = &g_cursor_save[cy * CURSOR_WIDTH + (x_start - g_cursor_save_x)];
    uint32_t *dst = fb->pixels + py * pitch_pixels + x_start;
    memcpy(dst, src, (x_end - x_start) * sizeof(uint32_t));
  }
}

/* Rendu complet de l'interface (appelé une seule fois au démarrage) */
void gui_render_full(void) {
  if (g_state != GUI_STATE_RUNNING)
    return;

  /* Force le rendu de tout l'écran */
  rect_t full_screen = {0, 0, g_screen_width, g_screen_height};
  compositor_invalidate_rect(full_screen);

  /* Rendu du compositeur (fond + couches) */
  compositor_render();

  /* Rendu de la menubar */
  menubar_draw();

  /* Rendu des fenêtres */
  wm_draw_all();

  /* Rendu du dock */
  dock_draw();

  /* Sauvegarde le fond sous le curseur puis dessine */
  save_cursor_background(g_mouse_x, g_mouse_y);
  draw_cursor(g_mouse_x, g_mouse_y);

  render_flip();
}

void gui_render(void) {
  if (g_state != GUI_STATE_RUNNING)
    return;

  /* Restaure le fond sous l'ancien curseur */
  restore_cursor_background();

  /* Traite les événements en attente */
  events_process();

  /* Sauvegarde le fond sous le nouveau curseur */
  save_cursor_background(g_mouse_x, g_mouse_y);

  /* Dessine le curseur à la nouvelle position */
  draw_cursor(g_mouse_x, g_mouse_y);

  g_needs_redraw = false;
}

void gui_request_quit(void) { g_quit_requested = true; }

void gui_set_wallpaper_color(uint32_t color) {
  compositor_set_background_color(color);
}

void gui_set_wallpaper_gradient(rgba_t color1, rgba_t color2,
                                gradient_direction_t dir) {
  compositor_set_background_gradient(color1, color2, dir);
}

/* Callback de dessin pour la fenêtre de démo */
static void demo_window_draw(window_t *win) {
  if (!win)
    return;

  /* Fond de contenu */
  draw_rect(win->content_bounds, COLOR_WINDOW_BG);

  int32_t x = win->content_bounds.x + 20;
  int32_t y = win->content_bounds.y + 20;

  /* Utiliser le renderer scalable si disponible (police plus fine) */
  if (ssfn_scalable_available()) {
    /* Titre en 14px */
    ssfn_render_text_size(x, y, 14, COLOR_TEXT_PRIMARY,
                          "Bienvenue dans ALOS GUI!");

    y += 20;
    ssfn_render_text_size(x, y, 12, 0xFF666666,
                          "Système d'exploitation éducatif");

    y += 18;
    ssfn_render_text_size(x, y, 11, 0xFF666666,
                          "Fonctionnalités: réseau, fichiers, GUI");

    y += 24;
    ssfn_render_text_size(x, y, 12, COLOR_TEXT_PRIMARY,
                          "Support UTF-8 complet:");

    y += 16;
    ssfn_render_text_size(x + 10, y, 11, 0xFF444444, "• Français: àéèêëïôùûç");
    y += 14;
    ssfn_render_text_size(x + 10, y, 11, 0xFF444444, "• Deutsch: äöüß");
    y += 14;
    ssfn_render_text_size(x + 10, y, 11, 0xFF444444, "• 日本語: ひらがな");
    y += 14;
    ssfn_render_text_size(x + 10, y, 11, 0xFF444444, "• Русский: Привет");
  } else if (ssfn_is_initialized()) {
    /* Fallback sur le renderer bitmap 16x16 */
    ssfn_set_fg(COLOR_TEXT_PRIMARY);
    ssfn_print_at(x, y, "Bienvenue dans ALOS GUI!");
    y += 20;
    ssfn_set_fg(0xFF666666);
    ssfn_print_at(x, y, "UTF-8: àéèêëïôùûç 日本語");
  }

  y += 24;

  /* Bouton */
  rect_t btn = {win->content_bounds.x + 20, y, 120, 28};
  draw_rounded_rect(btn, 6, COLOR_MACOS_BLUE);
  if (ssfn_scalable_available()) {
    ssfn_render_text_size(btn.x + 24, btn.y + 7, 12, 0xFFFFFFFF, "Démarrer ▶");
  }

  y += 40;

  /* Barre de progression */
  rect_t progress_bg = {win->content_bounds.x + 20, y, 200, 6};
  draw_rounded_rect(progress_bg, 3, COLOR_GRAY_2);
  rect_t progress_fg = {win->content_bounds.x + 20, y, 140, 6};
  draw_rounded_rect(progress_fg, 3, COLOR_MACOS_BLUE);
}

/* Callback pour le bouton de test */
static void test_button_clicked(gui_button_t* button) {
  (void)button;
  printf("Bouton cliqué!\n");
}

/* Callback de dessin pour la fenêtre de test des composants */
static void components_test_window_draw(window_t *win)
{
  if (!win)
    return;

  /* Dessiner le fond standard - c'est tout pour l'instant */
  draw_rect(win->content_bounds, COLOR_WINDOW_BG);
}

window_t *gui_create_components_test_window(void)
{
  rect_t bounds = {200, 150, 500, 400};
  window_t *win = wm_create_window(bounds, "Test Composants UI", WINDOW_STYLE_DEFAULT);

  if (!win)
    return NULL;

  /* --- 1. Création du Panel Principal (Root) --- */
  /* Les bounds doivent être relatifs à la fenêtre (0,0 = coin supérieur gauche du content area) */
  gui_panel_t *panel = panel_create((rect_t){0, 0, win->content_bounds.width, win->content_bounds.height});
  if (!panel) {
    wm_destroy_window(win);
    return NULL;
  }
  panel_set_bg_color(panel, rgba(240, 240, 245, 255));

  /* --- 2. Création des Enfants --- */
  gui_label_t *title_label = label_create((rect_t){20, 20, 460, 30},
                                          "Démonstration des Composants",
                                          rgba(50, 50, 50, 255));
  if (title_label)
    label_set_align(title_label, LABEL_ALIGN_CENTER);

  gui_button_t *btn_blue = button_create((rect_t){50, 80, 120, 35}, "Confirmer");
  if (btn_blue) {
    button_set_bg_color(btn_blue, BUTTON_STATE_NORMAL, rgba(0, 122, 255, 255));
    button_set_text_color(btn_blue, rgba(255, 255, 255, 255));
    button_set_on_click(btn_blue, test_button_clicked);
  }

  gui_button_t *btn_red = button_create((rect_t){190, 80, 120, 35}, "Annuler");
  if (btn_red) {
    button_set_bg_color(btn_red, BUTTON_STATE_NORMAL, rgba(255, 59, 48, 255));
    button_set_text_color(btn_red, rgba(255, 255, 255, 255));
  }

  gui_panel_t *sub_panel = panel_create((rect_t){50, 150, 400, 100});
  if (sub_panel) {
    panel_set_bg_color(sub_panel, rgba(255, 255, 255, 255));
    panel_set_border(sub_panel, rgba(200, 200, 200, 255), 1);
    panel_set_shadow(sub_panel, true);
  }

  gui_label_t *sub_label = label_create((rect_t){10, 10, 380, 20},
                                        "Je suis dans un sous-panel",
                                        rgba(100, 100, 100, 255));
  if (sub_label)
    label_set_align(sub_label, LABEL_ALIGN_CENTER);

  gui_button_t *sub_btn = button_create((rect_t){140, 50, 120, 30}, "Click Me");

  /* --- 3. Construction de l'Arbre (Hierarchy) --- */

  /* Remplissage du sous-panel */
  if (sub_panel) {
    if (sub_label) component_add_child((gui_component_t *)sub_panel, (gui_component_t *)sub_label);
    if (sub_btn) component_add_child((gui_component_t *)sub_panel, (gui_component_t *)sub_btn);
  }

  /* Remplissage du panel principal */
  if (title_label) component_add_child((gui_component_t *)panel, (gui_component_t *)title_label);
  if (btn_blue) component_add_child((gui_component_t *)panel, (gui_component_t *)btn_blue);
  if (btn_red) component_add_child((gui_component_t *)panel, (gui_component_t *)btn_red);
  if (sub_panel) component_add_child((gui_component_t *)panel, (gui_component_t *)sub_panel);

  /* --- 4. Attachement à la fenêtre via la nouvelle API --- */
  wm_set_root_component(win, (gui_component_t *)panel);

  return win;
}

void gui_setup_demo_dock(void) {
  /* Ajoute quelques applications de démo */
  dock_item_t *finder = dock_add_app("Finder", NULL);
  if (finder)
    finder->is_running = true;

  dock_add_app("Terminal", NULL);
  dock_add_app("Safari", NULL);
  dock_add_app("Mail", NULL);
  dock_add_app("Music", NULL);
  dock_add_app("Photos", NULL);
  dock_add_app("Settings", NULL);
}

/* Callbacks pour les menus */
static void menu_about(void) {
  gui_create_components_test_window();
}

static void menu_quit(void) { gui_request_quit(); }

void gui_setup_demo_menus(void) {
  menubar_set_app_name("Finder");

  /* Menu ALOS (Apple) */
  menu_t *alos_menu = menubar_add_menu("ALOS");
  if (alos_menu) {
    menubar_add_item(alos_menu, "A propos d'ALOS", NULL, menu_about);
    menubar_add_separator(alos_menu);
    menubar_add_item(alos_menu, "Preferences...", "Cmd+,", NULL);
    menubar_add_separator(alos_menu);
    menubar_add_item(alos_menu, "Quitter", "Cmd+Q", menu_quit);
  }

  /* Menu File */
  menu_t *file_menu = menubar_add_menu("File");
  if (file_menu) {
    menubar_add_item(file_menu, "Nouvelle fenetre", "Cmd+N", NULL);
    menubar_add_item(file_menu, "Ouvrir...", "Cmd+O", NULL);
    menubar_add_separator(file_menu);
    menubar_add_item(file_menu, "Fermer", "Cmd+W", NULL);
  }

  /* Menu Edit */
  menu_t *edit_menu = menubar_add_menu("Edit");
  if (edit_menu) {
    menubar_add_item(edit_menu, "Annuler", "Cmd+Z", NULL);
    menubar_add_item(edit_menu, "Retablir", "Cmd+Shift+Z", NULL);
    menubar_add_separator(edit_menu);
    menubar_add_item(edit_menu, "Couper", "Cmd+X", NULL);
    menubar_add_item(edit_menu, "Copier", "Cmd+C", NULL);
    menubar_add_item(edit_menu, "Coller", "Cmd+V", NULL);
  }

  /* Menu View */
  menu_t *view_menu = menubar_add_menu("View");
  if (view_menu) {
    menubar_add_item(view_menu, "Icones", "Cmd+1", NULL);
    menubar_add_item(view_menu, "Liste", "Cmd+2", NULL);
    menubar_add_item(view_menu, "Colonnes", "Cmd+3", NULL);
  }

  /* Menu Window */
  menu_t *window_menu = menubar_add_menu("Window");
  if (window_menu) {
    menubar_add_item(window_menu, "Minimiser", "Cmd+M", NULL);
    menubar_add_item(window_menu, "Zoom", NULL, NULL);
    menubar_add_separator(window_menu);
    menubar_add_item(window_menu, "Tout au premier plan", NULL, NULL);
  }

  /* Menu Help */
  menu_t *help_menu = menubar_add_menu("Help");
  if (help_menu) {
    menubar_add_item(help_menu, "Aide ALOS", NULL, NULL);
  }

  /* Configure l'horloge */
  menubar_set_time(14, 30);
}

/* ============================================================================
 * CURSEUR SOURIS
 * ============================================================================
 */

/* Curseur souris simple (flèche 12x19 pixels) */
static const uint8_t cursor_data[19][12] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0}, {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0}, {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0}, {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0}, {1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1},
    {1, 2, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0}, {1, 2, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0},
    {1, 2, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0}, {1, 1, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0},
    {1, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0},
};

/* Dessine le curseur DIRECTEMENT sur le front buffer (évite le flip complet) */
static void draw_cursor(int32_t x, int32_t y) {
  if (!g_mouse_visible)
    return;

  framebuffer_t *fb = render_get_framebuffer(); /* Front buffer! */
  if (!fb || !fb->pixels) return;
  
  uint32_t pitch_pixels = fb->pitch / 4;

  for (int32_t cy = 0; cy < CURSOR_HEIGHT; cy++) {
    int32_t py = y + cy;
    if (py < 0 || py >= (int32_t)g_screen_height) continue;
    
    for (int32_t cx = 0; cx < CURSOR_WIDTH; cx++) {
      uint8_t pixel = cursor_data[cy][cx];
      if (pixel == 0)
        continue; /* Transparent */

      int32_t px = x + cx;
      if (px < 0 || px >= (int32_t)g_screen_width) continue;

      /* Write directly to front buffer */
      uint32_t color = (pixel == 1) ? 0xFF000000 : 0xFFFFFFFF;
      fb->pixels[py * pitch_pixels + px] = color;
    }
  }
}

/* ============================================================================
 * CALLBACK SOURIS
 * ============================================================================
 */
