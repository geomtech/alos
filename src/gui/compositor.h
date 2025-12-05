/* src/gui/compositor.h - Compositeur graphique pour ALOS
 * 
 * Gère le Z-order des couches, les dirty rectangles,
 * et le rendu final vers le framebuffer.
 */
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "gui_types.h"

/* Nombre maximum de couches */
#define MAX_LAYERS 32

/* Types de couches */
typedef enum {
    LAYER_BACKGROUND,   /* Fond d'écran */
    LAYER_DESKTOP,      /* Icônes du bureau */
    LAYER_WINDOW,       /* Fenêtres normales */
    LAYER_PANEL,        /* Panneaux (menu bar) */
    LAYER_DOCK,         /* Dock */
    LAYER_POPUP,        /* Menus popup */
    LAYER_OVERLAY       /* Overlays (notifications) */
} layer_type_t;

/* Structure d'une couche */
typedef struct layer {
    uint32_t id;
    layer_type_t type;
    rect_t bounds;
    framebuffer_t* buffer;      /* Buffer de la couche (peut être NULL) */
    bool visible;
    bool needs_redraw;
    uint8_t opacity;            /* 0-255 */
    void* user_data;
    void (*draw_callback)(struct layer* layer);  /* Fonction de dessin */
    struct layer* next;
} layer_t;

/* Initialise le compositeur */
int compositor_init(framebuffer_t* fb);

/* Libère les ressources */
void compositor_shutdown(void);

/* Gestion des couches */
layer_t* compositor_create_layer(layer_type_t type, rect_t bounds);
void compositor_destroy_layer(layer_t* layer);
void compositor_add_layer(layer_t* layer);
void compositor_remove_layer(layer_t* layer);
void compositor_raise_layer(layer_t* layer);
void compositor_lower_layer(layer_t* layer);

/* Dirty rectangles */
void compositor_invalidate_rect(rect_t rect);
void compositor_invalidate_layer(layer_t* layer);

/* Rendu */
void compositor_render(void);
void compositor_render_region(rect_t region);

/* Fond d'écran */
void compositor_set_background_color(uint32_t color);
void compositor_set_background_gradient(rgba_t color1, rgba_t color2, gradient_direction_t dir);

#endif /* COMPOSITOR_H */
