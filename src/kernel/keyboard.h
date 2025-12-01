/* src/kernel/keyboard.h - Keyboard driver interface */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

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
 * Handler d'interruption clavier (appelé par l'IRQ1).
 */
void keyboard_handler_c(void);

#endif /* KEYBOARD_H */
