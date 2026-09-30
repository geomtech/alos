#ifndef _SYS_ALOS_PROCESS_CONTROL_H
#define _SYS_ALOS_PROCESS_CONTROL_H
#include "../../../../include/process_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
int alos_process_query(int pid, alos_process_info_t *information);
int alos_process_terminate(int pid, int raw_status);
int alos_process_wait(int pid, alos_process_exit_t *exit, uint32_t timeout_ms);
void alos_process_exit(int raw_status) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
