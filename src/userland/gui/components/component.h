/* src/userland/gui/components/component.h - Système de composants UI (base abstraite) */

#ifndef COMPONENT_H
#define COMPONENT_H

#include <stdint.h>
#include <stdbool.h>
#include "../gui_types.h"

/* Forward declarations */
struct gui_component;
struct window;

/* Type de composant (pour identification runtime) */
typedef enum {
    COMPONENT_TYPE_BASE = 0,
    COMPONENT_TYPE_PANEL,
    COMPONENT_TYPE_BUTTON,
    COMPONENT_TYPE_LABEL,
    COMPONENT_TYPE_TEXTBOX,
    COMPONENT_TYPE_SCROLLBAR,
    COMPONENT_TYPE_LISTBOX,
    COMPONENT_TYPE_TERMINAL,
    COMPONENT_TYPE_APPHUB,
    COMPONENT_TYPE_CUSTOM
} component_type_t;

/* Structure de base des composants UI */
typedef struct gui_component {
    uint32_t id;                            /* ID unique du composant */
    component_type_t type;                  /* Type de composant */
    rect_t bounds;                          /* Position/taille relative au parent */
    rect_t abs_bounds;                      /* Position/taille absolue (cache) */

    bool visible;                           /* Visible ? */
    bool enabled;                           /* Activé (réagit aux events) ? */
    bool dirty;                             /* Besoin de redraw ? */
    bool focused;                           /* A le focus clavier ? */

    struct gui_component* parent;           /* Parent (NULL si racine) */
    struct gui_component** children;        /* Tableau de pointeurs enfants */
    uint32_t child_count;                   /* Nombre d'enfants */
    uint32_t child_capacity;                /* Capacité allouée pour enfants */

    struct window* owner_window;            /* Fenêtre propriétaire (back-ref) */

    /* Callbacks virtuels - patterns objet en C */
    void (*on_draw)(struct gui_component*, framebuffer_t* fb);
    bool (*on_mouse_down)(struct gui_component*, point_t pos, mouse_button_t button);
    bool (*on_mouse_up)(struct gui_component*, point_t pos, mouse_button_t button);
    bool (*on_mouse_move)(struct gui_component*, point_t pos);
    bool (*on_key_down)(struct gui_component*, uint8_t scancode, char ch);
    bool (*on_key_up)(struct gui_component*, uint8_t scancode, char ch);
    void (*on_focus_gained)(struct gui_component*);
    void (*on_focus_lost)(struct gui_component*);
    void (*on_bounds_changed)(struct gui_component*);
    void (*on_destroy)(struct gui_component*);

    /* Données custom du composant (subclass) */
    void* user_data;
} gui_component_t;

/* === API de gestion des composants === */

/**
 * Créer un composant de base
 * @param type Type du composant
 * @param bounds Position et taille relatives au parent
 * @return Pointeur vers le composant créé (malloc), NULL si échec
 */
gui_component_t* component_create(component_type_t type, rect_t bounds);

/**
 * Détruire un composant et ses enfants récursivement
 * @param comp Composant à détruire
 */
void component_destroy(gui_component_t* comp);

/**
 * Ajouter un enfant à un composant
 * @param parent Composant parent
 * @param child Composant enfant
 * @return true si succès, false si échec
 */
bool component_add_child(gui_component_t* parent, gui_component_t* child);

/**
 * Retirer un enfant d'un composant (ne le détruit pas)
 * @param parent Composant parent
 * @param child Composant enfant
 * @return true si trouvé et retiré, false sinon
 */
bool component_remove_child(gui_component_t* parent, gui_component_t* child);

/**
 * Marquer un composant comme sale (redraw nécessaire)
 * @param comp Composant à invalider
 */
void component_invalidate(gui_component_t* comp);

/**
 * Dessiner un composant et ses enfants
 * @param comp Composant à dessiner
 * @param fb Framebuffer cible
 */
void component_draw(gui_component_t* comp, framebuffer_t* fb);

/**
 * Mettre à jour les bounds absolus d'un composant (récursif)
 * @param comp Composant à mettre à jour
 */
void component_update_abs_bounds(gui_component_t* comp);

/**
 * Test de hit (point dans composant ?)
 * @param comp Composant à tester
 * @param pos Position absolue à tester
 * @return true si le point est dans le composant
 */
bool component_contains_point(gui_component_t* comp, point_t pos);

/**
 * Trouver le composant enfant le plus profond contenant un point
 * @param comp Composant racine
 * @param pos Position absolue
 * @return Composant trouvé, ou NULL
 */
gui_component_t* component_hit_test(gui_component_t* comp, point_t pos);

/**
 * Donner le focus à un composant
 * @param comp Composant à focus
 */
void component_set_focus(gui_component_t* comp);

/**
 * Retirer le focus d'un composant
 * @param comp Composant à défocus
 */
void component_clear_focus(gui_component_t* comp);

/**
 * Déplacer un composant (modifie bounds.x/y)
 * @param comp Composant à déplacer
 * @param x Nouvelle position X relative
 * @param y Nouvelle position Y relative
 */
void component_set_position(gui_component_t* comp, int32_t x, int32_t y);

/**
 * Redimensionner un composant
 * @param comp Composant à redimensionner
 * @param width Nouvelle largeur
 * @param height Nouvelle hauteur
 */
void component_set_size(gui_component_t* comp, uint32_t width, uint32_t height);

/**
 * Définir les bounds complets d'un composant
 * @param comp Composant à modifier
 * @param bounds Nouveaux bounds
 */
void component_set_bounds(gui_component_t* comp, rect_t bounds);

/**
 * Afficher/cacher un composant
 * @param comp Composant
 * @param visible true pour afficher, false pour cacher
 */
void component_set_visible(gui_component_t* comp, bool visible);

/**
 * Activer/désactiver un composant
 * @param comp Composant
 * @param enabled true pour activer, false pour désactiver
 */
void component_set_enabled(gui_component_t* comp, bool enabled);

#endif /* COMPONENT_H */
