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