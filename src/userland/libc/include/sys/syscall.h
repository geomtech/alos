#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT 1
#define SYS_FORK 57
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_WAITPID 61
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
#define SYS_MPROTECT 125
#define SYS_MADVISE 219
#define SYS_ERRNO_LOCATION 220
#define SYS_VMINFO 221
#define SYS_CLOCK_GETTIME 222
#define SYS_NANOSLEEP_POSIX 223
#define SYS_GET_FS 224
#define SYS_SET_FS 225
#define SYS_CONTEXT_SWITCHES 226
#define SYS_FUTEX_WAIT 227
#define SYS_FUTEX_WAKE 228
#define SYS_THREAD_JOIN 229
#define SYS_THREAD_DETACH 230
#define SYS_THREAD_REGISTER 231
#define SYS_EXIT_GROUP 232
#define SYS_ISATTY 233
#define SYS_THREAD_STACK 234
#define SYS_READDIR_FD 235
#define SYS_FCNTL 236
#define SYS_DUP 237
#define SYS_DUP2 238
#define SYS_UNAME 260
#define SYS_NATIVE_SOCKET 240
#define SYS_NATIVE_POLL 241
#define SYS_FSYNC 250
#define SYS_PIPE2 251
#define SYS_ACCESS 252
#define SYS_MKDIR_MODE 253
#define SYS_GETENTROPY 261
#define SYS_PREAD 262
#define SYS_PWRITE 263
#define SYS_FTRUNCATE 264
#define SYS_MINCORE 265
#define SYS_SYSTEM_INFO 266
#define SYS_RESOLVE_IPV4 267
#define SYS_READV 270
#define SYS_WRITEV 271
#define SYS_IOCTL 272
#define SYS_SHM_CREATE_NATIVE 280
#define SYS_SHM_READONLY 281
#define SYS_SHM_INFO 282
#define SYS_SHM_SAME 283
#define SYS_RESOURCE_LIMIT 290
#define SYS_RESOURCE_SET_LIMIT 291
#define SYS_THREAD_NICE 292
#define SYS_STATVFS 300
#define SYS_FUTIMES 301
#define SYS_SET_PROCESS_TITLE 302
#define SYS_MSYNC 303
#define SYS_PROCESS_TERMINATE 304
#define SYS_PROCESS_WAIT 305
#define SYS_PROCESS_QUERY 306
#define SYS_NATIVE_PATH 310
#define SYS_KBHIT 100
#define SYS_CLEAR 101
#define SYS_MEMINFO 102
#define SYS_PS_INFO 103
#define SYS_PING 104
#define SYS_WGET 105
#define SYS_HTTPD 107
#define SYS_STAT 106
#define SYS_GET_FRAMEBUFFER 110
#define SYS_GET_EVENT 111
#define SYS_WAIT_EVENT 112
#define SYS_BRK 120
#define SYS_SLEEP 162
#define SYS_NANOSLEEP 162
#define SYS_GETCWD 183
#define SYS_GETTID 186
#define SYS_TKILL 200
#define SYS_SPAWN_WAIT 201
#define SYS_IPC_LISTEN 202
#define SYS_IPC_CONNECT 203
#define SYS_IPC_ACCEPT 204
#define SYS_IPC_SEND 205
#define SYS_IPC_RECV 206
#define SYS_SHM_CREATE 207
#define SYS_SHM_MAP 208
#define SYS_SHM_UNMAP 209
#define SYS_DISPLAY_ACQUIRE 210
#define SYS_DISPLAY_RELEASE 211
#define SYS_SHM_SIZE 212
#define SYS_GET_MICROSECONDS 163 /* Obtenir le temps en microsecondes */
#define SYS_SLEEP_MICROS 164 /* Dormir pendant un temps spécifié en microsecondes */

/* Structure pour SYS_PS_INFO */
typedef struct {
  uint32_t pid;
  uint32_t tid;
  char name[32];
  uint32_t state;
  uint64_t rip;
  uint64_t rsp;
} proc_info_t;

/* Structure pour SYS_MEMINFO */
typedef struct {
  uint32_t total_size;
  uint32_t free_size;
  uint32_t block_count;
  uint32_t free_block_count;
} meminfo_t;

/* Structure pour SYS_READDIR */
typedef struct {
  char name[256];
  uint32_t type;
  uint32_t size;
} userspace_dirent_t;

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
#ifdef __cplusplus
extern "C" {
#endif
long syscall0(long number);
long syscall1(long number, long arg1);
long syscall2(long number, long arg1, long arg2);
long syscall3(long number, long arg1, long arg2, long arg3);
long syscall4(long number, long arg1, long arg2, long arg3, long arg4);
long syscall5(long number, long arg1, long arg2, long arg3, long arg4,
              long arg5);
long syscall6(long number, long arg1, long arg2, long arg3, long arg4,
              long arg5, long arg6);

#ifdef __cplusplus
}
#endif
#endif
