#include <stdlib.h>

/* Toutes les locales ALOS ont le separateur decimal et les formats de C. */
float strtof_l(const char *text, char **end, locale_t locale) {
  (void)locale;
  return strtof(text, end);
}
double strtod_l(const char *text, char **end, locale_t locale) {
  (void)locale;
  return strtod(text, end);
}
long double strtold_l(const char *text, char **end, locale_t locale) {
  (void)locale;
  return strtold(text, end);
}
