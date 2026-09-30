/* Types partages par <wchar.h>, <wctype.h>, <uchar.h> et <locale.h>. */
#ifndef _BITS_ALOS_WCHAR_H
#define _BITS_ALOS_WCHAR_H

#ifndef __ALOS_WINT_T
#define __ALOS_WINT_T
typedef unsigned int wint_t;
#endif

#ifndef __ALOS_WCTYPE_T
#define __ALOS_WCTYPE_T
typedef unsigned long wctype_t;
#endif

#ifndef __ALOS_MBSTATE_T
#define __ALOS_MBSTATE_T
/* Etat de decodage UTF-8 (meme disposition que musl). */
typedef struct { unsigned __opaque1, __opaque2; } mbstate_t;
#endif

#ifndef __ALOS_LOCALE_T
#define __ALOS_LOCALE_T
typedef struct __locale_struct *locale_t;
#endif

#if !defined(__cplusplus) && !defined(__ALOS_WCHAR_T)
#define __ALOS_WCHAR_T
typedef __WCHAR_TYPE__ wchar_t;
#endif

#ifndef __ALOS_FILE_T
#define __ALOS_FILE_T
/* FILE est un type incomplet commun a <stdio.h> et <wchar.h>. */
typedef struct _IO_FILE FILE;
#endif

#define WEOF 0xffffffffU

#endif