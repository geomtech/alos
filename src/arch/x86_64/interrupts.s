; src/arch/x86_64/interrupts.s - Interrupt handlers for x86-64
; 
; This file contains:
; - ISR stubs for exceptions (0-31)
; - IRQ stubs (32-47)
; - Syscall handler (0x80)
; - GDT/IDT/TSS flush routines

[BITS 64]

section .text

; ============================================
; External C handlers
; ============================================
extern exception_handler
extern irq_handler
extern syscall_dispatcher
extern timer_handler_preempt

; ============================================
; GDT/IDT/TSS Flush Routines
; ============================================

; void gdt_flush(uint64_t gdtr_ptr)
global gdt_flush
gdt_flush:
    lgdt [rdi]              ; Load GDT from pointer in RDI
    
    ; Reload data segments
    mov ax, 0x10            ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Reload code segment via far return
    push qword 0x08         ; Kernel code segment
    lea rax, [rel .flush]
    push rax
    retfq
.flush:
    ret

; void tss_flush(uint16_t tss_selector)
global tss_flush
tss_flush:
    mov ax, di              ; TSS selector in DI
    ltr ax                  ; Load Task Register
    ret

; void idt_flush(uint64_t idtr_ptr)
global idt_flush
idt_flush:
    lidt [rdi]              ; Load IDT from pointer in RDI
    ret

; ============================================
; Common ISR Stub
; ============================================
; Saves all registers and calls exception_handler(frame)

%macro PUSH_ALL 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_ALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

isr_common_stub:
    PUSH_ALL
    
    ; Load kernel data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Call exception_handler(frame)
    mov rdi, rsp            ; RDI = pointer to interrupt_frame
    call exception_handler
    
    POP_ALL
    add rsp, 16             ; Remove error_code and int_no
    iretq

; ============================================
; Common IRQ Stub
; ============================================

irq_common_stub:
    PUSH_ALL
    
    ; Load kernel data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Call irq_handler(frame)
    mov rdi, rsp            ; RDI = pointer to interrupt_frame
    call irq_handler
    
    POP_ALL
    add rsp, 16             ; Remove error_code and int_no
    iretq

; ============================================
; ISR Macros
; ============================================

; ISR without error code
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0            ; Dummy error code
    push qword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

; ISR with error code (pushed by CPU)
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    ; Error code already pushed by CPU
    push qword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

; IRQ handler
%macro IRQ 2
global irq%1
irq%1:
    push qword 0            ; Dummy error code
    push qword %2           ; Interrupt number (32 + IRQ)
    jmp irq_common_stub
%endmacro

; ============================================
; Exception Handlers (ISR 0-31)
; ============================================

ISR_NOERRCODE 0     ; Division By Zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; NMI
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound Range Exceeded
ISR_NOERRCODE 6     ; Invalid Opcode
ISR_NOERRCODE 7     ; Device Not Available
ISR_ERRCODE   8     ; Double Fault
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun
ISR_ERRCODE   10    ; Invalid TSS
ISR_ERRCODE   11    ; Segment Not Present
ISR_ERRCODE   12    ; Stack-Segment Fault
ISR_ERRCODE   13    ; General Protection Fault
ISR_ERRCODE   14    ; Page Fault
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 FPU Error
ISR_ERRCODE   17    ; Alignment Check
ISR_NOERRCODE 18    ; Machine Check
ISR_NOERRCODE 19    ; SIMD Exception
ISR_NOERRCODE 20    ; Virtualization Exception
ISR_ERRCODE   21    ; Control Protection Exception
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Hypervisor Injection
ISR_NOERRCODE 29    ; VMM Communication
ISR_ERRCODE   30    ; Security Exception
ISR_NOERRCODE 31    ; Reserved

; ============================================
; IRQ Handlers (IRQ 0-15 -> INT 32-47)
; ============================================

; IRQ 0 (Timer) has a special handler for preemption support
; IRQ 0, 32           ; Timer - DISABLED, using irq0_preempt instead
IRQ 1, 33           ; Keyboard
IRQ 2, 34           ; Cascade
IRQ 3, 35           ; COM2
IRQ 4, 36           ; COM1
IRQ 5, 37           ; LPT2
IRQ 6, 38           ; Floppy
IRQ 7, 39           ; LPT1 / Spurious
IRQ 8, 40           ; RTC
IRQ 9, 41           ; Free
IRQ 10, 42          ; Free
IRQ 11, 43          ; Network
IRQ 12, 44          ; PS/2 Mouse
IRQ 13, 45          ; FPU
IRQ 14, 46          ; Primary ATA
IRQ 15, 47          ; Secondary ATA

; ============================================
; IRQ 0 Handler with Preemption Support
; ============================================
; This is a special handler for the timer IRQ that supports
; preemptive context switching. If timer_handler_preempt returns
; a non-zero value, it's the new RSP to switch to.
;
; Stack layout after PUSH_ALL (interrupt_frame):
;   [RSP+0]   R15
;   [RSP+8]   R14
;   ...       (other registers)
;   [RSP+120] RAX
;   [RSP+128] int_no
;   [RSP+136] error_code
;   [RSP+144] RIP      <- CPU pushed
;   [RSP+152] CS
;   [RSP+160] RFLAGS
;   [RSP+168] RSP      <- User RSP (if from Ring 3)
;   [RSP+176] SS       <- User SS (if from Ring 3)
;
global irq0
irq0:
    push qword 0            ; Dummy error code
    push qword 32           ; Interrupt number (IRQ0 = INT 32)
    
    PUSH_ALL
    
    ; Load kernel data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Call timer_handler_preempt(frame)
    ; Returns: 0 = no preemption, non-zero = new RSP
    mov rdi, rsp
    call timer_handler_preempt
    
    ; Check if preemption requested
    test rax, rax
    jz .no_preempt
    
    ; Preemption: switch to new stack
    ; RAX contains the new RSP (pointing to a saved context)
    ; The new stack has the same layout as our current stack
    mov rsp, rax
    
.no_preempt:
    POP_ALL
    add rsp, 16             ; Remove error_code and int_no
    iretq

; ============================================
; Syscall Handler (INT 0x80)
; ============================================
; Legacy syscall via interrupt (for compatibility)
; Convention: RAX = syscall number, RDI/RSI/RDX/R10/R8/R9 = arguments
; Return: RAX = result

global isr128
isr128:
    push qword 0            ; Dummy error code
    push qword 0x80         ; Interrupt number
    
    PUSH_ALL
    
    ; Load kernel data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Call syscall_dispatcher(frame)
    mov rdi, rsp
    call syscall_dispatcher
    
    ; Result is in RAX, which will be restored from the frame
    ; The dispatcher should have modified frame->rax
    
    POP_ALL
    add rsp, 16             ; Remove error_code and int_no
    iretq

; ============================================
; SYSCALL Entry Point (fast syscalls)
; ============================================
; Used when SYSCALL instruction is executed
; RCX = return RIP, R11 = return RFLAGS
; RAX = syscall number, RDI/RSI/RDX/R10/R8/R9 = arguments

global syscall_entry
syscall_entry:
    ; Swap to kernel GS (contains per-CPU data)
    swapgs
    
    ; Save user RSP and load kernel RSP
    ; For now, we use a simple approach with a fixed kernel stack
    ; In a real implementation, this would use per-CPU data
    mov [rel user_rsp], rsp
    mov rsp, [rel kernel_rsp]
    
    ; Build a fake interrupt frame for consistency
    push qword 0x1B         ; User SS (will be ignored, but for consistency)
    push qword [rel user_rsp] ; User RSP
    push r11                ; RFLAGS (saved in R11 by SYSCALL)
    push qword 0x23         ; User CS (will be ignored)
    push rcx                ; Return RIP (saved in RCX by SYSCALL)
    push qword 0            ; Error code (dummy)
    push qword 0x80         ; Interrupt number (syscall)
    
    PUSH_ALL
    
    ; Call syscall_dispatcher(frame)
    mov rdi, rsp
    call syscall_dispatcher
    
    POP_ALL
    add rsp, 16             ; Remove error_code and int_no
    
    ; Restore for SYSRET
    ; Stack now: [RIP] [CS] [RFLAGS] [RSP] [SS]
    pop rcx                 ; Return RIP -> RCX
    add rsp, 8              ; Skip CS
    pop r11                 ; RFLAGS -> R11
    ; Stack now: [RSP] [SS]
    
    ; NOTE: Cannot use "pop rsp" because it increments the NEW rsp value!
    ; Use a temporary register instead
    mov r10, [rsp]          ; Load user RSP into r10 (r10 is caller-saved, safe to use)
    add rsp, 16             ; Skip RSP and SS on kernel stack
    
    ; Swap back to user GS (must be done while still on kernel stack)
    swapgs
    
    ; Now restore user RSP and return
    mov rsp, r10            ; Restore user RSP
    sysretq

section .data
align 16
user_rsp: dq 0
kernel_rsp: dq 0            ; Will be set during initialization

section .text

; void syscall_set_kernel_stack(uint64_t rsp)
global syscall_set_kernel_stack
syscall_set_kernel_stack:
    mov [rel kernel_rsp], rdi
    ret

; ============================================
; INT 0x80 syscall handler (legacy compatibility)
; ============================================
; Must match syscall_regs_t structure layout:
;   - 15 general registers (R15...RAX)
;   - int_no (dummy)
;   - error_code (dummy)
;   - CPU-pushed: RIP, CS, RFLAGS, RSP, SS
;
global syscall_handler_asm
extern syscall_dispatcher

syscall_handler_asm:
    ; Push dummy int_no and error_code to match syscall_regs_t structure
    ; Note: Push in reverse order since stack grows down
    ; Structure expects: [rax][int_no][error_code][rip]...
    ; So we push error_code first (higher addr), then int_no (lower addr)
    push qword 0            ; Dummy error code (will be at higher address)
    push qword 0x80         ; Interrupt number (will be at lower address, right after rax)
    
    ; Save all registers (must match PUSH_ALL order)
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Pass pointer to saved registers as argument
    mov rdi, rsp
    call syscall_dispatcher
    
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    
    ; Remove int_no and error_code
    add rsp, 16
    
    iretq

; ============================================
; IRQ 11 handler (for network cards)
; ============================================
global irq11_handler
extern pcnet_irq_handler

irq11_handler:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    
    ; Call C handler
    call pcnet_irq_handler
    
    ; Send EOI to PIC
    mov al, 0x20
    out 0xA0, al        ; Slave PIC
    out 0x20, al        ; Master PIC
    
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    
    iretq
