#ifndef _ALOS_SYS_CDEFS_H
#define _ALOS_SYS_CDEFS_H

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#if __cplusplus >= 201103L
#define __THROW noexcept(true)
#else
#define __THROW throw()
#endif
#else
#define __BEGIN_DECLS
#define __END_DECLS
#define __THROW
#endif

#endif
