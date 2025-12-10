/* src/userland/gui/freetype_wrapper.c - Implémentation du wrapper FreeType
 * 
 * Wrapper autour de FreeType pour un environnement freestanding.
 * Charge les polices depuis la mémoire et rend les glyphes.
 */

#include "freetype_wrapper.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Inclure FreeType */
#define FT2_BUILD_LIBRARY
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_BITMAP_H

/* Library FreeType globale */
static FT_Library g_ft_library = NULL;
static bool g_ft_initialized = false;

/* Fonction de lecture mémoire pour FreeType */
static unsigned long ft_memory_read(FT_Stream stream, unsigned long offset,
                                    unsigned char* buffer, unsigned long count) {
    const uint8_t* data = (const uint8_t*)stream->descriptor.pointer;
    if (offset >= stream->size)
        return 0;
    if (offset + count > stream->size)
        count = stream->size - offset;
    if (buffer)
        memcpy(buffer, data + offset, count);
    return count;
}

static void ft_memory_close(FT_Stream stream) {
    /* Rien à faire, les données sont gérées ailleurs */
    (void)stream;
}

/* Initialise le système FreeType */
int ft_init(void) {
    if (g_ft_initialized)
        return 0;

    FT_Error error = FT_Init_FreeType(&g_ft_library);
    if (error) {
        printf("ft_init: FT_Init_FreeType failed: %d\n", error);
        return -1;
    }

    g_ft_initialized = true;
    return 0;
}

/* Libère les ressources */
void ft_cleanup(void) {
    if (g_ft_library) {
        FT_Done_FreeType(g_ft_library);
        g_ft_library = NULL;
    }
    g_ft_initialized = false;
}

/* Charge une police depuis la mémoire */
ft_font_t* ft_load_font_from_memory(const uint8_t* font_data, size_t font_size, uint32_t size) {
    if (!g_ft_initialized) {
        if (ft_init() != 0)
            return NULL;
    }

    if (!font_data || font_size == 0)
        return NULL;

    ft_font_t* font = (ft_font_t*)malloc(sizeof(ft_font_t));
    if (!font) {
        printf("ft_load_font_from_memory: malloc failed\n");
        return NULL;
    }

    memset(font, 0, sizeof(ft_font_t));
    font->font_data = font_data;
    font->font_size = font_size;
    font->size = size;
    font->library = g_ft_library;

    FT_Open_Args args;
    memset(&args, 0, sizeof(args));
    args.flags = FT_OPEN_MEMORY;
    args.memory_base = (const FT_Byte*)font_data;
    args.memory_size = (FT_Long)font_size;

    FT_Error error = FT_Open_Face(g_ft_library, &args, 0, &font->face);
    if (error) {
        printf("ft_load_font_from_memory: FT_Open_Face failed: %d\n", error);
        free(font);
        return NULL;
    }

    /* Définir la taille */
    error = FT_Set_Pixel_Sizes(font->face, 0, size);
    if (error) {
        printf("ft_load_font_from_memory: FT_Set_Pixel_Sizes failed: %d\n", error);
        FT_Done_Face(font->face);
        free(font);
        return NULL;
    }

    font->initialized = true;
    return font;
}

/* Libère une police */
void ft_free_font(ft_font_t* font) {
    if (!font)
        return;
    if (font->face) {
        FT_Done_Face(font->face);
        font->face = NULL;
    }
    free(font);
}

/* Définit la taille */
int ft_set_font_size(ft_font_t* font, uint32_t size) {
    if (!font || !font->face)
        return -1;

    FT_Error error = FT_Set_Pixel_Sizes(font->face, 0, size);
    if (error) {
        printf("ft_set_font_size: FT_Set_Pixel_Sizes failed: %d\n", error);
        return -1;
    }

    font->size = size;
    return 0;
}

/* Récupère les métriques d'un glyphe */
int ft_get_glyph_metrics(ft_font_t* font, uint32_t unicode, ft_glyph_metrics_t* metrics) {
    if (!font || !font->face || !metrics)
        return -1;

    FT_UInt glyph_index = FT_Get_Char_Index(font->face, unicode);
    if (glyph_index == 0)
        return -1;

    FT_Error error = FT_Load_Glyph(font->face, glyph_index, FT_LOAD_DEFAULT);
    if (error)
        return -1;

    FT_GlyphSlot slot = font->face->glyph;
    metrics->width = slot->bitmap.width;
    metrics->height = slot->bitmap.rows;
    metrics->xoff = slot->bitmap_left;
    metrics->yoff = -slot->bitmap_top;
    metrics->advance = slot->advance.x >> 6; /* Convertir de 26.6 fixed point */
    metrics->bearing_x = slot->metrics.horiBearingX >> 6;
    metrics->bearing_y = slot->metrics.horiBearingY >> 6;

    return 0;
}

/* Rend un glyphe */
int ft_render_glyph(ft_font_t* font, uint32_t unicode, uint8_t* buffer,
                    int32_t width, int32_t height) {
    if (!font || !font->face || !buffer)
        return -1;

    FT_UInt glyph_index = FT_Get_Char_Index(font->face, unicode);
    if (glyph_index == 0)
        return -1;

    FT_Error error = FT_Load_Glyph(font->face, glyph_index, FT_LOAD_RENDER);
    if (error)
        return -1;

    FT_Bitmap* bitmap = &font->face->glyph->bitmap;
    if (bitmap->width > (unsigned)width || bitmap->rows > (unsigned)height)
        return -1;

    /* Copier le bitmap dans le buffer */
    for (unsigned int y = 0; y < bitmap->rows; y++) {
        for (unsigned int x = 0; x < bitmap->width; x++) {
            uint8_t alpha = bitmap->buffer[y * bitmap->pitch + x];
            buffer[y * width + x] = alpha;
        }
    }

    return 0;
}

/* Décode UTF-8 */
uint32_t ft_decode_utf8(const char** str) {
    if (!str || !*str || !**str)
        return 0;

    const unsigned char* s = (const unsigned char*)*str;
    uint32_t unicode = 0;

    if ((s[0] & 0x80) == 0) {
        /* ASCII */
        unicode = s[0];
        (*str)++;
    } else if ((s[0] & 0xE0) == 0xC0) {
        /* 2 bytes */
        if ((s[1] & 0xC0) != 0x80)
            return 0;
        unicode = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        (*str) += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        /* 3 bytes */
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
            return 0;
        unicode = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        (*str) += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        /* 4 bytes */
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
            return 0;
        unicode = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | 
                  ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        (*str) += 4;
    } else {
        return 0;
    }

    return unicode;
}

/* Calcule la largeur d'un texte */
int32_t ft_get_text_width(ft_font_t* font, const char* text) {
    if (!font || !font->face || !text)
        return 0;

    int32_t width = 0;
    const char* s = text;

    while (*s) {
        uint32_t unicode = ft_decode_utf8(&s);
        if (unicode == 0)
            break;

        FT_UInt glyph_index = FT_Get_Char_Index(font->face, unicode);
        if (glyph_index == 0) {
            width += font->size / 2; /* Fallback */
            continue;
        }

        FT_Error error = FT_Load_Glyph(font->face, glyph_index, FT_LOAD_DEFAULT);
        if (error)
            continue;

        width += font->face->glyph->advance.x >> 6;
    }

    return width;
}

/* Calcule la hauteur */
int32_t ft_get_text_height(ft_font_t* font) {
    if (!font || !font->face)
        return 0;
    return font->face->size->metrics.height >> 6;
}

/* Calcule les dimensions */
void ft_get_text_dimensions(ft_font_t* font, const char* text,
                            int32_t* width, int32_t* height) {
    if (width)
        *width = ft_get_text_width(font, text);
    if (height)
        *height = ft_get_text_height(font);
}

/* Rend un texte dans le framebuffer */
void ft_render_text(ft_font_t* font, const char* text, int32_t x, int32_t y,
                    rgba_t color, framebuffer_t* fb) {
    if (!font || !font->face || !text || !fb || !fb->pixels) {
        return;
    }

    int32_t pen_x = x;
    int32_t pen_y = y;
    const char* s = text;

    while (*s) {
        uint32_t unicode = ft_decode_utf8(&s);
        if (unicode == 0)
            break;

        /* printf("DEBUG: char U+%X\n", unicode); */

        FT_UInt glyph_index = FT_Get_Char_Index(font->face, unicode);
        if (glyph_index == 0)
            continue;

        FT_Error error = FT_Load_Glyph(font->face, glyph_index, FT_LOAD_RENDER);
        if (error) {
            continue;
        }

        FT_Bitmap* bitmap = &font->face->glyph->bitmap;
        int32_t glyph_x = pen_x + font->face->glyph->bitmap_left;
        int32_t glyph_y = pen_y - font->face->glyph->bitmap_top;

        /* Rendre le glyphe */
        for (unsigned int row = 0; row < bitmap->rows; row++) {
            for (unsigned int col = 0; col < bitmap->width; col++) {
                uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                if (alpha == 0)
                    continue;

                int32_t px = glyph_x + (int32_t)col;
                int32_t py = glyph_y + (int32_t)row;

                if (px < 0 || py < 0 || px >= (int32_t)fb->width || py >= (int32_t)fb->height)
                    continue;

                /* Alpha blending */
                rgba_t final_color = color;
                final_color.a = (uint8_t)((uint32_t)color.a * alpha / 255);
                draw_pixel_alpha(px, py, final_color);
            }
        }

        pen_x += font->face->glyph->advance.x >> 6;
    }
}

