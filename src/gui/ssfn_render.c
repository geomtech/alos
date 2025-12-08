/* src/gui/ssfn_render.c - Wrapper SSFN Bitmap pour ALOS
 * 
 * Intègre le rendu de polices SSFN en mode bitmap simple dans le système GUI.
 * Support UTF-8 complet via Unifont.
 * 
 * NOTE: Ce wrapper utilise UNIQUEMENT le renderer bitmap SSFN pour la stabilité.
 * Le renderer scalable a été désactivé pour éviter les conflits avec le nouveau
 * système de heap kernel (spinlocks, etc.).
 * 
 * Avantages du mode bitmap:
 * - Pas d'allocations dynamiques (stabilité)
 * - Support UTF-8 complet via Unifont
 * - API simple et fiable
 * - Taille fixe des polices (consistance)
 */

#ifndef NULL
#define NULL ((void*)0)
#endif

/* Configuration SSFN - Mode Bitmap Uniquement
 * 
 * Ce fichier utilise uniquement le renderer bitmap SSFN pour la stabilité.
 * Le renderer scalable a été désactivé pour éviter les conflits avec le
 * nouveau système de heap kernel (utilisation de spinlocks).
 * 
 * Avantages:
 * - Aucune allocation dynamique (allocation-free)
 * - Support UTF-8 complet via Unifont
 * - API stable et prévisible
 * - Compatible avec tous les contextes kernel (interruptions, etc.)
 */
#define SSFN_memcmp memcmp
#define SSFN_memset memset

/* Renderer bitmap simple uniquement */
#define SSFN_CONSOLEBITMAP_TRUECOLOR
#define SSFN_CONSOLEBITMAP_CONTROL

#include "../mm/kheap.h"
#include "../include/string.h"
#include "ssfn.h"
#include "ssfn_render.h"
#include "render.h"
#include "font.h"
#include "../kernel/klog.h"

/* Police Unifont externe (définie dans unifont_sfn.c) */
extern ssfn_font_t *font_unifont_ssfn;

/* Police SSFN courante (renderer bitmap simple) */
static ssfn_font_t *current_ssfn_font = NULL;
static int ssfn_initialized = 0;

/* Initialise le rendu SSFN avec le framebuffer et Unifont */
int ssfn_init(void) {
    framebuffer_t *fb = render_get_framebuffer();
    if (!fb || !fb->pixels) return -1;
    
    /* Configuration du buffer destination */
    ssfn_dst.ptr = (uint8_t*)fb->pixels;
    ssfn_dst.w = (int)fb->width;
    ssfn_dst.h = (int)fb->height;
    ssfn_dst.p = (uint16_t)fb->pitch;
    ssfn_dst.fg = 0xFFFFFFFF;  /* Blanc par défaut */
    ssfn_dst.bg = 0;           /* Transparent */
    ssfn_dst.x = 0;
    ssfn_dst.y = 0;
    
    /* Charger Unifont pour le renderer bitmap simple */
    if (font_unifont_ssfn) {
        ssfn_src = font_unifont_ssfn;
        current_ssfn_font = font_unifont_ssfn;
        klog(LOG_INFO, "SSFN", "Unifont loaded for bitmap renderer");
    } else {
        klog(LOG_WARN, "SSFN", "font_unifont_ssfn is NULL!");
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

/* ============================================================================
 * RENDERER SCALABLE - Permet de choisir la taille de la police
 * ============================================================================ */

/* Sélectionne la taille de la police pour le renderer scalable */
int ssfn_set_size(int size) {
    /* Le renderer bitmap simple a une taille fixe, on ignore la demande */
    klog(LOG_WARN, "SSFN", "Bitmap renderer doesn't support scalable sizes");
    return -1;
}

/* Affiche une chaîne avec le renderer scalable */
int ssfn_render_text(int x, int y, uint32_t color, const char *str) {
    if (!str) return -1;
    
    klog(LOG_INFO, "SSFN", "render_text called");
    
    /* Utiliser le renderer bitmap simple avec les paramètres corrects */
    klog(LOG_INFO, "SSFN", "Using bitmap renderer fallback");
    ssfn_dst.x = x;
    ssfn_dst.y = y;
    ssfn_dst.fg = color;
    return ssfn_print(str);
}

/* Affiche une chaîne avec taille spécifique */
int ssfn_render_text_size(int x, int y, int size, uint32_t color, const char *str) {
    /* Le renderer bitmap simple ignore la taille demandée */
    klog(LOG_INFO, "SSFN", "Ignoring size request for bitmap renderer");
    return ssfn_render_text(x, y, color, str);
}

/* Vérifie si le renderer scalable est disponible */
int ssfn_scalable_available(void) {
    /* Le renderer scalable est désactivé, on utilise uniquement le bitmap */
    return 0;
}

/* Libère les ressources du renderer scalable */
void ssfn_cleanup(void) {
    /* Rien à nettoyer pour le renderer bitmap simple */
    klog(LOG_INFO, "SSFN", "Cleanup complete");
}
