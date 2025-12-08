/* src/gui/events.h - Système d'événements pour ALOS GUI
 * 
 * Gère la distribution des événements souris et clavier
 * vers les différents composants de l'interface.
 */
#ifndef EVENTS_H
#define EVENTS_H

#include "gui_types.h"

/* Taille de la file d'événements */
#define EVENT_QUEUE_SIZE 64

/* Initialise le système d'événements */
int events_init(void);

/* Libère les ressources */
void events_shutdown(void);

/* Ajoute un événement à la file */
void events_push(event_t* event);

/* Récupère le prochain événement (NULL si vide) */
event_t* events_pop(void);

/* Vérifie si la file est vide */
bool events_empty(void);

/* Traite tous les événements en attente */
void events_process(void);

/* Dispatch un événement vers les composants appropriés */
void events_dispatch(event_t* event);

/* Génère un événement souris */
void events_mouse_move(int32_t x, int32_t y);
void events_mouse_button(mouse_button_t button, bool pressed);
void events_mouse_scroll(int32_t delta);

/* Génère un événement clavier */
void events_key(uint8_t scancode, char character, bool pressed, key_modifier_t mods);

/* Récupère la position actuelle de la souris */
point_t events_get_mouse_pos(void);

/* Récupère les modificateurs clavier actifs */
key_modifier_t events_get_modifiers(void);

#endif /* EVENTS_H */
