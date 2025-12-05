/* src/gui/compositor.c - Implémentation du compositeur graphique */

#include "compositor.h"
#include "render.h"
#include "../mm/kheap.h"
#include "../include/string.h"

/* Framebuffer principal */
static framebuffer_t* g_main_fb = NULL;

/* Liste des couches (triée par Z-order) */
static layer_t* g_layers_head = NULL;
static uint32_t g_next_layer_id = 1;

/* Dirty rectangles */
#define MAX_DIRTY_RECTS 64
static rect_t g_dirty_rects[MAX_DIRTY_RECTS];
static uint32_t g_dirty_count = 0;

/* Fond d'écran */
static uint32_t g_bg_color = 0xFF1E3A5F;  /* Bleu foncé par défaut */
static bool g_bg_gradient = false;
static rgba_t g_bg_color1, g_bg_color2;
static gradient_direction_t g_bg_dir;

int compositor_init(framebuffer_t* fb) {
    if (!fb) return -1;
    g_main_fb = fb;
    g_layers_head = NULL;
    g_next_layer_id = 1;
    g_dirty_count = 0;
    
    /* Invalide tout l'écran au démarrage */
    rect_t full = {0, 0, fb->width, fb->height};
    compositor_invalidate_rect(full);
    
    return 0;
}

void compositor_shutdown(void) {
    /* Libère toutes les couches */
    layer_t* layer = g_layers_head;
    while (layer) {
        layer_t* next = layer->next;
        if (layer->buffer && layer->buffer->owns_memory) {
            kfree(layer->buffer->pixels);
            kfree(layer->buffer);
        }
        kfree(layer);
        layer = next;
    }
    g_layers_head = NULL;
}

layer_t* compositor_create_layer(layer_type_t type, rect_t bounds) {
    layer_t* layer = (layer_t*)kmalloc(sizeof(layer_t));
    if (!layer) return NULL;
    
    memset(layer, 0, sizeof(layer_t));
    layer->id = g_next_layer_id++;
    layer->type = type;
    layer->bounds = bounds;
    layer->visible = true;
    layer->needs_redraw = true;
    layer->opacity = 255;
    layer->buffer = NULL;
    layer->next = NULL;
    
    return layer;
}

void compositor_destroy_layer(layer_t* layer) {
    if (!layer) return;
    compositor_remove_layer(layer);
    
    if (layer->buffer) {
        if (layer->buffer->owns_memory && layer->buffer->pixels) {
            kfree(layer->buffer->pixels);
        }
        kfree(layer->buffer);
    }
    kfree(layer);
}

/* Insère une couche à la bonne position selon son type */
void compositor_add_layer(layer_t* layer) {
    if (!layer) return;
    
    /* Trouve la position d'insertion selon le type */
    layer_t** pp = &g_layers_head;
    while (*pp && (*pp)->type <= layer->type) {
        pp = &(*pp)->next;
    }
    
    layer->next = *pp;
    *pp = layer;
    
    compositor_invalidate_layer(layer);
}

void compositor_remove_layer(layer_t* layer) {
    if (!layer) return;
    
    layer_t** pp = &g_layers_head;
    while (*pp && *pp != layer) {
        pp = &(*pp)->next;
    }
    
    if (*pp) {
        compositor_invalidate_rect(layer->bounds);
        *pp = layer->next;
        layer->next = NULL;
    }
}

void compositor_raise_layer(layer_t* layer) {
    if (!layer || !layer->next) return;
    
    /* Retire la couche */
    layer_t** pp = &g_layers_head;
    while (*pp && *pp != layer) pp = &(*pp)->next;
    if (!*pp) return;
    *pp = layer->next;
    
    /* Trouve la dernière couche du même type */
    layer_t** insert = pp;
    while (*insert && (*insert)->type == layer->type) {
        insert = &(*insert)->next;
    }
    
    layer->next = *insert;
    *insert = layer;
    
    compositor_invalidate_layer(layer);
}

void compositor_lower_layer(layer_t* layer) {
    if (!layer) return;
    
    /* Retire la couche */
    layer_t** pp = &g_layers_head;
    while (*pp && *pp != layer) pp = &(*pp)->next;
    if (!*pp) return;
    *pp = layer->next;
    
    /* Trouve la première couche du même type */
    layer_t** insert = &g_layers_head;
    while (*insert && (*insert)->type < layer->type) {
        insert = &(*insert)->next;
    }
    
    layer->next = *insert;
    *insert = layer;
    
    compositor_invalidate_layer(layer);
}

void compositor_invalidate_rect(rect_t rect) {
    if (g_dirty_count >= MAX_DIRTY_RECTS) {
        /* Fusionne tout en un seul rectangle */
        g_dirty_rects[0].x = 0;
        g_dirty_rects[0].y = 0;
        g_dirty_rects[0].width = g_main_fb->width;
        g_dirty_rects[0].height = g_main_fb->height;
        g_dirty_count = 1;
        return;
    }
    
    /* Clipping au framebuffer */
    if (rect.x < 0) { rect.width += (uint32_t)rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.height += (uint32_t)rect.y; rect.y = 0; }
    if (rect.x + (int32_t)rect.width > (int32_t)g_main_fb->width)
        rect.width = g_main_fb->width - (uint32_t)rect.x;
    if (rect.y + (int32_t)rect.height > (int32_t)g_main_fb->height)
        rect.height = g_main_fb->height - (uint32_t)rect.y;
    
    if (rect.width == 0 || rect.height == 0) return;
    
    g_dirty_rects[g_dirty_count++] = rect;
}

void compositor_invalidate_layer(layer_t* layer) {
    if (layer) {
        layer->needs_redraw = true;
        compositor_invalidate_rect(layer->bounds);
    }
}

static void draw_background(rect_t region) {
    if (g_bg_gradient) {
        draw_gradient(region, g_bg_color1, g_bg_color2, g_bg_dir);
    } else {
        draw_rect(region, g_bg_color);
    }
}

void compositor_render(void) {
    if (!g_main_fb || g_dirty_count == 0) return;
    
    /* Rend chaque dirty rectangle */
    for (uint32_t i = 0; i < g_dirty_count; i++) {
        compositor_render_region(g_dirty_rects[i]);
    }
    
    g_dirty_count = 0;
    
    /* Flip si double buffering */
    render_flip();
}

void compositor_render_region(rect_t region) {
    /* Dessine le fond */
    draw_background(region);
    
    /* Dessine chaque couche visible qui intersecte la région */
    render_push_clip(region);
    
    for (layer_t* layer = g_layers_head; layer; layer = layer->next) {
        if (!layer->visible) continue;
        if (!rects_intersect(layer->bounds, region)) continue;
        
        /* Appelle le callback de dessin si défini */
        if (layer->draw_callback) {
            layer->draw_callback(layer);
        }
        
        /* Si la couche a un buffer, le copie */
        if (layer->buffer && layer->buffer->pixels) {
            if (layer->opacity == 255) {
                draw_bitmap(point_make(layer->bounds.x, layer->bounds.y),
                           layer->buffer->pixels,
                           layer->buffer->width, layer->buffer->height);
            } else {
                /* TODO: copie avec opacité */
                draw_bitmap_alpha(point_make(layer->bounds.x, layer->bounds.y),
                                 layer->buffer->pixels,
                                 layer->buffer->width, layer->buffer->height);
            }
        }
        
        layer->needs_redraw = false;
    }
    
    render_pop_clip();
}

void compositor_set_background_color(uint32_t color) {
    g_bg_color = color;
    g_bg_gradient = false;
    
    rect_t full = {0, 0, g_main_fb->width, g_main_fb->height};
    compositor_invalidate_rect(full);
}

void compositor_set_background_gradient(rgba_t color1, rgba_t color2, gradient_direction_t dir) {
    g_bg_color1 = color1;
    g_bg_color2 = color2;
    g_bg_dir = dir;
    g_bg_gradient = true;
    
    rect_t full = {0, 0, g_main_fb->width, g_main_fb->height};
    compositor_invalidate_rect(full);
}
