# ALOS GUI - Interface Graphique Style macOS

## Vue d'ensemble

L'interface graphique ALOS est un système de fenêtrage moderne inspiré de macOS, implémenté entièrement from scratch en C pour un environnement bare-metal x86_64.

## Architecture

```
src/gui/
├── gui_types.h      # Types de base (point, rect, rgba, events)
├── render.h/c       # Primitives de dessin
├── font.h/c         # Rendu de texte bitmap
├── compositor.h/c   # Gestion des couches et dirty rectangles
├── wm.h/c           # Window Manager
├── menubar.h/c      # Barre de menu supérieure
├── dock.h/c         # Dock avec effet de grossissement
├── events.h/c       # Système d'événements
└── gui.h/c          # Point d'entrée principal
```

## Modules

### 1. Primitives de rendu (`render.c/h`)

Fonctions de dessin de base :

```c
// Pixels et lignes
void draw_pixel(int32_t x, int32_t y, uint32_t color);
void draw_line(point_t p1, point_t p2, uint32_t color);

// Rectangles
void draw_rect(rect_t rect, uint32_t color);
void draw_rounded_rect(rect_t rect, uint32_t radius, uint32_t color);
void draw_rect_alpha(rect_t rect, rgba_t color);

// Cercles
void draw_circle(point_t center, uint32_t radius, uint32_t color);

// Dégradés
void draw_gradient(rect_t rect, rgba_t c1, rgba_t c2, gradient_direction_t dir);

// Effets
void draw_shadow(rect_t rect, uint32_t radius, shadow_params_t params);
void apply_blur(rect_t region, uint32_t radius);
```

### 2. Polices (`font.c/h`)

Rendu de texte avec police bitmap VGA 8x16 intégrée :

```c
void draw_text(const char* text, point_t pos, const font_t* font, uint32_t color);
void draw_text_alpha(const char* text, point_t pos, const font_t* font, rgba_t color);
text_bounds_t measure_text(const char* text, const font_t* font);
```

### 3. Compositeur (`compositor.c/h`)

Gère le Z-order des couches et les dirty rectangles :

```c
layer_t* compositor_create_layer(layer_type_t type, rect_t bounds);
void compositor_add_layer(layer_t* layer);
void compositor_invalidate_rect(rect_t rect);
void compositor_render(void);
```

Types de couches (du fond vers l'avant) :
- `LAYER_BACKGROUND` - Fond d'écran
- `LAYER_DESKTOP` - Icônes du bureau
- `LAYER_WINDOW` - Fenêtres normales
- `LAYER_PANEL` - Menu bar
- `LAYER_DOCK` - Dock
- `LAYER_POPUP` - Menus déroulants
- `LAYER_OVERLAY` - Notifications

### 4. Window Manager (`wm.c/h`)

Gestion des fenêtres style macOS :

```c
window_t* wm_create_window(rect_t bounds, const char* title, uint32_t flags);
void wm_destroy_window(window_t* win);
void wm_focus_window(window_t* win);
void wm_move_window(window_t* win, int32_t x, int32_t y);
void wm_resize_window(window_t* win, uint32_t w, uint32_t h);
```

Flags de fenêtre :
- `WINDOW_FLAG_CLOSABLE` - Bouton fermer
- `WINDOW_FLAG_MINIMIZABLE` - Bouton minimiser
- `WINDOW_FLAG_RESIZABLE` - Redimensionnable
- `WINDOW_FLAG_TITLEBAR` - Barre de titre
- `WINDOW_FLAG_SHADOW` - Ombre portée
- `WINDOW_FLAG_ROUNDED` - Coins arrondis

### 5. Menu Bar (`menubar.c/h`)

Barre de menu supérieure :

```c
void menubar_set_app_name(const char* name);
menu_t* menubar_add_menu(const char* label);
void menubar_add_item(menu_t* menu, const char* label, const char* shortcut, void (*on_click)(void));
void menubar_set_time(uint8_t hour, uint8_t minute);
```

### 6. Dock (`dock.c/h`)

Dock avec effet de grossissement :

```c
dock_item_t* dock_add_app(const char* name, const uint32_t* icon);
void dock_set_running(dock_item_t* item, bool running);
void dock_bounce(dock_item_t* item);
```

### 7. Événements (`events.c/h`)

Système d'événements :

```c
void events_mouse_move(int32_t x, int32_t y);
void events_mouse_button(mouse_button_t button, bool pressed);
void events_key(uint8_t scancode, char character, bool pressed, key_modifier_t mods);
void events_process(void);
```

## Utilisation

### Initialisation

```c
#include "gui/gui.h"

// Dans kernel_main() après l'initialisation du framebuffer
extern struct limine_framebuffer_request framebuffer_request;

void kernel_main(void) {
    // ... initialisation du kernel ...
    
    // Récupère le framebuffer Limine
    struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
    
    // Initialise le GUI
    if (gui_init(fb) == 0) {
        // Configure les menus et le dock de démo
        gui_setup_demo_menus();
        gui_setup_demo_dock();
        
        // Crée une fenêtre de démo
        gui_create_demo_window("Ma Fenêtre", 100, 100);
        
        // Lance la boucle principale
        gui_main_loop();
    }
}
```

### Création d'une fenêtre personnalisée

```c
void my_window_draw(window_t* win) {
    // Dessine le contenu de la fenêtre
    draw_rect(win->content_bounds, 0xFFFFFFFF);
    draw_text("Hello!", point_make(win->content_bounds.x + 10, 
                                   win->content_bounds.y + 10),
              font_system, 0xFF000000);
}

void create_my_window(void) {
    rect_t bounds = {100, 100, 400, 300};
    window_t* win = wm_create_window(bounds, "Ma Fenêtre", WINDOW_STYLE_DEFAULT);
    win->on_draw = my_window_draw;
}
```

### Ajout d'une application au dock

```c
// Icône 64x64 RGBA (optionnelle)
uint32_t my_icon[64 * 64] = { /* ... */ };

dock_item_t* app = dock_add_app("Mon App", my_icon);
app->is_running = true;
app->on_click = my_app_clicked;
```

## Palette de couleurs

Couleurs système macOS définies dans `gui_types.h` :

| Constante | Valeur | Description |
|-----------|--------|-------------|
| `COLOR_MACOS_BLUE` | `#007AFF` | Bleu système |
| `COLOR_MACOS_GREEN` | `#34C759` | Vert système |
| `COLOR_MACOS_RED` | `#FF3B30` | Rouge système |
| `COLOR_BTN_CLOSE` | `#FF5F57` | Bouton fermer |
| `COLOR_BTN_MINIMIZE` | `#FEBC2E` | Bouton minimiser |
| `COLOR_BTN_MAXIMIZE` | `#28C840` | Bouton maximiser |

## Dimensions standard

| Constante | Valeur | Description |
|-----------|--------|-------------|
| `MENUBAR_HEIGHT` | 28px | Hauteur menu bar |
| `TITLEBAR_HEIGHT` | 40px | Hauteur barre de titre |
| `DOCK_HEIGHT` | 70px | Hauteur du dock |
| `DOCK_ICON_SIZE` | 50px | Taille icônes dock |
| `WINDOW_CORNER_RADIUS` | 12px | Rayon coins fenêtre |

## Performance

### Double buffering
Le système utilise un double buffer pour éviter le scintillement. Activé par défaut.

### Dirty rectangles
Seules les zones modifiées sont redessinées. Utilisez `compositor_invalidate_rect()` pour marquer une zone comme modifiée.

### Clipping
Le clipping est géré automatiquement. Utilisez `render_push_clip()` / `render_pop_clip()` pour des zones personnalisées.

## Limitations actuelles

- Pas de support souris matérielle (nécessite driver PS/2 ou USB)
- Police bitmap uniquement (pas de TrueType)
- Pas d'accélération matérielle
- Flou gaussien simplifié (box blur)

## TODO

- [ ] Driver souris PS/2
- [ ] Support polices PSF
- [ ] Animations de fenêtre (ouverture/fermeture)
- [ ] Widgets (boutons, champs de texte, etc.)
- [ ] Thèmes (mode sombre)
- [ ] Drag & drop entre fenêtres
