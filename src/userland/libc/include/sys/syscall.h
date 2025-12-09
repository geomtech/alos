#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT 1
#define SYS_FORK 2
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_WAITPID 7
#define SYS_UNLINK 10
#define SYS_CHDIR 12
#define SYS_TIME 13
#define SYS_LSEEK 19
#define SYS_GETPID 20
#define SYS_SETUID 23
#define SYS_GETUID 24
#define SYS_ALARM 27
#define SYS_FSTAT 28
#define SYS_PAUSE 29
#define SYS_KILL 37
#define SYS_MKDIR 39
#define SYS_RMDIR 40
#define SYS_SOCKET 41
#define SYS_CONNECT 42
#define SYS_ACCEPT 43
#define SYS_SEND 44
#define SYS_RECV 45
#define SYS_BIND 49
#define SYS_LISTEN 50
#define SYS_SETSOCKOPT 54
#define SYS_GETSOCKOPT 55
#define SYS_CLONE 56
#define SYS_EXECVE 59
#define SYS_THREAD_CREATE 60
#define SYS_CREATE 85
#define SYS_READDIR 89
#define SYS_MMAP 90
#define SYS_MUNMAP 91
#define SYS_KBHIT 100
#define SYS_CLEAR 101
#define SYS_MEMINFO 102
#define SYS_STAT 106
#define SYS_GET_FRAMEBUFFER 110
#define SYS_GET_EVENT 111
#define SYS_BRK 120
#define SYS_SLEEP 162
#define SYS_NANOSLEEP 162
#define SYS_GETCWD 183
#define SYS_GETTID 186
#define SYS_TKILL 200

/* Event types for SYS_GET_EVENT */
#define INPUT_EVENT_NONE 0
#define INPUT_EVENT_KEY_PRESS 1
#define INPUT_EVENT_KEY_RELEASE 2
#define INPUT_EVENT_MOUSE_MOVE 3
#define INPUT_EVENT_MOUSE_BUTTON 4
#define INPUT_EVENT_MOUSE_SCROLL 5

/* Structure for SYS_GET_EVENT - must match kernel definition! */
typedef struct {
  uint32_t type;
  uint32_t time;
  union {
    struct {
      uint32_t key;
      uint32_t scancode;
      uint32_t flags;
    } key;
    struct {
      int32_t x;
      int32_t y;
      int32_t dx;
      int32_t dy;
      uint32_t buttons;
    } mouse;
  } data;
} input_event_t;

/* Syscall wrapper functions */
long syscall0(long number);
long syscall1(long number, long arg1);
long syscall2(long number, long arg1, long arg2);
long syscall3(long number, long arg1, long arg2, long arg3);
long syscall4(long number, long arg1, long arg2, long arg3, long arg4);
long syscall5(long number, long arg1, long arg2, long arg3, long arg4,
              long arg5);
long syscall6(long number, long arg1, long arg2, long arg3, long arg4,
              long arg5, long arg6);

#endif
