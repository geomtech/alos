/* src/gui/wm.c - Implémentation du Window Manager */

#include "wm.h"
#include "render.h"
#include "font.h"
#include "components/component.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

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

/* Optimisations de redimensionnement sans scintillement */
#define RESIZE_FLICKER_TIMEOUT_MS (40)
#define RESIZE_SLOW_THRESHOLD (RESIZE_FLICKER_TIMEOUT_MS * 3 / 4)

static window_t* g_resize_window = NULL;
static bool g_resize_received_bits_from_container = false;
static bool g_resize_received_bits_from_embed = true; // Initialisé à true car pas de fenêtre intégrée par défaut
static uint64_t g_resize_start_time_ms = 0;
static rect_t g_resize_queued_rect = {0, 0, 0, 0};
static bool g_resize_queued = false;
static bool g_resize_slow = false; // Indique si le redimensionnement précédent a dépassé RESIZE_SLOW_THRESHOLD

/* Fonction pour obtenir le timestamp actuel en millisecondes */
static uint64_t wm_get_current_time_ms(void) {
    /* Implémentation simple - à remplacer par une implémentation réelle */
    /* Pour l'instant, on utilise un compteur simple qui s'incrémente */
    static uint64_t counter = 0;
    return counter += 10; // Simule 10ms par appel
}

int wm_init(void) {
    g_windows_head = NULL;
    g_focused_window = NULL;
    g_next_window_id = 1;
    render_get_screen_size(&g_screen_width, &g_screen_height);

    /* Initialisation des variables d'optimisation */
    g_resize_window = NULL;
    g_resize_received_bits_from_container = false;
    g_resize_received_bits_from_embed = true;
    g_resize_start_time_ms = 0;
    g_resize_queued = false;
    g_resize_slow = false;

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

/* Wrapper pour le callback du compositeur */
static void wm_draw_window_layer(layer_t* layer) {
    if (layer && layer->user_data) {
        wm_draw_window((window_t*)layer->user_data);
    }
}

window_t* wm_create_window(rect_t bounds, const char* title, uint32_t flags) {
    window_t* win = (window_t*)malloc(sizeof(window_t));
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
        win->layer->draw_callback = wm_draw_window_layer;
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
            free(win->content_fb->pixels);
        }
        free(win->content_fb);
    }
    
    /* Libère le root component et tous ses enfants */
    if (win->root_component) {
        component_destroy(win->root_component);
        win->root_component = NULL;
    }
    
    /* Met à jour le focus */
    if (g_focused_window == win) {
        g_focused_window = g_windows_head;
        if (g_focused_window && g_focused_window->on_focus) {
            g_focused_window->on_focus(g_focused_window, true);
        }
    }
    
    free(win);
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

    // Vérifier si le redimensionnement est en cours et si le timeout n'est pas dépassé
    if (g_resize_window == win &&
        g_resize_start_time_ms + RESIZE_FLICKER_TIMEOUT_MS > wm_get_current_time_ms()) {
        // Mettre en file d'attente le redimensionnement
        g_resize_queued = true;
        g_resize_queued_rect = rect_make(win->bounds.x, win->bounds.y, width, height);
        return;
    }

    // Réinitialiser l'état de redimensionnement
    g_resize_queued = false;

    compositor_invalidate_rect(win->bounds);

    // Mettre à jour les dimensions de la fenêtre
    rect_t old_bounds = win->bounds;
    win->bounds.width = width;
    win->bounds.height = height;

    uint32_t titlebar_h = (win->flags & WINDOW_FLAG_TITLEBAR) ? TITLEBAR_HEIGHT : 0;
    win->content_bounds.width = width;
    win->content_bounds.height = height - titlebar_h;

    if (win->layer) {
        win->layer->bounds = win->bounds;
    }

    // Appeler le callback de redimensionnement si présent
    if (win->on_resize) {
        win->on_resize(win, width, height);
    }

    // Si le redimensionnement est dynamique (sans scintillement)
    bool dynamic_resize = false; // À déterminer en fonction des flags ou de la configuration
    if (dynamic_resize) {
        // Ne pas redessiner tout de suite
        g_resize_window = win;
        g_resize_received_bits_from_container = false;
        g_resize_received_bits_from_embed = true; // Pas de fenêtre intégrée pour l'instant
        g_resize_start_time_ms = wm_get_current_time_ms();
    } else {
        // Redessiner immédiatement
        wm_invalidate_window(win);
    }

    // Vérifier si le redimensionnement précédent était lent
    if (g_resize_slow) {
        // Copier les anciens bits de surface pour éviter les artefacts visuels
        // en cas de timeout du redimensionnement
        // (à implémenter lorsque le système de surface sera disponible)
    }
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
    render_push_clip(win->content_bounds);

    /* 1. Callback custom si présent */
    if (win->on_draw) {
        win->on_draw(win);
    }

    /* 2. Dessiner l'arbre de composants si présent */
    if (win->root_component) {
        /* Mettre à jour les bounds relatives du root pour correspondre au content area */
        win->root_component->bounds.x = 0;
        win->root_component->bounds.y = 0;
        win->root_component->bounds.width = win->content_bounds.width;
        win->root_component->bounds.height = win->content_bounds.height;

        /* Pour le root component (sans parent), définir directement les bounds absolus */
        win->root_component->abs_bounds.x = win->content_bounds.x;
        win->root_component->abs_bounds.y = win->content_bounds.y;
        win->root_component->abs_bounds.width = win->content_bounds.width;
        win->root_component->abs_bounds.height = win->content_bounds.height;

        /* Mettre à jour les bounds absolus des enfants uniquement (récursif) */
        for (uint32_t i = 0; i < win->root_component->child_count; i++) {
            component_update_abs_bounds(win->root_component->children[i]);
        }
        
        component_draw(win->root_component, render_get_framebuffer());
    }

    render_pop_clip();
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
        g_last_mouse_pos = pos;
        return;
    }

    /* Redimensionnement */
    if (g_resizing_window) {
        int32_t new_w = pos.x - g_resizing_window->bounds.x;
        int32_t new_h = pos.y - g_resizing_window->bounds.y;
        if (new_w < 200) new_w = 200;
        if (new_h < 100) new_h = 100;
        wm_resize_window(g_resizing_window, (uint32_t)new_w, (uint32_t)new_h);
        g_last_mouse_pos = pos;
        return;
    }

    g_last_mouse_pos = pos;

    /* Dispatcher aux composants de la fenêtre sous la souris */
    window_t* win = wm_find_window_at(pos);
    if (win && win->root_component && point_in_rect(pos, win->content_bounds)) {
        /* Position en coordonnées absolues (component utilise abs_bounds) */
        component_dispatch_mouse_move(win->root_component, pos);
    }
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
        return;
    }

    /* Dispatcher aux composants si clic dans le content area */
    if (win->root_component && point_in_rect(pos, win->content_bounds)) {
        component_dispatch_mouse_down(win->root_component, pos, button);
    }
}

void wm_handle_mouse_up(point_t pos, mouse_button_t button) {
    if (button != MOUSE_BUTTON_LEFT) return;

    /* Fin de drag/resize */
    bool was_dragging = (g_dragging_window != NULL);
    bool was_resizing = (g_resizing_window != NULL);
    g_dragging_window = NULL;
    g_resizing_window = NULL;

    /* Si on était en train de drag/resize, ne pas dispatcher aux composants */
    if (was_dragging || was_resizing) return;

    /* Dispatcher aux composants */
    window_t* win = wm_find_window_at(pos);
    if (win && win->root_component && point_in_rect(pos, win->content_bounds)) {
        component_dispatch_mouse_up(win->root_component, pos, button);
    }
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

/* === Gestion des composants === */

/* Vérifier et appliquer les redimensionnements en file d'attente */
static void wm_check_queued_resize(void) {
    if (g_resize_queued && g_resize_window) {
        // Vérifier si le timeout est dépassé
        if (g_resize_start_time_ms + RESIZE_FLICKER_TIMEOUT_MS <= wm_get_current_time_ms()) {
            // Appliquer le redimensionnement en file d'attente
            rect_t queued_rect = g_resize_queued_rect;
            g_resize_queued = false;

            // Appliquer les nouvelles dimensions
            window_t* win = g_resize_window;
            wm_resize_window(win, queued_rect.width, queued_rect.height);

            // Réinitialiser l'état de redimensionnement
            g_resize_window = NULL;
            g_resize_received_bits_from_container = false;
            g_resize_received_bits_from_embed = true;
        }
    }
}

/* Mettre à jour l'état du redimensionnement */
static void wm_update_resize_state(void) {
    if (g_resize_window) {
        // Vérifier si le timeout est dépassé
        if (g_resize_start_time_ms + RESIZE_FLICKER_TIMEOUT_MS <= wm_get_current_time_ms()) {
            // Le redimensionnement a pris trop de temps, marquer comme lent
            g_resize_slow = true;

            // Forcer le redessinement
            wm_invalidate_window(g_resize_window);

            // Réinitialiser l'état de redimensionnement
            g_resize_window = NULL;
            g_resize_received_bits_from_container = false;
            g_resize_received_bits_from_embed = true;
        } else {
            // Vérifier si les bits ont été reçus des deux côtés
            if (g_resize_received_bits_from_container && g_resize_received_bits_from_embed) {
                // Les deux côtés ont terminé le rendu, afficher le résultat
                wm_invalidate_window(g_resize_window);

                // Réinitialiser l'état de redimensionnement
                g_resize_window = NULL;
                g_resize_received_bits_from_container = false;
                g_resize_received_bits_from_embed = true;
                g_resize_slow = false;
            }
        }
    }
}

/* Helper pour propager owner_window récursivement */
static void propagate_owner_window(gui_component_t* comp, window_t* win) {
    if (!comp) return;
    comp->owner_window = (struct window*)win;
    for (uint32_t i = 0; i < comp->child_count; i++) {
        propagate_owner_window(comp->children[i], win);
    }
}

void wm_set_root_component(window_t* win, gui_component_t* root) {
    if (!win) return;

    /* Si ancien root, le déconnecter */
    if (win->root_component) {
        propagate_owner_window(win->root_component, NULL);
    }

    win->root_component = root;

    if (root) {
        /* Lier la fenêtre à tous les composants */
        propagate_owner_window(root, win);

        /* Positionner root pour couvrir tout le content area */
        root->bounds.x = 0;
        root->bounds.y = 0;
        root->bounds.width = win->content_bounds.width;
        root->bounds.height = win->content_bounds.height;
    }

    wm_invalidate_window(win);
}

/* Marquer la réception des bits de rendu du conteneur */
void wm_mark_container_bits_received(window_t* win) {
    if (g_resize_window == win) {
        g_resize_received_bits_from_container = true;
        wm_update_resize_state();
    }
}

/* Marquer la réception des bits de rendu de la fenêtre intégrée */
void wm_mark_embed_bits_received(window_t* win) {
    if (g_resize_window == win) {
        g_resize_received_bits_from_embed = true;
        wm_update_resize_state();
    }
}

/* Vérifier et appliquer les redimensionnements en file d'attente */
void wm_check_queued_resizes(void) {
    wm_check_queued_resize();
}
