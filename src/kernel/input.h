#ifndef INPUT_H
#define INPUT_H

#include "syscall.h" /* For input_event_t */
#include <stdint.h>

void input_init(void);
void input_push_event(input_event_t *event);
int input_pop_event(input_event_t *event);

#endif
