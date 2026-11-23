/* browser/main.c - ALOS Browser : fenetre GUI autour du moteur alosweb */
#include <gui.h>
#include "web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOOLBAR_H 36
#define STATUS_H 20
#define SCROLLBAR_W 12
#define HISTORY_MAX 32
#define KEY_ENTER_SC 0x1C
#define KEY_UP_SC 0x48
#define KEY_PGUP_SC 0x49
#define KEY_DOWN_SC 0x50
#define KEY_PGDN_SC 0x51

static gui_window_t *window;
static web_page_t page;
static int have_page;
static int scroll_y;
static char current_url[WEB_URL_MAX] = "about:home";
static char address[WEB_URL_MAX];
static int address_focus;
static char status[160] = "";
static char history[HISTORY_MAX][WEB_URL_MAX];
static int history_count;
static char title[128] = "";
static char *source;   /* HTML de la page courante, pour re-mise en page au redimensionnement */
static size_t source_len;

static void keep_source(const char *body, size_t len) {
  char *copy = malloc(len + 1);
  if (copy == NULL) return;
  memcpy(copy, body, len);
  copy[len] = '\0';
  free(source);
  source = copy;
  source_len = len;
}

static int view_w(void) { return (int)gui_window_width(window) - SCROLLBAR_W; }
static int view_h(void) { return (int)gui_window_height(window) - TOOLBAR_H - STATUS_H; }

static int max_scroll(void) {
  int m = have_page ? page.height - view_h() : 0;
  return m < 0 ? 0 : m;
}

static void clamp_scroll(void) {
  if (scroll_y > max_scroll()) scroll_y = max_scroll();
  if (scroll_y < 0) scroll_y = 0;
}

static void fill(int x, int y, int w, int h, uint32_t c) {
  gui_fill_rect(window, (gui_rect_t){x, y, (uint32_t)(w < 0 ? 0 : w), (uint32_t)(h < 0 ? 0 : h)}, c);
}

static void text(int x, int y, const char *s, uint32_t c) {
  web_draw_text(gui_window_pixels(window), gui_window_stride(window),
                (int)gui_window_width(window), (int)gui_window_height(window), x, y, s, c);
}

static void redraw(void) {
  int w = (int)gui_window_width(window), h = (int)gui_window_height(window);
  uint32_t *px = gui_window_pixels(window);
  uint32_t stride = gui_window_stride(window);
  /* contenu : rendu directement dans la zone sous la barre d'outils */
  if (have_page) {
    web_render(&page, px + (size_t)TOOLBAR_H * stride, stride, view_w(), view_h(),
               scroll_y, 0xFFFFFFFFu);
  } else {
    fill(0, TOOLBAR_H, view_w(), view_h(), 0xFFFFFFFFu);
  }
  /* barre de defilement */
  fill(w - SCROLLBAR_W, TOOLBAR_H, SCROLLBAR_W, view_h(), 0xFFE8EAED);
  if (have_page && page.height > view_h()) {
    int th = view_h() * view_h() / page.height;
    if (th < 16) th = 16;
    int ty = TOOLBAR_H + (view_h() - th) * scroll_y / max_scroll();
    fill(w - SCROLLBAR_W + 2, ty, SCROLLBAR_W - 4, th, 0xFF9AA0A6);
  }
  /* barre d'outils */
  fill(0, 0, w, TOOLBAR_H, 0xFFDEE1E6);
  fill(8, 6, 28, 24, history_count > 1 ? 0xFFFFFFFF : 0xFFC8CBD0);
  gui_draw_rect(window, (gui_rect_t){8, 6, 28, 24}, 0xFF80868B);
  text(16, 10, "<", 0xFF202124);
  fill(44, 6, 28, 24, 0xFFFFFFFF);
  gui_draw_rect(window, (gui_rect_t){44, 6, 28, 24}, 0xFF80868B);
  text(52, 10, "R", 0xFF202124);
  int ax = 80, aw = w - ax - 8;
  fill(ax, 6, aw, 24, 0xFFFFFFFF);
  gui_draw_rect(window, (gui_rect_t){ax, 6, (uint32_t)aw, 24},
                address_focus ? 0xFF1A73E8 : 0xFF80868B);
  const char *shown = address_focus ? address : current_url;
  size_t chars = (size_t)(aw - 12) / 8;
  size_t len = strlen(shown);
  const char *start = len > chars ? shown + (len - chars) : shown;
  text(ax + 6, 10, start, 0xFF202124);
  if (address_focus) fill(ax + 6 + (int)strlen(start) * 8, 9, 2, 18, 0xFF202124);
  /* barre d'etat */
  fill(0, h - STATUS_H, w, STATUS_H, 0xFFF1F3F4);
  const char *st = status[0] ? status : title;
  size_t sc = (size_t)(w - 12) / 8;
  char line[160];
  snprintf(line, sizeof(line), "%.*s", (int)(sc < sizeof(line) - 1 ? sc : sizeof(line) - 1), st);
  text(6, h - STATUS_H + 2, line, 0xFF5F6368);
  (void)gui_present(window, NULL);
}

static void set_status(const char *s) {
  snprintf(status, sizeof(status), "%s", s);
}

static int load(const char *url, int push_history) {
  set_status("Chargement...");
  snprintf(address, sizeof(address), "%s", url);
  redraw();
  web_response_t r;
  web_page_t fresh;
  int ok = web_fetch(url, &r) == 0;
  char error_html[512];
  const char *body;
  size_t body_len;
  if (ok) {
    body = r.body; body_len = r.length;
  } else {
    int n = snprintf(error_html, sizeof(error_html),
                     "<html><head><title>Erreur</title></head><body><h1>Impossible de charger la page</h1>"
                     "<p>%.200s</p><p><a href=\"about:home\">Retour a l'accueil</a></p></body></html>",
                     r.error);
    body = error_html; body_len = (size_t)n;
  }
  const char *base = ok ? r.final_url : url;
  if (web_layout(body, body_len, view_w(), base, &fresh) != 0) {
    set_status("Memoire insuffisante");
    if (ok) web_response_free(&r);
    return -1;
  }
  keep_source(body, body_len);
  if (have_page) web_page_free(&page);
  page = fresh; have_page = 1; scroll_y = 0;
  snprintf(current_url, sizeof(current_url), "%s", base);
  snprintf(title, sizeof(title), "%s", page.title[0] ? page.title : current_url);
  char caption[160];
  snprintf(caption, sizeof(caption), "%s - ALOS Browser", title);
  gui_set_title(window, caption);
  if (ok) {
    char buf[96];
    snprintf(buf, sizeof(buf), "HTTP %d, %u octets", r.status, (unsigned)r.length);
    set_status(r.status == 200 || strncmp(base, "http", 4) != 0 ? "" : buf);
    web_response_free(&r);
  } else set_status("Echec du chargement");
  if (push_history) {
    if (history_count == HISTORY_MAX) {
      memmove(history[0], history[1], sizeof(history[0]) * (HISTORY_MAX - 1));
      history_count--;
    }
    snprintf(history[history_count++], WEB_URL_MAX, "%s", current_url);
  }
  address_focus = 0;
  return 0;
}

static void go_back(void) {
  if (history_count < 2) return;
  history_count--;
  char prev[WEB_URL_MAX];
  snprintf(prev, sizeof(prev), "%s", history[history_count - 1]);
  history_count--; /* load() re-empile la page */
  load(prev, 1);
}

static void navigate_input(void) {
  char url[WEB_URL_MAX];
  if (strstr(address, "://") == NULL && strncmp(address, "about:", 6) != 0) {
    snprintf(url, sizeof(url), "http://%s", address);
  } else {
    snprintf(url, sizeof(url), "%s", address);
  }
  load(url, 1);
}

int main(int argc, char **argv) {
  window = gui_create_window("ALOS Browser", 800, 600,
                             GUI_WINDOW_RESIZABLE | GUI_WINDOW_MINIMIZABLE | GUI_WINDOW_MAXIMIZABLE);
  if (window == NULL) return 1;
  load(argc > 1 ? argv[1] : "about:home", 1);
  redraw();
  for (;;) {
    gui_event_t e;
    int rc = gui_wait_event(window, &e, 200);
    if (rc < 0) break;
    if (rc == 0) continue;
    switch (e.type) {
    case GUI_EVENT_CLOSE:
      gui_destroy_window(window);
      return 0;
    case GUI_EVENT_RESIZE:
      /* Nouvelle largeur : re-mise en page locale, sans refaire de requete. */
      if (have_page && source != NULL) {
        web_page_t fresh;
        if (web_layout(source, source_len, view_w(), current_url, &fresh) == 0) {
          web_page_free(&page);
          page = fresh;
        }
      }
      clamp_scroll();
      redraw();
      break;
    case GUI_EVENT_PAINT:
    case GUI_EVENT_FOCUS:
      redraw();
      break;
    case GUI_EVENT_MOUSE_DOWN:
      if (e.y < TOOLBAR_H) {
        if (e.x >= 8 && e.x < 36) { go_back(); }
        else if (e.x >= 44 && e.x < 72) {
          int saved = history_count;
          char url[WEB_URL_MAX];
          snprintf(url, sizeof(url), "%s", current_url);
          load(url, 0);
          history_count = saved;
        } else if (e.x >= 80) {
          address_focus = 1;
          snprintf(address, sizeof(address), "%s", current_url);
        }
      } else if (e.x >= (int)gui_window_width(window) - SCROLLBAR_W && max_scroll() > 0) {
        scroll_y = (e.y - TOOLBAR_H) * max_scroll() / view_h();
        clamp_scroll();
      } else if (have_page && e.y < TOOLBAR_H + view_h()) {
        address_focus = 0;
        int link = web_page_link_at(&page, e.x, e.y - TOOLBAR_H + scroll_y);
        if (link >= 0) {
          char url[WEB_URL_MAX];
          snprintf(url, sizeof(url), "%s", page.links[link]);
          load(url, 1);
        }
      }
      redraw();
      break;
    case GUI_EVENT_MOUSE_MOVE:
      if (have_page && e.y >= TOOLBAR_H && e.y < TOOLBAR_H + view_h()) {
        int link = web_page_link_at(&page, e.x, e.y - TOOLBAR_H + scroll_y);
        const char *next = link >= 0 ? page.links[link] : "";
        if (strcmp(status, next) != 0 && (link >= 0 || strncmp(status, "http", 4) == 0 ||
                                           strncmp(status, "file:", 5) == 0 ||
                                           strncmp(status, "about:", 6) == 0)) {
          set_status(next);
          redraw();
        }
      }
      break;
    case GUI_EVENT_KEY_DOWN:
      if (address_focus) {
        size_t len = strlen(address);
        if (e.scancode == KEY_ENTER_SC || e.key == '\n' || e.key == '\r') navigate_input();
        else if (e.key == 27) address_focus = 0;
        else if (e.key == '\b' || e.key == 127) { if (len) address[len - 1] = '\0'; }
        else if (e.key >= 32 && e.key < 127 && len < sizeof(address) - 1) {
          address[len] = (char)e.key; address[len + 1] = '\0';
        }
      } else if (e.scancode == KEY_DOWN_SC) scroll_y += 32;
      else if (e.scancode == KEY_UP_SC) scroll_y -= 32;
      else if (e.scancode == KEY_PGDN_SC || e.key == ' ') scroll_y += view_h() - 32;
      else if (e.scancode == KEY_PGUP_SC) scroll_y -= view_h() - 32;
      else if (e.key == '\b') go_back();
      else if (e.key == 'l' || e.key == '/') { address_focus = 1; snprintf(address, sizeof(address), "%s", current_url); }
      clamp_scroll();
      redraw();
      break;
    default:
      break;
    }
  }
  return 0;
}
