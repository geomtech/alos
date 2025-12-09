#ifndef _SETJMP_H
#define _SETJMP_H

#include <stdint.h>

/* buffer for rbx, rbp, r12, r13, r14, r15, rsp, rip */
typedef uint64_t jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#define _setjmp setjmp
#define _longjmp longjmp

#endif
