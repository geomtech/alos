#ifndef _INTERNAL_SYSCALL_H
#define _INTERNAL_SYSCALL_H

#include <sys/syscall.h>

long syscall0(long num);
long syscall1(long num, long arg1);
long syscall2(long num, long arg1, long arg2);
long syscall3(long num, long arg1, long arg2, long arg3);
long syscall4(long num, long arg1, long arg2, long arg3, long arg4);
long syscall5(long num, long arg1, long arg2, long arg3, long arg4, long arg5);

#endif
