/* src/locale/locale.c - Locales POSIX d'ALOS.
 *
 * ALOS ne fournit pas de base de donnees de locales : seules "C", "POSIX" et
 * "C.UTF-8" (et leurs equivalents "") sont acceptees, toutes avec LC_CTYPE en
 * UTF-8, collation par octets/points de code et formats de la locale C. Les
 * autres noms echouent avec ENOENT au lieu d'etre acceptes silencieusement. */
#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct __locale_struct {
  int utf8_names; /* bitmask des categories nommees "C.UTF-8" */
};

static struct __locale_struct c_locales[1 << LC_ALL];
static struct __locale_struct global_locale;
static __thread locale_t thread_locale;

static int valid_name(const char *name, int *utf8) {
  if (!*name || !strcmp(name, "C") || !strcmp(name, "POSIX")) {
    *utf8 = !*name;
    return 1;
  }
  if (!strcmp(name, "C.UTF-8") || !strcmp(name, "C.utf8") ||
      !strcmp(name, "POSIX.UTF-8")) {
    *utf8 = 1;
    return 1;
  }
  return 0;
}

static locale_t interned(int names) {
  return &c_locales[names & ((1 << LC_ALL) - 1)];
}

locale_t newlocale(int mask, const char *name, locale_t base) {
  int utf8, names;
  if (!name || (mask & ~LC_ALL_MASK) != 0) {
    errno = EINVAL;
    return 0;
  }
  if (!valid_name(name, &utf8)) {
    errno = ENOENT;
    return 0;
  }
  names = (base && base != LC_GLOBAL_LOCALE) ? base->utf8_names : 0;
  mask &= (1 << LC_ALL) - 1;
  names = utf8 ? (names | mask) : (names & ~mask);
  /* Les objets sont immuables et internes : newlocale ne consomme pas base
   * au-dela de l'etat qu'il porte, comme le permet POSIX. */
  return interned(names);
}

locale_t duplocale(locale_t loc) {
  if (loc == LC_GLOBAL_LOCALE) return interned(global_locale.utf8_names);
  if (!loc) {
    errno = EINVAL;
    return 0;
  }
  return loc;
}

void freelocale(locale_t loc) { (void)loc; }

locale_t uselocale(locale_t loc) {
  locale_t old = thread_locale ? thread_locale : LC_GLOBAL_LOCALE;
  if (loc) thread_locale = loc == LC_GLOBAL_LOCALE ? 0 : loc;
  return old;
}

static const char *category_name(int names, int cat) {
  return (names & (1 << cat)) ? "C.UTF-8" : "C";
}

char *setlocale(int cat, const char *name) {
  static char buf[6 * 16];
  int utf8, i;
  if ((unsigned)cat > LC_ALL) return 0;
  if (name) {
    if (!valid_name(name, &utf8)) return 0;
    if (cat == LC_ALL)
      global_locale.utf8_names = utf8 ? (1 << LC_ALL) - 1 : 0;
    else if (utf8)
      global_locale.utf8_names |= 1 << cat;
    else
      global_locale.utf8_names &= ~(1 << cat);
  }
  if (cat != LC_ALL)
    return (char *)category_name(global_locale.utf8_names, cat);
  for (i = 1; i < LC_ALL; i++)
    if (((global_locale.utf8_names >> i) & 1) !=
        (global_locale.utf8_names & 1))
      break;
  if (i == LC_ALL) return (char *)category_name(global_locale.utf8_names, 0);
  buf[0] = 0;
  for (i = 0; i < LC_ALL; i++) {
    strcat(buf, category_name(global_locale.utf8_names, i));
    if (i + 1 < LC_ALL) strcat(buf, ";");
  }
  return buf;
}

struct lconv *localeconv(void) {
  static struct lconv c_lconv = {
      ".", "", "", "", "", "", "", "", "",
      "", CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
      CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
      CHAR_MAX};
  return &c_lconv;
}

static const char c_time[] =
    "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat\0"
    "Sunday\0Monday\0Tuesday\0Wednesday\0Thursday\0Friday\0Saturday\0"
    "Jan\0Feb\0Mar\0Apr\0May\0Jun\0Jul\0Aug\0Sep\0Oct\0Nov\0Dec\0"
    "January\0February\0March\0April\0May\0June\0July\0August\0September\0"
    "October\0November\0December\0"
    "AM\0PM\0"
    "%a %b %e %T %Y\0%m/%d/%y\0%H:%M:%S\0%I:%M:%S %p\0\0\0%m/%d/%y\0"
    "0123456789\0%a %b %e %T %Y\0%H:%M:%S";

char *nl_langinfo_l(nl_item item, locale_t loc) {
  int cat = item >> 16, idx = item & 65535;
  const char *s;
  (void)loc;
  if (item == CODESET) return "UTF-8";
  switch (cat) {
  case LC_NUMERIC:
    if (idx > 1) return "";
    return idx ? "" : ".";
  case LC_TIME:
    if (idx > 0x31) return "";
    s = c_time;
    break;
  case LC_MONETARY:
    return idx ? "" : "-";
  case LC_MESSAGES:
    if (idx > 3) return "";
    s = "^[yY]\0^[nN]\0yes\0no";
    break;
  default:
    return "";
  }
  for (; idx; idx--, s++)
    for (; *s; s++)
      ;
  return (char *)s;
}

char *nl_langinfo(nl_item item) { return nl_langinfo_l(item, 0); }

int strcoll(const char *l, const char *r) { return strcmp(l, r); }
int strcoll_l(const char *l, const char *r, locale_t loc) {
  (void)loc;
  return strcmp(l, r);
}

size_t strxfrm(char *restrict dest, const char *restrict src, size_t n) {
  size_t l = strlen(src);
  if (n > l) strcpy(dest, src);
  return l;
}
size_t strxfrm_l(char *restrict dest, const char *restrict src, size_t n,
                 locale_t loc) {
  (void)loc;
  return strxfrm(dest, src, n);
}

int wcscoll(const wchar_t *l, const wchar_t *r) { return wcscmp(l, r); }
int wcscoll_l(const wchar_t *l, const wchar_t *r, locale_t loc) {
  (void)loc;
  return wcscmp(l, r);
}

size_t wcsxfrm(wchar_t *restrict dest, const wchar_t *restrict src, size_t n) {
  size_t l = wcslen(src);
  if (l < n) wmemcpy(dest, src, l + 1);
  else if (n) {
    wmemcpy(dest, src, n - 1);
    dest[n - 1] = 0;
  }
  return l;
}
size_t wcsxfrm_l(wchar_t *restrict dest, const wchar_t *restrict src, size_t n,
                 locale_t loc) {
  (void)loc;
  return wcsxfrm(dest, src, n);
}