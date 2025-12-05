/* src/gui/dock.h - Dock style macOS pour ALOS
 * 
 * Implémente le dock en bas de l'écran avec :
 * - Icônes d'applications
 * - Effet de grossissement au survol
 * - Indicateur d'application active
 * - Fond semi-transparent avec effet vitre
 */
#ifndef DOCK_H
#define DOCK_H

#include "gui_types.h"
#include "compositor.h"

/* Nombre maximum d'items dans le dock */
#define MAX_DOCK_ITEMS 32

/* Tailles */
#define DOCK_ICON_SIZE_BASE     50
#define DOCK_ICON_SIZE_MAX      75
#define DOCK_PADDING            8
#define DOCK_CORNER_RADIUS      16

/* Structure d'un item du dock */
typedef struct {
    char name[64];
    uint32_t icon[64 * 64];     /* Icône 64x64 RGBA */
    bool has_icon;
    bool is_running;            /* Point indicateur */
    bool is_bouncing;           /* Animation de rebond */
    float scale;                /* Échelle actuelle (1.0 - 1.5) */
    void (*on_click)(void);
} dock_item_t;

/* Initialise le dock */
int dock_init(void);

/* Libère les ressources */
void dock_shutdown(void);

/* Gestion des items */
dock_item_t* dock_add_app(const char* name, const uint32_t* icon);
void dock_remove_app(dock_item_t* item);
void dock_set_running(dock_item_t* item, bool running);
void dock_bounce(dock_item_t* item);

/* Dessin */
void dock_draw(void);

/* Événements */
void dock_handle_mouse_move(point_t pos);
void dock_handle_mouse_down(point_t pos);
void dock_handle_mouse_up(point_t pos);

/* Animation */
void dock_update(float delta_time);

/* Récupère la couche du dock */
layer_t* dock_get_layer(void);

/* Récupère les bounds du dock */
rect_t dock_get_bounds(void);

#endif /* DOCK_H */
