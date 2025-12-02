/* src/kernel/keyboard.h - Keyboard driver interface */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "keymap.h"

/* Taille du buffer clavier */
#define KEYBOARD_BUFFER_SIZE 256

/**
 * Lit un caractère du buffer clavier (bloquant).
 * Attend qu'un caractère soit disponible.
 * 
 * @return Le caractère lu
 */
char keyboard_getchar(void);

/**
 * Vérifie si un caractère est disponible dans le buffer.
 * 
 * @return true si un caractère est disponible
 */
bool keyboard_has_char(void);

/**
 * Vide le buffer clavier.
 */
void keyboard_clear_buffer(void);

/**
 * Lit un caractère du buffer clavier (non-bloquant).
 * 
 * @return Le caractère lu, ou 0 si aucun caractère disponible
 */
char keyboard_getchar_nonblock(void);

/**
 * Handler d'interruption clavier (appelé par l'IRQ1).
 */
void keyboard_handler_c(void);

/**
 * Change le layout clavier actif.
 * @param name Nom du layout ("qwerty", "azerty", etc.)
 * @return true si le layout a été changé, false si non trouvé
 */
bool keyboard_set_layout(const char* name);

/**
 * Récupère le nom du layout clavier actif.
 * @return Nom du layout actuel
 */
const char* keyboard_get_layout(void);

#endif /* KEYBOARD_H */
