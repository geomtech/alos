/* Adaptation interne : macros utilisees par les sources musl importees
 * (voir COPYRIGHT.musl, licence MIT). */
#ifndef _ALOS_MUSL_FEATURES_H
#define _ALOS_MUSL_FEATURES_H
#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) \
  extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))

/* Prototypes internes de musl (normalement dans src/include/string.h). */
char *__strchrnul(const char *, int);
char *__stpcpy(char *, const char *);
char *__stpncpy(char *, const char *, __SIZE_TYPE__);
void *__memrchr(const void *, int, __SIZE_TYPE__);
#endif
