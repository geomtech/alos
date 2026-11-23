/* browser/html.c - Tokenizer HTML tolerant et mise en page en flux (blocs + lignes) */
#include "web.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CHAR_W 8
#define CHAR_H 16
#define MARGIN 10
#define MAX_DEPTH 32

#define COLOR_TEXT 0xFF202124u
#define COLOR_LINK 0xFF1A0DABu
#define COLOR_RULE 0xFFB0B4B8u
#define COLOR_QUOTE 0xFF80868Bu
#define COLOR_CODE 0xFF8A2D1Eu

typedef struct {
  web_page_t *page;
  const char *base;
  int width;       /* largeur disponible */
  int x, y;        /* curseur ; y = haut de la ligne courante */
  int indent;
  size_t line_start;
  int line_h;
  int line_words;
  int bold, pre, heading_scale, link, in_code, in_quote;
  int pending_space;
  int list_depth;
  int list_ordered[MAX_DEPTH];
  int list_counter[MAX_DEPTH];
  int oom;
} layout_t;

static web_item_t *add_item(layout_t *l, web_item_type_t type) {
  web_page_t *p = l->page;
  if (p->count == p->capacity) {
    size_t cap = p->capacity ? p->capacity * 2 : 128;
    web_item_t *n = realloc(p->items, cap * sizeof(*n));
    if (n == NULL) { l->oom = 1; return NULL; }
    p->items = n; p->capacity = cap;
  }
  web_item_t *item = &p->items[p->count++];
  memset(item, 0, sizeof(*item));
  item->type = type;
  item->link = -1;
  return item;
}

static void flush_line(layout_t *l) {
  if (l->line_words == 0) { l->x = l->indent + MARGIN; return; }
  for (size_t i = l->line_start; i < l->page->count; i++) l->page->items[i].y = l->y;
  l->y += l->line_h + 4;
  l->x = l->indent + MARGIN;
  l->line_start = l->page->count;
  l->line_h = CHAR_H;
  l->line_words = 0;
  l->pending_space = 0;
}

static void block_break(layout_t *l, int gap) {
  flush_line(l);
  l->y += gap;
}

static void emit_word(layout_t *l, const char *w, size_t n, uint32_t color) {
  if (n == 0 || l->oom) return;
  int scale = l->heading_scale ? l->heading_scale : 1;
  int space = l->pending_space && l->line_words > 0 ? CHAR_W * scale : 0;
  int ww = (int)n * CHAR_W * scale;
  int limit = l->width - MARGIN;
  if (l->x + space + ww > limit && l->line_words > 0) {
    flush_line(l);
    space = 0;
  }
  /* Mot plus large que la ligne : on le coupe. */
  while (ww > limit - l->x && n > 1 && (limit - l->x) >= CHAR_W * scale) {
    size_t fit = (size_t)((limit - l->x) / (CHAR_W * scale));
    if (fit == 0 || fit >= n) break;
    web_item_t *it = add_item(l, WEB_ITEM_TEXT);
    if (it == NULL) return;
    it->text = malloc(fit + 1);
    if (it->text == NULL) { l->oom = 1; return; }
    memcpy(it->text, w, fit); it->text[fit] = '\0';
    it->x = l->x + space; it->w = (int)fit * CHAR_W * scale;
    it->h = CHAR_H * scale; it->color = color; it->scale = scale;
    it->bold = l->bold || l->heading_scale; it->link = l->link;
    it->underline = l->link >= 0;
    if (it->h > l->line_h) l->line_h = it->h;
    l->line_words++;
    flush_line(l);
    space = 0; w += fit; n -= fit; ww = (int)n * CHAR_W * scale;
  }
  web_item_t *it = add_item(l, WEB_ITEM_TEXT);
  if (it == NULL) return;
  it->text = malloc(n + 1);
  if (it->text == NULL) { l->oom = 1; return; }
  memcpy(it->text, w, n); it->text[n] = '\0';
  it->x = l->x + space; it->w = ww; it->h = CHAR_H * scale;
  it->color = color; it->scale = scale;
  it->bold = l->bold || l->heading_scale; it->link = l->link;
  it->underline = l->link >= 0;
  if (it->h > l->line_h) l->line_h = it->h;
  l->x += space + ww;
  l->line_words++;
  l->pending_space = 0;
}

static uint32_t current_color(const layout_t *l) {
  if (l->link >= 0) return COLOR_LINK;
  if (l->in_code) return COLOR_CODE;
  if (l->in_quote) return COLOR_QUOTE;
  return COLOR_TEXT;
}

/* Ajoute du texte (deja decode) au flux courant. */
static void add_text(layout_t *l, const char *text, size_t n) {
  char word[256];
  size_t wl = 0;
  uint32_t color = current_color(l);
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)text[i];
    if (l->pre) {
      if (c == '\n') {
        if (wl) { emit_word(l, word, wl, color); wl = 0; }
        l->line_words = l->line_words ? l->line_words : 1;
        flush_line(l);
        continue;
      }
      if (c == '\r') continue;
      if (c == ' ' || c == '\t') {
        if (wl) { emit_word(l, word, wl, color); wl = 0; }
        l->x += CHAR_W * (c == '\t' ? 4 : 1);
        l->pending_space = 0;
        continue;
      }
    } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (wl) { emit_word(l, word, wl, color); wl = 0; }
      l->pending_space = 1;
      continue;
    }
    if (wl < sizeof(word) - 1) word[wl++] = (char)((c >= 32 && c < 127) ? c : '?');
    else { emit_word(l, word, wl, color); wl = 0; word[wl++] = (char)c; }
  }
  if (wl) emit_word(l, word, wl, color);
}

/* Decode les entites puis ajoute le texte. UTF-8 multi-octets -> un seul '?'. */
static void add_decoded(layout_t *l, const char *s, size_t n) {
  char *buf = malloc(n + 1);
  if (buf == NULL) { l->oom = 1; return; }
  size_t o = 0;
  for (size_t i = 0; i < n;) {
    unsigned char c = (unsigned char)s[i];
    if (c == '&') {
      size_t j = i + 1;
      while (j < n && j - i < 10 && s[j] != ';' && s[j] != ' ') j++;
      if (j < n && s[j] == ';') {
        char name[12];
        size_t nl = j - i - 1;
        memcpy(name, s + i + 1, nl); name[nl] = '\0';
        int out = -1;
        if (!strcmp(name, "amp")) out = '&';
        else if (!strcmp(name, "lt")) out = '<';
        else if (!strcmp(name, "gt")) out = '>';
        else if (!strcmp(name, "quot")) out = '"';
        else if (!strcmp(name, "apos")) out = '\'';
        else if (!strcmp(name, "nbsp")) out = ' ';
        else if (name[0] == '#') {
          long v = (name[1] == 'x' || name[1] == 'X') ? strtol(name + 2, NULL, 16)
                                                      : strtol(name + 1, NULL, 10);
          out = (v >= 32 && v < 127) ? (int)v : '?';
        } else out = '?';
        buf[o++] = (char)out;
        i = j + 1;
        continue;
      }
    }
    if (c >= 0xC0) { buf[o++] = '?'; i++; continue; }
    if (c >= 0x80) { i++; continue; }
    buf[o++] = (char)c;
    i++;
  }
  add_text(l, buf, o);
  free(buf);
}

static int add_link(layout_t *l, const char *href, size_t n) {
  web_page_t *p = l->page;
  if (p->link_count == p->link_capacity) {
    size_t cap = p->link_capacity ? p->link_capacity * 2 : 32;
    char **nl = realloc(p->links, cap * sizeof(*nl));
    if (nl == NULL) { l->oom = 1; return -1; }
    p->links = nl; p->link_capacity = cap;
  }
  char raw[WEB_URL_MAX], resolved[WEB_URL_MAX];
  if (n >= sizeof(raw)) n = sizeof(raw) - 1;
  memcpy(raw, href, n); raw[n] = '\0';
  web_url_resolve(l->base, raw, resolved, sizeof(resolved));
  char *copy = malloc(strlen(resolved) + 1);
  if (copy == NULL) { l->oom = 1; return -1; }
  strcpy(copy, resolved);
  p->links[p->link_count] = copy;
  return (int)p->link_count++;
}

/* Valeur d'un attribut dans la chaine de balise ; renvoie 1 si trouve. */
static int get_attr(const char *tag, size_t len, const char *name,
                    const char **value, size_t *vlen) {
  size_t nl = strlen(name);
  for (size_t i = 0; i + nl < len; i++) {
    int boundary = i == 0 || tag[i - 1] == ' ' || tag[i - 1] == '\t' || tag[i - 1] == '\n';
    if (!boundary || strncasecmp(tag + i, name, nl) != 0) continue;
    size_t j = i + nl;
    while (j < len && tag[j] == ' ') j++;
    if (j >= len || tag[j] != '=') continue;
    j++;
    while (j < len && tag[j] == ' ') j++;
    if (j < len && (tag[j] == '"' || tag[j] == '\'')) {
      char q = tag[j++];
      size_t s = j;
      while (j < len && tag[j] != q) j++;
      *value = tag + s; *vlen = j - s;
    } else {
      size_t s = j;
      while (j < len && tag[j] != ' ' && tag[j] != '\t' && tag[j] != '\n') j++;
      *value = tag + s; *vlen = j - s;
    }
    return 1;
  }
  return 0;
}

static void rule(layout_t *l, int x, int w, int h, uint32_t color) {
  web_item_t *it = add_item(l, WEB_ITEM_RECT);
  if (it == NULL) return;
  it->x = x; it->y = l->y; it->w = w; it->h = h; it->color = color;
}

static void handle_tag(layout_t *l, const char *tag, size_t len, int closing) {
  char name[16];
  size_t nl = 0;
  while (nl < len && nl < sizeof(name) - 1 && tag[nl] != ' ' && tag[nl] != '\t' &&
         tag[nl] != '\n' && tag[nl] != '/')
    { char c = tag[nl]; name[nl++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); }
  name[nl] = '\0';
  int hn = (nl == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') ? name[1] - '0' : 0;

  if (hn) {
    block_break(l, closing ? 6 : 10);
    l->heading_scale = closing ? 0 : (hn == 1 ? 2 : (hn == 2 ? 2 : 1));
    l->bold = !closing && hn >= 3;
    if (!closing && hn == 2) l->heading_scale = 2;
    if (closing) l->bold = 0;
  } else if (!strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "section") ||
             !strcmp(name, "article") || !strcmp(name, "header") || !strcmp(name, "footer") ||
             !strcmp(name, "nav") || !strcmp(name, "main") || !strcmp(name, "form") ||
             !strcmp(name, "table") || !strcmp(name, "tr") || !strcmp(name, "dl") ||
             !strcmp(name, "dt") || !strcmp(name, "dd") || !strcmp(name, "figure")) {
    block_break(l, !strcmp(name, "p") ? 8 : 0);
  } else if (!strcmp(name, "br")) {
    if (l->line_words == 0) l->line_words = 1;
    flush_line(l);
  } else if (!strcmp(name, "hr")) {
    block_break(l, 4);
    rule(l, MARGIN, l->width - 2 * MARGIN, 1, COLOR_RULE);
    l->y += 9;
  } else if (!strcmp(name, "b") || !strcmp(name, "strong") || !strcmp(name, "th")) {
    l->bold = !closing;
    if (!strcmp(name, "th")) l->pending_space = 1;
  } else if (!strcmp(name, "td")) {
    l->pending_space = 1;
    l->x += closing ? 0 : 2 * CHAR_W;
  } else if (!strcmp(name, "code") || !strcmp(name, "tt") || !strcmp(name, "kbd")) {
    l->in_code = !closing;
  } else if (!strcmp(name, "pre")) {
    block_break(l, 6);
    l->pre = !closing;
  } else if (!strcmp(name, "blockquote")) {
    block_break(l, 6);
    l->indent += closing ? -24 : 24;
    if (l->indent < 0) l->indent = 0;
    l->in_quote = !closing;
    l->x = l->indent + MARGIN;
  } else if (!strcmp(name, "ul") || !strcmp(name, "ol")) {
    block_break(l, closing ? 4 : 2);
    if (closing) {
      if (l->list_depth > 0) l->list_depth--;
      l->indent -= 24;
      if (l->indent < 0) l->indent = 0;
    } else if (l->list_depth < MAX_DEPTH - 1) {
      l->list_depth++;
      l->list_ordered[l->list_depth] = name[0] == 'o';
      l->list_counter[l->list_depth] = 0;
      l->indent += 24;
    }
    l->x = l->indent + MARGIN;
  } else if (!strcmp(name, "li") && !closing) {
    block_break(l, 0);
    char marker[16];
    if (l->list_depth > 0 && l->list_ordered[l->list_depth]) {
      int v = ++l->list_counter[l->list_depth], i = 0;
      char rev[8];
      while (v > 0 && i < 7) { rev[i++] = (char)('0' + v % 10); v /= 10; }
      size_t o = 0;
      while (i > 0) marker[o++] = rev[--i];
      marker[o++] = '.'; marker[o] = '\0';
    } else {
      strcpy(marker, "*");
    }
    int saved = l->indent;
    l->indent -= 20; if (l->indent < 0) l->indent = 0;
    l->x = l->indent + MARGIN;
    emit_word(l, marker, strlen(marker), COLOR_TEXT);
    l->indent = saved;
    l->x = saved + MARGIN;
    l->pending_space = 0;
  } else if (!strcmp(name, "a")) {
    if (closing) l->link = -1;
    else {
      const char *v; size_t vl;
      l->link = get_attr(tag, len, "href", &v, &vl) ? add_link(l, v, vl) : -1;
    }
  } else if (!strcmp(name, "img") && !closing) {
    const char *v; size_t vl;
    char alt[80] = "[image]";
    if (get_attr(tag, len, "alt", &v, &vl) && vl > 0 && vl < sizeof(alt) - 3) {
      alt[0] = '['; memcpy(alt + 1, v, vl); alt[vl + 1] = ']'; alt[vl + 2] = '\0';
    }
    add_decoded(l, alt, strlen(alt));
  }
}

int web_layout(const char *html, size_t length, int width, const char *base_url,
               web_page_t *page) {
  layout_t l;
  memset(page, 0, sizeof(*page));
  memset(&l, 0, sizeof(l));
  l.page = page; l.base = base_url ? base_url : ""; l.width = width < 80 ? 80 : width;
  l.y = MARGIN; l.x = MARGIN; l.line_h = CHAR_H; l.link = -1;
  int in_title = 0, skip = 0;
  char title[sizeof(page->title)];
  size_t title_len = 0;
  size_t i = 0;
  while (i < length && !l.oom) {
    if (html[i] == '<') {
      if (length - i >= 4 && memcmp(html + i, "<!--", 4) == 0) {
        const char *end = NULL;
        for (size_t k = i + 4; k + 3 <= length; k++)
          if (memcmp(html + k, "-->", 3) == 0) { end = html + k + 3; break; }
        i = end ? (size_t)(end - html) : length;
        continue;
      }
      size_t j = i + 1;
      char quote = 0;
      while (j < length && (quote || html[j] != '>')) {
        if (quote) { if (html[j] == quote) quote = 0; }
        else if (html[j] == '"' || html[j] == '\'') quote = html[j];
        j++;
      }
      const char *tag = html + i + 1;
      size_t tl = j - i - 1;
      i = j < length ? j + 1 : length;
      int closing = 0;
      if (tl > 0 && tag[0] == '/') { closing = 1; tag++; tl--; }
      if (tl == 0 || tag[0] == '!' || tag[0] == '?') continue;
      char nm[10]; size_t nn = 0;
      while (nn < tl && nn < sizeof(nm) - 1 && tag[nn] != ' ' && tag[nn] != '>' &&
             tag[nn] != '/' && tag[nn] != '\n' && tag[nn] != '\t')
        { char c = tag[nn]; nm[nn++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); }
      nm[nn] = '\0';
      if (!strcmp(nm, "script") || !strcmp(nm, "style")) {
        if (!closing) {
          skip = 1;
          /* saute jusqu'a la balise fermante correspondante */
          for (size_t k = i; k + 2 + nn < length; k++) {
            if (html[k] == '<' && html[k + 1] == '/' &&
                strncasecmp(html + k + 2, nm, nn) == 0) { i = k; break; }
            if (k + 3 + nn >= length) i = length;
          }
        } else skip = 0;
        continue;
      }
      if (!strcmp(nm, "title")) { in_title = !closing; continue; }
      if (!strcmp(nm, "head")) { continue; }
      if (!skip) handle_tag(&l, tag, tl, closing);
      continue;
    }
    size_t j = i;
    while (j < length && html[j] != '<') j++;
    if (in_title) {
      for (size_t k = i; k < j && title_len < sizeof(title) - 1; k++) {
        unsigned char c = (unsigned char)html[k];
        title[title_len++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : (c >= 32 && c < 127 ? (char)c : '?');
      }
    } else if (!skip) {
      add_decoded(&l, html + i, j - i);
    }
    i = j;
  }
  flush_line(&l);
  page->height = l.y + MARGIN;
  title[title_len] = '\0';
  memcpy(page->title, title, title_len + 1);
  if (l.oom) { web_page_free(page); return -1; }
  return 0;
}

void web_page_free(web_page_t *page) {
  for (size_t i = 0; i < page->count; i++) free(page->items[i].text);
  for (size_t i = 0; i < page->link_count; i++) free(page->links[i]);
  free(page->items);
  free(page->links);
  memset(page, 0, sizeof(*page));
}

int web_page_link_at(const web_page_t *page, int x, int y) {
  for (size_t i = 0; i < page->count; i++) {
    const web_item_t *it = &page->items[i];
    if (it->type == WEB_ITEM_TEXT && it->link >= 0 && x >= it->x && y >= it->y &&
        x < it->x + it->w && y < it->y + it->h)
      return it->link;
  }
  return -1;
}
