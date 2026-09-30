/* C-locale parser derived from musl 1.2.6 src/time/strptime.c.
 * See src/userland/libc/COPYRIGHT.musl for the MIT license.
 * ALOS has no locale-specific eras, alternate digits or timezone database. */
#include <time.h>
#include <langinfo.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

static int number(const char **input, int width, int64_t limit, int64_t *out)
{
  const char *s = *input;
  int64_t value = 0;
  if (!isdigit((unsigned char)*s)) return 0;
  while (width-- && isdigit((unsigned char)*s)) {
    int digit = *s++ - '0';
    if (value > (limit - digit) / 10) return 0;
    value = value * 10 + digit;
  }
  *input = s;
  *out = value;
  return 1;
}

static const char *parse(const char *s, const char *f, struct tm *tm,
                         int depth)
{
  int century = 0, relyear = 0, want_century = 0;
  if (depth > 8) return 0;
  while (*f) {
    if (*f != '%') {
      if (isspace((unsigned char)*f))
        while (isspace((unsigned char)*s)) s++;
      else if (*s++ != *f) return 0;
      f++;
      continue;
    }
    f++;
    if (*f == '+') f++;
    int width = -1;
    int64_t n;
    if (isdigit((unsigned char)*f)) {
      if (!number(&f, INT_MAX, INT_MAX, &n) || !n) return 0;
      width = (int)n;
    }
    int modifier = 0;
    if (*f == 'E' || *f == 'O') modifier = *f++;
    if (!*f) return 0;
    char spec = *f++;
    if (modifier == 'E' && !strchr("cCxXyY", spec)) return 0;
    if (modifier == 'O' && !strchr("deHImMSuUVwWy", spec)) return 0;
    const char *sub = 0;
    int *dest = 0, min = 0, max = 0, adjust = 0;
    switch (spec) {
    case 'a': case 'A': case 'b': case 'B': case 'h': {
      int days = spec == 'a' || spec == 'A';
      int count = days ? 7 : 12;
      int base = days ? ABDAY_1 : ABMON_1;
      int found = 0;
      for (int i = 2 * count - 1; i >= 0; i--) {
        const char *name = nl_langinfo(base + i);
        size_t len = strlen(name);
        if (len && !strncasecmp(s, name, len)) {
          s += len;
          if (days) tm->tm_wday = i % count;
          else tm->tm_mon = i % count;
          found = 1;
          break;
        }
      }
      if (!found) return 0;
      break;
    }
    case 'c': sub = nl_langinfo(D_T_FMT); break;
    case 'D': sub = "%m/%d/%y"; break;
    case 'F': {
      char text[32];
      int count = 0;
      if (width < 0) width = (int)sizeof(text) - 1;
      while (s[count] && count < width && count < (int)sizeof(text) - 1) {
        text[count] = s[count];
        count++;
      }
      text[count] = 0;
      const char *end = parse(text, "%12Y-%m-%d", tm, depth + 1);
      if (!end) return 0;
      s += end - text;
      break;
    }
    case 'r': sub = nl_langinfo(T_FMT_AMPM); break;
    case 'R': sub = "%H:%M"; break;
    case 'T': sub = "%H:%M:%S"; break;
    case 'x': sub = nl_langinfo(D_FMT); break;
    case 'X': sub = nl_langinfo(T_FMT); break;
    case 'C': case 'y': case 'Y': case 'g': case 'G': {
      int neg = 0;
      if (*s == '+' || *s == '-') neg = *s++ == '-';
      if (width < 0) width = spec == 'C' || spec == 'y' || spec == 'g' ? 2 : 4;
      if (!number(&s, width, (int64_t)INT_MAX + 1901, &n)) return 0;
      if (neg) n = -n;
      if (spec == 'Y') {
        n -= 1900;
        if (n < INT_MIN || n > INT_MAX) return 0;
        tm->tm_year = (int)n;
        want_century = 0;
      } else if (spec == 'C') {
        if (n < INT_MIN || n > INT_MAX) return 0;
        century = (int)n;
        want_century |= 2;
      } else if (spec == 'y') {
        if (n < 0 || n > 99) return 0;
        relyear = (int)n;
        want_century |= 1;
      } else if (spec == 'g' && (n < 0 || n > 99)) return 0;
      break;
    }
    case 'd': case 'e': dest = &tm->tm_mday; min = 1; max = 31; break;
    case 'H': dest = &tm->tm_hour; max = 23; break;
    case 'I': dest = &tm->tm_hour; min = 1; max = 12; break;
    case 'j': dest = &tm->tm_yday; min = 1; max = 366; adjust = 1; break;
    case 'm': dest = &tm->tm_mon; min = 1; max = 12; adjust = 1; break;
    case 'M': dest = &tm->tm_min; max = 59; break;
    case 'S': dest = &tm->tm_sec; max = 60; break;
    case 'u': dest = &tm->tm_wday; min = 1; max = 7; break;
    case 'w': dest = &tm->tm_wday; max = 6; break;
    /* Comme musl, les semaines ISO sont analysees sans date civile derivee. */
    case 'U': case 'W': min = 0; max = 53; goto week;
    case 'V': min = 1; max = 53;
    week:
      if (!number(&s, 2, 99, &n) || n < min || n > max) return 0;
      break;
    case 'n': case 't':
      while (isspace((unsigned char)*s)) s++;
      break;
    case 'p': {
      const char *am = nl_langinfo(AM_STR), *pm = nl_langinfo(PM_STR);
      size_t len = strlen(am);
      if (len && !strncasecmp(s, am, len)) {
        tm->tm_hour %= 12;
        s += len;
      } else {
        len = strlen(pm);
        if (!len || strncasecmp(s, pm, len)) return 0;
        tm->tm_hour = tm->tm_hour % 12 + 12;
        s += len;
      }
      break;
    }
    case 'z': {
      int neg = *s == '-';
      if (*s != '+' && *s != '-') return 0;
      s++;
      const char *begin = s;
      if (!number(&s, 4, 9999, &n) || s - begin != 4 ||
          n / 100 > 23 || n % 100 > 59) return 0;
      tm->tm_gmtoff = (n / 100 * 60 + n % 100) * 60;
      if (neg) tm->tm_gmtoff = -tm->tm_gmtoff;
      break;
    }
    case 'Z':
      if (strncmp(s, "UTC", 3) && strncmp(s, "GMT", 3)) return 0;
      s += 3;
      if (isalnum((unsigned char)*s) || *s == '_' ||
          *s == '+' || *s == '-') return 0;
      tm->tm_isdst = 0;
      tm->tm_gmtoff = 0;
      tm->tm_zone = "UTC";
      break;
    case '%': if (*s++ != '%') return 0; break;
    default: return 0;
    }
    if (dest) {
      while (isspace((unsigned char)*s)) s++;
      if (!number(&s, spec == 'j' ? 3 : 2, 999, &n) ||
          n < min || n > max) return 0;
      *dest = (int)n - adjust;
      if (spec == 'u') *dest %= 7;
    }
    if (sub) {
      if (!*sub) return 0;
      s = parse(s, sub, tm, depth + 1);
      if (!s) return 0;
    }
  }
  if (want_century) {
    int64_t year = relyear;
    if (want_century & 2) year += (int64_t)century * 100 - 1900;
    else if (year <= 68) year += 100;
    if (year < INT_MIN || year > INT_MAX) return 0;
    tm->tm_year = (int)year;
  }
  return s;
}

char *strptime(const char *restrict s, const char *restrict format,
               struct tm *restrict tm)
{
  return (char *)parse(s, format, tm, 0);
}
