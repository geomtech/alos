/* src/userland/gui/components/panel.h - Composant Panel (conteneur) */

#ifndef PANEL_H
#define PANEL_H

#include "component.h"

/* Structure Panel (étend gui_component_t) */
typedef struct {
    gui_component_t base;          /* Héritage de la classe de base */

    rgba_t bg_color;               /* Couleur de fond */
    rgba_t border_color;           /* Couleur de la bordure */
    uint32_t border_width;         /* Épaisseur de la bordure (0 = pas de bordure) */
    uint32_t corner_radius;        /* Rayon des coins arrondis */
    bool has_shadow;               /* Ombre portée ? */
} gui_panel_t;

/**
 * Créer un panel
 * @param bounds Position et taille
 * @return Pointeur vers le panel créé (malloc), NULL si échec
 */
gui_panel_t* panel_create(rect_t bounds);

/**
 * Définir la couleur de fond
 * @param panel Panel à modifier
 * @param color Couleur (alpha 0 = transparent)
 */
void panel_set_bg_color(gui_panel_t* panel, rgba_t color);

/**
 * Définir la bordure
 * @param panel Panel à modifier
 * @param color Couleur de la bordure
 * @param width Épaisseur en pixels (0 = pas de bordure)
 */
void panel_set_border(gui_panel_t* panel, rgba_t color, uint32_t width);

/**
 * Définir le rayon des coins arrondis
 * @param panel Panel à modifier
 * @param radius Rayon en pixels
 */
void panel_set_corner_radius(gui_panel_t* panel, uint32_t radius);

/**
 * Activer/désactiver l'ombre portée
 * @param panel Panel à modifier
 * @param has_shadow true pour activer, false pour désactiver
 */
void panel_set_shadow(gui_panel_t* panel, bool has_shadow);

#endif /* PANEL_H */
