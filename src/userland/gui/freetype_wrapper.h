/* src/userland/gui/freetype_wrapper.h - Wrapper FreeType pour ALOS
 * 
 * Interface simplifiée pour le rendu de polices TrueType avec FreeType.
 * Adapté pour un environnement freestanding.
 */

#ifndef FREETYPE_WRAPPER_H
#define FREETYPE_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gui_types.h"

/* Forward declaration de FT_Face */
typedef struct FT_FaceRec_* FT_Face;
typedef struct FT_LibraryRec_* FT_Library;

/* Structure représentant une police TrueType chargée */
typedef struct ft_font {
    FT_Face face;                /* Face FreeType */
    FT_Library library;          /* Library FreeType */
    const uint8_t* font_data;    /* Données de la police TTF en mémoire */
    size_t font_size;            /* Taille des données */
    uint32_t size;               /* Taille de la police en pixels */
    bool initialized;            /* Police initialisée ? */
} ft_font_t;

/* Structure pour les métriques d'un glyphe */
typedef struct {
    int32_t width;               /* Largeur du glyphe en pixels */
    int32_t height;              /* Hauteur du glyphe en pixels */
    int32_t xoff;                /* Décalage X */
    int32_t yoff;                /* Décalage Y */
    int32_t advance;             /* Avancement horizontal */
    int32_t bearing_x;           /* Bearing horizontal */
    int32_t bearing_y;           /* Bearing vertical */
} ft_glyph_metrics_t;

/* Initialise le système de polices TrueType */
int ft_init(void);

/* Libère les ressources du système TrueType */
void ft_cleanup(void);

/* Charge une police TrueType depuis la mémoire
 * @param font_data Pointeur vers les données TTF
 * @param font_size Taille des données en octets
 * @param size Taille de la police en pixels
 * @return Pointeur vers la police chargée, ou NULL en cas d'erreur
 */
ft_font_t* ft_load_font_from_memory(const uint8_t* font_data, size_t font_size, uint32_t size);

/* Libère une police chargée */
void ft_free_font(ft_font_t* font);

/* Définit la taille d'une police
 * @param font Police à modifier
 * @param size Nouvelle taille en pixels
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int ft_set_font_size(ft_font_t* font, uint32_t size);

/* Récupère les métriques d'un glyphe Unicode
 * @param font Police à utiliser
 * @param unicode Code Unicode du caractère
 * @param metrics Structure à remplir avec les métriques
 * @return 0 en cas de succès, -1 si le glyphe n'existe pas
 */
int ft_get_glyph_metrics(ft_font_t* font, uint32_t unicode, ft_glyph_metrics_t* metrics);

/* Rend un glyphe dans un buffer
 * @param font Police à utiliser
 * @param unicode Code Unicode du caractère
 * @param buffer Buffer de sortie (doit être pré-alloué, au moins width*height)
 * @param width Largeur du buffer
 * @param height Hauteur du buffer
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int ft_render_glyph(ft_font_t* font, uint32_t unicode, uint8_t* buffer, 
                    int32_t width, int32_t height);

/* Calcule la largeur d'un texte UTF-8
 * @param font Police à utiliser
 * @param text Texte UTF-8 (null-terminated)
 * @return Largeur en pixels
 */
int32_t ft_get_text_width(ft_font_t* font, const char* text);

/* Calcule la hauteur d'un texte (hauteur de ligne)
 * @param font Police à utiliser
 * @return Hauteur en pixels
 */
int32_t ft_get_text_height(ft_font_t* font);

/* Calcule les dimensions complètes d'un texte UTF-8
 * @param font Police à utiliser
 * @param text Texte UTF-8 (null-terminated)
 * @param width Pointeur pour stocker la largeur (peut être NULL)
 * @param height Pointeur pour stocker la hauteur (peut être NULL)
 */
void ft_get_text_dimensions(ft_font_t* font, const char* text, 
                            int32_t* width, int32_t* height);

/* Rend un texte UTF-8 dans un framebuffer
 * @param font Police à utiliser
 * @param text Texte UTF-8 à rendre
 * @param x Position X de départ
 * @param y Position Y de départ (baseline)
 * @param color Couleur RGBA
 * @param fb Framebuffer de destination
 */
void ft_render_text(ft_font_t* font, const char* text, int32_t x, int32_t y,
                    rgba_t color, framebuffer_t* fb);

/* Décode un caractère UTF-8 et avance le pointeur
 * @param str Pointeur vers le pointeur de la chaîne UTF-8
 * @return Code Unicode du caractère, ou 0 en cas d'erreur
 */
uint32_t ft_decode_utf8(const char** str);

#endif /* FREETYPE_WRAPPER_H */
