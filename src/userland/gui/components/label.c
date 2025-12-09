/* src/userland/gui/components/label.c - Implémentation du composant Label */

#include "label.h"
#include "../render.h"
#include "../font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations des callbacks */
static void label_on_draw(gui_component_t* comp, framebuffer_t* fb);
static void label_on_destroy(gui_component_t* comp);

/* === Création === */

gui_label_t* label_create(rect_t bounds, const char* text, rgba_t color) {
    /* Créer le composant de base */
    gui_component_t* base = component_create(COMPONENT_TYPE_LABEL, bounds);
    if (!base) {
        return NULL;
    }

    /* Allouer la structure label */
    gui_label_t* label = (gui_label_t*)malloc(sizeof(gui_label_t));
    if (!label) {
        component_destroy(base);
        printf("label_create: malloc failed\n");
        return NULL;
    }

    /* Copier la base */
    label->base = *base;
    free(base);  /* Libérer la base temporaire */

    /* Configurer les callbacks spécifiques au label */
    label->base.on_draw = label_on_draw;
    label->base.on_destroy = label_on_destroy;

    /* Initialiser les champs spécifiques */
    label->text = NULL;
    if (text) {
        label->text = strdup(text);
        if (!label->text) {
            free(label);
            printf("label_create: strdup failed\n");
            return NULL;
        }
    }

    label->text_color = color;
    label->bg_color = rgba(0, 0, 0, 0);  /* Transparent par défaut */
    label->align = LABEL_ALIGN_LEFT;
    label->font = font_system;            /* Police par défaut */
    label->word_wrap = false;

    /* Stocker le pointeur label dans user_data de la base */
    label->base.user_data = label;

    return label;
}

/* === Callbacks === */

static void label_on_draw(gui_component_t* comp, framebuffer_t* fb) {
    gui_label_t* label = (gui_label_t*)comp->user_data;
    if (!label || !label->text) return;

    /* Dessiner fond si opaque */
    if (label->bg_color.a > 0) {
        draw_rect_alpha(comp->abs_bounds, label->bg_color);
    }

    /* Calculer position du texte selon alignement */
    int32_t text_x = comp->abs_bounds.x;
    int32_t text_y = comp->abs_bounds.y;

    /* Obtenir dimensions du texte */
    text_bounds_t bounds = measure_text(label->text, label->font);
    uint32_t text_width = bounds.width;
    uint32_t text_height = bounds.height;

    /* Ajuster X selon alignement */
    switch (label->align) {
        case LABEL_ALIGN_LEFT:
            text_x += 2;  /* Petit padding */
            break;
        case LABEL_ALIGN_CENTER:
            text_x += (int32_t)(comp->abs_bounds.width - text_width) / 2;
            break;
        case LABEL_ALIGN_RIGHT:
            text_x += (int32_t)(comp->abs_bounds.width - text_width) - 2;
            break;
    }

    /* Centrer verticalement */
    text_y += (int32_t)(comp->abs_bounds.height - text_height) / 2;

    /* Dessiner le texte */
    point_t pos = {text_x, text_y};
    draw_text_alpha(label->text, pos, label->font, label->text_color);
}

static void label_on_destroy(gui_component_t* comp) {
    gui_label_t* label = (gui_label_t*)comp->user_data;
    if (!label) return;

    /* Libérer le texte */
    if (label->text) {
        free(label->text);
        label->text = NULL;
    }

    /* Libérer la structure label elle-même */
    free(label);
    comp->user_data = NULL;
}

/* === Setters === */

void label_set_text(gui_label_t* label, const char* text) {
    if (!label) return;

    /* Libérer ancien texte */
    if (label->text) {
        free(label->text);
        label->text = NULL;
    }

    /* Copier nouveau texte */
    if (text) {
        label->text = strdup(text);
        if (!label->text) {
            printf("label_set_text: strdup failed\n");
            return;
        }
    }

    component_invalidate(&label->base);
}

void label_set_text_color(gui_label_t* label, rgba_t color) {
    if (!label) return;

    label->text_color = color;
    component_invalidate(&label->base);
}

void label_set_bg_color(gui_label_t* label, rgba_t color) {
    if (!label) return;

    label->bg_color = color;
    component_invalidate(&label->base);
}

void label_set_align(gui_label_t* label, label_align_t align) {
    if (!label) return;

    label->align = align;
    component_invalidate(&label->base);
}

void label_set_font(gui_label_t* label, const font_t* font) {
    if (!label || !font) return;

    label->font = font;
    component_invalidate(&label->base);
}

void label_set_word_wrap(gui_label_t* label, bool wrap) {
    if (!label) return;

    label->word_wrap = wrap;
    component_invalidate(&label->base);
}
