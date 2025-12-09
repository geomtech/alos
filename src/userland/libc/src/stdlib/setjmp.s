global setjmp
global _setjmp
global longjmp
global _longjmp
global __longjmp_chk

section .text

setjmp:
_setjmp:
    ; rdi = env (jmp_buf)
    mov [rdi], rbx
    mov [rdi+8], rbp
    mov [rdi+16], r12
    mov [rdi+24], r13
    mov [rdi+32], r14
    mov [rdi+40], r15
    lea rdx, [rsp+8] ; caller rsp (skip return address)
    mov [rdi+48], rdx
    mov rdx, [rsp]   ; return address (rip)
    mov [rdi+56], rdx
    xor rax, rax
    ret

longjmp:
_longjmp:
__longjmp_chk:
    ; rdi = env, rsi = val
    mov rbx, [rdi]
    mov rbp, [rdi+8]
    mov r12, [rdi+16]
    mov r13, [rdi+24]
    mov r14, [rdi+32]
    mov r15, [rdi+40]
    mov rsp, [rdi+48] ; Restore stack pointer
    mov rdx, [rdi+56] ; Return address
    
    mov rax, rsi
    test rax, rax
    jnz .ret
    inc rax      ; if val is 0, return 1
.ret:
    mov [rsp], rdx ; Put return address on stack
    ret
