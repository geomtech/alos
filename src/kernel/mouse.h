/* src/kernel/mouse.h - Driver souris PS/2 pour ALOS
 *
 * Gère la souris PS/2 via le contrôleur 8042.
 * Supporte les mouvements, les 3 boutons et la molette (si disponible).
 */
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

/* Boutons de la souris (compatibles avec gui_types.h) */
#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

/* État de la souris */
typedef struct {
    int32_t x;              /* Position X absolue */
    int32_t y;              /* Position Y absolue */
    int32_t dx;             /* Déplacement X relatif (dernier mouvement) */
    int32_t dy;             /* Déplacement Y relatif (dernier mouvement) */
    int8_t scroll;          /* Défilement molette (-1, 0, +1) */
    uint8_t buttons;        /* État des boutons (bitmask) */
    uint8_t buttons_changed;/* Boutons qui ont changé depuis le dernier événement */
} mouse_state_t;

/* Callback pour les événements souris */
typedef void (*mouse_callback_t)(const mouse_state_t* state);

/**
 * Initialise le driver souris PS/2.
 * 
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int mouse_init(void);

/**
 * Définit les limites de l'écran pour la position de la souris.
 * 
 * @param width Largeur de l'écran
 * @param height Hauteur de l'écran
 */
void mouse_set_bounds(uint32_t width, uint32_t height);

/**
 * Définit la position de la souris.
 * 
 * @param x Position X
 * @param y Position Y
 */
void mouse_set_position(int32_t x, int32_t y);

/**
 * Récupère l'état actuel de la souris.
 * 
 * @return Pointeur vers l'état de la souris (lecture seule)
 */
const mouse_state_t* mouse_get_state(void);

/**
 * Enregistre un callback pour les événements souris.
 * 
 * @param callback Fonction à appeler lors d'un événement
 */
void mouse_set_callback(mouse_callback_t callback);

/**
 * Vérifie si un bouton est actuellement pressé.
 * 
 * @param button Bouton à vérifier (MOUSE_BTN_*)
 * @return true si le bouton est pressé
 */
bool mouse_button_pressed(uint8_t button);

/**
 * Vérifie si un bouton vient d'être pressé (front montant).
 * 
 * @param button Bouton à vérifier
 * @return true si le bouton vient d'être pressé
 */
bool mouse_button_just_pressed(uint8_t button);

/**
 * Vérifie si un bouton vient d'être relâché (front descendant).
 * 
 * @param button Bouton à vérifier
 * @return true si le bouton vient d'être relâché
 */
bool mouse_button_just_released(uint8_t button);

/**
 * Handler d'interruption IRQ12 (appelé par le gestionnaire d'interruptions).
 */
void mouse_irq_handler(void);

/**
 * Active ou désactive la souris.
 */
void mouse_enable(bool enabled);

/**
 * Vérifie si la souris est initialisée et active.
 */
bool mouse_is_available(void);

#endif /* MOUSE_H */
