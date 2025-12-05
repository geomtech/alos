/* src/gui/gui_types.h - Types de base pour l'interface graphique ALOS
 * 
 * Ce fichier définit les structures et types fondamentaux utilisés
 * par tous les modules du système GUI style macOS.
 */
#ifndef GUI_TYPES_H
#define GUI_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * TYPES GÉOMÉTRIQUES
 * ============================================================================ */

/**
 * Point 2D avec coordonnées entières.
 */
typedef struct {
    int32_t x;
    int32_t y;
} point_t;

/**
 * Taille 2D (largeur, hauteur).
 */
typedef struct {
    uint32_t width;
    uint32_t height;
} size_t_gui;

/**
 * Rectangle défini par position et dimensions.
 */
typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} rect_t;

/**
 * Marges (padding/margin) pour les éléments UI.
 */
typedef struct {
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t left;
} insets_t;

/* ============================================================================
 * COULEURS
 * ============================================================================ */

/**
 * Couleur RGBA (8 bits par composante).
 * Format mémoire : 0xAARRGGBB (compatible framebuffer Limine)
 */
typedef struct {
    uint8_t r;      /* Rouge (0-255) */
    uint8_t g;      /* Vert (0-255) */
    uint8_t b;      /* Bleu (0-255) */
    uint8_t a;      /* Alpha (0=transparent, 255=opaque) */
} rgba_t;

/**
 * Convertit une structure rgba_t en valeur 32-bit ARGB.
 */
static inline uint32_t rgba_to_u32(rgba_t c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | 
           ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

/**
 * Convertit une valeur 32-bit ARGB en structure rgba_t.
 */
static inline rgba_t u32_to_rgba(uint32_t color) {
    rgba_t c;
    c.a = (color >> 24) & 0xFF;
    c.r = (color >> 16) & 0xFF;
    c.g = (color >> 8) & 0xFF;
    c.b = color & 0xFF;
    return c;
}

/**
 * Crée une couleur RGBA à partir des composantes.
 */
static inline rgba_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rgba_t c = {r, g, b, a};
    return c;
}

/**
 * Crée une couleur RGB opaque.
 */
static inline rgba_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return rgba(r, g, b, 255);
}

/* ============================================================================
 * PALETTE DE COULEURS macOS
 * ============================================================================ */

/* Couleurs système macOS */
#define COLOR_MACOS_BLUE        0xFF007AFF  /* Bleu système */
#define COLOR_MACOS_GREEN       0xFF34C759  /* Vert système */
#define COLOR_MACOS_INDIGO      0xFF5856D6  /* Indigo */
#define COLOR_MACOS_ORANGE      0xFFFF9500  /* Orange */
#define COLOR_MACOS_PINK        0xFFFF2D55  /* Rose */
#define COLOR_MACOS_PURPLE      0xFFAF52DE  /* Violet */
#define COLOR_MACOS_RED         0xFFFF3B30  /* Rouge */
#define COLOR_MACOS_TEAL        0xFF5AC8FA  /* Bleu-vert */
#define COLOR_MACOS_YELLOW      0xFFFFCC00  /* Jaune */

/* Gris système */
#define COLOR_GRAY_1            0xFFF5F5F7  /* Sidebar background */
#define COLOR_GRAY_2            0xFFE5E5EA  /* Séparateurs */
#define COLOR_GRAY_3            0xFFD1D1D6  /* Bordures légères */
#define COLOR_GRAY_4            0xFFC7C7CC  /* Texte désactivé */
#define COLOR_GRAY_5            0xFF8E8E93  /* Texte secondaire */
#define COLOR_GRAY_6            0xFF636366  /* Texte tertiaire */

/* Texte */
#define COLOR_TEXT_PRIMARY      0xFF1C1C1E  /* Texte principal */
#define COLOR_TEXT_SECONDARY    0xFF3A3A3C  /* Texte secondaire */

/* Fenêtres */
#define COLOR_WINDOW_BG         0xFFFFFFFF  /* Fond de fenêtre */
#define COLOR_TITLEBAR_BG       0xE6F6F6F6  /* Barre de titre (semi-transparent) */
#define COLOR_SIDEBAR_BG        0xFFF5F5F7  /* Sidebar */

/* Boutons de fenêtre macOS */
#define COLOR_BTN_CLOSE         0xFFFF5F57  /* Rouge - fermer */
#define COLOR_BTN_MINIMIZE      0xFFFEBC2E  /* Jaune - minimiser */
#define COLOR_BTN_MAXIMIZE      0xFF28C840  /* Vert - maximiser */

/* Dock */
#define COLOR_DOCK_BG           0xB3FFFFFF  /* Fond dock (70% opaque) */
#define COLOR_DOCK_BORDER       0x33FFFFFF  /* Bordure dock */

/* Menu bar */
#define COLOR_MENUBAR_BG        0xE6FFFFFF  /* Fond menu bar (90% opaque) */

/* ============================================================================
 * DIRECTIONS ET ALIGNEMENTS
 * ============================================================================ */

/**
 * Direction pour les dégradés.
 */
typedef enum {
    GRADIENT_HORIZONTAL,    /* Gauche vers droite */
    GRADIENT_VERTICAL,      /* Haut vers bas */
    GRADIENT_DIAGONAL_TL,   /* Haut-gauche vers bas-droite */
    GRADIENT_DIAGONAL_TR    /* Haut-droite vers bas-gauche */
} gradient_direction_t;

/**
 * Alignement horizontal du texte.
 */
typedef enum {
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT
} text_align_t;

/**
 * Alignement vertical du texte.
 */
typedef enum {
    TEXT_VALIGN_TOP,
    TEXT_VALIGN_MIDDLE,
    TEXT_VALIGN_BOTTOM
} text_valign_t;

/* ============================================================================
 * PARAMÈTRES D'EFFETS VISUELS
 * ============================================================================ */

/**
 * Paramètres pour les ombres portées.
 */
typedef struct {
    int32_t offset_x;       /* Décalage horizontal */
    int32_t offset_y;       /* Décalage vertical */
    uint32_t blur_radius;   /* Rayon de flou */
    uint32_t spread;        /* Extension de l'ombre */
    rgba_t color;           /* Couleur de l'ombre */
} shadow_params_t;

/**
 * Paramètres pour les bordures.
 */
typedef struct {
    uint32_t width;         /* Épaisseur de la bordure */
    uint32_t radius;        /* Rayon des coins arrondis */
    rgba_t color;           /* Couleur de la bordure */
} border_params_t;

/* ============================================================================
 * FRAMEBUFFER
 * ============================================================================ */

/**
 * Structure représentant un framebuffer (écran ou buffer hors-écran).
 */
typedef struct {
    uint32_t* pixels;       /* Pointeur vers les pixels (format ARGB) */
    uint32_t width;         /* Largeur en pixels */
    uint32_t height;        /* Hauteur en pixels */
    uint32_t pitch;         /* Octets par ligne (peut être > width * 4) */
    bool owns_memory;       /* true si le buffer doit être libéré */
} framebuffer_t;

/* ============================================================================
 * ÉVÉNEMENTS
 * ============================================================================ */

/**
 * Types d'événements.
 */
typedef enum {
    EVENT_NONE = 0,
    EVENT_MOUSE_MOVE,       /* Déplacement souris */
    EVENT_MOUSE_DOWN,       /* Bouton souris enfoncé */
    EVENT_MOUSE_UP,         /* Bouton souris relâché */
    EVENT_MOUSE_SCROLL,     /* Molette souris */
    EVENT_KEY_DOWN,         /* Touche enfoncée */
    EVENT_KEY_UP,           /* Touche relâchée */
    EVENT_WINDOW_CLOSE,     /* Demande de fermeture fenêtre */
    EVENT_WINDOW_RESIZE,    /* Redimensionnement fenêtre */
    EVENT_WINDOW_FOCUS,     /* Fenêtre prend le focus */
    EVENT_WINDOW_BLUR       /* Fenêtre perd le focus */
} event_type_t;

/**
 * Boutons de souris.
 */
typedef enum {
    MOUSE_BUTTON_NONE = 0,
    MOUSE_BUTTON_LEFT = 1,
    MOUSE_BUTTON_RIGHT = 2,
    MOUSE_BUTTON_MIDDLE = 4
} mouse_button_t;

/**
 * Modificateurs clavier.
 */
typedef enum {
    MOD_NONE = 0,
    MOD_SHIFT = 1,
    MOD_CTRL = 2,
    MOD_ALT = 4,
    MOD_META = 8           /* Touche Command/Windows */
} key_modifier_t;

/**
 * Structure d'événement générique.
 */
typedef struct {
    event_type_t type;      /* Type d'événement */
    uint32_t timestamp;     /* Horodatage en millisecondes */
    
    union {
        /* Événement souris */
        struct {
            point_t position;       /* Position du curseur */
            mouse_button_t button;  /* Bouton concerné */
            int32_t scroll_delta;   /* Delta de scroll (pour MOUSE_SCROLL) */
        } mouse;
        
        /* Événement clavier */
        struct {
            uint8_t scancode;       /* Scancode matériel */
            char character;         /* Caractère ASCII (si applicable) */
            key_modifier_t mods;    /* Modificateurs actifs */
        } key;
        
        /* Événement fenêtre */
        struct {
            uint32_t window_id;     /* ID de la fenêtre concernée */
            rect_t new_bounds;      /* Nouvelles dimensions (pour RESIZE) */
        } window;
    };
} event_t;

/* ============================================================================
 * FLAGS DE FENÊTRE
 * ============================================================================ */

#define WINDOW_FLAG_CLOSABLE    (1 << 0)    /* Bouton fermer visible */
#define WINDOW_FLAG_MINIMIZABLE (1 << 1)    /* Bouton minimiser visible */
#define WINDOW_FLAG_RESIZABLE   (1 << 2)    /* Fenêtre redimensionnable */
#define WINDOW_FLAG_TITLEBAR    (1 << 3)    /* Barre de titre visible */
#define WINDOW_FLAG_SHADOW      (1 << 4)    /* Ombre portée */
#define WINDOW_FLAG_ROUNDED     (1 << 5)    /* Coins arrondis */
#define WINDOW_FLAG_TRANSPARENT (1 << 6)    /* Fond semi-transparent */

/* Style de fenêtre par défaut (style macOS) */
#define WINDOW_STYLE_DEFAULT    (WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MINIMIZABLE | \
                                 WINDOW_FLAG_RESIZABLE | WINDOW_FLAG_TITLEBAR | \
                                 WINDOW_FLAG_SHADOW | WINDOW_FLAG_ROUNDED)

/* ============================================================================
 * DIMENSIONS STANDARD macOS
 * ============================================================================ */

#define MENUBAR_HEIGHT          28      /* Hauteur de la barre de menu */
#define TITLEBAR_HEIGHT         40      /* Hauteur de la barre de titre */
#define DOCK_HEIGHT             70      /* Hauteur du dock */
#define DOCK_ICON_SIZE          50      /* Taille des icônes du dock */
#define DOCK_ICON_SPACING       8       /* Espacement entre icônes */
#define DOCK_MARGIN_BOTTOM      10      /* Marge en bas de l'écran */
#define WINDOW_CORNER_RADIUS    12      /* Rayon des coins de fenêtre */
#define BUTTON_RADIUS           6       /* Rayon des boutons de fenêtre */
#define CARD_CORNER_RADIUS      10      /* Rayon des cartes de contenu */
#define WIDGET_CORNER_RADIUS    20      /* Rayon des widgets */

/* ============================================================================
 * UTILITAIRES
 * ============================================================================ */

/**
 * Vérifie si un point est dans un rectangle.
 */
static inline bool point_in_rect(point_t p, rect_t r) {
    return p.x >= r.x && p.x < (int32_t)(r.x + r.width) &&
           p.y >= r.y && p.y < (int32_t)(r.y + r.height);
}

/**
 * Vérifie si deux rectangles se chevauchent.
 */
static inline bool rects_intersect(rect_t a, rect_t b) {
    return !(a.x + (int32_t)a.width <= b.x || b.x + (int32_t)b.width <= a.x ||
             a.y + (int32_t)a.height <= b.y || b.y + (int32_t)b.height <= a.y);
}

/**
 * Calcule l'intersection de deux rectangles.
 */
static inline rect_t rect_intersect(rect_t a, rect_t b) {
    rect_t result;
    int32_t x1 = (a.x > b.x) ? a.x : b.x;
    int32_t y1 = (a.y > b.y) ? a.y : b.y;
    int32_t x2_a = a.x + (int32_t)a.width;
    int32_t x2_b = b.x + (int32_t)b.width;
    int32_t y2_a = a.y + (int32_t)a.height;
    int32_t y2_b = b.y + (int32_t)b.height;
    int32_t x2 = (x2_a < x2_b) ? x2_a : x2_b;
    int32_t y2 = (y2_a < y2_b) ? y2_a : y2_b;
    
    if (x2 > x1 && y2 > y1) {
        result.x = x1;
        result.y = y1;
        result.width = (uint32_t)(x2 - x1);
        result.height = (uint32_t)(y2 - y1);
    } else {
        result.x = 0;
        result.y = 0;
        result.width = 0;
        result.height = 0;
    }
    return result;
}

/**
 * Crée un rectangle à partir de coordonnées.
 */
static inline rect_t rect_make(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    rect_t r = {x, y, w, h};
    return r;
}

/**
 * Crée un point.
 */
static inline point_t point_make(int32_t x, int32_t y) {
    point_t p = {x, y};
    return p;
}

/**
 * Valeur absolue pour int32_t.
 */
static inline int32_t abs_i32(int32_t x) {
    return (x < 0) ? -x : x;
}

/**
 * Minimum de deux valeurs.
 */
static inline int32_t min_i32(int32_t a, int32_t b) {
    return (a < b) ? a : b;
}

/**
 * Maximum de deux valeurs.
 */
static inline int32_t max_i32(int32_t a, int32_t b) {
    return (a > b) ? a : b;
}

/**
 * Clamp une valeur entre min et max.
 */
static inline int32_t clamp_i32(int32_t val, int32_t min_val, int32_t max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

#endif /* GUI_TYPES_H */
