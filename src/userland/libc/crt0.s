; src/userland/libc/crt0.s - C Runtime Startup
; rewritten for NASM (Intel syntax)

bits 64
section .text
global _start
extern main
extern exit

_start:
    ; The kernel pushes arguments on the stack:
    ; RSP -> argc
    ;        argv pointers...

    ; Clear base pointer for backtraces
    xor rbp, rbp

    ; Get argc from stack (it's at the top)
    pop rdi

    ; Get argv from stack (it's right after argc was popped, so current RSP)
    mov rsi, rsp

    ; Align stack to 16 bytes (ABI requirement)
    ; RSP was 16-byte aligned before argc was pushed by kernel?
    ; If so, after pop rdi, RSP is 8-byte aligned.
    ; We need to ensure 16-byte alignment before 'call'.
    and rsp, -16

    ; Call main(argc, argv)
    call main

    ; Call exit(ret) with return value from main
    mov rdi, rax
    call exit

    ; Should never reach here
.hang:
    jmp .hang
