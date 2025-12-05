# ALOS GUI - Référence API

## gui.h - Point d'entrée principal

### Initialisation

```c
int gui_init(struct limine_framebuffer* fb);
```
Initialise le système GUI complet.
- **Paramètres** : `fb` - Framebuffer Limine
- **Retour** : 0 en cas de succès, -1 en cas d'erreur

```c
void gui_shutdown(void);
```
Libère toutes les ressources du GUI.

### Boucle principale

```c
void gui_main_loop(void);
```
Boucle principale du GUI (bloquante). Combine traitement des événements, mise à jour et rendu.

```c
void gui_request_quit(void);
```
Demande l'arrêt de la boucle principale.

```c
void gui_process_events(void);
void gui_update(float delta_time);
void gui_render(void);
```
Fonctions individuelles pour un contrôle plus fin.

### Configuration

```c
void gui_set_wallpaper_color(uint32_t color);
void gui_set_wallpaper_gradient(rgba_t color1, rgba_t color2, gradient_direction_t dir);
```
Configure le fond d'écran.

---

## render.h - Primitives de dessin

### Initialisation

```c
int render_init(struct limine_framebuffer* fb);
framebuffer_t* render_get_framebuffer(void);
void render_get_screen_size(uint32_t* width, uint32_t* height);
```

### Double buffering

```c
void render_set_double_buffer(bool enabled);
void render_flip(void);
framebuffer_t* render_get_active_buffer(void);
```

### Clipping

```c
void render_set_clip(const rect_t* clip);
rect_t render_get_clip(void);
void render_push_clip(rect_t clip);
void render_pop_clip(void);
```

### Primitives de base

```c
void draw_pixel(int32_t x, int32_t y, uint32_t color);
void draw_pixel_alpha(int32_t x, int32_t y, rgba_t color);
uint32_t read_pixel(int32_t x, int32_t y);
void draw_line(point_t p1, point_t p2, uint32_t color);
void draw_line_aa(point_t p1, point_t p2, rgba_t color);
void draw_hline(int32_t x1, int32_t x2, int32_t y, uint32_t color);
void draw_vline(int32_t x, int32_t y1, int32_t y2, uint32_t color);
```

### Rectangles

```c
void draw_rect(rect_t rect, uint32_t color);
void draw_rect_alpha(rect_t rect, rgba_t color);
void draw_rect_outline(rect_t rect, uint32_t color, uint32_t thickness);
void draw_rounded_rect(rect_t rect, uint32_t radius, uint32_t color);
void draw_rounded_rect_alpha(rect_t rect, uint32_t radius, rgba_t color);
```

### Cercles

```c
void draw_circle(point_t center, uint32_t radius, uint32_t color);
void draw_circle_alpha(point_t center, uint32_t radius, rgba_t color);
void draw_circle_outline(point_t center, uint32_t radius, uint32_t color, uint32_t thickness);
void draw_ellipse(point_t center, uint32_t rx, uint32_t ry, uint32_t color);
```

### Dégradés

```c
void draw_gradient(rect_t rect, rgba_t color1, rgba_t color2, gradient_direction_t dir);
void draw_rounded_gradient(rect_t rect, uint32_t radius, rgba_t color1, rgba_t color2, gradient_direction_t dir);
```

### Effets

```c
void draw_shadow(rect_t rect, uint32_t radius, shadow_params_t params);
void apply_blur(rect_t region, uint32_t radius);
void apply_blur_fast(rect_t region, uint32_t radius);
void draw_glass_rect(rect_t rect, uint32_t radius, rgba_t tint, uint32_t blur_radius);
```

### Bitmaps

```c
void draw_bitmap(point_t dest, const uint32_t* src, uint32_t src_width, uint32_t src_height);
void draw_bitmap_alpha(point_t dest, const uint32_t* src, uint32_t src_width, uint32_t src_height);
void draw_bitmap_scaled(rect_t dest_rect, const uint32_t* src, uint32_t src_width, uint32_t src_height);
```

### Utilitaires

```c
void render_clear(uint32_t color);
uint32_t blend_colors(uint32_t bg, rgba_t fg);
rgba_t lerp_color(rgba_t c1, rgba_t c2, float t);
shadow_params_t shadow_default(void);
shadow_params_t shadow_card(void);
shadow_params_t shadow_window(void);
```

---

## font.h - Rendu de texte

### Polices intégrées

```c
extern const font_t* font_system;  // 8x16 VGA
extern const font_t* font_small;   // 8x8 (TODO)
extern const font_t* font_large;   // 16x32 (TODO)
```

### Rendu

```c
void draw_char(char c, point_t pos, const font_t* font, uint32_t color);
void draw_char_alpha(char c, point_t pos, const font_t* font, rgba_t color);
void draw_text(const char* text, point_t pos, const font_t* font, uint32_t color);
void draw_text_alpha(const char* text, point_t pos, const font_t* font, rgba_t color);
void draw_text_ex(const char* text, rect_t bounds, const font_t* font, rgba_t color, text_options_t options);
```

### Mesures

```c
text_bounds_t measure_text(const char* text, const font_t* font);
uint32_t char_width(char c, const font_t* font);
uint32_t text_fit_width(const char* text, const font_t* font, uint32_t max_width);
```

### Options

```c
text_options_t text_options_default(void);
text_options_t text_options_centered(void);
text_options_t text_options_right(void);
```

---

## wm.h - Window Manager

### Création/Destruction

```c
window_t* wm_create_window(rect_t bounds, const char* title, uint32_t flags);
void wm_destroy_window(window_t* win);
```

### Focus

```c
void wm_focus_window(window_t* win);
window_t* wm_get_focused_window(void);
```

### Manipulation

```c
void wm_move_window(window_t* win, int32_t x, int32_t y);
void wm_resize_window(window_t* win, uint32_t width, uint32_t height);
void wm_minimize_window(window_t* win);
void wm_maximize_window(window_t* win);
void wm_restore_window(window_t* win);
void wm_close_window(window_t* win);
```

### Recherche

```c
window_t* wm_find_window_at(point_t pos);
window_t* wm_get_window_by_id(uint32_t id);
window_t* wm_get_first_window(void);
```

### Structure window_t

```c
typedef struct window {
    uint32_t id;
    char title[256];
    rect_t bounds;
    rect_t content_bounds;
    uint32_t flags;
    bool is_focused;
    bool is_minimized;
    bool is_maximized;
    
    void* user_data;
    void (*on_draw)(struct window* win);
    void (*on_close)(struct window* win);
    void (*on_resize)(struct window* win, uint32_t w, uint32_t h);
    void (*on_focus)(struct window* win, bool focused);
} window_t;
```

---

## menubar.h - Barre de menu

### Configuration

```c
void menubar_set_app_name(const char* name);
void menubar_set_time(uint8_t hour, uint8_t minute);
```

### Menus

```c
menu_t* menubar_add_menu(const char* label);
void menubar_add_item(menu_t* menu, const char* label, const char* shortcut, void (*on_click)(void));
void menubar_add_separator(menu_t* menu);
```

---

## dock.h - Dock

### Gestion des items

```c
dock_item_t* dock_add_app(const char* name, const uint32_t* icon);
void dock_remove_app(dock_item_t* item);
void dock_set_running(dock_item_t* item, bool running);
void dock_bounce(dock_item_t* item);
```

### Structure dock_item_t

```c
typedef struct {
    char name[64];
    uint32_t icon[64 * 64];
    bool has_icon;
    bool is_running;
    void (*on_click)(void);
} dock_item_t;
```

---

## events.h - Événements

### Génération d'événements

```c
void events_mouse_move(int32_t x, int32_t y);
void events_mouse_button(mouse_button_t button, bool pressed);
void events_mouse_scroll(int32_t delta);
void events_key(uint8_t scancode, char character, bool pressed, key_modifier_t mods);
```

### Traitement

```c
void events_push(event_t* event);
event_t* events_pop(void);
bool events_empty(void);
void events_process(void);
```

### État

```c
point_t events_get_mouse_pos(void);
key_modifier_t events_get_modifiers(void);
```

---

## gui_types.h - Types de base

### Géométrie

```c
typedef struct { int32_t x, y; } point_t;
typedef struct { int32_t x, y; uint32_t width, height; } rect_t;
```

### Couleurs

```c
typedef struct { uint8_t r, g, b, a; } rgba_t;

rgba_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
rgba_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t rgba_to_u32(rgba_t c);
rgba_t u32_to_rgba(uint32_t color);
```

### Événements

```c
typedef enum {
    EVENT_MOUSE_MOVE, EVENT_MOUSE_DOWN, EVENT_MOUSE_UP, EVENT_MOUSE_SCROLL,
    EVENT_KEY_DOWN, EVENT_KEY_UP,
    EVENT_WINDOW_CLOSE, EVENT_WINDOW_RESIZE, EVENT_WINDOW_FOCUS, EVENT_WINDOW_BLUR
} event_type_t;

typedef enum { MOUSE_BUTTON_LEFT=1, MOUSE_BUTTON_RIGHT=2, MOUSE_BUTTON_MIDDLE=4 } mouse_button_t;
typedef enum { MOD_SHIFT=1, MOD_CTRL=2, MOD_ALT=4, MOD_META=8 } key_modifier_t;
```

### Utilitaires

```c
bool point_in_rect(point_t p, rect_t r);
bool rects_intersect(rect_t a, rect_t b);
rect_t rect_intersect(rect_t a, rect_t b);
rect_t rect_make(int32_t x, int32_t y, uint32_t w, uint32_t h);
point_t point_make(int32_t x, int32_t y);
```
