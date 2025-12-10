/* src/gui/batch_render.h - Batch Rendering pour optimiser les performances
 *
 * Ce module fournit un système de batch rendering qui permet de regrouper
 * les opérations de rendu pour réduire les appels système et améliorer
 * les performances.
 */

#ifndef BATCH_RENDER_H
#define BATCH_RENDER_H

#include "gui_types.h"
#include <stdbool.h>

/* Type d'opérations de rendu supportées */
typedef enum {
    BATCH_OP_NONE = 0,
    BATCH_OP_FILL_RECT,      /* Remplissage de rectangle */
    BATCH_OP_DRAW_RECT,      /* Dessin de rectangle */
    BATCH_OP_DRAW_LINE,      /* Dessin de ligne */
    BATCH_OP_DRAW_TEXT,      /* Dessin de texte */
    BATCH_OP_COPY_RECT,      /* Copie de rectangle */
    BATCH_OP_MAX
} batch_op_type_t;

/* Structure pour une opération de rendu */
typedef struct {
    batch_op_type_t type;
    rect_t rect;
    point_t p1;
    point_t p2;
    uint32_t color;
    rgba_t rgba_color;
    const char *text;
    uint32_t text_length;
} batch_op_t;

/* Initialise le système de batch rendering */
void batch_render_init(void);

/* Commence un nouveau batch */
void batch_render_begin(void);

/* Ajoute une opération au batch courant */
void batch_render_add_op(batch_op_t *op);

/* Termine le batch et exécute toutes les opérations */
void batch_render_end(void);

/* Exécute toutes les opérations en batch */
void batch_render_flush(void);

/* Retourne le nombre d'opérations dans le batch courant */
int batch_render_get_op_count(void);

/* Active ou désactive le batch rendering */
void batch_render_set_enabled(bool enabled);

/* Vérifie si le batch rendering est activé */
bool batch_render_is_enabled(void);

#endif /* BATCH_RENDER_H */