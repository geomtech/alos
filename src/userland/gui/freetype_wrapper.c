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

/*
 * Petit cache de glyphes applicatif.
 *
 * FreeType conserve les tables de police, mais FT_Load_Glyph(...RENDER)
 * rasterise à nouveau le glyphe. Une GUI redessine les mêmes labels/titres
 * très souvent : conserver le bitmap alpha et les métriques évite donc le
 * travail FreeType répété.
 *
 * 128 sets x 4 ways = 512 glyphes. La recherche reste bornée à 4 entrées.
 */
#define FT_GLYPH_CACHE_SETS 128U
#define FT_GLYPH_CACHE_WAYS 4U

typedef struct {
    bool valid;
    ft_font_t* font;
    uint32_t unicode;
    uint32_t font_size;
    uint32_t last_used;

    uint16_t width;
    uint16_t height;
    int16_t bitmap_left;
    int16_t bitmap_top;
    int32_t advance;
    int32_t bearing_x;
    int32_t bearing_y;

    /* Bitmap alpha 8-bit, compact: width * height octets. */
    uint8_t* alpha;
} ft_cached_glyph_t;

static ft_cached_glyph_t
    g_glyph_cache[FT_GLYPH_CACHE_SETS][FT_GLYPH_CACHE_WAYS];
static uint32_t g_glyph_cache_clock = 1;
static uint32_t g_glyph_cache_hits = 0;
static uint32_t g_glyph_cache_misses = 0;

static uint32_t glyph_cache_set(ft_font_t* font, uint32_t unicode) {
    uintptr_t font_key = (uintptr_t)font;
    uint32_t h = unicode * 2654435761U;
    h ^= (uint32_t)(font_key >> 4);
    h ^= font->size * 2246822519U;
    return h & (FT_GLYPH_CACHE_SETS - 1U);
}

static void glyph_cache_release_entry(ft_cached_glyph_t* entry) {
    if (!entry)
        return;
    if (entry->alpha) {
        free(entry->alpha);
        entry->alpha = NULL;
    }
    memset(entry, 0, sizeof(*entry));
}

static void glyph_cache_invalidate_font(ft_font_t* font) {
    if (!font)
        return;

    for (uint32_t set = 0; set < FT_GLYPH_CACHE_SETS; set++) {
        for (uint32_t way = 0; way < FT_GLYPH_CACHE_WAYS; way++) {
            ft_cached_glyph_t* entry = &g_glyph_cache[set][way];
            if (entry->valid && entry->font == font)
                glyph_cache_release_entry(entry);
        }
    }
}

static void glyph_cache_clear(void) {
    for (uint32_t set = 0; set < FT_GLYPH_CACHE_SETS; set++) {
        for (uint32_t way = 0; way < FT_GLYPH_CACHE_WAYS; way++)
            glyph_cache_release_entry(&g_glyph_cache[set][way]);
    }
    g_glyph_cache_clock = 1;
    g_glyph_cache_hits = 0;
    g_glyph_cache_misses = 0;
}

static void glyph_cache_maybe_print_stats(void) {
    uint32_t total = g_glyph_cache_hits + g_glyph_cache_misses;
    if (total == 0 || (total & 2047U) != 0)
        return;

    uint32_t hit_rate = (g_glyph_cache_hits * 100U) / total;
    printf("FONT PERF: cache=%u%% hits=%u misses=%u\n",
           hit_rate, g_glyph_cache_hits, g_glyph_cache_misses);
}

static ft_cached_glyph_t* glyph_cache_load(ft_font_t* font,
                                            uint32_t unicode) {
    if (!font || !font->face)
        return NULL;

    uint32_t set_index = glyph_cache_set(font, unicode);
    ft_cached_glyph_t* set = g_glyph_cache[set_index];

    for (uint32_t way = 0; way < FT_GLYPH_CACHE_WAYS; way++) {
        ft_cached_glyph_t* entry = &set[way];
        if (entry->valid &&
            entry->font == font &&
            entry->unicode == unicode &&
            entry->font_size == font->size) {
            entry->last_used = ++g_glyph_cache_clock;
            g_glyph_cache_hits++;
            glyph_cache_maybe_print_stats();
            return entry;
        }
    }

    g_glyph_cache_misses++;
    glyph_cache_maybe_print_stats();

    FT_UInt glyph_index = FT_Get_Char_Index(font->face, unicode);
    if (glyph_index == 0)
        return NULL;

    FT_Error error =
        FT_Load_Glyph(font->face, glyph_index, FT_LOAD_RENDER);
    if (error)
        return NULL;

    FT_GlyphSlot slot = font->face->glyph;
    FT_Bitmap* bitmap = &slot->bitmap;

    /* Choisir une voie libre, sinon la moins récemment utilisée du set. */
    ft_cached_glyph_t* victim = &set[0];
    for (uint32_t way = 0; way < FT_GLYPH_CACHE_WAYS; way++) {
        if (!set[way].valid) {
            victim = &set[way];
            break;
        }
        if (set[way].last_used < victim->last_used)
            victim = &set[way];
    }

    glyph_cache_release_entry(victim);

    size_t bitmap_size = (size_t)bitmap->width * (size_t)bitmap->rows;
    uint8_t* alpha = NULL;
    if (bitmap_size != 0) {
        alpha = (uint8_t*)malloc(bitmap_size);
        if (!alpha)
            return NULL;

        for (uint32_t row = 0; row < bitmap->rows; row++) {
            uint8_t* dst = alpha + (size_t)row * bitmap->width;
            const uint8_t* src =
                bitmap->buffer + (ptrdiff_t)row * bitmap->pitch;

            if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
                for (uint32_t col = 0; col < bitmap->width; col++) {
                    uint8_t byte = src[col >> 3];
                    dst[col] = (byte & (0x80U >> (col & 7U))) ? 255 : 0;
                }
            } else {
                /* FT_LOAD_RENDER produit normalement un bitmap GRAY 8-bit. */
                memcpy(dst, src, bitmap->width);
            }
        }
    }

    victim->valid = true;
    victim->font = font;
    victim->unicode = unicode;
    victim->font_size = font->size;
    victim->last_used = ++g_glyph_cache_clock;
    victim->width = (uint16_t)bitmap->width;
    victim->height = (uint16_t)bitmap->rows;
    victim->bitmap_left = (int16_t)slot->bitmap_left;
    victim->bitmap_top = (int16_t)slot->bitmap_top;
    victim->advance = slot->advance.x >> 6;
    victim->bearing_x = slot->metrics.horiBearingX >> 6;
    victim->bearing_y = slot->metrics.horiBearingY >> 6;
    victim->alpha = alpha;

    return victim;
}

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
    glyph_cache_clear();

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

    glyph_cache_invalidate_font(font);

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

    glyph_cache_invalidate_font(font);

    FT_Error error = FT_Set_Pixel_Sizes(font->face, 0, size);
    if (error) {
        printf("ft_set_font_size: FT_Set_Pixel_Sizes failed: %d\n", error);
        return -1;
    }

    font->size = size;
    return 0;
}

/* Récupère les métriques d'un glyphe */
int ft_get_glyph_metrics(ft_font_t* font, uint32_t unicode,
                         ft_glyph_metrics_t* metrics) {
    if (!font || !metrics)
        return -1;

    ft_cached_glyph_t* glyph = glyph_cache_load(font, unicode);
    if (!glyph)
        return -1;

    metrics->width = glyph->width;
    metrics->height = glyph->height;
    metrics->xoff = glyph->bitmap_left;
    metrics->yoff = -glyph->bitmap_top;
    metrics->advance = glyph->advance;
    metrics->bearing_x = glyph->bearing_x;
    metrics->bearing_y = glyph->bearing_y;
    return 0;
}

/* Rend un glyphe */
int ft_render_glyph(ft_font_t* font, uint32_t unicode, uint8_t* buffer,
                    int32_t width, int32_t height) {
    if (!font || !buffer || width <= 0 || height <= 0)
        return -1;

    ft_cached_glyph_t* glyph = glyph_cache_load(font, unicode);
    if (!glyph)
        return -1;
    if ((int32_t)glyph->width > width || (int32_t)glyph->height > height)
        return -1;

    for (uint32_t row = 0; row < glyph->height; row++) {
        memcpy(buffer + (size_t)row * (size_t)width,
               glyph->alpha + (size_t)row * glyph->width,
               glyph->width);
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

        ft_cached_glyph_t* glyph = glyph_cache_load(font, unicode);
        if (glyph)
            width += glyph->advance;
        else
            width += (int32_t)(font->size / 2);
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
    if (!font || !font->face || !text || !fb || !fb->pixels)
        return;

    int32_t pen_x = x;
    int32_t pen_y = y;
    const char* s = text;
    uint32_t opaque_color = rgba_to_u32(color);

    while (*s) {
        uint32_t unicode = ft_decode_utf8(&s);
        if (unicode == 0)
            break;

        ft_cached_glyph_t* glyph = glyph_cache_load(font, unicode);
        if (!glyph) {
            pen_x += (int32_t)(font->size / 2);
            continue;
        }

        int32_t glyph_x = pen_x + glyph->bitmap_left;
        int32_t glyph_y = pen_y - glyph->bitmap_top;

        /* Rejeter le bitmap entier avant d'entrer dans ses pixels. */
        if (glyph->alpha &&
            glyph_x < (int32_t)fb->width &&
            glyph_y < (int32_t)fb->height &&
            glyph_x + (int32_t)glyph->width > 0 &&
            glyph_y + (int32_t)glyph->height > 0) {

            uint32_t row_start =
                glyph_y < 0 ? (uint32_t)(-glyph_y) : 0;
            uint32_t row_end = glyph->height;
            if (glyph_y + (int32_t)row_end > (int32_t)fb->height)
                row_end = (uint32_t)((int32_t)fb->height - glyph_y);

            uint32_t col_start =
                glyph_x < 0 ? (uint32_t)(-glyph_x) : 0;
            uint32_t col_end = glyph->width;
            if (glyph_x + (int32_t)col_end > (int32_t)fb->width)
                col_end = (uint32_t)((int32_t)fb->width - glyph_x);

            for (uint32_t row = row_start; row < row_end; row++) {
                const uint8_t* alpha_row =
                    glyph->alpha + (size_t)row * glyph->width;

                for (uint32_t col = col_start; col < col_end; col++) {
                    uint8_t coverage = alpha_row[col];
                    if (coverage == 0)
                        continue;

                    int32_t px = glyph_x + (int32_t)col;
                    int32_t py = glyph_y + (int32_t)row;

                    if (coverage == 255 && color.a == 255) {
                        draw_pixel(px, py, opaque_color);
                    } else {
                        rgba_t final_color = color;
                        final_color.a =
                            (uint8_t)(((uint32_t)color.a * coverage) / 255U);
                        draw_pixel_alpha(px, py, final_color);
                    }
                }
            }
        }

        pen_x += glyph->advance;
    }
}

