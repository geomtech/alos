; src/interrupts.s

extern timer_handler_c
extern keyboard_handler_c 

; --- IDT FLUSH ---
global idt_flush
idt_flush:
    mov eax, [esp+4]  ; Récupère le pointeur vers l'IDT passé en argument
    lidt [eax]        ; Charge l'IDT
    ret

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