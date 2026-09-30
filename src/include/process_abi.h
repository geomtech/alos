#ifndef ALOS_PROCESS_ABI_H
#define ALOS_PROCESS_ABI_H
#include <stdint.h>
#define ALOS_PROCESS_WAIT_FOREVER UINT32_MAX
#define ALOS_PROCESS_EXIT_NORMAL 0U
#define ALOS_PROCESS_EXIT_FORCED 1U
#define ALOS_PROCESS_ALIVE 1U
#define ALOS_PROCESS_EXITING 2U
#define ALOS_PROCESS_ZOMBIE 3U
typedef struct {
  uint32_t pid;
  int32_t raw_status;
  uint32_t reason;
  uint32_t reserved;
} alos_process_exit_t;
typedef struct {
  uint32_t pid, parent_pid, thread_count, state;
  int32_t main_thread_nice;
  uint32_t has_main_thread_nice;
} alos_process_info_t;
#endif
