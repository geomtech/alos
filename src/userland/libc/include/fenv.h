#ifndef _FENV_H
#define _FENV_H
#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned short fexcept_t;
typedef struct { unsigned short __control_word; unsigned short __status_word; unsigned int __mxcsr; } fenv_t;

#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x04
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_INEXACT    0x20
#define FE_ALL_EXCEPT (FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW|FE_UNDERFLOW|FE_INEXACT)

#define FE_TONEAREST  0
#define FE_DOWNWARD   0x400
#define FE_UPWARD     0x800
#define FE_TOWARDZERO 0xc00

#define FE_DFL_ENV ((const fenv_t *)0)

static inline int feclearexcept(int __excepts) { (void)__excepts; return 0; }
static inline int feraiseexcept(int __excepts) { (void)__excepts; return 0; }
static inline int fetestexcept(int __excepts) { (void)__excepts; return 0; }
static inline int fegetexceptflag(fexcept_t *__flagp, int __excepts) { (void)__excepts; if (__flagp) *__flagp = 0; return 0; }
static inline int fesetexceptflag(const fexcept_t *__flagp, int __excepts) { (void)__flagp; (void)__excepts; return 0; }
static inline int fegetround(void) { return FE_TONEAREST; }
static inline int fesetround(int __round) { return __round == FE_TONEAREST ? 0 : -1; }
static inline int fegetenv(fenv_t *__envp) { if (__envp) *__envp = (fenv_t){0}; return 0; }
static inline int feholdexcept(fenv_t *__envp) { return fegetenv(__envp); }
static inline int fesetenv(const fenv_t *__envp) { (void)__envp; return 0; }
static inline int feupdateenv(const fenv_t *__envp) { (void)__envp; return 0; }

#ifdef __cplusplus
}
#endif
#endif
