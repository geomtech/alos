/* src/gui/wm.c - Implémentation du Window Manager */

#include "wm.h"
#include "render.h"
#include "font.h"
#include "../mm/kheap.h"
#include "../include/string.h"

/* Liste des fenêtres */
static window_t* g_windows_head = NULL;
static window_t* g_focused_window = NULL;
static uint32_t g_next_window_id = 1;

/* Dimensions de l'écran */
static uint32_t g_screen_width = 0;
static uint32_t g_screen_height = 0;

/* État du drag/resize */
static window_t* g_dragging_window = NULL;
static window_t* g_resizing_window = NULL;
static point_t g_last_mouse_pos = {0, 0};

int wm_init(void) {
    g_windows_head = NULL;
    g_focused_window = NULL;
    g_next_window_id = 1;
    render_get_screen_size(&g_screen_width, &g_screen_height);
    return 0;
}

void wm_shutdown(void) {
    window_t* win = g_windows_head;
    while (win) {
        window_t* next = win->next;
        wm_destroy_window(win);
        win = next;
    }
    g_windows_head = NULL;
    g_focused_window = NULL;
}

window_t* wm_create_window(rect_t bounds, const char* title, uint32_t flags) {
    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    
    memset(win, 0, sizeof(window_t));
    win->id = g_next_window_id++;
    win->bounds = bounds;
    win->flags = flags;
    
    if (title) {
        strncpy(win->title, title, 255);
        win->title[255] = '\0';
    }
    
    /* Calcule la zone de contenu */
    uint32_t titlebar_h = (flags & WINDOW_FLAG_TITLEBAR) ? TITLEBAR_HEIGHT : 0;
    win->content_bounds.x = bounds.x;
    win->content_bounds.y = bounds.y + (int32_t)titlebar_h;
    win->content_bounds.width = bounds.width;
    win->content_bounds.height = bounds.height - titlebar_h;
    
    /* Crée la couche du compositeur */
    win->layer = compositor_create_layer(LAYER_WINDOW, bounds);
    if (win->layer) {
        win->layer->user_data = win;
        compositor_add_layer(win->layer);
    }
    
    /* Ajoute à la liste */
    win->next = g_windows_head;
    g_windows_head = win;
    
    /* Focus automatique */
    wm_focus_window(win);
    
    return win;
}

void wm_destroy_window(window_t* win) {
    if (!win) return;
    
    /* Retire de la liste */
    window_t** pp = &g_windows_head;
    while (*pp && *pp != win) pp = &(*pp)->next;
    if (*pp) *pp = win->next;
    
    /* Libère la couche */
    if (win->layer) {
        compositor_destroy_layer(win->layer);
    }
    
    /* Libère le buffer de contenu */
    if (win->content_fb) {
        if (win->content_fb->owns_memory && win->content_fb->pixels) {
            kfree(win->content_fb->pixels);
        }
        kfree(win->content_fb);
    }
    
    /* Met à jour le focus */
    if (g_focused_window == win) {
        g_focused_window = g_windows_head;
        if (g_focused_window && g_focused_window->on_focus) {
            g_focused_window->on_focus(g_focused_window, true);
        }
    }
    
    kfree(win);
}

void wm_focus_window(window_t* win) {
    if (!win || win == g_focused_window) return;
    
    /* Retire le focus de l'ancienne fenêtre */
    if (g_focused_window) {
        g_focused_window->is_focused = false;
        if (g_focused_window->on_focus) {
            g_focused_window->on_focus(g_focused_window, false);
        }
        wm_invalidate_window(g_focused_window);
    }
    
    /* Donne le focus à la nouvelle */
    g_focused_window = win;
    win->is_focused = true;
    if (win->on_focus) {
        win->on_focus(win, true);
    }
    
    /* Met la fenêtre au premier plan */
    if (win->layer) {
        compositor_raise_layer(win->layer);
    }
    
    wm_invalidate_window(win);
}

window_t* wm_get_focused_window(void) {
    return g_focused_window;
}

void wm_move_window(window_t* win, int32_t x, int32_t y) {
    if (!win) return;
    
    compositor_invalidate_rect(win->bounds);
    
    win->bounds.x = x;
    win->bounds.y = y;
    
    uint32_t titlebar_h = (win->flags & WINDOW_FLAG_TITLEBAR) ? TITLEBAR_HEIGHT : 0;
    win->content_bounds.x = x;
    win->content_bounds.y = y + (int32_t)titlebar_h;
    
    if (win->layer) {
        win->layer->bounds = win->bounds;
    }
    
    wm_invalidate_window(win);
}

void wm_resize_window(window_t* win, uint32_t width, uint32_t height) {
    if (!win) return;
    
    compositor_invalidate_rect(win->bounds);
    
    win->bounds.width = width;
    win->bounds.height = height;
    
    uint32_t titlebar_h = (win->flags & WINDOW_FLAG_TITLEBAR) ? TITLEBAR_HEIGHT : 0;
    win->content_bounds.width = width;
    win->content_bounds.height = height - titlebar_h;
    
    if (win->layer) {
        win->layer->bounds = win->bounds;
    }
    
    if (win->on_resize) {
        win->on_resize(win, width, height);
    }
    
    wm_invalidate_window(win);
}

void wm_minimize_window(window_t* win) {
    if (!win || win->is_minimized) return;
    win->is_minimized = true;
    if (win->layer) win->layer->visible = false;
    compositor_invalidate_rect(win->bounds);
}

void wm_maximize_window(window_t* win) {
    if (!win || win->is_maximized) return;
    
    win->restore_bounds = win->bounds;
    win->is_maximized = true;
    
    /* Maximise en tenant compte de la menu bar */
    wm_move_window(win, 0, MENUBAR_HEIGHT);
    wm_resize_window(win, g_screen_width, g_screen_height - MENUBAR_HEIGHT - DOCK_HEIGHT - DOCK_MARGIN_BOTTOM);
}

void wm_restore_window(window_t* win) {
    if (!win) return;
    
    if (win->is_minimized) {
        win->is_minimized = false;
        if (win->layer) win->layer->visible = true;
        wm_invalidate_window(win);
    }
    
    if (win->is_maximized) {
        win->is_maximized = false;
        wm_move_window(win, win->restore_bounds.x, win->restore_bounds.y);
        wm_resize_window(win, win->restore_bounds.width, win->restore_bounds.height);
    }
}

void wm_close_window(window_t* win) {
    if (!win) return;
    if (win->on_close) {
        win->on_close(win);
    }
    wm_destroy_window(win);
}

/* Dessine les boutons de fenêtre macOS */
static void draw_window_buttons(window_t* win, bool hovered) {
    int32_t btn_y = win->bounds.y + (TITLEBAR_HEIGHT - 12) / 2;
    int32_t btn_x = win->bounds.x + 12;
    
    /* Bouton fermer (rouge) */
    if (win->flags & WINDOW_FLAG_CLOSABLE) {
        uint32_t color = win->is_focused ? COLOR_BTN_CLOSE : COLOR_GRAY_4;
        draw_circle(point_make(btn_x, btn_y + 6), 6, color);
        btn_x += 20;
    }
    
    /* Bouton minimiser (jaune) */
    if (win->flags & WINDOW_FLAG_MINIMIZABLE) {
        uint32_t color = win->is_focused ? COLOR_BTN_MINIMIZE : COLOR_GRAY_4;
        draw_circle(point_make(btn_x, btn_y + 6), 6, color);
        btn_x += 20;
    }
    
    /* Bouton maximiser (vert) */
    if (win->flags & WINDOW_FLAG_RESIZABLE) {
        uint32_t color = win->is_focused ? COLOR_BTN_MAXIMIZE : COLOR_GRAY_4;
        draw_circle(point_make(btn_x, btn_y + 6), 6, color);
    }
    
    (void)hovered;
}

void wm_draw_window(window_t* win) {
    if (!win || win->is_minimized) return;
    
    uint32_t radius = (win->flags & WINDOW_FLAG_ROUNDED) ? WINDOW_CORNER_RADIUS : 0;
    
    /* Ombre portée */
    if (win->flags & WINDOW_FLAG_SHADOW) {
        shadow_params_t shadow = shadow_window();
        draw_shadow(win->bounds, radius, shadow);
    }
    
    /* Fond de la fenêtre */
    if (win->flags & WINDOW_FLAG_TRANSPARENT) {
        draw_rounded_rect_alpha(win->bounds, radius, rgba(255, 255, 255, 230));
    } else {
        draw_rounded_rect(win->bounds, radius, COLOR_WINDOW_BG);
    }
    
    /* Barre de titre */
    if (win->flags & WINDOW_FLAG_TITLEBAR) {
        rect_t titlebar = {
            win->bounds.x,
            win->bounds.y,
            win->bounds.width,
            TITLEBAR_HEIGHT
        };
        
        /* Fond de la titlebar (semi-transparent si focus) */
        rgba_t tb_color = win->is_focused ? 
            rgba(246, 246, 246, 240) : rgba(220, 220, 220, 240);
        draw_rounded_rect_alpha(titlebar, radius, tb_color);
        
        /* Boutons */
        draw_window_buttons(win, false);
        
        /* Titre centré */
        if (win->title[0]) {
            text_bounds_t tb = measure_text(win->title, font_system);
            int32_t tx = win->bounds.x + ((int32_t)win->bounds.width - (int32_t)tb.width) / 2;
            int32_t ty = win->bounds.y + (TITLEBAR_HEIGHT - (int32_t)tb.height) / 2;
            
            rgba_t text_color = win->is_focused ? 
                u32_to_rgba(COLOR_TEXT_PRIMARY) : u32_to_rgba(COLOR_GRAY_5);
            draw_text_alpha(win->title, point_make(tx, ty), font_system, text_color);
        }
        
        /* Séparateur sous la titlebar */
        draw_hline(win->bounds.x, win->bounds.x + (int32_t)win->bounds.width - 1,
                   win->bounds.y + TITLEBAR_HEIGHT - 1, COLOR_GRAY_2);
    }
    
    /* Contenu de la fenêtre */
    if (win->on_draw) {
        render_push_clip(win->content_bounds);
        win->on_draw(win);
        render_pop_clip();
    }
}

void wm_draw_all(void) {
    for (window_t* win = g_windows_head; win; win = win->next) {
        wm_draw_window(win);
    }
}

void wm_invalidate_window(window_t* win) {
    if (win && win->layer) {
        compositor_invalidate_layer(win->layer);
    }
}

/* Vérifie si un point est sur un bouton de fenêtre */
static int get_button_at(window_t* win, point_t pos) {
    if (!(win->flags & WINDOW_FLAG_TITLEBAR)) return -1;
    
    int32_t btn_y = win->bounds.y + (TITLEBAR_HEIGHT - 12) / 2;
    int32_t btn_x = win->bounds.x + 12;
    
    /* Bouton fermer */
    if (win->flags & WINDOW_FLAG_CLOSABLE) {
        if (pos.x >= btn_x - 6 && pos.x <= btn_x + 6 &&
            pos.y >= btn_y && pos.y <= btn_y + 12) {
            return 0;
        }
        btn_x += 20;
    }
    
    /* Bouton minimiser */
    if (win->flags & WINDOW_FLAG_MINIMIZABLE) {
        if (pos.x >= btn_x - 6 && pos.x <= btn_x + 6 &&
            pos.y >= btn_y && pos.y <= btn_y + 12) {
            return 1;
        }
        btn_x += 20;
    }
    
    /* Bouton maximiser */
    if (win->flags & WINDOW_FLAG_RESIZABLE) {
        if (pos.x >= btn_x - 6 && pos.x <= btn_x + 6 &&
            pos.y >= btn_y && pos.y <= btn_y + 12) {
            return 2;
        }
    }
    
    return -1;
}

/* Vérifie si un point est sur la barre de titre */
static bool is_on_titlebar(window_t* win, point_t pos) {
    if (!(win->flags & WINDOW_FLAG_TITLEBAR)) return false;
    
    return pos.x >= win->bounds.x && 
           pos.x < win->bounds.x + (int32_t)win->bounds.width &&
           pos.y >= win->bounds.y && 
           pos.y < win->bounds.y + TITLEBAR_HEIGHT;
}

/* Vérifie si un point est sur le bord de redimensionnement */
static bool is_on_resize_border(window_t* win, point_t pos) {
    if (!(win->flags & WINDOW_FLAG_RESIZABLE)) return false;
    
    int32_t margin = 8;
    int32_t right = win->bounds.x + (int32_t)win->bounds.width;
    int32_t bottom = win->bounds.y + (int32_t)win->bounds.height;
    
    return pos.x >= right - margin && pos.x < right &&
           pos.y >= bottom - margin && pos.y < bottom;
}

void wm_handle_mouse_move(point_t pos) {
    /* Drag de fenêtre */
    if (g_dragging_window) {
        int32_t dx = pos.x - g_last_mouse_pos.x;
        int32_t dy = pos.y - g_last_mouse_pos.y;
        wm_move_window(g_dragging_window,
                       g_dragging_window->bounds.x + dx,
                       g_dragging_window->bounds.y + dy);
    }
    
    /* Redimensionnement */
    if (g_resizing_window) {
        int32_t new_w = pos.x - g_resizing_window->bounds.x;
        int32_t new_h = pos.y - g_resizing_window->bounds.y;
        if (new_w < 200) new_w = 200;
        if (new_h < 100) new_h = 100;
        wm_resize_window(g_resizing_window, (uint32_t)new_w, (uint32_t)new_h);
    }
    
    g_last_mouse_pos = pos;
}

void wm_handle_mouse_down(point_t pos, mouse_button_t button) {
    if (button != MOUSE_BUTTON_LEFT) return;
    
    window_t* win = wm_find_window_at(pos);
    if (!win) return;
    
    /* Focus */
    wm_focus_window(win);
    
    /* Vérifie les boutons de fenêtre */
    int btn = get_button_at(win, pos);
    if (btn == 0) {
        wm_close_window(win);
        return;
    } else if (btn == 1) {
        wm_minimize_window(win);
        return;
    } else if (btn == 2) {
        if (win->is_maximized) wm_restore_window(win);
        else wm_maximize_window(win);
        return;
    }
    
    /* Redimensionnement */
    if (is_on_resize_border(win, pos)) {
        g_resizing_window = win;
        g_last_mouse_pos = pos;
        return;
    }
    
    /* Drag de la titlebar */
    if (is_on_titlebar(win, pos)) {
        g_dragging_window = win;
        g_last_mouse_pos = pos;
        
        /* Si maximisée, restaure d'abord */
        if (win->is_maximized) {
            wm_restore_window(win);
        }
    }
}

void wm_handle_mouse_up(point_t pos, mouse_button_t button) {
    (void)pos;
    if (button != MOUSE_BUTTON_LEFT) return;
    
    g_dragging_window = NULL;
    g_resizing_window = NULL;
}

window_t* wm_find_window_at(point_t pos) {
    /* Parcourt les fenêtres du premier plan vers l'arrière */
    window_t* found = NULL;
    for (window_t* win = g_windows_head; win; win = win->next) {
        if (win->is_minimized) continue;
        if (point_in_rect(pos, win->bounds)) {
            found = win;
        }
    }
    return found;
}

window_t* wm_get_window_by_id(uint32_t id) {
    for (window_t* win = g_windows_head; win; win = win->next) {
        if (win->id == id) return win;
    }
    return NULL;
}

window_t* wm_get_first_window(void) {
    return g_windows_head;
}
