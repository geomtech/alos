/* src/gui/hw_accel.h - Interface pour l'accélération matérielle 2D
 *
 * Ce module fournit une interface pour utiliser l'accélération matérielle
 * 2D lorsque disponible. Actuellement, il s'agit d'une interface de base
 * qui pourrait être étendue pour utiliser des fonctionnalités spécifiques
 * du matériel.
 */

#ifndef HW_ACCEL_H
#define HW_ACCEL_H

#include "gui_types.h"
#include <stdbool.h>

/* Types d'opérations matérielles supportées */
typedef enum {
    HW_OP_NONE = 0,
    HW_OP_FILL_RECT,      /* Remplissage de rectangle */
    HW_OP_COPY_RECT,      /* Copie de rectangle (blit) */
    HW_OP_BLEND_RECT,     /* Copie avec alpha blending */
    HW_OP_MAX
} hw_op_type_t;

/* Structure pour les opérations matérielles */
typedef struct {
    hw_op_type_t type;
    rect_t src_rect;
    rect_t dst_rect;
    uint32_t color;
    uint8_t alpha;
    bool enabled;
} hw_op_t;

/* Initialise le système d'accélération matérielle */
void hw_accel_init(void);

/* Vérifie si l'accélération matérielle est disponible */
bool hw_accel_is_available(void);

/* Active ou désactive l'accélération matérielle */
void hw_accel_set_enabled(bool enabled);

/* Exécute une opération matérielle */
bool hw_accel_execute_op(hw_op_t *op);

/* Remplit un rectangle avec accélération matérielle */
bool hw_accel_fill_rect(rect_t rect, uint32_t color);

/* Copie un rectangle avec accélération matérielle */
bool hw_accel_copy_rect(rect_t src_rect, point_t dst_point);

/* Copie un rectangle avec alpha blending matériel */
bool hw_accel_blend_rect(rect_t src_rect, point_t dst_point, uint8_t alpha);

/* Synchronise et attend la fin des opérations matérielles */
void hw_accel_sync(void);

#endif /* HW_ACCEL_H */