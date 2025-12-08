/* src/gui/wm.h - Window Manager pour ALOS
 * 
 * Gère les fenêtres style macOS avec :
 * - Barre de titre avec boutons rouge/jaune/vert
 * - Coins arrondis et ombres portées
 * - Drag & drop, redimensionnement
 * - Focus et Z-order
 */
#ifndef WM_H
#define WM_H

#include "gui_types.h"
#include "compositor.h"

/* Nombre maximum de fenêtres */
#define MAX_WINDOWS 64

/* Structure d'une fenêtre */
typedef struct window {
    uint32_t id;
    char title[256];
    rect_t bounds;              /* Position et taille totales */
    rect_t content_bounds;      /* Zone de contenu (sans titlebar) */
    uint32_t flags;
    bool is_focused;
    bool is_minimized;
    bool is_maximized;
    bool is_dragging;
    bool is_resizing;
    point_t drag_offset;
    rect_t restore_bounds;      /* Bounds avant maximisation */
    
    layer_t* layer;             /* Couche du compositeur */
    framebuffer_t* content_fb;  /* Buffer pour le contenu */
    
    void* user_data;
    void (*on_draw)(struct window* win);
    void (*on_close)(struct window* win);
    void (*on_resize)(struct window* win, uint32_t w, uint32_t h);
    void (*on_focus)(struct window* win, bool focused);
    
    struct window* next;
} window_t;

/* Initialise le window manager */
int wm_init(void);

/* Libère les ressources */
void wm_shutdown(void);

/* Création et destruction de fenêtres */
window_t* wm_create_window(rect_t bounds, const char* title, uint32_t flags);
void wm_destroy_window(window_t* win);

/* Gestion du focus */
void wm_focus_window(window_t* win);
window_t* wm_get_focused_window(void);

/* Manipulation des fenêtres */
void wm_move_window(window_t* win, int32_t x, int32_t y);
void wm_resize_window(window_t* win, uint32_t width, uint32_t height);
void wm_minimize_window(window_t* win);
void wm_maximize_window(window_t* win);
void wm_restore_window(window_t* win);
void wm_close_window(window_t* win);

/* Dessin */
void wm_draw_window(window_t* win);
void wm_draw_all(void);
void wm_invalidate_window(window_t* win);

/* Événements souris */
void wm_handle_mouse_move(point_t pos);
void wm_handle_mouse_down(point_t pos, mouse_button_t button);
void wm_handle_mouse_up(point_t pos, mouse_button_t button);

/* Recherche */
window_t* wm_find_window_at(point_t pos);
window_t* wm_get_window_by_id(uint32_t id);

/* Itération */
window_t* wm_get_first_window(void);

#endif /* WM_H */
