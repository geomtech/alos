/* Adaptation interne de locale_impl.h de musl : sous ALOS toutes les locales
 * utilisent UTF-8 pour LC_CTYPE, MB_CUR_MAX vaut donc toujours 4. */
#ifndef _ALOS_MUSL_LOCALE_IMPL_H
#define _ALOS_MUSL_LOCALE_IMPL_H
#include <features.h>
#include <locale.h>
#include <stdlib.h>
#undef MB_CUR_MAX
#define MB_CUR_MAX 4
#define CURRENT_UTF8 1
#endif