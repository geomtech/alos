; src/boot.s
MBALIGN  equ  1 << 0            ; aligner sur les pages
MEMINFO  equ  1 << 1            ; demander la carte mémoire (indispensable pour votre PMM plus tard)
FLAGS    equ  MBALIGN | MEMINFO
MAGIC    equ  0x1BADB002        ; Le "Magic Number" que GRUB cherche
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stack_bottom:
resb 16384 ; 16 KiB de stack (c'est énorme pour un début, mais safe)
stack_top:

align 4
global multiboot_magic
multiboot_magic:
resb 4              ; Stocke le magic number de EAX

global multiboot_info_ptr
multiboot_info_ptr:
resb 4              ; Stocke le pointeur vers la structure Multiboot Info de EBX

section .text
global _start:function (_start.end - _start)
_start:
    ; 1. Sauvegarder les infos Multiboot AVANT de toucher aux registres
    ; GRUB met le magic number dans EAX et l'adresse de la struct dans EBX
    mov [multiboot_magic], eax
    mov [multiboot_info_ptr], ebx

    ; 2. Initialiser la stack pointer (ESP)
    mov esp, stack_top

    ; 3. Appeler le kernel C avec les paramètres Multiboot
    ; Convention d'appel cdecl : arguments poussés de droite à gauche
    extern kernel_main
    push ebx            ; 2ème argument : pointeur vers multiboot_info
    push eax            ; 1er argument  : magic number
    call kernel_main
    add esp, 8          ; Nettoyer la stack (2 arguments * 4 octets)

    ; 3. Si le kernel retourne (ne devrait pas), on gèle le CPU
    cli
.hang:
    hlt
    jmp .hang
.end:

global gdt_flush
gdt_flush:
    mov eax, [esp + 4]  ; Récupère l'argument (le pointeur gdt_ptr)
    lgdt [eax]          ; Charge la nouvelle GDT

    ; Maintenant le moment critique : recharger les registres de segment
    mov ax, 0x10        ; 0x10 est l'offset de notre Data Segment (Index 2 * 8 octets = 16 = 0x10)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Pour changer CS (Code Segment), il faut faire un saut "loin" (Far Jump)
    ; 0x08 est l'offset de notre Code Segment (Index 1 * 8 octets = 8 = 0x08)
    jmp 0x08:.flush   

.flush:
    ret                 ; Retour au code C