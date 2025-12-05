/* src/gui/ssfn_render.c - Wrapper SSFN pour ALOS
 * 
 * Intègre le rendu de polices scalables SSFN dans le système GUI.
 * Support UTF-8 complet via Unifont.
 */

/* Définitions nécessaires pour SSFN */
#ifndef NULL
#define NULL ((void*)0)
#endif

#define SSFN_CONSOLEBITMAP_TRUECOLOR
#define SSFN_CONSOLEBITMAP_CONTROL
#include "ssfn.h"
#include "ssfn_render.h"
#include "render.h"
#include "font.h"

/* Police Unifont externe (définie dans unifont_sfn.c) */
extern ssfn_font_t *font_unifont_ssfn;

/* Police SSFN courante */
static ssfn_font_t *current_ssfn_font = NULL;
static int ssfn_initialized = 0;

/* Initialise le rendu SSFN avec le framebuffer et Unifont */
int ssfn_init(void) {
    framebuffer_t *fb = render_get_framebuffer();
    if (!fb || !fb->pixels) return -1;
    
    ssfn_dst.ptr = (uint8_t*)fb->pixels;
    ssfn_dst.w = (int)fb->width;
    ssfn_dst.h = (int)fb->height;
    ssfn_dst.p = (uint16_t)fb->pitch;
    ssfn_dst.fg = 0xFFFFFFFF;  /* Blanc par défaut */
    ssfn_dst.bg = 0;           /* Transparent */
    ssfn_dst.x = 0;
    ssfn_dst.y = 0;
    
    /* Charger Unifont par défaut */
    if (font_unifont_ssfn) {
        ssfn_src = font_unifont_ssfn;
        current_ssfn_font = font_unifont_ssfn;
    }
    
    ssfn_initialized = 1;
    return 0;
}

/* Vérifie si SSFN est initialisé */
int ssfn_is_initialized(void) {
    return ssfn_initialized;
}

/* Définit la police SSFN à utiliser */
void ssfn_set_font(ssfn_font_t *font) {
    current_ssfn_font = font;
    ssfn_src = font;
}

/* Obtient la police SSFN courante */
ssfn_font_t *ssfn_get_font(void) {
    return current_ssfn_font;
}

/* Définit la couleur de premier plan */
void ssfn_set_fg(uint32_t color) {
    ssfn_dst.fg = color;
}

/* Définit la couleur d'arrière-plan (0 = transparent) */
void ssfn_set_bg(uint32_t color) {
    ssfn_dst.bg = color;
}

/* Définit la position du curseur */
void ssfn_set_cursor(int x, int y) {
    ssfn_dst.x = x;
    ssfn_dst.y = y;
}

/* Obtient la position X du curseur */
int ssfn_get_cursor_x(void) {
    return ssfn_dst.x;
}

/* Obtient la position Y du curseur */
int ssfn_get_cursor_y(void) {
    return ssfn_dst.y;
}

/* Affiche une chaîne UTF-8 avec SSFN */
int ssfn_print(const char *str) {
    if (!str || !ssfn_src) return SSFN_ERR_INVINP;
    
    int ret = SSFN_OK;
    char *s = (char*)str;
    
    while (*s) {
        uint32_t unicode = ssfn_utf8(&s);
        ret = ssfn_putc(unicode);
        if (ret != SSFN_OK && ret != SSFN_ERR_NOGLYPH) break;
    }
    
    return ret;
}

/* Affiche une chaîne à une position donnée */
int ssfn_print_at(int x, int y, const char *str) {
    ssfn_set_cursor(x, y);
    return ssfn_print(str);
}

/* Affiche une chaîne avec couleur à une position donnée */
int ssfn_print_color(int x, int y, uint32_t fg, const char *str) {
    ssfn_set_cursor(x, y);
    ssfn_set_fg(fg);
    return ssfn_print(str);
}

/* Obtient la hauteur de la police courante */
int ssfn_font_height(void) {
    if (!ssfn_src) return 16;
    return ssfn_src->height;
}

/* Obtient la largeur moyenne d'un caractère */
int ssfn_font_width(void) {
    if (!ssfn_src) return 8;
    return ssfn_src->width;
}

/* Mesure la largeur d'une chaîne en pixels */
int ssfn_text_width(const char *str) {
    if (!str || !ssfn_src) return 0;
    
    int width = 0;
    char *s = (char*)str;
    uint8_t *ptr, *chr;
    uint32_t unicode;
    int i, j;
    
    while (*s) {
        unicode = ssfn_utf8(&s);
        if (unicode == '\n' || unicode == '\r') continue;
        if (unicode == '\t') {
            width += ssfn_src->width * 4;
            continue;
        }
        
        /* Chercher le glyphe */
        chr = NULL;
        for(ptr = (uint8_t*)ssfn_src + ssfn_src->characters_offs, i = 0; i < 0x110000; i++) {
            if(ptr[0] == 0xFF) { i += 65535; ptr++; }
            else if((ptr[0] & 0xC0) == 0xC0) { j = (((ptr[0] & 0x3F) << 8) | ptr[1]); i += j; ptr += 2; }
            else if((ptr[0] & 0xC0) == 0x80) { j = (ptr[0] & 0x3F); i += j; ptr++; }
            else { if((uint32_t)i == unicode) { chr = ptr; break; } ptr += 6 + ptr[1] * (ptr[0] & 0x40 ? 6 : 5); }
        }
        
        if (chr) {
            width += chr[4];  /* advance x */
        } else {
            width += ssfn_src->width;  /* fallback */
        }
    }
    
    return width;
}
