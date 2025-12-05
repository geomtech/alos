/* src/gui/dock.c - Implémentation du dock */

#include "dock.h"
#include "render.h"
#include "font.h"
#include "../mm/kheap.h"
#include "../include/string.h"

/* État du dock */
static layer_t* g_dock_layer = NULL;
static dock_item_t g_items[MAX_DOCK_ITEMS];
static uint32_t g_item_count = 0;
static int32_t g_hovered_item = -1;
static point_t g_mouse_pos = {0, 0};

/* Dimensions de l'écran */
static uint32_t g_screen_width = 0;
static uint32_t g_screen_height = 0;

/* Bounds calculés du dock */
static rect_t g_dock_bounds = {0, 0, 0, 0};

/* Calcule les bounds du dock */
static void calculate_dock_bounds(void) {
    if (g_item_count == 0) {
        g_dock_bounds.width = 100;
    } else {
        g_dock_bounds.width = g_item_count * (DOCK_ICON_SIZE_BASE + DOCK_PADDING) + DOCK_PADDING * 2;
    }
    g_dock_bounds.height = DOCK_HEIGHT;
    g_dock_bounds.x = ((int32_t)g_screen_width - (int32_t)g_dock_bounds.width) / 2;
    g_dock_bounds.y = (int32_t)g_screen_height - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM;
    
    if (g_dock_layer) {
        g_dock_layer->bounds = g_dock_bounds;
    }
}

int dock_init(void) {
    render_get_screen_size(&g_screen_width, &g_screen_height);
    
    g_item_count = 0;
    g_hovered_item = -1;
    
    /* Initialise les scales */
    for (uint32_t i = 0; i < MAX_DOCK_ITEMS; i++) {
        g_items[i].scale = 1.0f;
    }
    
    calculate_dock_bounds();
    
    /* Crée la couche du dock */
    g_dock_layer = compositor_create_layer(LAYER_DOCK, g_dock_bounds);
    if (g_dock_layer) {
        compositor_add_layer(g_dock_layer);
    }
    
    return 0;
}

void dock_shutdown(void) {
    if (g_dock_layer) {
        compositor_destroy_layer(g_dock_layer);
        g_dock_layer = NULL;
    }
    g_item_count = 0;
}

dock_item_t* dock_add_app(const char* name, const uint32_t* icon) {
    if (g_item_count >= MAX_DOCK_ITEMS) return NULL;
    
    dock_item_t* item = &g_items[g_item_count++];
    memset(item, 0, sizeof(dock_item_t));
    
    if (name) {
        strncpy(item->name, name, 63);
        item->name[63] = '\0';
    }
    
    if (icon) {
        memcpy(item->icon, icon, 64 * 64 * sizeof(uint32_t));
        item->has_icon = true;
    }
    
    item->scale = 1.0f;
    
    calculate_dock_bounds();
    compositor_invalidate_layer(g_dock_layer);
    
    return item;
}

void dock_remove_app(dock_item_t* item) {
    if (!item) return;
    
    /* Trouve l'index */
    int32_t idx = -1;
    for (uint32_t i = 0; i < g_item_count; i++) {
        if (&g_items[i] == item) {
            idx = (int32_t)i;
            break;
        }
    }
    
    if (idx < 0) return;
    
    /* Décale les items suivants */
    for (uint32_t i = (uint32_t)idx; i < g_item_count - 1; i++) {
        g_items[i] = g_items[i + 1];
    }
    g_item_count--;
    
    calculate_dock_bounds();
    compositor_invalidate_layer(g_dock_layer);
}

void dock_set_running(dock_item_t* item, bool running) {
    if (item) {
        item->is_running = running;
        compositor_invalidate_layer(g_dock_layer);
    }
}

void dock_bounce(dock_item_t* item) {
    if (item) {
        item->is_bouncing = true;
        /* TODO: animation */
    }
}

/* Dessine une icône placeholder (carré coloré avec initiale) */
static void draw_placeholder_icon(int32_t x, int32_t y, uint32_t size, const char* name, uint32_t color) {
    rect_t icon_rect = {x, y, size, size};
    draw_rounded_rect(icon_rect, 12, color);
    
    /* Initiale centrée */
    if (name && name[0]) {
        char initial[2] = {name[0], '\0'};
        text_bounds_t tb = measure_text(initial, font_system);
        int32_t tx = x + ((int32_t)size - (int32_t)tb.width) / 2;
        int32_t ty = y + ((int32_t)size - (int32_t)tb.height) / 2;
        draw_text(initial, point_make(tx, ty), font_system, 0xFFFFFFFF);
    }
}

/* Couleurs pour les icônes placeholder */
static uint32_t get_placeholder_color(uint32_t index) {
    static const uint32_t colors[] = {
        COLOR_MACOS_BLUE,
        COLOR_MACOS_GREEN,
        COLOR_MACOS_ORANGE,
        COLOR_MACOS_PURPLE,
        COLOR_MACOS_RED,
        COLOR_MACOS_TEAL,
        COLOR_MACOS_PINK,
        COLOR_MACOS_INDIGO
    };
    return colors[index % 8];
}

void dock_draw(void) {
    if (g_item_count == 0) return;
    
    /* Fond du dock avec effet vitre */
    shadow_params_t shadow = shadow_card();
    shadow.offset_y = -2;
    draw_shadow(g_dock_bounds, DOCK_CORNER_RADIUS, shadow);
    
    draw_rounded_rect_alpha(g_dock_bounds, DOCK_CORNER_RADIUS, rgba(255, 255, 255, 180));
    
    /* Bordure subtile */
    /* TODO: draw_rounded_rect_outline */
    
    /* Calcule les positions des icônes avec effet de grossissement */
    float scales[MAX_DOCK_ITEMS];
    int32_t positions[MAX_DOCK_ITEMS];
    
    /* Calcule les scales basées sur la distance à la souris */
    int32_t base_x = g_dock_bounds.x + DOCK_PADDING;
    int32_t total_width = 0;
    
    for (uint32_t i = 0; i < g_item_count; i++) {
        float target_scale = 1.0f;
        
        if (point_in_rect(g_mouse_pos, g_dock_bounds)) {
            int32_t icon_center_x = base_x + (int32_t)i * (DOCK_ICON_SIZE_BASE + DOCK_PADDING) + 
                                    DOCK_ICON_SIZE_BASE / 2;
            int32_t dist = abs_i32(g_mouse_pos.x - icon_center_x);
            
            if (dist < 100) {
                /* Effet de propagation */
                float factor = 1.0f - (float)dist / 100.0f;
                target_scale = 1.0f + 0.5f * factor * factor;
            }
        }
        
        /* Interpolation douce vers la scale cible */
        float diff = target_scale - g_items[i].scale;
        g_items[i].scale += diff * 0.3f;
        
        scales[i] = g_items[i].scale;
        total_width += (int32_t)((float)DOCK_ICON_SIZE_BASE * scales[i]) + DOCK_PADDING;
    }
    
    /* Calcule les positions centrées */
    int32_t start_x = g_dock_bounds.x + ((int32_t)g_dock_bounds.width - total_width) / 2;
    int32_t x = start_x;
    
    for (uint32_t i = 0; i < g_item_count; i++) {
        positions[i] = x;
        x += (int32_t)((float)DOCK_ICON_SIZE_BASE * scales[i]) + DOCK_PADDING;
    }
    
    /* Dessine les icônes */
    for (uint32_t i = 0; i < g_item_count; i++) {
        dock_item_t* item = &g_items[i];
        uint32_t size = (uint32_t)((float)DOCK_ICON_SIZE_BASE * scales[i]);
        
        /* Position Y ajustée pour que les icônes "montent" quand elles grossissent */
        int32_t icon_y = g_dock_bounds.y + (int32_t)g_dock_bounds.height - (int32_t)size - 10;
        
        if (item->has_icon) {
            /* Dessine l'icône avec mise à l'échelle */
            rect_t dest = {positions[i], icon_y, size, size};
            draw_bitmap_scaled(dest, item->icon, 64, 64);
        } else {
            /* Icône placeholder */
            draw_placeholder_icon(positions[i], icon_y, size, item->name, get_placeholder_color(i));
        }
        
        /* Indicateur d'application active (point) */
        if (item->is_running) {
            int32_t dot_x = positions[i] + (int32_t)size / 2;
            int32_t dot_y = g_dock_bounds.y + (int32_t)g_dock_bounds.height - 6;
            draw_circle(point_make(dot_x, dot_y), 2, 0xFF333333);
        }
    }
    
    /* Tooltip si survolé */
    if (g_hovered_item >= 0 && g_hovered_item < (int32_t)g_item_count) {
        dock_item_t* item = &g_items[g_hovered_item];
        if (item->name[0]) {
            text_bounds_t tb = measure_text(item->name, font_system);
            
            uint32_t size = (uint32_t)((float)DOCK_ICON_SIZE_BASE * scales[g_hovered_item]);
            int32_t tooltip_x = positions[g_hovered_item] + (int32_t)size / 2 - (int32_t)tb.width / 2;
            int32_t tooltip_y = g_dock_bounds.y - 30;
            
            /* Fond du tooltip */
            rect_t tooltip_bg = {
                tooltip_x - 8,
                tooltip_y - 4,
                tb.width + 16,
                tb.height + 8
            };
            draw_rounded_rect_alpha(tooltip_bg, 6, rgba(30, 30, 30, 220));
            
            /* Texte */
            draw_text_alpha(item->name, point_make(tooltip_x, tooltip_y), 
                           font_system, rgba(255, 255, 255, 255));
        }
    }
}

static int32_t find_item_at(point_t pos) {
    if (!point_in_rect(pos, g_dock_bounds)) return -1;
    
    int32_t base_x = g_dock_bounds.x + DOCK_PADDING;
    
    for (uint32_t i = 0; i < g_item_count; i++) {
        int32_t icon_x = base_x + (int32_t)i * (DOCK_ICON_SIZE_BASE + DOCK_PADDING);
        int32_t icon_y = g_dock_bounds.y + 10;
        
        rect_t icon_rect = {icon_x, icon_y, DOCK_ICON_SIZE_BASE, DOCK_ICON_SIZE_BASE};
        if (point_in_rect(pos, icon_rect)) {
            return (int32_t)i;
        }
    }
    
    return -1;
}

void dock_handle_mouse_move(point_t pos) {
    g_mouse_pos = pos;
    
    int32_t old_hovered = g_hovered_item;
    g_hovered_item = find_item_at(pos);
    
    /* Toujours redessiner pour l'effet de grossissement */
    if (point_in_rect(pos, g_dock_bounds) || old_hovered != g_hovered_item) {
        compositor_invalidate_layer(g_dock_layer);
    }
}

void dock_handle_mouse_down(point_t pos) {
    int32_t idx = find_item_at(pos);
    if (idx >= 0) {
        dock_item_t* item = &g_items[idx];
        if (item->on_click) {
            item->on_click();
        }
    }
}

void dock_handle_mouse_up(point_t pos) {
    (void)pos;
}

void dock_update(float delta_time) {
    (void)delta_time;
    /* TODO: animations de rebond */
}

layer_t* dock_get_layer(void) {
    return g_dock_layer;
}

rect_t dock_get_bounds(void) {
    return g_dock_bounds;
}
