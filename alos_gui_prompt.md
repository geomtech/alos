# Prompt pour Cascade Code - Interface graphique ALOS (style macOS)

## Contexte du projet

Je développe **ALOS**, un OS kernel x86_64 en C qui utilise le bootloader **Limine** pour obtenir un framebuffer. Je souhaite implémenter une interface graphique moderne inspirée de **macOS** (voir capture d'écran jointe).

---

## Stack technique

- **Architecture** : x86_64
- **Langage** : C pur (C11/C17)
- **Framebuffer** : Fourni par Limine (adresse mémoire, width, height, pitch, bpp)
- **Contrainte** : Pas de bibliothèque graphique externe (tout from scratch)
- **Build system** : Makefile compatible avec la chaîne de compilation croisée x86_64-elf-gcc

---

## Style visuel souhaité (inspiré macOS)

D'après la capture d'écran fournie, voici les éléments de style à reproduire :

### 1. **Barre de menu supérieure** (Menu Bar)
- Fond **dégradé subtil bleu clair semi-transparent** avec effet de flou (glassmorphism)
- Hauteur : ~28-30px
- Contenu de gauche à droite :
  - Logo Apple (remplacer par logo ALOS)
  - Nom de l'application active en **gras** ("Music" dans l'exemple)
  - Menus textuels : File, Edit, Song, View, Controls, Account, Window, Help
  - À droite : icônes système (WiFi, recherche, batterie, horloge : "Tue Apr 1 9:41 AM")
- **Police** : SF Pro Text / San Francisco (équivalent à implémenter : police sans-serif, anti-aliasée)
- **Ombres douces** sous la barre

### 2. **Fenêtres d'application**
- **Bordures arrondies** : rayon de ~12-15px
- **Barre de titre** :
  - Hauteur : ~40px
  - Fond semi-transparent avec flou (effet vitrail)
  - 3 boutons macOS (rouge, jaune, vert) en haut à gauche, espacés de 8px
  - Titre centré ou aligné à gauche selon le contexte
- **Ombre portée** : 
  - Décalage Y : ~20-30px
  - Rayon de flou : ~40-50px
  - Opacité : 30-40%
  - Couleur : noir/gris foncé
- **Corps de fenêtre** :
  - Fond blanc ou avec dégradés subtils
  - Barre latérale (sidebar) : fond gris clair (#F5F5F7) avec items cliquables
  - Contenu principal : grilles de cartes (albums, playlists) avec **images arrondies** (rayon ~8px)

### 3. **Dock (barre d'applications en bas)**
- Position : **centrée horizontalement**, ~10px du bas de l'écran
- Hauteur : ~70-80px (adaptable)
- Fond : **semi-transparent avec flou prononcé** (glassmorphism fort)
- Bordure subtile : 1px blanc semi-transparent
- **Effet de grossissement au survol** :
  - Les icônes survolées grossissent jusqu'à ~120-150% de leur taille
  - Les icônes adjacentes grossissent légèrement (effet de propagation)
  - Animation fluide (courbe ease-out)
- **Icônes** :
  - Taille de base : ~50-60px
  - Espacement : ~8-10px
  - Réflexion subtile sous chaque icône (effet miroir)
  - Indicateur d'app active : petit point lumineux sous l'icône

### 4. **Widgets et composants**
- **Widgets dashboard** (en haut à gauche sur la capture) :
  - Calendrier avec fond dégradé translucide
  - Widget météo avec typo légère et icône
  - Bordures arrondies ~20px
  - Effet de flou d'arrière-plan (backdrop blur)
- **Cartes de contenu** (albums, playlists) :
  - Ombre douce portée
  - Bordures arrondies ~10px
  - Hover : légère élévation (ombre plus prononcée)
  - Texte : titre en gras, sous-titre en gris clair

### 5. **Player audio flottant** (en bas au centre)
- Fond blanc opaque avec bordures arrondies
- Contrôles : shuffle, précédent, play/pause, suivant, répéter
- Affichage : pochette miniature + titre + artiste
- Icônes à droite : paroles, queue, AirPlay, volume
- Ombre portée moyenne

### 6. **Palette de couleurs**
- **Bleu système macOS** : #007AFF (accents, boutons)
- **Gris clairs** : #F5F5F7 (sidebars), #E5E5EA (séparateurs)
- **Gris foncés** : #1C1C1E (texte), #3A3A3C (texte secondaire)
- **Transparence** : rgba(255,255,255,0.7) pour les effets vitrés
- **Dégradés** : subtils, du blanc vers gris très clair

### 7. **Typographie**
- **Police principale** : équivalent SF Pro / San Francisco
  - Fallback : Arial, Helvetica
- **Tailles** :
  - Menu bar : 13-14px
  - Titres fenêtres : 13px bold
  - Titres cartes : 16-18px bold
  - Corps de texte : 13-15px regular
- **Anti-aliasing** : obligatoire (subpixel si possible)

### 8. **Animations et transitions**
- **Ouverture de fenêtre** : scale de 0.8 à 1.0 + fade-in (200-300ms)
- **Hover sur cartes** : élévation progressive (150ms ease-out)
- **Dock** : grossissement fluide au survol (200ms)
- **Clics** : feedback visuel immédiat (changement de couleur/opacité)

---

## Architecture logicielle demandée

Organise le code en modules C séparés et bien commentés (commentaires en français) :

### 1. **Primitives de rendu** (`render.c/h`)
```c
// Fonctions de base
void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_rect(rect_t rect, uint32_t color);
void draw_rounded_rect(rect_t rect, uint32_t radius, uint32_t color);
void draw_line(point_t p1, point_t p2, uint32_t color);
void draw_circle(point_t center, uint32_t radius, uint32_t color);

// Alpha blending
void draw_rect_alpha(rect_t rect, rgba_t color);
void draw_gradient(rect_t rect, rgba_t color1, rgba_t color2, gradient_direction_t dir);

// Effets
void draw_shadow(rect_t rect, uint32_t radius, shadow_params_t params);
void apply_blur(rect_t region, uint32_t radius); // Gaussian blur pour glassmorphism
```

### 2. **Gestion des polices** (`font.c/h`)
```c
// Chargement bitmap font (PSF ou format custom)
font_t* load_bitmap_font(const char* path);
void draw_char(char c, point_t pos, font_t* font, uint32_t color);
void draw_text(const char* text, point_t pos, font_t* font, uint32_t color);
text_bounds_t measure_text(const char* text, font_t* font);

// Anti-aliasing basique
void draw_text_aa(const char* text, point_t pos, font_t* font, rgba_t color);
```

### 3. **Window Manager** (`wm.c/h`)
```c
// Structure de fenêtre
typedef struct window {
    uint32_t id;
    rect_t bounds;
    char title[256];
    uint32_t flags; // WINDOW_RESIZABLE, WINDOW_CLOSABLE, etc.
    bool is_focused;
    framebuffer_t* fb; // Buffer local pour le contenu
    struct window* next;
} window_t;

// API
window_t* wm_create_window(rect_t bounds, const char* title, uint32_t flags);
void wm_destroy_window(window_t* win);
void wm_draw_window(window_t* win); // Dessine bordures, ombre, barre de titre
void wm_focus_window(window_t* win);
void wm_handle_mouse(mouse_event_t* event);
```

### 4. **Compositeur** (`compositor.c/h`)
```c
// Gestion du Z-order et rendu final
void compositor_init(framebuffer_t* fb);
void compositor_add_layer(layer_t* layer);
void compositor_render(); // Compose toutes les couches vers le framebuffer principal
void compositor_invalidate_rect(rect_t rect); // Dirty rectangles
```

### 5. **Barre de menu** (`menubar.c/h`)
```c
void menubar_init();
void menubar_set_app_name(const char* name);
void menubar_add_menu(const char* label);
void menubar_draw();
void menubar_handle_click(point_t click_pos);
```

### 6. **Dock** (`dock.c/h`)
```c
typedef struct dock_item {
    char name[64];
    uint32_t icon_data[64*64]; // Image 64x64 RGBA
    bool is_running;
} dock_item_t;

void dock_init();
void dock_add_app(dock_item_t* item);
void dock_draw();
void dock_handle_mouse_move(point_t mouse_pos); // Effet de grossissement
void dock_handle_click(point_t click_pos);
```

### 7. **Gestionnaire d'événements** (`events.c/h`)
```c
typedef enum {
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP
} event_type_t;

void event_dispatch(event_t* event);
```

---

## Livrables attendus

### Fichiers C/H
- `render.c/h` - Primitives de dessin
- `font.c/h` - Rendu de texte
- `wm.c/h` - Window manager
- `compositor.c/h` - Compositeur
- `menubar.c/h` - Barre de menu
- `dock.c/h` - Dock avec effet magnification
- `events.c/h` - Système d'événements
- `gui.c/h` - Point d'entrée principal du GUI

### Makefile
```makefile
# Compatible avec x86_64-elf-gcc
CC = x86_64-elf-gcc
CFLAGS = -Wall -Wextra -O2 -ffreestanding -nostdlib -mcmodel=kernel

GUI_OBJS = render.o font.o wm.o compositor.o menubar.o dock.o events.o gui.o

gui.a: $(GUI_OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
```

### Documentation
- `INTEGRATION.md` : Comment intégrer le GUI dans ALOS
- `API.md` : Documentation des fonctions publiques
- Exemples d'utilisation pour tester chaque module

---

## Démarche progressive

**Phase 1** : Primitives de base
- `draw_pixel`, `draw_rect`, `draw_line`
- Fonction de blending alpha
- Test : afficher un rectangle semi-transparent

**Phase 2** : Formes avancées et effets
- `draw_rounded_rect` avec anti-aliasing
- `draw_shadow` (ombre portée)
- `draw_gradient`
- Test : fenêtre avec bordures arrondies et ombre

**Phase 3** : Rendu de texte
- Chargement d'une bitmap font PSF
- `draw_text` avec support UTF-8 basique
- Test : afficher "Hello ALOS" en blanc sur fond noir

**Phase 4** : Window Manager
- Structure `window_t`
- Dessin de la barre de titre macOS (3 boutons)
- Gestion du drag pour déplacer les fenêtres
- Test : créer 2 fenêtres superposées

**Phase 5** : Compositeur
- Z-order des fenêtres
- Dirty rectangles pour optimiser le rendu
- Test : animer une fenêtre qui passe au premier plan

**Phase 6** : Menu bar
- Barre semi-transparente avec flou
- Horloge système à droite
- Menus déroulants au clic
- Test : afficher l'heure et changer le nom de l'app

**Phase 7** : Dock
- Positionnement centré en bas
- Effet de grossissement au survol de la souris
- Indicateur d'application active
- Test : 5 icônes dans le dock avec animation

**Phase 8** : Intégration finale
- Connecter tous les modules
- Optimisations de performance
- Polish des animations

---

## Contraintes et bonnes pratiques

### Performance
- **Dirty rectangles** : ne redessiner que les zones modifiées
- **Double buffering** : utiliser un framebuffer temporaire
- **Clipping** : ne pas dessiner hors des bounds visibles
- **Cache des glyphes** : pré-rendre les caractères communs

### Qualité du code
- **Commentaires en français** pour toutes les fonctions publiques
- **Gestion d'erreurs** : vérifier les pointeurs NULL, bounds checking
- **Pas de fuites mémoire** : libérer les ressources allouées
- **Style cohérent** : K&R ou Allman (à choisir)

### Compatibilité
- Tester avec différentes résolutions (1920x1080, 2560x1440, etc.)
- Supporter au minimum RGB32 (8 bits par composante)
- Gérer les cas edge (fenêtre hors écran, etc.)

---

## Inspiration visuelle (références)

Voir la capture d'écran jointe de macOS avec :
- Apple Music avec interface moderne
- Widgets dashboard semi-transparents
- Dock avec effet de grossissement
- Fenêtre avec ombre portée prononcée
- Palette de couleurs bleues/blanches

**Objectif** : reproduire le "look and feel" macOS moderne dans un environnement bare-metal x86_64.

---

## Questions pour toi (Cascade Code)

1. Proposes-tu une architecture différente/meilleure ?
2. Quelle méthode utiliser pour le gaussian blur (effet vitre) de manière performante ?
3. Comment gérer l'anti-aliasing des bordures arrondies sans lib externe ?
4. Suggères-tu un format de police bitmap plus adapté que PSF ?

---

**Commence par implémenter les primitives de rendu (`render.c/h`) avec les fonctions de base listées ci-dessus. Code propre, commenté en français, prêt pour la production.**