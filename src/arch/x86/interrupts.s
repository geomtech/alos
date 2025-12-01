; src/interrupts.s

extern timer_handler_c
extern keyboard_handler_c 
extern exception_handler_c

; --- IDT FLUSH ---
global idt_flush
idt_flush:
    mov eax, [esp+4]  ; Récupère le pointeur vers l'IDT passé en argument
    lidt [eax]        ; Charge l'IDT
    ret

; ============================================
; EXCEPTION HANDLERS (0-31)
; ============================================

; Macro pour exceptions SANS code d'erreur
%macro EXCEPTION_NOERRCODE 1
global isr%1
isr%1:
    cli
    push dword 0          ; Dummy error code
    push dword %1         ; Exception number
    jmp exception_common
%endmacro

; Macro pour exceptions AVEC code d'erreur
%macro EXCEPTION_ERRCODE 1
global isr%1
isr%1:
    cli
    ; Le CPU a déjà pushé le code d'erreur
    push dword %1         ; Exception number
    jmp exception_common
%endmacro

; Exceptions sans code d'erreur
EXCEPTION_NOERRCODE 0   ; Division by Zero
EXCEPTION_NOERRCODE 1   ; Debug
EXCEPTION_NOERRCODE 2   ; NMI
EXCEPTION_NOERRCODE 3   ; Breakpoint
EXCEPTION_NOERRCODE 4   ; Overflow
EXCEPTION_NOERRCODE 5   ; Bound Range Exceeded
EXCEPTION_NOERRCODE 6   ; Invalid Opcode
EXCEPTION_NOERRCODE 7   ; Device Not Available
EXCEPTION_ERRCODE   8   ; Double Fault (avec code d'erreur)
EXCEPTION_NOERRCODE 9   ; Coprocessor Segment Overrun (obsolète)
EXCEPTION_ERRCODE   10  ; Invalid TSS
EXCEPTION_ERRCODE   11  ; Segment Not Present
EXCEPTION_ERRCODE   12  ; Stack-Segment Fault
EXCEPTION_ERRCODE   13  ; General Protection Fault
EXCEPTION_ERRCODE   14  ; Page Fault
EXCEPTION_NOERRCODE 15  ; Reserved
EXCEPTION_NOERRCODE 16  ; x87 FPU Error
EXCEPTION_ERRCODE   17  ; Alignment Check
EXCEPTION_NOERRCODE 18  ; Machine Check
EXCEPTION_NOERRCODE 19  ; SIMD Exception
EXCEPTION_NOERRCODE 20  ; Virtualization Exception
EXCEPTION_NOERRCODE 21  ; Reserved
EXCEPTION_NOERRCODE 22  ; Reserved
EXCEPTION_NOERRCODE 23  ; Reserved
EXCEPTION_NOERRCODE 24  ; Reserved
EXCEPTION_NOERRCODE 25  ; Reserved
EXCEPTION_NOERRCODE 26  ; Reserved
EXCEPTION_NOERRCODE 27  ; Reserved
EXCEPTION_NOERRCODE 28  ; Reserved
EXCEPTION_NOERRCODE 29  ; Reserved
EXCEPTION_ERRCODE   30  ; Security Exception
EXCEPTION_NOERRCODE 31  ; Reserved

; Handler commun pour toutes les exceptions
exception_common:
    pusha                 ; Sauvegarder tous les registres
    
    ; Appeler le handler C avec les paramètres sur la stack
    ; Stack: [pusha regs] [exception_num] [error_code] [eip] [cs] [eflags]
    call exception_handler_c
    
    popa                  ; Restaurer les registres
    add esp, 8            ; Nettoyer exception_num et error_code
    iretd

; --- HANDLER TIMER (IRQ 0) ---
global irq0_handler
irq0_handler:
    pusha
    call timer_handler_c
    popa
    iretd

; --- HANDLER CLAVIER (IRQ 1) ---
global irq1_handler
irq1_handler:
    pusha
    call keyboard_handler_c
    popa
    iretd

; --- HANDLER PCnet (IRQ 11) ---
; IRQ 11 = IDT index 43 (32 + 11 = 43, mais IRQ 8-15 sont sur le slave à 40-47)
; Donc IRQ 11 = 40 + (11 - 8) = 40 + 3 = 43
global irq11_handler
extern pcnet_irq_handler
irq11_handler:
    pusha
    call pcnet_irq_handler
    ; EOI au Slave (0xA0) ET au Master (0x20) car IRQ > 7
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iretd

; --- HANDLER IDE Primary (IRQ 14) ---
; IRQ 14 = IDT index 46 (32 + 14 = 46)
global irq14_handler
extern ata_irq_handler
irq14_handler:
    pusha
    call ata_irq_handler
    ; EOI au Slave (0xA0) ET au Master (0x20) car IRQ > 7
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iretd

; --- HANDLER IDE Secondary (IRQ 15) ---
; IRQ 15 = IDT index 47 (32 + 15 = 47)
global irq15_handler
irq15_handler:
    pusha
    ; On ne fait rien pour l'IDE secondaire pour l'instant, juste EOI
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iretd