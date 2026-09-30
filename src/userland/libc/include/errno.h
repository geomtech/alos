#ifndef _ERRNO_H
#define _ERRNO_H

#include "../../../include/errno.h"

#ifdef __cplusplus
extern "C" {
#endif
int *__errno_location(void);
#define errno (*__errno_location())
#ifdef __cplusplus
}
#endif
#endif
