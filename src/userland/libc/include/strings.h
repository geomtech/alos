#ifndef _STRINGS_H
#define _STRINGS_H

#include <stddef.h>
#include <bits/alos_wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
int strcasecmp_l(const char *, const char *, locale_t);
int strncasecmp_l(const char *, const char *, size_t, locale_t);

#ifdef __cplusplus
}
#endif
#endif