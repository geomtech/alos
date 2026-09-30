#ifndef FUTEX_H
#define FUTEX_H
#include <stdint.h>
int futex_wait_private(uint32_t *address, uint32_t expected, uint32_t timeout_ms);
int futex_wake_private(uint32_t *address, int count);
#endif
