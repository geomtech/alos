/* src/kernel/fb_console.h - Framebuffer Console for Limine */
#ifndef FB_CONSOLE_H
#define FB_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/limine.h"

/* Console dimensions (in characters) */
#define FB_CONSOLE_COLS     80
#define FB_CONSOLE_ROWS     25
#define FB_CONSOLE_BUFFER_LINES 100

/* Font dimensions (8x16 VGA font) */
#define FONT_WIDTH          8
#define FONT_HEIGHT         16

/* Colors (32-bit ARGB) */
#define FB_COLOR_BLACK      0xFF000000
#define FB_COLOR_BLUE       0xFF0000AA
#define FB_COLOR_GREEN      0xFF00AA00
#define FB_COLOR_CYAN       0xFF00AAAA
#define FB_COLOR_RED        0xFFAA0000
#define FB_COLOR_MAGENTA    0xFFAA00AA
#define FB_COLOR_BROWN      0xFFAA5500
#define FB_COLOR_LIGHT_GRAY 0xFFAAAAAA
#define FB_COLOR_DARK_GRAY  0xFF555555
#define FB_COLOR_LIGHT_BLUE 0xFF5555FF
#define FB_COLOR_LIGHT_GREEN 0xFF55FF55
#define FB_COLOR_LIGHT_CYAN 0xFF55FFFF
#define FB_COLOR_LIGHT_RED  0xFFFF5555
#define FB_COLOR_LIGHT_MAGENTA 0xFFFF55FF
#define FB_COLOR_YELLOW     0xFFFFFF55
#define FB_COLOR_WHITE      0xFFFFFFFF

/* VGA color compatibility mapping */
extern uint32_t vga_to_fb_color[16];

/**
 * Initialize the framebuffer console.
 * @param fb Limine framebuffer structure
 * @return 0 on success, -1 on failure
 */
int fb_console_init(struct limine_framebuffer *fb);

/**
 * Check if framebuffer console is available.
 */
bool fb_console_available(void);

/**
 * Clear the screen with a background color.
 */
void fb_console_clear(uint32_t bg_color);

/**
 * Set the current foreground and background colors.
 */
void fb_console_set_color(uint32_t fg, uint32_t bg);

/**
 * Set color using VGA color indices (0-15).
 */
void fb_console_set_vga_color(uint8_t fg, uint8_t bg);

/**
 * Print a single character.
 */
void fb_console_putc(char c);

/**
 * Print a null-terminated string.
 */
void fb_console_puts(const char *str);

/**
 * Print a hexadecimal number.
 */
void fb_console_put_hex(uint64_t value);

/**
 * Print a decimal number.
 */
void fb_console_put_dec(uint64_t value);

/**
 * Scroll the view up.
 */
void fb_console_scroll_up(void);

/**
 * Scroll the view down.
 */
void fb_console_scroll_down(void);

/**
 * Refresh the display from the buffer.
 */
void fb_console_refresh(void);

/**
 * Get current cursor position.
 */
void fb_console_get_cursor(int *col, int *row);

/**
 * Set cursor position.
 */
void fb_console_set_cursor(int col, int row);

/**
 * Enable or disable the framebuffer console.
 * When disabled, all output functions become no-ops.
 * Used when switching to GUI mode.
 */
void fb_console_set_enabled(bool enabled);

/**
 * Check if the framebuffer console is enabled.
 */
bool fb_console_is_enabled(void);

#endif /* FB_CONSOLE_H */
