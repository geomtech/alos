#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

#include "../../../../include/utsname.h"

#ifdef __cplusplus
extern "C" {
#endif
int uname(struct utsname *name);
#ifdef __cplusplus
}
#endif

#endif
