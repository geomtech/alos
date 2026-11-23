/* browser/url.c - Analyse et resolution d'URL */
#include "web.h"
#include <stdlib.h>
#include <string.h>

static void copy_bounded(char *dst, size_t size, const char *src, size_t n) {
  if (size == 0) return;
  if (n >= size) n = size - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

int web_url_parse(const char *text, web_url_t *url) {
  memset(url, 0, sizeof(*url));
  const char *colon = strchr(text, ':');
  if (colon == NULL || colon == text || colon - text >= (long)sizeof(url->scheme))
    return -1;
  for (const char *p = text; p < colon; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+')) return -1;
    url->scheme[p - text] = c;
  }
  const char *rest = colon + 1;
  if (strcmp(url->scheme, "about") == 0) {
    copy_bounded(url->path, sizeof(url->path), rest, strlen(rest));
    return 0;
  }
  if (rest[0] != '/' || rest[1] != '/') return -1;
  rest += 2;
  if (strcmp(url->scheme, "file") == 0) {
    copy_bounded(url->path, sizeof(url->path), rest, strlen(rest));
    if (url->path[0] == '\0') strcpy(url->path, "/");
    return 0;
  }
  if (strcmp(url->scheme, "http") != 0 && strcmp(url->scheme, "https") != 0)
    return -1;
  const char *end = rest + strcspn(rest, "/?#");
  const char *hostend = end;
  const char *at = memchr(rest, '@', (size_t)(end - rest));
  if (at != NULL) rest = at + 1;
  const char *port = memchr(rest, ':', (size_t)(hostend - rest));
  url->port = strcmp(url->scheme, "https") == 0 ? 443 : 80;
  if (port != NULL) {
    int value = 0;
    for (const char *p = port + 1; p < hostend; p++) {
      if (*p < '0' || *p > '9') return -1;
      value = value * 10 + (*p - '0');
      if (value > 65535) return -1;
    }
    if (value > 0) url->port = value;
    hostend = port;
  }
  if (hostend == rest) return -1;
  copy_bounded(url->host, sizeof(url->host), rest, (size_t)(hostend - rest));
  if (*end == '\0') {
    strcpy(url->path, "/");
  } else if (*end == '/') {
    const char *frag = strchr(end, '#');
    copy_bounded(url->path, sizeof(url->path), end,
                 frag ? (size_t)(frag - end) : strlen(end));
  } else {
    const char *frag = strchr(end, '#');
    url->path[0] = '/';
    copy_bounded(url->path + 1, sizeof(url->path) - 1, end,
                 frag ? (size_t)(frag - end) : strlen(end));
  }
  return 0;
}

void web_url_resolve(const char *base, const char *ref, char *out, size_t size) {
  web_url_t b;
  out[0] = '\0';
  if (size == 0) return;
  while (*ref == ' ') ref++;
  {
    /* schema explicite (avant tout '/', '?' ou '#') : URL absolue ou
     * non supportee (mailto:, javascript:...), renvoyee telle quelle. */
    const char *colon = strchr(ref, ':');
    const char *stop = strpbrk(ref, "/?#");
    if (colon != NULL && colon != ref && (stop == NULL || colon < stop)) {
      copy_bounded(out, size, ref, strlen(ref));
      return;
    }
  }
  if (web_url_parse(base, &b) != 0) {
    copy_bounded(out, size, ref, strlen(ref));
    return;
  }
  char prefix[WEB_URL_MAX];
  if (strcmp(b.scheme, "file") == 0) {
    strcpy(prefix, "file://");
  } else if (strcmp(b.scheme, "about") == 0) {
    copy_bounded(out, size, ref, strlen(ref));
    return;
  } else {
    int dflt = strcmp(b.scheme, "https") == 0 ? 443 : 80;
    size_t n = 0;
    n += strlen(b.scheme);
    memcpy(prefix, b.scheme, n);
    memcpy(prefix + n, "://", 3);
    n += 3;
    size_t hl = strlen(b.host);
    memcpy(prefix + n, b.host, hl);
    n += hl;
    if (b.port != dflt) {
      char tmp[8];
      int v = b.port, i = 0;
      char rev[8];
      while (v > 0) { rev[i++] = (char)('0' + v % 10); v /= 10; }
      int j = 0;
      tmp[j++] = ':';
      while (i > 0) tmp[j++] = rev[--i];
      memcpy(prefix + n, tmp, (size_t)j);
      n += (size_t)j;
    }
    prefix[n] = '\0';
  }
  char path[WEB_URL_MAX];
  if (ref[0] == '#' || ref[0] == '\0') {
    copy_bounded(path, sizeof(path), b.path, strlen(b.path));
  } else if (ref[0] == '/' && ref[1] == '/') {
    char tmp[WEB_URL_MAX];
    size_t sl = strlen(b.scheme);
    memcpy(tmp, b.scheme, sl);
    tmp[sl] = ':';
    copy_bounded(tmp + sl + 1, sizeof(tmp) - sl - 1, ref, strlen(ref));
    copy_bounded(out, size, tmp, strlen(tmp));
    return;
  } else if (ref[0] == '/') {
    copy_bounded(path, sizeof(path), ref, strlen(ref));
  } else {
    /* relatif : repertoire de base + ref, puis normalisation de ./ et ../ */
    char joined[2 * WEB_URL_MAX];
    const char *slash = strrchr(b.path, '/');
    size_t dir = slash ? (size_t)(slash - b.path) + 1 : 0;
    if (dir > WEB_URL_MAX - 2) dir = WEB_URL_MAX - 2;
    memcpy(joined, b.path, dir);
    size_t rl = strlen(ref);
    if (rl > WEB_URL_MAX - 2) rl = WEB_URL_MAX - 2;
    memcpy(joined + dir, ref, rl);
    joined[dir + rl] = '\0';
    char *w = path;
    const char *r = joined;
    const char *limit = path + sizeof(path) - 1;
    while (*r && w < limit) {
      if (r[0] == '/' && r[1] == '.' && (r[2] == '/' || r[2] == '\0')) {
        r += 2;
        if (*r == '\0' && w < limit) *w++ = '/';
        continue;
      }
      if (r[0] == '/' && r[1] == '.' && r[2] == '.' &&
          (r[3] == '/' || r[3] == '\0')) {
        if (w > path) {
          w--;
          while (w > path && *w != '/') w--;
        }
        r += 3;
        if (*r == '\0' && w < limit) *w++ = '/';
        continue;
      }
      *w++ = *r++;
    }
    *w = '\0';
    if (path[0] == '\0') strcpy(path, "/");
  }
  char *frag = strchr(path, '#');
  if (frag) *frag = '\0';
  size_t pl = strlen(prefix);
  if (pl >= size) pl = size - 1;
  memcpy(out, prefix, pl);
  copy_bounded(out + pl, size - pl, path, strlen(path));
}
