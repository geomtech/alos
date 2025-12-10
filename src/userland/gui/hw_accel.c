/* src/gui/hw_accel.c - Implémentation de l'accélération matérielle 2D
 *
 * Ce module fournit une implémentation de base pour l'accélération matérielle.
 * Actuellement, il s'agit d'une implémentation logicielle qui pourrait être
 * remplacée par des appels matériels spécifiques lorsque disponibles.
 */

#include "hw_accel.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

/* État de l'accélération matérielle */
static bool g_hw_accel_available = false;
static bool g_hw_accel_enabled = false;

/* Statistiques de performance */
static uint32_t g_ops_executed = 0;
static uint32_t g_ops_failed = 0;

void hw_accel_init(void) {
    /* Pour l'instant, l'accélération matérielle n'est pas disponible */
    /* Cela pourrait être détecté dynamiquement sur du vrai matériel */
    g_hw_accel_available = false;
    g_hw_accel_enabled = false;

    printf("HW_ACCEL: Initialized (software fallback mode)\n");
}

bool hw_accel_is_available(void) {
    return g_hw_accel_available;
}

void hw_accel_set_enabled(bool enabled) {
    g_hw_accel_enabled = enabled && g_hw_accel_available;
    printf("HW_ACCEL: %s\n", g_hw_accel_enabled ? "Enabled" : "Disabled");
}

bool hw_accel_execute_op(hw_op_t *op) {
    if (!op || !g_hw_accel_enabled) {
        g_ops_failed++;
        return false;
    }

    /* Implémentation logicielle de fallback */
    switch (op->type) {
        case HW_OP_FILL_RECT: {
            /* Utiliser l'implémentation logicielle */
            draw_rect(op->dst_rect, op->color);
            g_ops_executed++;
            return true;
        }

        case HW_OP_COPY_RECT: {
            /* Copie de rectangle - implémentation logicielle */
            framebuffer_t *fb = render_get_active_buffer();
            uint32_t pitch_pixels = fb->pitch / 4;

            /* Calculer les dimensions de copie */
            uint32_t copy_width = op->src_rect.width;
            uint32_t copy_height = op->src_rect.height;

            /* Limiter aux dimensions du framebuffer */
            if (op->dst_rect.x + copy_width > fb->width) {
                copy_width = fb->width - op->dst_rect.x;
            }
            if (op->dst_rect.y + copy_height > fb->height) {
                copy_height = fb->height - op->dst_rect.y;
            }

            /* Copie ligne par ligne */
            for (uint32_t y = 0; y < copy_height; y++) {
                uint32_t *src_row = fb->pixels +
                    (op->src_rect.y + y) * pitch_pixels +
                    op->src_rect.x;
                uint32_t *dst_row = fb->pixels +
                    (op->dst_rect.y + y) * pitch_pixels +
                    op->dst_rect.x;

                memcpy(dst_row, src_row, copy_width * sizeof(uint32_t));
            }

            g_ops_executed++;
            return true;
        }

        case HW_OP_BLEND_RECT: {
            /* Copie avec alpha blending - implémentation logicielle */
            framebuffer_t *fb = render_get_active_buffer();
            uint32_t pitch_pixels = fb->pitch / 4;

            uint32_t copy_width = op->src_rect.width;
            uint32_t copy_height = op->src_rect.height;

            if (op->dst_rect.x + copy_width > fb->width) {
                copy_width = fb->width - op->dst_rect.x;
            }
            if (op->dst_rect.y + copy_height > fb->height) {
                copy_height = fb->height - op->dst_rect.y;
            }

            for (uint32_t y = 0; y < copy_height; y++) {
                uint32_t *src_row = fb->pixels +
                    (op->src_rect.y + y) * pitch_pixels +
                    op->src_rect.x;
                uint32_t *dst_row = fb->pixels +
                    (op->dst_rect.y + y) * pitch_pixels +
                    op->dst_rect.x;

                for (uint32_t x = 0; x < copy_width; x++) {
                    rgba_t fg_color = u32_to_rgba(src_row[x]);
                    fg_color.a = (uint8_t)((fg_color.a * op->alpha) / 255);
                    dst_row[x] = blend_colors(dst_row[x], fg_color);
                }
            }

            g_ops_executed++;
            return true;
        }

        default:
            g_ops_failed++;
            return false;
    }
}

bool hw_accel_fill_rect(rect_t rect, uint32_t color) {
    hw_op_t op = {
        .type = HW_OP_FILL_RECT,
        .dst_rect = rect,
        .color = color,
        .enabled = true
    };
    return hw_accel_execute_op(&op);
}

bool hw_accel_copy_rect(rect_t src_rect, point_t dst_point) {
    hw_op_t op = {
        .type = HW_OP_COPY_RECT,
        .src_rect = src_rect,
        .dst_rect = rect_make(dst_point.x, dst_point.y, src_rect.width, src_rect.height),
        .enabled = true
    };
    return hw_accel_execute_op(&op);
}

bool hw_accel_blend_rect(rect_t src_rect, point_t dst_point, uint8_t alpha) {
    hw_op_t op = {
        .type = HW_OP_BLEND_RECT,
        .src_rect = src_rect,
        .dst_rect = rect_make(dst_point.x, dst_point.y, src_rect.width, src_rect.height),
        .alpha = alpha,
        .enabled = true
    };
    return hw_accel_execute_op(&op);
}

void hw_accel_sync(void) {
    /* Synchronisation - dans une implémentation matérielle réelle,
     * cela attendrait que toutes les opérations soient terminées */
    if (g_hw_accel_enabled) {
        /* Implémentation logicielle - rien à faire */
    }
}

void hw_accel_print_stats(void) {
    printf("HW_ACCEL Stats: %u ops executed, %u ops failed\n",
           g_ops_executed, g_ops_failed);
}