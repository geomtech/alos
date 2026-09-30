#ifndef _SYS_FUTEX_H
#define _SYS_FUTEX_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int futex_wait(uint32_t *address, uint32_t expected, uint32_t timeout_ms);
int futex_wake(uint32_t *address, int count);
#ifdef __cplusplus
}
#endif
#endif
