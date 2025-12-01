/* src/console.h - Console virtuelle avec scrolling */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stddef.h>

/* Dimensions de l'écran VGA */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* Taille du buffer virtuel (nombre de lignes) */
#define CONSOLE_BUFFER_LINES 100

/* Couleurs VGA */
#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_DARK_GREY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW        14
#define VGA_COLOR_WHITE         15

/**
 * Initialise la console virtuelle.
 */
void console_init(void);

/**
 * Efface la console avec une couleur de fond.
 */
void console_clear(uint8_t bg_color);

/**
 * Définit la couleur courante (texte et fond).
 */
void console_set_color(uint8_t fg, uint8_t bg);

/**
 * Affiche un caractère à la position courante.
 */
void console_putc(char c);

/**
 * Affiche une chaîne de caractères.
 */
void console_puts(const char* str);

/**
 * Affiche un nombre en hexadécimal.
 */
void console_put_hex(uint32_t value);

/**
 * Affiche un octet en hexadécimal (2 caractères).
 */
void console_put_hex_byte(uint8_t value);

/**
 * Affiche un nombre en décimal.
 */
void console_put_dec(uint32_t value);

/**
 * Scroll la vue vers le haut (montre les lignes précédentes).
 */
void console_scroll_up(void);

/**
 * Scroll la vue vers le bas (montre les lignes suivantes).
 */
void console_scroll_down(void);

/**
 * Rafraîchit l'affichage VGA depuis le buffer.
 */
void console_refresh(void);

/**
 * Retourne la ligne de vue actuelle.
 */
int console_get_view_line(void);

/**
 * Retourne la ligne d'écriture actuelle.
 */
int console_get_current_line(void);

#endif /* CONSOLE_H */
