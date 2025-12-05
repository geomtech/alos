/* src/gui/render.h - Primitives de rendu graphique pour ALOS
 * 
 * Ce module fournit les fonctions de dessin de base :
 * - Pixels, lignes, rectangles
 * - Formes avec coins arrondis
 * - Alpha blending et dégradés
 * - Effets visuels (ombres, flou)
 */
#ifndef RENDER_H
#define RENDER_H

#include "gui_types.h"
#include "../include/limine.h"

/* ============================================================================
 * INITIALISATION
 * ============================================================================ */

/**
 * Initialise le système de rendu avec le framebuffer Limine.
 * 
 * @param fb Pointeur vers le framebuffer Limine
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int render_init(struct limine_framebuffer* fb);

/**
 * Retourne le framebuffer principal.
 */
framebuffer_t* render_get_framebuffer(void);

/**
 * Retourne les dimensions de l'écran.
 */
void render_get_screen_size(uint32_t* width, uint32_t* height);

/* ============================================================================
 * DOUBLE BUFFERING
 * ============================================================================ */

/**
 * Active ou désactive le double buffering.
 * Quand activé, les dessins vont dans un buffer temporaire
 * et sont copiés vers l'écran avec render_flip().
 */
void render_set_double_buffer(bool enabled);

/**
 * Copie le back buffer vers le framebuffer principal.
 * Ne fait rien si le double buffering est désactivé.
 */
void render_flip(void);

/**
 * Retourne le buffer actif (back buffer si double buffering, sinon front).
 */
framebuffer_t* render_get_active_buffer(void);

/* ============================================================================
 * CLIPPING
 * ============================================================================ */

/**
 * Définit la zone de clipping (les dessins hors de cette zone sont ignorés).
 * 
 * @param clip Rectangle de clipping (NULL pour désactiver)
 */
void render_set_clip(const rect_t* clip);

/**
 * Récupère la zone de clipping actuelle.
 */
rect_t render_get_clip(void);

/**
 * Pousse une nouvelle zone de clipping sur la pile.
 * La nouvelle zone est l'intersection avec la zone actuelle.
 */
void render_push_clip(rect_t clip);

/**
 * Restaure la zone de clipping précédente.
 */
void render_pop_clip(void);

/* ============================================================================
 * PRIMITIVES DE BASE
 * ============================================================================ */

/**
 * Dessine un pixel à la position donnée.
 * 
 * @param x Coordonnée X
 * @param y Coordonnée Y
 * @param color Couleur ARGB 32-bit
 */
void draw_pixel(int32_t x, int32_t y, uint32_t color);

/**
 * Dessine un pixel avec alpha blending.
 * 
 * @param x Coordonnée X
 * @param y Coordonnée Y
 * @param color Couleur RGBA
 */
void draw_pixel_alpha(int32_t x, int32_t y, rgba_t color);

/**
 * Lit la couleur d'un pixel.
 * 
 * @param x Coordonnée X
 * @param y Coordonnée Y
 * @return Couleur du pixel (0 si hors limites)
 */
uint32_t read_pixel(int32_t x, int32_t y);

/**
 * Dessine une ligne entre deux points (algorithme de Bresenham).
 * 
 * @param p1 Point de départ
 * @param p2 Point d'arrivée
 * @param color Couleur ARGB
 */
void draw_line(point_t p1, point_t p2, uint32_t color);

/**
 * Dessine une ligne avec anti-aliasing (algorithme de Wu).
 */
void draw_line_aa(point_t p1, point_t p2, rgba_t color);

/**
 * Dessine une ligne horizontale (optimisée).
 */
void draw_hline(int32_t x1, int32_t x2, int32_t y, uint32_t color);

/**
 * Dessine une ligne verticale (optimisée).
 */
void draw_vline(int32_t x, int32_t y1, int32_t y2, uint32_t color);

/* ============================================================================
 * RECTANGLES
 * ============================================================================ */

/**
 * Remplit un rectangle avec une couleur unie.
 * 
 * @param rect Rectangle à remplir
 * @param color Couleur ARGB
 */
void draw_rect(rect_t rect, uint32_t color);

/**
 * Remplit un rectangle avec alpha blending.
 */
void draw_rect_alpha(rect_t rect, rgba_t color);

/**
 * Dessine le contour d'un rectangle.
 * 
 * @param rect Rectangle
 * @param color Couleur ARGB
 * @param thickness Épaisseur du contour
 */
void draw_rect_outline(rect_t rect, uint32_t color, uint32_t thickness);

/**
 * Remplit un rectangle avec coins arrondis.
 * 
 * @param rect Rectangle
 * @param radius Rayon des coins
 * @param color Couleur ARGB
 */
void draw_rounded_rect(rect_t rect, uint32_t radius, uint32_t color);

/**
 * Remplit un rectangle arrondi avec alpha blending.
 */
void draw_rounded_rect_alpha(rect_t rect, uint32_t radius, rgba_t color);

/**
 * Dessine le contour d'un rectangle arrondi.
 */
void draw_rounded_rect_outline(rect_t rect, uint32_t radius, uint32_t color, uint32_t thickness);

/* ============================================================================
 * CERCLES ET ELLIPSES
 * ============================================================================ */

/**
 * Remplit un cercle.
 * 
 * @param center Centre du cercle
 * @param radius Rayon
 * @param color Couleur ARGB
 */
void draw_circle(point_t center, uint32_t radius, uint32_t color);

/**
 * Remplit un cercle avec alpha blending.
 */
void draw_circle_alpha(point_t center, uint32_t radius, rgba_t color);

/**
 * Dessine le contour d'un cercle.
 */
void draw_circle_outline(point_t center, uint32_t radius, uint32_t color, uint32_t thickness);

/**
 * Remplit une ellipse.
 */
void draw_ellipse(point_t center, uint32_t rx, uint32_t ry, uint32_t color);

/* ============================================================================
 * DÉGRADÉS
 * ============================================================================ */

/**
 * Remplit un rectangle avec un dégradé linéaire.
 * 
 * @param rect Rectangle à remplir
 * @param color1 Couleur de départ
 * @param color2 Couleur d'arrivée
 * @param dir Direction du dégradé
 */
void draw_gradient(rect_t rect, rgba_t color1, rgba_t color2, gradient_direction_t dir);

/**
 * Remplit un rectangle arrondi avec un dégradé.
 */
void draw_rounded_gradient(rect_t rect, uint32_t radius, rgba_t color1, rgba_t color2, gradient_direction_t dir);

/* ============================================================================
 * EFFETS VISUELS
 * ============================================================================ */

/**
 * Dessine une ombre portée sous un rectangle.
 * L'ombre est dessinée AVANT le rectangle lui-même.
 * 
 * @param rect Rectangle source
 * @param radius Rayon des coins (0 pour rectangle normal)
 * @param params Paramètres de l'ombre
 */
void draw_shadow(rect_t rect, uint32_t radius, shadow_params_t params);

/**
 * Applique un flou gaussien sur une région.
 * Utilisé pour l'effet glassmorphism (vitre).
 * 
 * @param region Zone à flouter
 * @param radius Rayon du flou (1-20 recommandé)
 */
void apply_blur(rect_t region, uint32_t radius);

/**
 * Applique un flou gaussien rapide (box blur itéré).
 * Plus rapide que apply_blur mais moins précis.
 */
void apply_blur_fast(rect_t region, uint32_t radius);

/**
 * Dessine un rectangle avec effet vitre (glassmorphism).
 * Combine flou d'arrière-plan + couleur semi-transparente.
 * 
 * @param rect Rectangle
 * @param radius Rayon des coins
 * @param tint Couleur de teinte (avec alpha)
 * @param blur_radius Rayon du flou
 */
void draw_glass_rect(rect_t rect, uint32_t radius, rgba_t tint, uint32_t blur_radius);

/* ============================================================================
 * IMAGES ET BITMAPS
 * ============================================================================ */

/**
 * Copie un bitmap vers le framebuffer.
 * 
 * @param dest Position de destination
 * @param src Pointeur vers les pixels source (format ARGB)
 * @param src_width Largeur du bitmap source
 * @param src_height Hauteur du bitmap source
 */
void draw_bitmap(point_t dest, const uint32_t* src, uint32_t src_width, uint32_t src_height);

/**
 * Copie un bitmap avec alpha blending.
 */
void draw_bitmap_alpha(point_t dest, const uint32_t* src, uint32_t src_width, uint32_t src_height);

/**
 * Copie une portion d'un bitmap (sprite sheet).
 * 
 * @param dest Position de destination
 * @param src Pointeur vers les pixels source
 * @param src_width Largeur totale du bitmap source
 * @param src_rect Rectangle source à copier
 */
void draw_bitmap_region(point_t dest, const uint32_t* src, uint32_t src_width, rect_t src_rect);

/**
 * Copie un bitmap avec mise à l'échelle.
 * 
 * @param dest_rect Rectangle de destination (définit la taille)
 * @param src Pointeur vers les pixels source
 * @param src_width Largeur du bitmap source
 * @param src_height Hauteur du bitmap source
 */
void draw_bitmap_scaled(rect_t dest_rect, const uint32_t* src, uint32_t src_width, uint32_t src_height);

/* ============================================================================
 * UTILITAIRES
 * ============================================================================ */

/**
 * Efface l'écran avec une couleur.
 */
void render_clear(uint32_t color);

/**
 * Mélange deux couleurs avec alpha blending.
 * 
 * @param bg Couleur de fond
 * @param fg Couleur de premier plan (avec alpha)
 * @return Couleur résultante
 */
uint32_t blend_colors(uint32_t bg, rgba_t fg);

/**
 * Interpole linéairement entre deux couleurs.
 * 
 * @param c1 Première couleur
 * @param c2 Deuxième couleur
 * @param t Facteur d'interpolation (0.0 = c1, 1.0 = c2)
 * @return Couleur interpolée
 */
rgba_t lerp_color(rgba_t c1, rgba_t c2, float t);

/**
 * Crée une ombre par défaut style macOS.
 */
shadow_params_t shadow_default(void);

/**
 * Crée une ombre légère pour les cartes.
 */
shadow_params_t shadow_card(void);

/**
 * Crée une ombre prononcée pour les fenêtres.
 */
shadow_params_t shadow_window(void);

#endif /* RENDER_H */
