#ifndef XSTATE_H
#define XSTATE_H
#include <stdint.h>
void xstate_init(void);
void *xstate_create(void);
void xstate_save(void *state);
void xstate_restore(const void *state);
void xstate_destroy(void *state);
void xstate_reset(void *state);
#endif
