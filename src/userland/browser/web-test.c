/* browser/web-test.c - Tests du moteur alosweb (hote et ALOS)
 *   hote : gcc -o web-test web-test.c url.c http.c html.c render.c ../gui/fonts/roboto.c
 *   ./web-test [--ppm page.ppm] [url]   (url -> fetch reseau/fichier et rendu) */
#include "web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("web-test FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void resolve_case(const char *base, const char *ref, const char *want) {
  char out[WEB_URL_MAX];
  web_url_resolve(base, ref, out, sizeof(out));
  if (strcmp(out, want)) { printf("resolve(%s,%s)=%s want %s\n", base, ref, out, want); ++failures; }
}

int main(int argc, char **argv) {
  web_url_t u;
  CHECK(web_url_parse("http://Example.com:8080/a/b?x=1#f", &u) == 0);
  CHECK(!strcmp(u.host, "Example.com") && u.port == 8080 && !strcmp(u.path, "/a/b?x=1"));
  CHECK(web_url_parse("http://h", &u) == 0 && !strcmp(u.path, "/") && u.port == 80);
  CHECK(web_url_parse("https://h/", &u) == 0 && u.port == 443);
  CHECK(web_url_parse("file:///www/index.html", &u) == 0 && !strcmp(u.path, "/www/index.html"));
  CHECK(web_url_parse("about:test", &u) == 0 && !strcmp(u.path, "test"));
  CHECK(web_url_parse("ftp://h/", &u) != 0);
  CHECK(web_url_parse("http://h:99999/", &u) != 0);
  resolve_case("http://h/a/b.html", "c.html", "http://h/a/c.html");
  resolve_case("http://h/a/b.html", "../c.html", "http://h/c.html");
  resolve_case("http://h/a/b.html", "/x", "http://h/x");
  resolve_case("http://h:81/a/b.html", "//o/y", "http://o/y");
  resolve_case("http://h:81/a/b.html", "y", "http://h:81/a/y");
  resolve_case("http://h/a/b.html", "#top", "http://h/a/b.html");
  resolve_case("http://h/a/b.html", "https://z/", "https://z/");
  resolve_case("file:///www/index.html", "p.html", "file:///www/p.html");

  char chunked[] = "5\r\nhello\r\n6;ext\r\n world\r\n0\r\n\r\n";
  size_t n = web_dechunk(chunked, strlen(chunked));
  CHECK(n == 11 && !memcmp(chunked, "hello world", 11));

  const char *html = "<html><head><title>T &amp; T</title><style>p{}</style></head><body>"
                     "<h1>Hi</h1><p>Un <a href='/l'>lien</a> <b>gras</b></p><script>x<y</script>"
                     "<ul><li>a<li>b</ul></body></html>";
  web_page_t page;
  CHECK(web_layout(html, strlen(html), 300, "http://h/dir/", &page) == 0);
  CHECK(!strcmp(page.title, "T &amp; T")); /* le titre n'est pas decode : choix assume */
  CHECK(page.link_count == 1 && !strcmp(page.links[0], "http://h/l"));
  int found_script = 0, found_lien = 0;
  for (size_t i = 0; i < page.count; i++) {
    if (page.items[i].text && strstr(page.items[i].text, "x<y")) found_script = 1;
    if (page.items[i].text && !strcmp(page.items[i].text, "lien")) {
      found_lien = 1;
      CHECK(web_page_link_at(&page, page.items[i].x + 1, page.items[i].y + 1) == 0);
    }
  }
  CHECK(!found_script && found_lien && page.height > 50);
  web_page_free(&page);

  /* Mise en page etroite : aucun mot ne depasse la largeur. */
  const char *longtxt = "<p>aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa bb cc</p>";
  CHECK(web_layout(longtxt, strlen(longtxt), 120, "", &page) == 0);
  for (size_t i = 0; i < page.count; i++) CHECK(page.items[i].x + page.items[i].w <= 120);
  web_page_free(&page);

  const char *ppm = NULL, *target = "about:test";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
    else target = argv[i];
  }
  web_response_t r;
  if (web_fetch(target, &r) == 0) {
    CHECK(r.status == 200 || ppm != NULL || argc > 1);
    CHECK(web_layout(r.body, r.length, 640, r.final_url, &page) == 0);
    printf("web-test: %s -> status %d, %zu octets, %zu elements, titre \"%s\"\n",
           target, r.status, r.length, page.count, page.title);
    if (ppm) {
      int w = 640, h = page.height < 480 ? 480 : page.height;
      uint32_t *px = malloc((size_t)w * h * 4);
      web_render(&page, px, (uint32_t)w, w, h, 0, 0xFFFFFFFFu);
      FILE *f = fopen(ppm, "wb");
      if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
          unsigned char rgb[3] = {(px[i] >> 16) & 255, (px[i] >> 8) & 255, px[i] & 255};
          fwrite(rgb, 1, 3, f);
        }
        fclose(f);
      }
      free(px);
    }
    web_page_free(&page);
    web_response_free(&r);
  } else {
    printf("web-test: fetch %s echoue : %s\n", target, r.error);
    if (argc > 1) ++failures;
  }
  printf("web-test: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
