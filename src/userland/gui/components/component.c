/* src/userland/gui/components/component.c - Implémentation du système de composants UI */

#include "component.h"
#include "../wm.h"
#include "../render.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Compteur global pour IDs uniques */
static uint32_t g_next_component_id = 1;

/* Composant qui a capturé la souris (reçoit mouse_up même si souris hors bounds) */
static gui_component_t* g_captured_component = NULL;

/* Capacité initiale du tableau d'enfants */
#define INITIAL_CHILD_CAPACITY 4

/* === Création/Destruction === */

void component_init(gui_component_t* comp, component_type_t type, rect_t bounds) {
    if (!comp) return;

    /* Ne pas utiliser memset - la structure peut contenir des données valides */
    comp->id = g_next_component_id++;
    comp->type = type;
    comp->bounds = bounds;
    comp->abs_bounds = bounds;  /* Sera mis à jour par component_update_abs_bounds() */
    comp->visible = true;
    comp->enabled = true;
    comp->dirty = true;          /* Forcer redraw initial */
    comp->focused = false;

    comp->parent = NULL;
    comp->children = NULL;
    comp->child_count = 0;
    comp->child_capacity = 0;
    comp->owner_window = NULL;

    /* Tous les callbacks NULL par défaut (subclasses les définiront) */
    comp->on_draw = NULL;
    comp->on_mouse_down = NULL;
    comp->on_mouse_up = NULL;
    comp->on_mouse_move = NULL;
    comp->on_key_down = NULL;
    comp->on_key_up = NULL;
    comp->on_focus_gained = NULL;
    comp->on_focus_lost = NULL;
    comp->on_bounds_changed = NULL;
    comp->on_destroy = NULL;

    comp->user_data = NULL;
}

gui_component_t* component_create(component_type_t type, rect_t bounds) {
    gui_component_t* comp = (gui_component_t*)malloc(sizeof(gui_component_t));
    if (!comp) {
        printf("component_create: malloc failed\n");
        return NULL;
    }

    component_init(comp, type, bounds);
    return comp;
}

void component_destroy(gui_component_t* comp) {
    if (!comp) return;

    /* Callback custom avant destruction */
    if (comp->on_destroy) {
        comp->on_destroy(comp);
    }

    /* Détruire tous les enfants récursivement */
    for (uint32_t i = 0; i < comp->child_count; i++) {
        component_destroy(comp->children[i]);
    }

    /* Libérer tableau d'enfants */
    if (comp->children) {
        free(comp->children);
        comp->children = NULL;
    }

    /* Libérer user_data si nécessaire (doit être fait par on_destroy du subclass) */

    /* Libérer le composant lui-même */
    free(comp);
}

/* === Gestion de la hiérarchie === */

/* Helper pour propager owner_window récursivement */
static void component_propagate_owner(gui_component_t* comp, struct window* win) {
    if (!comp) return;
    comp->owner_window = win;
    for (uint32_t i = 0; i < comp->child_count; i++) {
        component_propagate_owner(comp->children[i], win);
    }
}

bool component_add_child(gui_component_t* parent, gui_component_t* child) {
    if (!parent || !child) return false;

    /* Allouer/réallouer tableau d'enfants si nécessaire */
    if (parent->child_count >= parent->child_capacity) {
        uint32_t new_capacity = parent->child_capacity == 0 ?
                                INITIAL_CHILD_CAPACITY :
                                parent->child_capacity * 2;

        gui_component_t** new_children = (gui_component_t**)realloc(
            parent->children,
            new_capacity * sizeof(gui_component_t*)
        );

        if (!new_children) {
            printf("component_add_child: realloc failed\n");
            return false;
        }

        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }

    /* Ajouter l'enfant */
    parent->children[parent->child_count++] = child;
    child->parent = parent;

    /* Propager owner_window récursivement */
    component_propagate_owner(child, parent->owner_window);

    /* Mettre à jour les bounds absolus */
    component_update_abs_bounds(child);

    /* Invalider le parent pour redraw */
    component_invalidate(parent);

    return true;
}

bool component_remove_child(gui_component_t* parent, gui_component_t* child) {
    if (!parent || !child || parent->child_count == 0) return false;

    /* Chercher l'enfant */
    uint32_t index = 0;
    bool found = false;
    for (; index < parent->child_count; index++) {
        if (parent->children[index] == child) {
            found = true;
            break;
        }
    }

    if (!found) return false;

    /* Décaler les éléments suivants */
    for (uint32_t i = index; i < parent->child_count - 1; i++) {
        parent->children[i] = parent->children[i + 1];
    }

    parent->child_count--;
    child->parent = NULL;
    child->owner_window = NULL;

    /* Invalider le parent */
    component_invalidate(parent);

    return true;
}

/* === Rendu === */

void component_invalidate(gui_component_t* comp) {
    if (!comp) return;

    comp->dirty = true;

    /* Propager à la fenêtre propriétaire pour invalidation compositor */
    if (comp->owner_window) {
        wm_invalidate_window((window_t*)comp->owner_window);
    }

    /* Marquer aussi les enfants comme dirty */
    for (uint32_t i = 0; i < comp->child_count; i++) {
        comp->children[i]->dirty = true;
    }
}

void component_draw(gui_component_t* comp, framebuffer_t* fb) {
    if (!comp || !comp->visible || !fb) return;

    /* Appeler callback custom de dessin (sans clipping pour le moment) */
    if (comp->on_draw) {
        comp->on_draw(comp, fb);
    }

    /* Dessiner les enfants */
    for (uint32_t i = 0; i < comp->child_count; i++) {
        component_draw(comp->children[i], fb);
    }

    /* Marquer comme propre */
    comp->dirty = false;
}

/* === Bounds et positionnement === */

void component_update_abs_bounds(gui_component_t* comp) {
    if (!comp) return;

    if (comp->parent) {
        /* Position absolue = position parent + position relative */
        comp->abs_bounds.x = comp->parent->abs_bounds.x + comp->bounds.x;
        comp->abs_bounds.y = comp->parent->abs_bounds.y + comp->bounds.y;
    } else {
        /* Pas de parent : absolue = relative */
        comp->abs_bounds.x = comp->bounds.x;
        comp->abs_bounds.y = comp->bounds.y;
    }

    comp->abs_bounds.width = comp->bounds.width;
    comp->abs_bounds.height = comp->bounds.height;

    /* Mettre à jour récursivement les enfants */
    for (uint32_t i = 0; i < comp->child_count; i++) {
        component_update_abs_bounds(comp->children[i]);
    }
}

void component_set_position(gui_component_t* comp, int32_t x, int32_t y) {
    if (!comp) return;

    comp->bounds.x = x;
    comp->bounds.y = y;

    component_update_abs_bounds(comp);

    if (comp->on_bounds_changed) {
        comp->on_bounds_changed(comp);
    }

    component_invalidate(comp);
}

void component_set_size(gui_component_t* comp, uint32_t width, uint32_t height) {
    if (!comp) return;

    comp->bounds.width = width;
    comp->bounds.height = height;

    component_update_abs_bounds(comp);

    if (comp->on_bounds_changed) {
        comp->on_bounds_changed(comp);
    }

    component_invalidate(comp);
}

void component_set_bounds(gui_component_t* comp, rect_t bounds) {
    if (!comp) return;

    comp->bounds = bounds;

    component_update_abs_bounds(comp);

    if (comp->on_bounds_changed) {
        comp->on_bounds_changed(comp);
    }

    component_invalidate(comp);
}

/* === Visibilité et état === */

void component_set_visible(gui_component_t* comp, bool visible) {
    if (!comp || comp->visible == visible) return;

    comp->visible = visible;
    component_invalidate(comp);
}

void component_set_enabled(gui_component_t* comp, bool enabled) {
    if (!comp) return;
    comp->enabled = enabled;
}

/* === Hit testing === */

bool component_contains_point(gui_component_t* comp, point_t pos) {
    if (!comp || !comp->visible) return false;

    return pos.x >= comp->abs_bounds.x &&
           pos.x < (int32_t)(comp->abs_bounds.x + comp->abs_bounds.width) &&
           pos.y >= comp->abs_bounds.y &&
           pos.y < (int32_t)(comp->abs_bounds.y + comp->abs_bounds.height);
}

gui_component_t* component_hit_test(gui_component_t* comp, point_t pos) {
    if (!comp || !component_contains_point(comp, pos)) {
        return NULL;
    }

    /* Parcourir les enfants en ordre inverse (du dessus vers le dessous) */
    if (comp->child_count > 0) {
        for (uint32_t i = comp->child_count; i > 0; i--) {
            uint32_t idx = i - 1;
            gui_component_t* child_result = component_hit_test(comp->children[idx], pos);
            if (child_result) {
                return child_result;
            }
        }
    }

    /* Aucun enfant ne contient le point, retourner ce composant */
    return comp;
}

/* === Focus === */

void component_set_focus(gui_component_t* comp) {
    if (!comp || comp->focused) return;

    /* Retirer focus de l'ancien composant focus (à implémenter avec window) */
    /* TODO: appeler component_clear_focus sur le composant focus actuel */

    comp->focused = true;

    if (comp->on_focus_gained) {
        comp->on_focus_gained(comp);
    }

    component_invalidate(comp);
}

void component_clear_focus(gui_component_t* comp) {
    if (!comp || !comp->focused) return;

    comp->focused = false;

    if (comp->on_focus_lost) {
        comp->on_focus_lost(comp);
    }

    component_invalidate(comp);
}

/* === Dispatch d'événements === */

void component_dispatch_mouse_move(gui_component_t* root, point_t pos) {
    if (!root || !root->visible) return;

    /* Trouver le composant sous la souris */
    gui_component_t* target = component_hit_test(root, pos);

    /* Notifier tous les composants dans l'arbre (pour hover states) */
    /* On parcourt récursivement et chaque composant décide s'il est concerné */
    if (root->on_mouse_move && root->enabled) {
        root->on_mouse_move(root, pos);
    }

    /* Dispatcher aux enfants */
    for (uint32_t i = 0; i < root->child_count; i++) {
        component_dispatch_mouse_move(root->children[i], pos);
    }
}

void component_dispatch_mouse_down(gui_component_t* root, point_t pos, mouse_button_t button) {
    if (!root || !root->visible) return;

    /* Trouver le composant le plus profond sous la souris */
    gui_component_t* target = component_hit_test(root, pos);

    if (target && target->enabled && target->on_mouse_down) {
        /* Donner le focus au composant cliqué */
        component_set_focus(target);
        /* Si le composant consomme l'événement, il capture la souris */
        if (target->on_mouse_down(target, pos, button)) {
            g_captured_component = target;
        }
    }
}

void component_dispatch_mouse_up(gui_component_t* root, point_t pos, mouse_button_t button) {
    if (!root || !root->visible) return;

    /* Priorité au composant qui a capturé la souris */
    if (g_captured_component) {
        gui_component_t* captured = g_captured_component;
        g_captured_component = NULL;  /* Libérer la capture */
        if (captured->on_mouse_up) {
            captured->on_mouse_up(captured, pos, button);
        }
        return;
    }

    /* Fallback: composant sous la souris */
    gui_component_t* target = component_hit_test(root, pos);

    if (target && target->enabled && target->on_mouse_up) {
        target->on_mouse_up(target, pos, button);
    }
}
