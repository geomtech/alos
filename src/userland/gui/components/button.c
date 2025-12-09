/* src/userland/gui/components/button.c - Implémentation du composant Button */

#include "button.h"
#include "../render.h"
#include "../font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations des callbacks */
static void button_on_draw(gui_component_t* comp, framebuffer_t* fb);
static bool button_on_mouse_down(gui_component_t* comp, point_t pos, mouse_button_t button);
static bool button_on_mouse_up(gui_component_t* comp, point_t pos, mouse_button_t button);
static bool button_on_mouse_move(gui_component_t* comp, point_t pos);
static void button_on_destroy(gui_component_t* comp);

/* === Création === */

gui_button_t* button_create(rect_t bounds, const char* text) {
    /* Allouer la structure button (contient base intégrée) */
    gui_button_t* button = (gui_button_t*)malloc(sizeof(gui_button_t));
    if (!button) {
        printf("button_create: malloc failed\n");
        return NULL;
    }

    /* Initialiser à zéro puis initialiser la base directement */
    memset(button, 0, sizeof(gui_button_t));
    component_init(&button->base, COMPONENT_TYPE_BUTTON, bounds);

    /* Configurer les callbacks */
    button->base.on_draw = button_on_draw;
    button->base.on_mouse_down = button_on_mouse_down;
    button->base.on_mouse_up = button_on_mouse_up;
    button->base.on_mouse_move = button_on_mouse_move;
    button->base.on_destroy = button_on_destroy;

    /* Initialiser les champs spécifiques */
    button->text = NULL;
    if (text) {
        button->text = strdup(text);
        if (!button->text) {
            free(button);
            printf("button_create: strdup failed\n");
            return NULL;
        }
    }

    button->font = font_system;

    /* Couleurs par défaut (style macOS) */
    button->bg_color_normal = rgba(240, 240, 240, 255);    /* Gris clair */
    button->bg_color_hover = rgba(220, 220, 220, 255);     /* Gris moyen */
    button->bg_color_pressed = rgba(180, 180, 180, 255);   /* Gris foncé */
    button->bg_color_disabled = rgba(200, 200, 200, 128);  /* Gris translucide */

    button->text_color = rgba(50, 50, 50, 255);            /* Noir */
    button->border_color = rgba(180, 180, 180, 255);       /* Bordure grise */

    button->state = BUTTON_STATE_NORMAL;
    button->corner_radius = 6;
    button->has_shadow = true;

    button->on_click = NULL;
    button->user_data = NULL;

    return button;
}

/* === Callbacks === */

static void button_on_draw(gui_component_t* comp, framebuffer_t* fb) {
    /* comp est le premier membre de gui_button_t, donc on peut caster directement */
    gui_button_t* button = (gui_button_t*)comp;
    (void)fb;

    /* Déterminer la couleur de fond selon l'état */
    rgba_t bg_color;
    switch (button->state) {
        case BUTTON_STATE_HOVER:
            bg_color = button->bg_color_hover;
            break;
        case BUTTON_STATE_PRESSED:
            bg_color = button->bg_color_pressed;
            break;
        case BUTTON_STATE_DISABLED:
            bg_color = button->bg_color_disabled;
            break;
        default:
            bg_color = button->bg_color_normal;
            break;
    }

    /* Dessiner ombre si activée */
    if (button->has_shadow && button->state != BUTTON_STATE_PRESSED) {
        shadow_params_t shadow = shadow_default();
        shadow.blur_radius = 4;
        shadow.offset_y = 2;
        shadow.color = rgba(0, 0, 0, 77);  /* Alpha 77 ≈ 0.3 opacity */
        draw_shadow(comp->abs_bounds, button->corner_radius, shadow);
    }

    /* Dessiner fond arrondi */
    draw_rounded_rect_alpha(comp->abs_bounds, button->corner_radius, bg_color);

    /* Dessiner bordure */
    draw_rounded_rect_outline(comp->abs_bounds, button->corner_radius,
                               rgba_to_u32(button->border_color), 1);

    /* Dessiner texte centré */
    if (button->text) {
        text_bounds_t bounds = measure_text(button->text, button->font);
        uint32_t text_width = bounds.width;
        uint32_t text_height = bounds.height;

        int32_t text_x = comp->abs_bounds.x + (int32_t)(comp->abs_bounds.width - text_width) / 2;
        int32_t text_y = comp->abs_bounds.y + (int32_t)(comp->abs_bounds.height - text_height) / 2;

        /* Léger offset si pressé (effet d'enfoncement) */
        if (button->state == BUTTON_STATE_PRESSED) {
            text_y += 1;
        }

        point_t pos = {text_x, text_y};
        draw_text_alpha(button->text, pos, button->font, button->text_color);
    }
}

static bool button_on_mouse_down(gui_component_t* comp, point_t pos, mouse_button_t btn) {
    gui_button_t* button = (gui_button_t*)comp;
    if (!comp->enabled) return false;

    if (btn == MOUSE_BUTTON_LEFT && component_contains_point(comp, pos)) {
        button->state = BUTTON_STATE_PRESSED;
        component_invalidate(comp);
        return true;  /* Event consommé */
    }

    return false;
}

static bool button_on_mouse_up(gui_component_t* comp, point_t pos, mouse_button_t btn) {
    gui_button_t* button = (gui_button_t*)comp;
    if (!comp->enabled) return false;

    if (btn == MOUSE_BUTTON_LEFT && button->state == BUTTON_STATE_PRESSED) {
        /* Clic réussi si le relâchement est sur le bouton */
        if (component_contains_point(comp, pos)) {
            button->state = BUTTON_STATE_HOVER;

            /* Appeler callback de clic */
            if (button->on_click) {
                button->on_click(button);
            }
        } else {
            button->state = BUTTON_STATE_NORMAL;
        }

        component_invalidate(comp);
        return true;
    }

    return false;
}

static bool button_on_mouse_move(gui_component_t* comp, point_t pos) {
    gui_button_t* button = (gui_button_t*)comp;
    if (!comp->enabled) return false;

    bool contains = component_contains_point(comp, pos);
    button_state_t new_state = button->state;

    /* Transitions d'état selon position de la souris */
    if (button->state != BUTTON_STATE_PRESSED) {
        new_state = contains ? BUTTON_STATE_HOVER : BUTTON_STATE_NORMAL;
    } else {
        /* Si pressé, rester pressé tant que souris dans le bouton */
        new_state = contains ? BUTTON_STATE_PRESSED : BUTTON_STATE_HOVER;
    }

    /* Invalider si l'état a changé */
    if (new_state != button->state) {
        button->state = new_state;
        component_invalidate(comp);
    }

    return contains;  /* Consommer l'événement si souris sur le bouton */
}

static void button_on_destroy(gui_component_t* comp) {
    gui_button_t* button = (gui_button_t*)comp;

    /* Libérer le texte */
    if (button->text) {
        free(button->text);
        button->text = NULL;
    }

    /* NOTE: Ne PAS libérer button ici - c'est la même allocation que comp
     * component_destroy() fera le free(comp) final */
}

/* === Setters === */

void button_set_text(gui_button_t* button, const char* text) {
    if (!button) return;

    if (button->text) {
        free(button->text);
        button->text = NULL;
    }

    if (text) {
        button->text = strdup(text);
        if (!button->text) {
            printf("button_set_text: strdup failed\n");
            return;
        }
    }

    component_invalidate(&button->base);
}

void button_set_on_click(gui_button_t* button, button_click_callback_t callback) {
    if (!button) return;
    button->on_click = callback;
}

void button_set_bg_color(gui_button_t* button, button_state_t state, rgba_t color) {
    if (!button) return;

    switch (state) {
        case BUTTON_STATE_NORMAL:
            button->bg_color_normal = color;
            break;
        case BUTTON_STATE_HOVER:
            button->bg_color_hover = color;
            break;
        case BUTTON_STATE_PRESSED:
            button->bg_color_pressed = color;
            break;
        case BUTTON_STATE_DISABLED:
            button->bg_color_disabled = color;
            break;
    }

    component_invalidate(&button->base);
}

void button_set_text_color(gui_button_t* button, rgba_t color) {
    if (!button) return;
    button->text_color = color;
    component_invalidate(&button->base);
}

void button_set_corner_radius(gui_button_t* button, uint32_t radius) {
    if (!button) return;
    button->corner_radius = radius;
    component_invalidate(&button->base);
}

void button_set_shadow(gui_button_t* button, bool has_shadow) {
    if (!button) return;
    button->has_shadow = has_shadow;
    component_invalidate(&button->base);
}
