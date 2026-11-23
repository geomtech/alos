/* browser/http.c - Client HTTP/1.1 minimal (sockets BSD) + file: / about: */
#include "web.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_REDIRECTS 5

static const char about_home[] =
    "<html><head><title>ALOS Browser</title></head><body>"
    "<h1>ALOS Browser</h1>"
    "<p>Premier navigateur web d'ALOS : moteur <b>alosweb</b> (client HTTP, "
    "mise en page HTML minimale, rendu logiciel). Etape intermediaire "
    "avant le portage de Chromium ; voir <code>docs/browser.md</code>.</p>"
    "<h2>Pages locales</h2><ul>"
    "<li><a href=\"file:///www/index.html\">/www/index.html</a> "
    "(page servie par httpd)</li>"
    "<li><a href=\"about:test\">about:test</a> (page de test de rendu)</li>"
    "</ul>"
    "<h2>Reseau</h2><ul>"
    "<li><a href=\"http://example.com/\">http://example.com/</a></li>"
    "<li><a href=\"http://10.0.2.2:8000/\">http://10.0.2.2:8000/</a> "
    "(serveur de l'hote sous QEMU)</li></ul>"
    "<p>HTTPS n'est pas encore supporte (TLS a porter). "
    "Tapez une URL dans la barre d'adresse puis Entree ; "
    "Retour arriere = page precedente ; fleches/PageUp/PageDown = defilement.</p>"
    "</body></html>";

static const char about_test[] =
    "<html><head><title>Test de rendu</title></head><body>"
    "<h1>Titre h1</h1><h2>Titre h2</h2><h3>Titre h3</h3>"
    "<p>Un paragraphe avec du <b>gras</b>, du <i>texte</i>, du <code>code</code> "
    "et un <a href=\"about:home\">lien vers l'accueil</a>. Ce texte est assez "
    "long pour verifier le retour a la ligne automatique de la mise en page "
    "dans une fenetre de largeur variable.</p>"
    "<hr><ul><li>Premier element</li><li>Deuxieme element<ul><li>Imbrique</li>"
    "</ul></li></ul><ol><li>Un</li><li>Deux</li></ol>"
    "<blockquote>Une citation en retrait.</blockquote>"
    "<pre>bloc  preformate\n  ligne 2</pre>"
    "<p>Entites : &lt;tag&gt; &amp; &quot;guillemets&quot; &#65;&#x42;</p>"
    "<table><tr><td>A1</td><td>B1</td></tr><tr><td>A2</td><td>B2</td></tr></table>"
    "</body></html>";

static char *dup_n(const char *s, size_t n) {
  char *r = malloc(n + 1);
  if (r == NULL) return NULL;
  memcpy(r, s, n);
  r[n] = '\0';
  return r;
}

static void set_error(web_response_t *r, const char *msg) {
  snprintf(r->error, sizeof(r->error), "%s", msg);
}

void web_response_free(web_response_t *response) {
  free(response->body);
  response->body = NULL;
  response->length = 0;
}

size_t web_dechunk(char *data, size_t length) {
  size_t in = 0, out = 0;
  while (in < length) {
    size_t size = 0;
    int digits = 0;
    while (in < length) {
      char c = data[in];
      int v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else break;
      size = size * 16 + (size_t)v;
      digits++;
      in++;
    }
    if (digits == 0) break;
    while (in < length && data[in] != '\n') in++; /* extensions de chunk */
    if (in < length) in++;
    if (size == 0) break;
    if (size > length - in) size = length - in;
    memmove(data + out, data + in, size);
    out += size;
    in += size;
    if (in < length && data[in] == '\r') in++;
    if (in < length && data[in] == '\n') in++;
  }
  return out;
}

static int fetch_file(const web_url_t *url, web_response_t *r) {
  int fd = open(url->path, O_RDONLY);
  if (fd < 0) { set_error(r, "Fichier introuvable"); return -1; }
  size_t cap = 4096, len = 0;
  char *buf = malloc(cap + 1);
  if (buf == NULL) { close(fd); set_error(r, "Memoire insuffisante"); return -1; }
  for (;;) {
    if (len == cap) {
      if (cap >= WEB_MAX_BODY) break;
      char *n = realloc(buf, cap * 2 + 1);
      if (n == NULL) break;
      buf = n; cap *= 2;
    }
    ssize_t got = read(fd, buf + len, cap - len);
    if (got <= 0) break;
    len += (size_t)got;
  }
  close(fd);
  buf[len] = '\0';
  r->body = buf; r->length = len; r->status = 200;
  return 0;
}

static int connect_host(const web_url_t *url, char *err, size_t errsize) {
  struct addrinfo hints, *res = NULL;
  char port[8];
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port, sizeof(port), "%d", url->port);
  if (getaddrinfo(url->host, port, &hints, &res) != 0 || res == NULL) {
    snprintf(err, errsize, "Resolution DNS impossible : %s", url->host);
    return -1;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    snprintf(err, errsize, "Creation de socket impossible");
    return -1;
  }
  if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    close(fd);
    freeaddrinfo(res);
    snprintf(err, errsize, "Connexion refusee : %s:%d", url->host, url->port);
    return -1;
  }
  freeaddrinfo(res);
  return fd;
}

static int send_all(int fd, const char *data, size_t length) {
  while (length > 0) {
    ssize_t n = send(fd, data, length, 0);
    if (n <= 0) return -1;
    data += n; length -= (size_t)n;
  }
  return 0;
}

/* Une requete GET. location = URL de redirection eventuelle (taille WEB_URL_MAX). */
static int http_once(const web_url_t *url, web_response_t *r, char *location) {
  location[0] = '\0';
  if (strcmp(url->scheme, "https") == 0) {
    set_error(r, "HTTPS non supporte : TLS n'est pas encore porte sur ALOS");
    return -1;
  }
  int fd = connect_host(url, r->error, sizeof(r->error));
  if (fd < 0) return -1;
  char request[WEB_URL_MAX + 512];
  int n = snprintf(request, sizeof(request),
                   "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: ALOS-Browser/0.1\r\n"
                   "Accept: text/html,*/*;q=0.5\r\nAccept-Encoding: identity\r\n"
                   "Connection: close\r\n\r\n",
                   url->path, url->host);
  if (n <= 0 || (size_t)n >= sizeof(request) || send_all(fd, request, (size_t)n)) {
    close(fd);
    set_error(r, "Echec d'envoi de la requete");
    return -1;
  }
  size_t cap = 8192, len = 0;
  char *buf = malloc(cap + 1);
  if (buf == NULL) { close(fd); set_error(r, "Memoire insuffisante"); return -1; }
  for (;;) {
    if (len == cap) {
      if (cap >= WEB_MAX_BODY) break;
      char *grown = realloc(buf, cap * 2 + 1);
      if (grown == NULL) break;
      buf = grown; cap *= 2;
    }
    ssize_t got = recv(fd, buf + len, cap - len, 0);
    if (got <= 0) break;
    len += (size_t)got;
  }
  close(fd);
  buf[len] = '\0';
  char *sep = strstr(buf, "\r\n\r\n");
  if (len < 12 || strncmp(buf, "HTTP/", 5) != 0 || sep == NULL) {
    free(buf);
    set_error(r, "Reponse HTTP invalide ou vide");
    return -1;
  }
  r->status = atoi(buf + (buf[7] == ' ' ? 8 : 9));
  int chunked = 0;
  char *line = strstr(buf, "\r\n") + 2; /* saute la ligne de statut */
  while (line <= sep) {
    char *eol = strstr(line, "\r\n");
    if (eol == NULL || eol == line) break;
    if (strncasecmp(line, "transfer-encoding:", 18) == 0) {
      for (char *c = line + 18; c + 7 <= eol; c++)
        if (strncasecmp(c, "chunked", 7) == 0) { chunked = 1; break; }
    } else if (strncasecmp(line, "location:", 9) == 0) {
      const char *v = line + 9;
      while (*v == ' ') v++;
      size_t l = (size_t)(eol - v);
      if (l >= WEB_URL_MAX) l = WEB_URL_MAX - 1;
      memcpy(location, v, l);
      location[l] = '\0';
    }
    line = eol + 2;
  }
  size_t offset = (size_t)(sep + 4 - buf);
  size_t body_len = len - offset;
  memmove(buf, buf + offset, body_len);
  buf[body_len] = '\0';
  if (chunked) body_len = web_dechunk(buf, body_len);
  buf[body_len] = '\0';
  r->body = buf; r->length = body_len;
  return 0;
}

int web_fetch(const char *text, web_response_t *r) {
  char current[WEB_URL_MAX];
  memset(r, 0, sizeof(*r));
  snprintf(current, sizeof(current), "%s", text);
  for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
    web_url_t url;
    if (web_url_parse(current, &url) != 0) {
      set_error(r, "URL invalide ou schema non supporte");
      return -1;
    }
    snprintf(r->final_url, sizeof(r->final_url), "%s", current);
    if (strcmp(url.scheme, "about") == 0) {
      const char *page = strcmp(url.path, "test") == 0 ? about_test : about_home;
      r->body = dup_n(page, strlen(page));
      if (r->body == NULL) { set_error(r, "Memoire insuffisante"); return -1; }
      r->length = strlen(page); r->status = 200;
      return 0;
    }
    if (strcmp(url.scheme, "file") == 0) return fetch_file(&url, r);
    char location[WEB_URL_MAX];
    if (http_once(&url, r, location) != 0) return -1;
    if (r->status >= 300 && r->status < 400 && location[0] != '\0' &&
        r->status != 304) {
      web_response_free(r);
      char next[WEB_URL_MAX];
      web_url_resolve(current, location, next, sizeof(next));
      snprintf(current, sizeof(current), "%s", next);
      continue;
    }
    return 0;
  }
  set_error(r, "Trop de redirections");
  return -1;
}
