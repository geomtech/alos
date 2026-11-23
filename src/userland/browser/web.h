/* browser/web.h - Moteur web minimal d'ALOS ("alosweb")
 *
 * Etape intermediaire vers Chromium : un client HTTP, un parseur/mise en
 * page HTML tres reduit et un rendu logiciel, sans dependance a la GUI.
 * Le moteur est volontairement isole derriere cette API (fetch -> layout ->
 * render) afin de pouvoir etre remplace plus tard par Chromium/Content Shell
 * sans toucher a la fenetre du navigateur (main.c).
 */
#ifndef ALOS_WEB_H
#define ALOS_WEB_H

#include <stddef.h>
#include <stdint.h>

#define WEB_URL_MAX 1100
#define WEB_MAX_BODY (4u * 1024u * 1024u)

typedef struct {
  char scheme[8];
  char host[256];
  char path[WEB_URL_MAX];
  int port;
} web_url_t;

/* URL absolue http/https/file/about. 0 en cas de succes, -1 sinon. */
int web_url_parse(const char *text, web_url_t *url);
/* Resout ref (absolu, //hote, /chemin, relatif ou #ancre) par rapport a base. */
void web_url_resolve(const char *base, const char *ref, char *out, size_t size);

typedef struct {
  char *body;
  size_t length;
  int status;
  char final_url[WEB_URL_MAX];
  char error[128];
} web_response_t;

/* Recupere une URL (http:, file:, about:). 0 si une page est disponible
 * (meme une page d'erreur HTTP), -1 sinon avec response->error renseigne. */
int web_fetch(const char *url, web_response_t *response);
void web_response_free(web_response_t *response);
/* Decode un corps "Transfer-Encoding: chunked" en place, renvoie la nouvelle taille. */
size_t web_dechunk(char *data, size_t length);

typedef enum { WEB_ITEM_TEXT, WEB_ITEM_RECT } web_item_type_t;

typedef struct {
  web_item_type_t type;
  int32_t x, y, w, h;
  uint32_t color;
  int scale;      /* 1 = 8x16, 2 = 16x32 */
  int bold;
  int underline;
  int link;       /* index dans page->links, -1 sinon */
  char *text;
} web_item_t;

typedef struct {
  web_item_t *items;
  size_t count, capacity;
  char **links;
  size_t link_count, link_capacity;
  int height;
  char title[128];
} web_page_t;

int web_layout(const char *html, size_t length, int width, const char *base_url,
               web_page_t *page);
void web_page_free(web_page_t *page);
/* Index de l'element lien sous (x, y) en coordonnees de page, ou -1. */
int web_page_link_at(const web_page_t *page, int x, int y);

/* Rendu logiciel ARGB. scroll_y = ordonnee de page affichee en haut. */
void web_render(const web_page_t *page, uint32_t *pixels, uint32_t stride,
                int width, int height, int scroll_y, uint32_t background);
void web_draw_text(uint32_t *pixels, uint32_t stride, int width, int height,
                   int x, int y, const char *text, uint32_t color);

#endif
