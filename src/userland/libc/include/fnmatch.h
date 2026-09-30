#ifndef _FNMATCH_H
#define _FNMATCH_H
#ifdef __cplusplus
extern "C" {
#endif
#define FNM_PATHNAME 0x1
#define FNM_NOESCAPE 0x2
#define FNM_PERIOD 0x4
#define FNM_NOMATCH 1
int fnmatch(const char *pattern, const char *string, int flags);
#ifdef __cplusplus
}
#endif
#endif
