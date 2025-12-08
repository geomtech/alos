/* src/userland/libc/src/sys/syscalls.c - Syscall wrappers */
#include "internal/syscall.h"
#include <sys/syscall.h>
#include <unistd.h>

/* Using inline assembly for syscalls as defined in original libc.h */

long syscall0(long num) {
  long result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(num)
                   : "memory", "rcx", "r11");
  return result;
}

long syscall1(long num, long arg1) {
  long result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(num), "D"(arg1)
                   : "memory", "rcx", "r11");
  return result;
}

long syscall2(long num, long arg1, long arg2) {
  long result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(num), "D"(arg1), "S"(arg2)
                   : "memory", "rcx", "r11");
  return result;
}

long syscall3(long num, long arg1, long arg2, long arg3) {
  long result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
                   : "memory", "rcx", "r11");
  return result;
}

long syscall4(long num, long arg1, long arg2, long arg3, long arg4) {
  long result;
  register long r10 __asm__("r10") = arg4;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
                   : "memory", "rcx", "r11");
  return result;
}

long syscall5(long num, long arg1, long arg2, long arg3, long arg4, long arg5) {
  long result;
  register long r10 __asm__("r10") = arg4;
  register long r8 __asm__("r8") = arg5;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10),
                     "r"(r8)
                   : "memory", "rcx", "r11");
  return result;
}
