/* src/gui/font.h - Système de rendu de texte pour ALOS
 * 
 * Ce module gère le chargement et le rendu des polices bitmap.
 * Supporte le format PSF (PC Screen Font) et un format bitmap simple.
 * Inclut un rendu avec anti-aliasing basique.
 */
#ifndef FONT_H
#define FONT_H

#include "gui_types.h"

/* ============================================================================
 * STRUCTURES
 * ============================================================================ */

/**
 * Style de police.
 */
typedef enum {
    FONT_STYLE_REGULAR = 0,
    FONT_STYLE_BOLD = 1,
    FONT_STYLE_ITALIC = 2,
    FONT_STYLE_BOLD_ITALIC = 3
} font_style_t;

/**
 * Structure représentant une police bitmap.
 */
typedef struct {
    const uint8_t* glyphs;      /* Données des glyphes (bitmap 1-bit) */
    uint32_t glyph_width;       /* Largeur d'un glyphe en pixels */
    uint32_t glyph_height;      /* Hauteur d'un glyphe en pixels */
    uint32_t bytes_per_glyph;   /* Octets par glyphe */
    uint32_t first_char;        /* Premier caractère (généralement 0 ou 32) */
    uint32_t num_chars;         /* Nombre de caractères */
    font_style_t style;         /* Style de la police */
    const char* name;           /* Nom de la police */
} font_t;

/**
 * Dimensions d'un texte rendu.
 */
typedef struct {
    uint32_t width;             /* Largeur totale en pixels */
    uint32_t height;            /* Hauteur totale en pixels */
    uint32_t baseline;          /* Position de la ligne de base */
} text_bounds_t;

/**
 * Options de rendu de texte.
 */
typedef struct {
    text_align_t align;         /* Alignement horizontal */
    text_valign_t valign;       /* Alignement vertical */
    bool wrap;                  /* Retour à la ligne automatique */
    uint32_t max_width;         /* Largeur max (pour wrap) */
    int32_t letter_spacing;     /* Espacement entre lettres */
    int32_t line_height;        /* Hauteur de ligne (0 = auto) */
    bool antialias;             /* Anti-aliasing activé */
} text_options_t;

/* ============================================================================
 * POLICES INTÉGRÉES
 * ============================================================================ */

/**
 * Police système par défaut (Roboto 8x16).
 */
extern const font_t* font_system;

/**
 * Police Roboto (8x16, moderne).
 */
extern const font_t* font_roboto;

/**
 * Police VGA (8x16, classique).
 */
extern const font_t* font_vga;

/**
 * Police système petite (8x8).
 */
extern const font_t* font_small;

/**
 * Police système grande (16x32).
 */
extern const font_t* font_large;

/* ============================================================================
 * INITIALISATION
 * ============================================================================ */

/**
 * Initialise le système de polices.
 * Charge les polices intégrées.
 * 
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int font_init(void);

/* ============================================================================
 * CHARGEMENT DE POLICES
 * ============================================================================ */

/**
 * Charge une police depuis un fichier PSF.
 * 
 * @param data Pointeur vers les données PSF en mémoire
 * @param size Taille des données
 * @return Pointeur vers la police chargée, ou NULL en cas d'erreur
 */
font_t* font_load_psf(const uint8_t* data, size_t size);

/**
 * Charge une police depuis des données bitmap brutes.
 * 
 * @param glyphs Données des glyphes (1 bit par pixel, MSB first)
 * @param width Largeur d'un glyphe
 * @param height Hauteur d'un glyphe
 * @param first_char Premier caractère
 * @param num_chars Nombre de caractères
 * @return Pointeur vers la police créée
 */
font_t* font_create_bitmap(const uint8_t* glyphs, uint32_t width, uint32_t height,
                           uint32_t first_char, uint32_t num_chars);

/**
 * Libère une police chargée.
 */
void font_free(font_t* font);

/* ============================================================================
 * RENDU DE TEXTE
 * ============================================================================ */

/**
 * Dessine un caractère à la position donnée.
 * 
 * @param c Caractère à dessiner
 * @param pos Position (coin haut-gauche)
 * @param font Police à utiliser
 * @param color Couleur ARGB
 */
void draw_char(char c, point_t pos, const font_t* font, uint32_t color);

/**
 * Dessine un caractère avec alpha blending.
 */
void draw_char_alpha(char c, point_t pos, const font_t* font, rgba_t color);

/**
 * Dessine une chaîne de caractères.
 * 
 * @param text Texte à dessiner (null-terminated)
 * @param pos Position (coin haut-gauche)
 * @param font Police à utiliser
 * @param color Couleur ARGB
 */
void draw_text(const char* text, point_t pos, const font_t* font, uint32_t color);

/**
 * Dessine du texte avec alpha blending.
 */
void draw_text_alpha(const char* text, point_t pos, const font_t* font, rgba_t color);

/**
 * Dessine du texte avec options avancées.
 * 
 * @param text Texte à dessiner
 * @param bounds Rectangle de destination
 * @param font Police à utiliser
 * @param color Couleur
 * @param options Options de rendu
 */
void draw_text_ex(const char* text, rect_t bounds, const font_t* font, 
                  rgba_t color, text_options_t options);

/**
 * Dessine du texte avec anti-aliasing.
 * Utilise un sur-échantillonnage pour lisser les bords.
 */
void draw_text_aa(const char* text, point_t pos, const font_t* font, rgba_t color);

/* ============================================================================
 * MESURES DE TEXTE
 * ============================================================================ */

/**
 * Mesure les dimensions d'un texte.
 * 
 * @param text Texte à mesurer
 * @param font Police à utiliser
 * @return Dimensions du texte
 */
text_bounds_t measure_text(const char* text, const font_t* font);

/**
 * Mesure les dimensions d'un texte avec options.
 */
text_bounds_t measure_text_ex(const char* text, const font_t* font, text_options_t options);

/**
 * Calcule la largeur d'un caractère.
 */
uint32_t char_width(char c, const font_t* font);

/**
 * Calcule le nombre de caractères qui tiennent dans une largeur donnée.
 */
uint32_t text_fit_width(const char* text, const font_t* font, uint32_t max_width);

/* ============================================================================
 * UTILITAIRES
 * ============================================================================ */

/**
 * Retourne les options de texte par défaut.
 */
text_options_t text_options_default(void);

/**
 * Retourne les options pour du texte centré.
 */
text_options_t text_options_centered(void);

/**
 * Retourne les options pour du texte aligné à droite.
 */
text_options_t text_options_right(void);

#endif /* FONT_H */
