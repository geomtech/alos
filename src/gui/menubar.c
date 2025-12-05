/* src/gui/menubar.c - Implémentation de la barre de menu */

#include "menubar.h"
#include "render.h"
#include "font.h"
#include "../mm/kheap.h"
#include "../include/string.h"

/* État de la menubar */
static layer_t* g_menubar_layer = NULL;
static menu_t g_menus[MAX_MENUS];
static uint32_t g_menu_count = 0;
static int32_t g_hovered_menu = -1;
static int32_t g_open_menu = -1;
static int32_t g_hovered_item = -1;

/* Nom de l'application */
static char g_app_name[64] = "ALOS";

/* Horloge */
static uint8_t g_hour = 12;
static uint8_t g_minute = 0;
static char g_time_str[32] = "12:00";

/* Dimensions de l'écran */
static uint32_t g_screen_width = 0;

int menubar_init(void) {
    render_get_screen_size(&g_screen_width, NULL);
    
    /* Crée la couche de la menubar */
    rect_t bounds = {0, 0, g_screen_width, MENUBAR_HEIGHT};
    g_menubar_layer = compositor_create_layer(LAYER_PANEL, bounds);
    if (g_menubar_layer) {
        g_menubar_layer->draw_callback = NULL;  /* On dessine manuellement */
        compositor_add_layer(g_menubar_layer);
    }
    
    g_menu_count = 0;
    g_hovered_menu = -1;
    g_open_menu = -1;
    
    return 0;
}

void menubar_shutdown(void) {
    if (g_menubar_layer) {
        compositor_destroy_layer(g_menubar_layer);
        g_menubar_layer = NULL;
    }
    g_menu_count = 0;
}

void menubar_set_app_name(const char* name) {
    if (name) {
        strncpy(g_app_name, name, 63);
        g_app_name[63] = '\0';
    }
    if (g_menubar_layer) {
        compositor_invalidate_layer(g_menubar_layer);
    }
}

void menubar_set_app_icon(const uint32_t* icon, uint32_t size) {
    (void)icon; (void)size;
    /* TODO: stocker et afficher l'icône */
}

menu_t* menubar_add_menu(const char* label) {
    if (g_menu_count >= MAX_MENUS) return NULL;
    
    menu_t* menu = &g_menus[g_menu_count++];
    memset(menu, 0, sizeof(menu_t));
    
    if (label) {
        strncpy(menu->label, label, 63);
        menu->label[63] = '\0';
    }
    
    return menu;
}

void menubar_add_item(menu_t* menu, const char* label, const char* shortcut, void (*on_click)(void)) {
    if (!menu || menu->item_count >= MAX_MENU_ITEMS) return;
    
    menu_item_t* item = &menu->items[menu->item_count++];
    memset(item, 0, sizeof(menu_item_t));
    
    if (label) {
        strncpy(item->label, label, 63);
        item->label[63] = '\0';
    }
    if (shortcut) {
        strncpy(item->shortcut, shortcut, 15);
        item->shortcut[15] = '\0';
    }
    item->enabled = true;
    item->on_click = on_click;
}

void menubar_add_separator(menu_t* menu) {
    if (!menu || menu->item_count >= MAX_MENU_ITEMS) return;
    
    menu_item_t* item = &menu->items[menu->item_count++];
    memset(item, 0, sizeof(menu_item_t));
    item->separator = true;
}

void menubar_set_time(uint8_t hour, uint8_t minute) {
    g_hour = hour % 24;
    g_minute = minute % 60;
    
    /* Format HH:MM */
    g_time_str[0] = '0' + (g_hour / 10);
    g_time_str[1] = '0' + (g_hour % 10);
    g_time_str[2] = ':';
    g_time_str[3] = '0' + (g_minute / 10);
    g_time_str[4] = '0' + (g_minute % 10);
    g_time_str[5] = '\0';
    
    if (g_menubar_layer) {
        compositor_invalidate_layer(g_menubar_layer);
    }
}

void menubar_update_time(void) {
    /* TODO: lire l'heure système */
}

static void draw_menu_dropdown(menu_t* menu) {
    if (!menu || menu->item_count == 0) return;
    
    /* Calcule les dimensions du dropdown */
    uint32_t max_width = 150;
    for (uint32_t i = 0; i < menu->item_count; i++) {
        if (!menu->items[i].separator) {
            text_bounds_t tb = measure_text(menu->items[i].label, font_system);
            uint32_t w = tb.width + 40;  /* Marge pour le raccourci */
            if (menu->items[i].shortcut[0]) {
                text_bounds_t stb = measure_text(menu->items[i].shortcut, font_system);
                w += stb.width + 20;
            }
            if (w > max_width) max_width = w;
        }
    }
    
    uint32_t item_height = 24;
    uint32_t total_height = 8;  /* Padding */
    for (uint32_t i = 0; i < menu->item_count; i++) {
        total_height += menu->items[i].separator ? 9 : item_height;
    }
    total_height += 8;  /* Padding */
    
    menu->dropdown_bounds.x = menu->bounds.x;
    menu->dropdown_bounds.y = MENUBAR_HEIGHT;
    menu->dropdown_bounds.width = max_width;
    menu->dropdown_bounds.height = total_height;
    
    /* Ombre */
    shadow_params_t shadow = shadow_card();
    draw_shadow(menu->dropdown_bounds, 8, shadow);
    
    /* Fond du dropdown */
    draw_rounded_rect_alpha(menu->dropdown_bounds, 8, rgba(255, 255, 255, 245));
    
    /* Items */
    int32_t y = menu->dropdown_bounds.y + 8;
    for (uint32_t i = 0; i < menu->item_count; i++) {
        menu_item_t* item = &menu->items[i];
        
        if (item->separator) {
            /* Ligne de séparation */
            draw_hline(menu->dropdown_bounds.x + 8,
                      menu->dropdown_bounds.x + (int32_t)menu->dropdown_bounds.width - 8,
                      y + 4, COLOR_GRAY_2);
            y += 9;
        } else {
            /* Highlight si survolé */
            if ((int32_t)i == g_hovered_item) {
                rect_t highlight = {
                    menu->dropdown_bounds.x + 4,
                    y,
                    menu->dropdown_bounds.width - 8,
                    item_height
                };
                draw_rounded_rect(highlight, 4, COLOR_MACOS_BLUE);
            }
            
            /* Label */
            rgba_t text_color = ((int32_t)i == g_hovered_item) ? 
                rgba(255, 255, 255, 255) : u32_to_rgba(COLOR_TEXT_PRIMARY);
            if (!item->enabled) {
                text_color = u32_to_rgba(COLOR_GRAY_4);
            }
            
            draw_text_alpha(item->label, 
                           point_make(menu->dropdown_bounds.x + 12, y + 4),
                           font_system, text_color);
            
            /* Raccourci */
            if (item->shortcut[0]) {
                text_bounds_t stb = measure_text(item->shortcut, font_system);
                int32_t sx = menu->dropdown_bounds.x + (int32_t)menu->dropdown_bounds.width - 
                            (int32_t)stb.width - 12;
                rgba_t shortcut_color = ((int32_t)i == g_hovered_item) ?
                    rgba(255, 255, 255, 180) : u32_to_rgba(COLOR_GRAY_5);
                draw_text_alpha(item->shortcut, point_make(sx, y + 4), 
                               font_system, shortcut_color);
            }
            
            y += (int32_t)item_height;
        }
    }
}

void menubar_draw(void) {
    /* Fond semi-transparent avec effet vitre */
    rect_t bar = {0, 0, g_screen_width, MENUBAR_HEIGHT};
    draw_rect_alpha(bar, rgba(255, 255, 255, 220));
    
    /* Bordure inférieure subtile */
    draw_hline(0, (int32_t)g_screen_width - 1, MENUBAR_HEIGHT - 1, COLOR_GRAY_2);
    
    int32_t x = 12;
    
    /* Logo ALOS (simple texte pour l'instant) */
    draw_text_alpha("@", point_make(x, 6), font_system, rgba(0, 0, 0, 255));
    x += 20;
    
    /* Nom de l'application (en gras) */
    draw_text_alpha(g_app_name, point_make(x, 6), font_system, rgba(0, 0, 0, 255));
    text_bounds_t app_tb = measure_text(g_app_name, font_system);
    x += (int32_t)app_tb.width + 20;
    
    /* Menus */
    for (uint32_t i = 0; i < g_menu_count; i++) {
        menu_t* menu = &g_menus[i];
        text_bounds_t tb = measure_text(menu->label, font_system);
        
        menu->bounds.x = x - 8;
        menu->bounds.y = 0;
        menu->bounds.width = tb.width + 16;
        menu->bounds.height = MENUBAR_HEIGHT;
        
        /* Highlight si survolé ou ouvert */
        if ((int32_t)i == g_hovered_menu || (int32_t)i == g_open_menu) {
            draw_rounded_rect_alpha(menu->bounds, 4, rgba(0, 0, 0, 30));
        }
        
        rgba_t text_color = rgba(0, 0, 0, 255);
        draw_text_alpha(menu->label, point_make(x, 6), font_system, text_color);
        
        x += (int32_t)tb.width + 20;
    }
    
    /* Horloge à droite */
    text_bounds_t time_tb = measure_text(g_time_str, font_system);
    int32_t time_x = (int32_t)g_screen_width - (int32_t)time_tb.width - 12;
    draw_text_alpha(g_time_str, point_make(time_x, 6), font_system, rgba(0, 0, 0, 255));
    
    /* Dropdown si un menu est ouvert */
    if (g_open_menu >= 0 && g_open_menu < (int32_t)g_menu_count) {
        draw_menu_dropdown(&g_menus[g_open_menu]);
    }
}

static int32_t find_menu_at(point_t pos) {
    if (pos.y < 0 || pos.y >= MENUBAR_HEIGHT) return -1;
    
    for (uint32_t i = 0; i < g_menu_count; i++) {
        if (point_in_rect(pos, g_menus[i].bounds)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t find_item_at(menu_t* menu, point_t pos) {
    if (!menu || !point_in_rect(pos, menu->dropdown_bounds)) return -1;
    
    int32_t y = menu->dropdown_bounds.y + 8;
    uint32_t item_height = 24;
    
    for (uint32_t i = 0; i < menu->item_count; i++) {
        uint32_t h = menu->items[i].separator ? 9 : item_height;
        if (pos.y >= y && pos.y < y + (int32_t)h) {
            if (!menu->items[i].separator) {
                return (int32_t)i;
            }
            return -1;
        }
        y += (int32_t)h;
    }
    return -1;
}

void menubar_handle_mouse_move(point_t pos) {
    int32_t old_hovered = g_hovered_menu;
    int32_t old_item = g_hovered_item;
    
    g_hovered_menu = find_menu_at(pos);
    
    /* Si un menu est ouvert et on survole un autre, ouvre celui-ci */
    if (g_open_menu >= 0 && g_hovered_menu >= 0 && g_hovered_menu != g_open_menu) {
        g_open_menu = g_hovered_menu;
        g_hovered_item = -1;
    }
    
    /* Survol des items du dropdown */
    if (g_open_menu >= 0) {
        g_hovered_item = find_item_at(&g_menus[g_open_menu], pos);
    }
    
    if (old_hovered != g_hovered_menu || old_item != g_hovered_item) {
        compositor_invalidate_layer(g_menubar_layer);
    }
}

void menubar_handle_mouse_down(point_t pos) {
    int32_t menu_idx = find_menu_at(pos);
    
    if (menu_idx >= 0) {
        /* Toggle le menu */
        if (g_open_menu == menu_idx) {
            g_open_menu = -1;
        } else {
            g_open_menu = menu_idx;
        }
        g_hovered_item = -1;
        compositor_invalidate_layer(g_menubar_layer);
        return;
    }
    
    /* Clic sur un item du dropdown */
    if (g_open_menu >= 0) {
        menu_t* menu = &g_menus[g_open_menu];
        int32_t item_idx = find_item_at(menu, pos);
        
        if (item_idx >= 0) {
            menu_item_t* item = &menu->items[item_idx];
            if (item->enabled && item->on_click) {
                item->on_click();
            }
        }
        
        /* Ferme le menu */
        g_open_menu = -1;
        g_hovered_item = -1;
        compositor_invalidate_layer(g_menubar_layer);
    }
}

void menubar_handle_mouse_up(point_t pos) {
    (void)pos;
    /* Rien de spécial pour l'instant */
}

layer_t* menubar_get_layer(void) {
    return g_menubar_layer;
}
