/* src/userland/gui/components/label.h - Composant Label (texte statique) */

#ifndef LABEL_H
#define LABEL_H

#include "component.h"
#include "../font.h"

/* Alignement du texte */
typedef enum {
    LABEL_ALIGN_LEFT,
    LABEL_ALIGN_CENTER,
    LABEL_ALIGN_RIGHT
} label_align_t;

/* Structure Label (étend gui_component_t) */
typedef struct {
    gui_component_t base;        /* Héritage de la classe de base */

    char* text;                  /* Texte à afficher (malloc) */
    rgba_t text_color;           /* Couleur du texte */
    rgba_t bg_color;             /* Couleur de fond (0 = transparent) */
    label_align_t align;         /* Alignement du texte */
    const font_t* font;          /* Police utilisée */
    bool word_wrap;              /* Word wrap activé ? */
} gui_label_t;

/**
 * Créer un label
 * @param bounds Position et taille
 * @param text Texte à afficher (copié)
 * @param color Couleur du texte
 * @return Pointeur vers le label créé (malloc), NULL si échec
 */
gui_label_t* label_create(rect_t bounds, const char* text, rgba_t color);

/**
 * Définir le texte du label
 * @param label Label à modifier
 * @param text Nouveau texte (copié)
 */
void label_set_text(gui_label_t* label, const char* text);

/**
 * Définir la couleur du texte
 * @param label Label à modifier
 * @param color Nouvelle couleur
 */
void label_set_text_color(gui_label_t* label, rgba_t color);

/**
 * Définir la couleur de fond
 * @param label Label à modifier
 * @param color Nouvelle couleur (alpha 0 = transparent)
 */
void label_set_bg_color(gui_label_t* label, rgba_t color);

/**
 * Définir l'alignement
 * @param label Label à modifier
 * @param align Nouvel alignement
 */
void label_set_align(gui_label_t* label, label_align_t align);

/**
 * Définir la police
 * @param label Label à modifier
 * @param font Police à utiliser
 */
void label_set_font(gui_label_t* label, const font_t* font);

/**
 * Activer/désactiver le word wrap
 * @param label Label à modifier
 * @param wrap true pour activer, false pour désactiver
 */
void label_set_word_wrap(gui_label_t* label, bool wrap);

#endif /* LABEL_H */
