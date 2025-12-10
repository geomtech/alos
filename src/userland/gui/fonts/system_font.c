/* src/userland/gui/fonts/system_font.c - Police TTF système intégrée
 * 
 * Contient une police TrueType système pour ALOS.
 * Pour l'instant, utilise une police de fallback ou charge depuis un fichier.
 * 
 * NOTE: Pour intégrer une vraie police TTF, utilisez un outil comme:
 *   xxd -i font.ttf > system_font_data.h
 * puis incluez les données ici.
 */

#include "../font.h"
#include "../freetype_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Police système TTF (sera chargée depuis les données) */
static ft_font_t* g_system_ttf_font = NULL;
static font_t g_system_font_struct = {0};

/* Inclure les données de la police générées */
#include "system_font_data.h"

/* Charge la police système TTF */
static int load_system_ttf_font(void) {
    if (g_system_ttf_font)
        return 0;

    /* Initialiser FreeType si nécessaire */
    if (ft_init() != 0) {
        printf("load_system_ttf_font: ft_init failed\n");
        return -1;
    }

    /* Charger la police depuis les données en mémoire */
    g_system_ttf_font = ft_load_font_from_memory(
        src_userland_gui_fonts_system_font_ttf,
        src_userland_gui_fonts_system_font_ttf_len,
        16  /* Taille par défaut 16px */
    );

    if (!g_system_ttf_font) {
        printf("load_system_ttf_font: failed to load font\n");
        return -1;
    }

    /* Remplir la structure font_t */
    g_system_font_struct.type = FONT_TYPE_TRUETYPE;
    g_system_font_struct.ft_font = g_system_ttf_font;
    g_system_font_struct.size = 16;
    g_system_font_struct.style = FONT_STYLE_REGULAR;
    g_system_font_struct.name = "Roboto Regular";
    g_system_font_struct.glyphs = NULL;
    g_system_font_struct.glyph_width = 0;
    g_system_font_struct.glyph_height = 0;

    return 0;
}

/* Obtient la police système TTF */
font_t* font_get_system_ttf(void) {
    if (g_system_ttf_font) {
        return &g_system_font_struct;
    }
    if (load_system_ttf_font() == 0) {
        return &g_system_font_struct;
    }
    return NULL;
}

/* Libère la police système TTF */
void font_free_system_ttf(void) {
    if (g_system_ttf_font) {
        ft_free_font(g_system_ttf_font);
        g_system_ttf_font = NULL;
    }
    memset(&g_system_font_struct, 0, sizeof(g_system_font_struct));
}

