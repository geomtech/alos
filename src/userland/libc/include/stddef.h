#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL ((void *)0)

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __WCHAR_TYPE__ wchar_t;

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
