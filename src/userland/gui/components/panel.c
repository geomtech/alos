/* src/userland/gui/components/panel.c - Implémentation du composant Panel */

#include "panel.h"
#include "../render.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations des callbacks */
static void panel_on_draw(gui_component_t* comp, framebuffer_t* fb);
static void panel_on_destroy(gui_component_t* comp);

/* === Création === */

gui_panel_t* panel_create(rect_t bounds) {
    /* Allouer la structure panel (contient base intégrée) */
    gui_panel_t* panel = (gui_panel_t*)malloc(sizeof(gui_panel_t));
    if (!panel) {
        printf("panel_create: malloc failed\n");
        return NULL;
    }

    /* Initialiser à zéro puis initialiser la base directement */
    memset(panel, 0, sizeof(gui_panel_t));
    component_init(&panel->base, COMPONENT_TYPE_PANEL, bounds);

    /* Configurer les callbacks */
    panel->base.on_draw = panel_on_draw;
    panel->base.on_destroy = panel_on_destroy;

    /* Initialiser les champs spécifiques */
    panel->bg_color = rgba(245, 245, 245, 255);      /* Gris très clair */
    panel->border_color = rgba(200, 200, 200, 255);  /* Gris moyen */
    panel->border_width = 1;
    panel->corner_radius = 4;
    panel->has_shadow = false;

    return panel;
}

/* === Callbacks === */

static void panel_on_draw(gui_component_t* comp, framebuffer_t* fb) {
    (void)fb;
    gui_panel_t* panel = (gui_panel_t*)comp;

    /* Dessiner ombre si activée */
    if (panel->has_shadow) {
        shadow_params_t shadow = shadow_default();
        shadow.blur_radius = 3;
        shadow.offset_y = 1;
        shadow.color = rgba(0, 0, 0, 51);  /* Alpha 51 ≈ 0.2 opacity */
        draw_shadow(comp->abs_bounds, panel->corner_radius, shadow);
    }

    /* Dessiner fond */
    if (panel->corner_radius > 0) {
        draw_rounded_rect_alpha(comp->abs_bounds, panel->corner_radius, panel->bg_color);
    } else {
        draw_rect_alpha(comp->abs_bounds, panel->bg_color);
    }

    /* Dessiner bordure si épaisseur > 0 */
    if (panel->border_width > 0) {
        if (panel->corner_radius > 0) {
            draw_rounded_rect_outline(comp->abs_bounds, panel->corner_radius,
                                       rgba_to_u32(panel->border_color), panel->border_width);
        } else {
            draw_rect_outline(comp->abs_bounds, rgba_to_u32(panel->border_color), panel->border_width);
        }
    }

    /* Les enfants seront dessinés par component_draw() */
}

static void panel_on_destroy(gui_component_t* comp) {
    /* Panel n'a pas de ressources dynamiques à libérer
     * component_destroy() fera le free(comp) final */
    (void)comp;
}

/* === Setters === */

void panel_set_bg_color(gui_panel_t* panel, rgba_t color) {
    if (!panel) return;

    panel->bg_color = color;
    component_invalidate(&panel->base);
}

void panel_set_border(gui_panel_t* panel, rgba_t color, uint32_t width) {
    if (!panel) return;

    panel->border_color = color;
    panel->border_width = width;
    component_invalidate(&panel->base);
}

void panel_set_corner_radius(gui_panel_t* panel, uint32_t radius) {
    if (!panel) return;

    panel->corner_radius = radius;
    component_invalidate(&panel->base);
}

void panel_set_shadow(gui_panel_t* panel, bool has_shadow) {
    if (!panel) return;

    panel->has_shadow = has_shadow;
    component_invalidate(&panel->base);
}
