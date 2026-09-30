#ifndef _STDDEF_H
#define _STDDEF_H

#ifdef __cplusplus
#define NULL nullptr
#else
#define NULL ((void *)0)
#endif

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef struct { long long integer; long double floating; } max_align_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
