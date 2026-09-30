#ifndef ALOS_SYS_RANDOM_H
#define ALOS_SYS_RANDOM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
int getentropy(void *buffer, size_t length);
#ifdef __cplusplus
}
#endif

#endif
