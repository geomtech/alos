/* src/userland/gui/components/button.h - Composant Button (bouton cliquable) */

#ifndef BUTTON_H
#define BUTTON_H

#include "component.h"
#include "../font.h"

/* Forward declaration */
struct gui_button;

/* Callback de clic */
typedef void (*button_click_callback_t)(struct gui_button* button);

/* États du bouton */
typedef enum {
    BUTTON_STATE_NORMAL,
    BUTTON_STATE_HOVER,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_DISABLED
} button_state_t;

/* Structure Button (étend gui_component_t) */
typedef struct gui_button {
    gui_component_t base;          /* Héritage de la classe de base */

    char* text;                    /* Texte du bouton (malloc) */
    const font_t* font;            /* Police */

    /* Couleurs selon l'état */
    rgba_t bg_color_normal;        /* Couleur fond état normal */
    rgba_t bg_color_hover;         /* Couleur fond hover */
    rgba_t bg_color_pressed;       /* Couleur fond pressé */
    rgba_t bg_color_disabled;      /* Couleur fond désactivé */

    rgba_t text_color;             /* Couleur du texte */
    rgba_t border_color;           /* Couleur de la bordure */

    button_state_t state;          /* État actuel */

    uint32_t corner_radius;        /* Rayon des coins arrondis */
    bool has_shadow;               /* Ombre portée ? */

    /* Callbacks */
    button_click_callback_t on_click;  /* Appelé lors du clic */
    void* user_data;                   /* Données custom */
} gui_button_t;

/**
 * Créer un bouton
 * @param bounds Position et taille
 * @param text Texte du bouton (copié)
 * @return Pointeur vers le bouton créé (malloc), NULL si échec
 */
gui_button_t* button_create(rect_t bounds, const char* text);

/**
 * Définir le texte du bouton
 * @param button Bouton à modifier
 * @param text Nouveau texte (copié)
 */
void button_set_text(gui_button_t* button, const char* text);

/**
 * Définir le callback de clic
 * @param button Bouton à modifier
 * @param callback Fonction à appeler lors du clic
 */
void button_set_on_click(gui_button_t* button, button_click_callback_t callback);

/**
 * Définir la couleur de fond pour un état
 * @param button Bouton à modifier
 * @param state État concerné
 * @param color Couleur
 */
void button_set_bg_color(gui_button_t* button, button_state_t state, rgba_t color);

/**
 * Définir la couleur du texte
 * @param button Bouton à modifier
 * @param color Couleur
 */
void button_set_text_color(gui_button_t* button, rgba_t color);

/**
 * Définir le rayon des coins arrondis
 * @param button Bouton à modifier
 * @param radius Rayon en pixels
 */
void button_set_corner_radius(gui_button_t* button, uint32_t radius);

/**
 * Activer/désactiver l'ombre portée
 * @param button Bouton à modifier
 * @param has_shadow true pour activer, false pour désactiver
 */
void button_set_shadow(gui_button_t* button, bool has_shadow);

#endif /* BUTTON_H */
