/* src/gui/menubar.h - Barre de menu style macOS pour ALOS
 * 
 * Implémente la barre de menu supérieure avec :
 * - Logo ALOS à gauche
 * - Nom de l'application active
 * - Menus déroulants
 * - Icônes système à droite (horloge, etc.)
 */
#ifndef MENUBAR_H
#define MENUBAR_H

#include "gui_types.h"
#include "compositor.h"

/* Nombre maximum de menus */
#define MAX_MENUS 16
#define MAX_MENU_ITEMS 32

/* Structure d'un item de menu */
typedef struct {
    char label[64];
    char shortcut[16];          /* Ex: "Cmd+Q" */
    bool enabled;
    bool separator;             /* true = séparateur horizontal */
    void (*on_click)(void);
} menu_item_t;

/* Structure d'un menu */
typedef struct {
    char label[64];
    menu_item_t items[MAX_MENU_ITEMS];
    uint32_t item_count;
    bool is_open;
    rect_t bounds;              /* Position dans la menubar */
    rect_t dropdown_bounds;     /* Position du dropdown */
} menu_t;

/* Initialise la barre de menu */
int menubar_init(void);

/* Libère les ressources */
void menubar_shutdown(void);

/* Configuration */
void menubar_set_app_name(const char* name);
void menubar_set_app_icon(const uint32_t* icon, uint32_t size);

/* Gestion des menus */
menu_t* menubar_add_menu(const char* label);
void menubar_add_item(menu_t* menu, const char* label, const char* shortcut, void (*on_click)(void));
void menubar_add_separator(menu_t* menu);

/* Dessin */
void menubar_draw(void);

/* Événements */
void menubar_handle_mouse_move(point_t pos);
void menubar_handle_mouse_down(point_t pos);
void menubar_handle_mouse_up(point_t pos);

/* Horloge système */
void menubar_set_time(uint8_t hour, uint8_t minute);
void menubar_update_time(void);

/* Récupère la couche de la menubar */
layer_t* menubar_get_layer(void);

#endif /* MENUBAR_H */
