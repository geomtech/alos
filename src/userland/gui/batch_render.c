/* src/gui/batch_render.c - Implémentation du Batch Rendering
 *
 * Ce module fournit une implémentation du système de batch rendering
 * qui permet de regrouper les opérations de rendu pour améliorer
 * les performances.
 */

#include "batch_render.h"
#include "render.h"
#include "font.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Nombre maximum d'opérations dans un batch */
#define MAX_BATCH_OPS 256

/* Structure pour le batch courant */
typedef struct {
    batch_op_t ops[MAX_BATCH_OPS];
    int op_count;
    bool is_active;
    bool enabled;
} batch_state_t;

static batch_state_t g_batch_state;

/* Statistiques de performance */
static uint32_t g_batches_executed = 0;
static uint32_t g_ops_executed = 0;
static uint32_t g_ops_batched = 0;

void batch_render_init(void) {
    g_batch_state.op_count = 0;
    g_batch_state.is_active = false;
    g_batch_state.enabled = true;

    g_batches_executed = 0;
    g_ops_executed = 0;
    g_ops_batched = 0;

    printf("BATCH_RENDER: Initialized\n");
}

void batch_render_begin(void) {
    if (!g_batch_state.enabled || g_batch_state.is_active) {
        return;
    }

    g_batch_state.op_count = 0;
    g_batch_state.is_active = true;
}

void batch_render_add_op(batch_op_t *op) {
    if (!g_batch_state.enabled || !g_batch_state.is_active || !op) {
        return;
    }

    if (g_batch_state.op_count >= MAX_BATCH_OPS) {
        /* Batch plein - exécuter et recommencer */
        batch_render_end();
        batch_render_begin();
    }

    /* Ajouter l'opération au batch */
    g_batch_state.ops[g_batch_state.op_count++] = *op;
    g_ops_batched++;
}

void batch_render_end(void) {
    if (!g_batch_state.enabled || !g_batch_state.is_active) {
        return;
    }

    g_batch_state.is_active = false;
    batch_render_flush();
}

void batch_render_flush(void) {
    if (g_batch_state.op_count == 0) {
        return;
    }

    /* Exécuter toutes les opérations dans le batch */
    for (int i = 0; i < g_batch_state.op_count; i++) {
        batch_op_t *op = &g_batch_state.ops[i];

        switch (op->type) {
            case BATCH_OP_FILL_RECT:
                draw_rect(op->rect, op->color);
                break;

            case BATCH_OP_DRAW_RECT:
                draw_rect_outline(op->rect, op->color, 1);
                break;

            case BATCH_OP_DRAW_LINE:
                draw_line(op->p1, op->p2, op->color);
                break;

            case BATCH_OP_DRAW_TEXT:
                if (op->text && op->text_length > 0) {
                    draw_text_alpha(op->text, op->p1, font_system, op->rgba_color);
                }
                break;

            case BATCH_OP_COPY_RECT:
                /* Copie de rectangle - pourrait être optimisé avec l'accélération matérielle */
                {
                    framebuffer_t *fb = render_get_active_buffer();
                    uint32_t pitch_pixels = fb->pitch / 4;

                    for (uint32_t y = 0; y < op->rect.height; y++) {
                        uint32_t *src_row = fb->pixels +
                            (op->rect.y + y) * pitch_pixels +
                            op->rect.x;
                        uint32_t *dst_row = fb->pixels +
                            (op->p1.y + y) * pitch_pixels +
                            op->p1.x;

                        memcpy(dst_row, src_row, op->rect.width * sizeof(uint32_t));
                    }
                }
                break;

            default:
                break;
        }
    }

    /* Mettre à jour les statistiques */
    g_batches_executed++;
    g_ops_executed += g_batch_state.op_count;

    /* Réinitialiser le batch */
    g_batch_state.op_count = 0;
}

int batch_render_get_op_count(void) {
    return g_batch_state.op_count;
}

void batch_render_set_enabled(bool enabled) {
    g_batch_state.enabled = enabled;
    printf("BATCH_RENDER: %s\n", enabled ? "Enabled" : "Disabled");
}

bool batch_render_is_enabled(void) {
    return g_batch_state.enabled;
}

void batch_render_print_stats(void) {
    printf("BATCH_RENDER Stats: %u batches, %u ops executed, %u ops batched\n",
           g_batches_executed, g_ops_executed, g_ops_batched);
}