/* src/stdlib/wcsto.c - Conversions numeriques larges (wcstol, wcstod...).
 *
 * Les chaines numeriques acceptees par strto* sont purement ASCII : on copie
 * le prefixe ASCII de la chaine large dans un tampon etroit, on appelle la
 * conversion etroite eprouvee, puis on reporte la position de fin (un
 * caractere large ASCII correspond a exactement un octet). */
#include <stdlib.h>
#include <wchar.h>

#define WCSTO_STACK 128

static char *narrow_prefix(const wchar_t *s, char *stack, size_t *len) {
  size_t n = 0;
  char *buf;
  while (s[n] > 0 && (unsigned)s[n] < 128) n++;
  buf = n < WCSTO_STACK ? stack : malloc(n + 1);
  if (!buf) {
    /* Sans memoire, on convertit au plus le debut : les nombres realistes
     * tiennent dans le tampon de pile. */
    n = WCSTO_STACK - 1;
    buf = stack;
  }
  for (size_t i = 0; i < n; i++) buf[i] = (char)s[i];
  buf[n] = 0;
  *len = n;
  return buf;
}

#define WCSTO_BODY(expr)                                                    \
  char stack[WCSTO_STACK], *end;                                           \
  size_t len;                                                              \
  char *buf = narrow_prefix(s, stack, &len);                               \
  __typeof__(expr) r = expr;                                               \
  if (p) *p = (wchar_t *)s + (end - buf);                                  \
  if (buf != stack) free(buf);                                             \
  return r;

long wcstol(const wchar_t *restrict s, wchar_t **restrict p, int base) {
  WCSTO_BODY(strtol(buf, &end, base))
}
unsigned long wcstoul(const wchar_t *restrict s, wchar_t **restrict p,
                      int base) {
  WCSTO_BODY(strtoul(buf, &end, base))
}
long long wcstoll(const wchar_t *restrict s, wchar_t **restrict p, int base) {
  WCSTO_BODY(strtoll(buf, &end, base))
}
unsigned long long wcstoull(const wchar_t *restrict s, wchar_t **restrict p,
                            int base) {
  WCSTO_BODY(strtoull(buf, &end, base))
}
float wcstof(const wchar_t *restrict s, wchar_t **restrict p) {
  WCSTO_BODY(strtof(buf, &end))
}
double wcstod(const wchar_t *restrict s, wchar_t **restrict p) {
  WCSTO_BODY(strtod(buf, &end))
}
long double wcstold(const wchar_t *restrict s, wchar_t **restrict p) {
  WCSTO_BODY(strtold(buf, &end))
}