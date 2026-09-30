#ifndef _LANGINFO_H
#define _LANGINFO_H

#include <bits/alos_wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int nl_item;

#define ABDAY_1 0x20000
#define DAY_1   0x20007
#define ABMON_1 0x2000E
#define MON_1   0x2001A
#define AM_STR  0x20026
#define PM_STR  0x20027
#define D_T_FMT 0x20028
#define D_FMT   0x20029
#define T_FMT   0x2002A
#define T_FMT_AMPM 0x2002B
#define ERA     0x2002C
#define ERA_D_FMT 0x2002E
#define ALT_DIGITS 0x2002F
#define ERA_D_T_FMT 0x20030
#define ERA_T_FMT 0x20031
#define CODESET 14
#define CRNCYSTR 0x4000F
#define RADIXCHAR 0x10000
#define THOUSEP 0x10001
#define YESEXPR 0x50000
#define NOEXPR  0x50001

#define ABDAY_2 (ABDAY_1 + 1)
#define ABDAY_7 (ABDAY_1 + 6)
#define DAY_7   (DAY_1 + 6)
#define ABMON_12 (ABMON_1 + 11)
#define MON_12  (MON_1 + 11)

char *nl_langinfo(nl_item);
char *nl_langinfo_l(nl_item, locale_t);

#ifdef __cplusplus
}
#endif
#endif