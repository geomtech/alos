/* src/gui/gui.c - Implémentation du point d'entrée GUI */

#include "gui.h"
#include "ssfn_render.h"
#include "../kernel/mouse.h"
#include "../mm/kheap.h"
#include "../include/string.h"

/* État global */
static gui_state_t g_state = GUI_STATE_UNINITIALIZED;
static bool g_quit_requested = false;
static framebuffer_t* g_framebuffer = NULL;

/* Dimensions de l'écran */
static uint32_t g_screen_width = 0;
static uint32_t g_screen_height = 0;

/* Position du curseur souris */
static int32_t g_mouse_x = 0;
static int32_t g_mouse_y = 0;
static bool g_mouse_visible = true;
static bool g_needs_redraw = false;

/* Dimensions du curseur */
#define CURSOR_WIDTH  12
#define CURSOR_HEIGHT 19

/* Déclaration forward du curseur */
static void draw_cursor(int32_t x, int32_t y);

int gui_init(struct limine_framebuffer* fb) {
    if (!fb) return -1;
    
    /* Initialise le système de rendu */
    if (render_init(fb) != 0) {
        return -1;
    }
    
    g_framebuffer = render_get_framebuffer();
    render_get_screen_size(&g_screen_width, &g_screen_height);
    
    /* Désactive le double buffering pour l'instant (dessine directement) */
    render_set_double_buffer(false);
    
    /* Efface complètement l'écran pour supprimer la console */
    render_clear(0xFF000000);
    
    /* Initialise les polices */
    font_init();
    
    /* Initialise SSFN avec Unifont (support UTF-8) */
    ssfn_init();
    
    /* Initialise le compositeur */
    if (compositor_init(g_framebuffer) != 0) {
        return -1;
    }
    
    /* Configure le fond d'écran par défaut (dégradé bleu style macOS) */
    compositor_set_background_gradient(
        rgba(30, 80, 140, 255),   /* Bleu foncé */
        rgba(100, 160, 220, 255), /* Bleu clair */
        GRADIENT_VERTICAL
    );
    
    /* Initialise le window manager */
    if (wm_init() != 0) {
        return -1;
    }
    
    /* Initialise la barre de menu */
    if (menubar_init() != 0) {
        return -1;
    }
    
    /* Initialise le dock */
    if (dock_init() != 0) {
        return -1;
    }
    
    /* Initialise le système d'événements */
    if (events_init() != 0) {
        return -1;
    }
    
    g_state = GUI_STATE_RUNNING;
    g_quit_requested = false;
    
    /* Test UTF-8 avec SSFN */
    if (ssfn_is_initialized()) {
        ssfn_set_fg(0xFFFFFFFF);  /* Blanc */
        ssfn_print_at(20, 50, "ALOS - UTF-8 Test:");
        ssfn_print_at(20, 70, "English: Hello World!");
        ssfn_print_at(20, 90, "Français: Bonjour le monde! éàüö");
        ssfn_print_at(20, 110, "日本語: こんにちは世界");
        ssfn_print_at(20, 130, "中文: 你好世界");
        ssfn_print_at(20, 150, "Русский: Привет мир");
        ssfn_print_at(20, 170, "العربية: مرحبا بالعالم");
        ssfn_print_at(20, 190, "Emoji: ★ ♠ ♣ ♥ ♦ ☺ ☻");
    }
    
    return 0;
}

void gui_shutdown(void) {
    g_state = GUI_STATE_SHUTDOWN;
    
    events_shutdown();
    dock_shutdown();
    menubar_shutdown();
    wm_shutdown();
    compositor_shutdown();
    
    g_state = GUI_STATE_UNINITIALIZED;
}

gui_state_t gui_get_state(void) {
    return g_state;
}

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

void gui_process_events(void) {
    events_process();
}

void gui_update(float delta_time) {
    dock_update(delta_time);
    /* TODO: autres animations */
}

/* Sauvegarde de la zone sous le curseur pour restauration rapide */
static uint32_t g_cursor_save[CURSOR_WIDTH * CURSOR_HEIGHT];
static int32_t g_cursor_save_x = -1;
static int32_t g_cursor_save_y = -1;

/* Sauvegarde les pixels sous le curseur */
static void save_cursor_background(int32_t x, int32_t y) {
    for (int32_t cy = 0; cy < CURSOR_HEIGHT; cy++) {
        for (int32_t cx = 0; cx < CURSOR_WIDTH; cx++) {
            int32_t px = x + cx;
            int32_t py = y + cy;
            if (px >= 0 && px < (int32_t)g_screen_width &&
                py >= 0 && py < (int32_t)g_screen_height) {
                g_cursor_save[cy * CURSOR_WIDTH + cx] = read_pixel(px, py);
            }
        }
    }
    g_cursor_save_x = x;
    g_cursor_save_y = y;
}

/* Restaure les pixels sous le curseur */
static void restore_cursor_background(void) {
    if (g_cursor_save_x < 0) return;
    
    for (int32_t cy = 0; cy < CURSOR_HEIGHT; cy++) {
        for (int32_t cx = 0; cx < CURSOR_WIDTH; cx++) {
            int32_t px = g_cursor_save_x + cx;
            int32_t py = g_cursor_save_y + cy;
            if (px >= 0 && px < (int32_t)g_screen_width &&
                py >= 0 && py < (int32_t)g_screen_height) {
                draw_pixel(px, py, g_cursor_save[cy * CURSOR_WIDTH + cx]);
            }
        }
    }
}

/* Rendu complet de l'interface (appelé une seule fois au démarrage) */
void gui_render_full(void) {
    if (g_state != GUI_STATE_RUNNING) return;
    
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
    if (g_state != GUI_STATE_RUNNING) return;
    
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

void gui_main_loop(void) {
    while (!g_quit_requested && g_state == GUI_STATE_RUNNING) {
        gui_process_events();
        gui_update(0.016f);  /* ~60 FPS */
        gui_render();
        
        /* TODO: synchronisation avec le timer */
    }
}

void gui_request_quit(void) {
    g_quit_requested = true;
}

void gui_set_wallpaper_color(uint32_t color) {
    compositor_set_background_color(color);
}

void gui_set_wallpaper_gradient(rgba_t color1, rgba_t color2, gradient_direction_t dir) {
    compositor_set_background_gradient(color1, color2, dir);
}

/* Callback de dessin pour la fenêtre de démo */
static void demo_window_draw(window_t* win) {
    if (!win) return;
    
    /* Fond de contenu */
    draw_rect(win->content_bounds, COLOR_WINDOW_BG);
    
    int32_t x = win->content_bounds.x + 20;
    int32_t y = win->content_bounds.y + 20;
    
    /* Utiliser SSFN pour le texte UTF-8 */
    if (ssfn_is_initialized()) {
        ssfn_set_fg(COLOR_TEXT_PRIMARY);
        ssfn_print_at(x, y, "Bienvenue dans ALOS GUI!");
        
        y += 24;
        ssfn_set_fg(0xFF666666);
        ssfn_print_at(x, y, "Système d'exploitation éducatif");
        
        y += 24;
        ssfn_print_at(x, y, "Fonctionnalités: réseau, système de fichiers, GUI");
        
        y += 32;
        ssfn_set_fg(COLOR_TEXT_PRIMARY);
        ssfn_print_at(x, y, "Support UTF-8 complet:");
        
        y += 20;
        ssfn_set_fg(0xFF444444);
        ssfn_print_at(x + 10, y, "• Français: àéèêëïôùûç");
        y += 18;
        ssfn_print_at(x + 10, y, "• Deutsch: äöüß");
        y += 18;
        ssfn_print_at(x + 10, y, "• 日本語: ひらがな");
        y += 18;
        ssfn_print_at(x + 10, y, "• Русский: Привет");
    }
    
    y += 30;
    
    /* Bouton */
    rect_t btn = {win->content_bounds.x + 20, y, 140, 32};
    draw_rounded_rect(btn, 6, COLOR_MACOS_BLUE);
    if (ssfn_is_initialized()) {
        ssfn_set_fg(0xFFFFFFFF);
        ssfn_print_at(btn.x + 20, btn.y + 8, "Démarrer ▶");
    }
    
    y += 50;
    
    /* Barre de progression */
    rect_t progress_bg = {win->content_bounds.x + 20, y, 200, 8};
    draw_rounded_rect(progress_bg, 4, COLOR_GRAY_2);
    rect_t progress_fg = {win->content_bounds.x + 20, y, 140, 8};
    draw_rounded_rect(progress_fg, 4, COLOR_MACOS_BLUE);
}

window_t* gui_create_demo_window(const char* title, int32_t x, int32_t y) {
    rect_t bounds = {x, y, 400, 300};
    window_t* win = wm_create_window(bounds, title, WINDOW_STYLE_DEFAULT);
    
    if (win) {
        win->on_draw = demo_window_draw;
    }
    
    return win;
}

void gui_setup_demo_dock(void) {
    /* Ajoute quelques applications de démo */
    dock_item_t* finder = dock_add_app("Finder", NULL);
    if (finder) finder->is_running = true;
    
    dock_add_app("Terminal", NULL);
    dock_add_app("Safari", NULL);
    dock_add_app("Mail", NULL);
    dock_add_app("Music", NULL);
    dock_add_app("Photos", NULL);
    dock_add_app("Settings", NULL);
}

/* Callbacks pour les menus */
static void menu_about(void) {
    gui_create_demo_window("A propos d'ALOS", 200, 150);
}

static void menu_quit(void) {
    gui_request_quit();
}

static void menu_new_window(void) {
    static int window_count = 1;
    char title[64];
    
    /* Génère un titre unique */
    title[0] = 'F'; title[1] = 'e'; title[2] = 'n'; title[3] = 'e';
    title[4] = 't'; title[5] = 'r'; title[6] = 'e'; title[7] = ' ';
    title[8] = '0' + (window_count % 10);
    title[9] = '\0';
    
    gui_create_demo_window(title, 100 + window_count * 30, 100 + window_count * 30);
    window_count++;
}

void gui_setup_demo_menus(void) {
    menubar_set_app_name("Finder");
    
    /* Menu ALOS (Apple) */
    menu_t* alos_menu = menubar_add_menu("ALOS");
    if (alos_menu) {
        menubar_add_item(alos_menu, "A propos d'ALOS", NULL, menu_about);
        menubar_add_separator(alos_menu);
        menubar_add_item(alos_menu, "Preferences...", "Cmd+,", NULL);
        menubar_add_separator(alos_menu);
        menubar_add_item(alos_menu, "Quitter", "Cmd+Q", menu_quit);
    }
    
    /* Menu File */
    menu_t* file_menu = menubar_add_menu("File");
    if (file_menu) {
        menubar_add_item(file_menu, "Nouvelle fenetre", "Cmd+N", menu_new_window);
        menubar_add_item(file_menu, "Ouvrir...", "Cmd+O", NULL);
        menubar_add_separator(file_menu);
        menubar_add_item(file_menu, "Fermer", "Cmd+W", NULL);
    }
    
    /* Menu Edit */
    menu_t* edit_menu = menubar_add_menu("Edit");
    if (edit_menu) {
        menubar_add_item(edit_menu, "Annuler", "Cmd+Z", NULL);
        menubar_add_item(edit_menu, "Retablir", "Cmd+Shift+Z", NULL);
        menubar_add_separator(edit_menu);
        menubar_add_item(edit_menu, "Couper", "Cmd+X", NULL);
        menubar_add_item(edit_menu, "Copier", "Cmd+C", NULL);
        menubar_add_item(edit_menu, "Coller", "Cmd+V", NULL);
    }
    
    /* Menu View */
    menu_t* view_menu = menubar_add_menu("View");
    if (view_menu) {
        menubar_add_item(view_menu, "Icones", "Cmd+1", NULL);
        menubar_add_item(view_menu, "Liste", "Cmd+2", NULL);
        menubar_add_item(view_menu, "Colonnes", "Cmd+3", NULL);
    }
    
    /* Menu Window */
    menu_t* window_menu = menubar_add_menu("Window");
    if (window_menu) {
        menubar_add_item(window_menu, "Minimiser", "Cmd+M", NULL);
        menubar_add_item(window_menu, "Zoom", NULL, NULL);
        menubar_add_separator(window_menu);
        menubar_add_item(window_menu, "Tout au premier plan", NULL, NULL);
    }
    
    /* Menu Help */
    menu_t* help_menu = menubar_add_menu("Help");
    if (help_menu) {
        menubar_add_item(help_menu, "Aide ALOS", NULL, NULL);
    }
    
    /* Configure l'horloge */
    menubar_set_time(14, 30);
}

/* ============================================================================
 * CURSEUR SOURIS
 * ============================================================================ */

/* Curseur souris simple (flèche 12x19 pixels) */
static const uint8_t cursor_data[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
};

static void draw_cursor(int32_t x, int32_t y) {
    if (!g_mouse_visible) return;
    
    for (int32_t cy = 0; cy < CURSOR_HEIGHT; cy++) {
        for (int32_t cx = 0; cx < CURSOR_WIDTH; cx++) {
            uint8_t pixel = cursor_data[cy][cx];
            if (pixel == 0) continue;  /* Transparent */
            
            int32_t px = x + cx;
            int32_t py = y + cy;
            
            if (px >= 0 && px < (int32_t)g_screen_width &&
                py >= 0 && py < (int32_t)g_screen_height) {
                if (pixel == 1) {
                    draw_pixel(px, py, 0xFF000000);  /* Noir (contour) */
                } else {
                    draw_pixel(px, py, 0xFFFFFFFF);  /* Blanc (intérieur) */
                }
            }
        }
    }
}

/* ============================================================================
 * CALLBACK SOURIS
 * ============================================================================ */

void gui_mouse_callback(const mouse_state_t* state) {
    if (!state || g_state != GUI_STATE_RUNNING) return;
    
    /* Restaure le fond sous l'ancien curseur */
    restore_cursor_background();
    
    /* Met à jour la position */
    g_mouse_x = state->x;
    g_mouse_y = state->y;
    
    /* Génère les événements pour le système GUI */
    if (state->dx != 0 || state->dy != 0) {
        events_mouse_move(g_mouse_x, g_mouse_y);
    }
    
    /* Boutons */
    if (state->buttons_changed & MOUSE_BTN_LEFT) {
        events_mouse_button(MOUSE_BUTTON_LEFT, (state->buttons & MOUSE_BTN_LEFT) != 0);
    }
    if (state->buttons_changed & MOUSE_BTN_RIGHT) {
        events_mouse_button(MOUSE_BUTTON_RIGHT, (state->buttons & MOUSE_BTN_RIGHT) != 0);
    }
    if (state->buttons_changed & MOUSE_BTN_MIDDLE) {
        events_mouse_button(MOUSE_BUTTON_MIDDLE, (state->buttons & MOUSE_BTN_MIDDLE) != 0);
    }
    
    /* Molette */
    if (state->scroll != 0) {
        events_mouse_scroll(state->scroll);
    }
    
    /* Traite les événements (mouvements pour hover, clics, etc.) */
    events_process();
    
    /* Redessine la menubar si la souris est dans cette zone (pour le hover) */
    if (g_mouse_y < 24) {
        menubar_draw();
    }
    
    /* Sauvegarde le fond sous le nouveau curseur et dessine */
    save_cursor_background(g_mouse_x, g_mouse_y);
    draw_cursor(g_mouse_x, g_mouse_y);
}
