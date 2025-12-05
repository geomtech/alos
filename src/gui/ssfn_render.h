/* src/gui/ssfn_render.h - Interface SSFN pour ALOS
 * 
 * Rendu de polices SSFN avec support UTF-8 complet.
 */
#ifndef SSFN_RENDER_H
#define SSFN_RENDER_H

#include "ssfn.h"

/* Police Unifont (support Unicode complet) */
extern ssfn_font_t *font_unifont_ssfn;

/* Initialisation */
int ssfn_init(void);
int ssfn_is_initialized(void);
void ssfn_set_font(ssfn_font_t *font);
ssfn_font_t *ssfn_get_font(void);

/* Configuration */
void ssfn_set_fg(uint32_t color);
void ssfn_set_bg(uint32_t color);
void ssfn_set_cursor(int x, int y);
int ssfn_get_cursor_x(void);
int ssfn_get_cursor_y(void);

/* Rendu UTF-8 */
int ssfn_print(const char *str);
int ssfn_print_at(int x, int y, const char *str);
int ssfn_print_color(int x, int y, uint32_t fg, const char *str);

/* Mesures */
int ssfn_font_height(void);
int ssfn_font_width(void);
int ssfn_text_width(const char *str);

#endif /* SSFN_RENDER_H */
