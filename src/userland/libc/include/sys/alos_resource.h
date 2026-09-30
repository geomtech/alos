#ifndef _SYS_ALOS_RESOURCE_H
#define _SYS_ALOS_RESOURCE_H

#ifdef __cplusplus
extern "C" {
#endif
/* Current thread only. Getter uses an output argument: -1 is a valid nice. */
int alos_thread_get_nice(int *value);
int alos_thread_set_nice(int value);
int alos_thread_can_set_nice(int value);
#ifdef __cplusplus
}
#endif
#endif
