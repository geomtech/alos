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

; ============================================================
; void tss_flush(uint32_t tss_selector)
; ============================================================
; Charge le Task Register avec le sélecteur du TSS.
; Le CPU utilise le TSS pour connaître la stack kernel lors
; d'un changement de privilège (Ring 3 -> Ring 0).
;
global tss_flush
tss_flush:
    mov ax, [esp + 4]   ; Récupérer le sélecteur TSS (0x28)
    ltr ax              ; Load Task Register
    ret

; ============================================================
; void enter_usermode(uint32_t user_esp, uint32_t user_eip)
; ============================================================
; Effectue un saut vers le mode utilisateur (Ring 3).
; 
; Cette fonction prépare une stack frame pour IRET qui va :
; 1. Changer CS vers le User Code Segment (0x1B)
; 2. Changer SS vers le User Data Segment (0x23)
; 3. Restaurer les flags avec IF=1 (interruptions activées)
; 4. Sauter vers user_eip avec la stack user_esp
;
; Paramètres:
;   [esp+4] = user_esp : Stack pointer en mode utilisateur
;   [esp+8] = user_eip : Adresse de la fonction utilisateur
;
global enter_usermode
enter_usermode:
    cli                     ; Désactiver les interruptions pendant la transition
    
    ; Récupérer les paramètres
    mov eax, [esp + 4]      ; EAX = user_esp (stack utilisateur)
    mov ecx, [esp + 8]      ; ECX = user_eip (point d'entrée)
    
    ; Initialiser les segments de données pour Ring 3
    ; User Data Segment = 0x23 (index 4 * 8 + RPL 3)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Note: SS sera changé par IRET
    
    ; Récupérer les valeurs pour la stack IRET
    mov eax, [esp + 4]      ; EAX = user_esp
    
    ; ========================================
    ; Préparer la stack pour IRET
    ; ========================================
    ; IRET attend (du sommet vers le bas) :
    ;   [ESP+0]  = EIP     (où sauter)
    ;   [ESP+4]  = CS      (nouveau Code Segment)
    ;   [ESP+8]  = EFLAGS  (nouveaux flags)
    ;   [ESP+12] = ESP     (nouvelle stack) - seulement si changement de privilège
    ;   [ESP+16] = SS      (nouveau Stack Segment) - seulement si changement de privilège
    ;
    ; On push dans l'ordre inverse (SS en premier, EIP en dernier)
    
    push dword 0x23         ; SS - User Data Segment (Ring 3)
    push dword [esp + 8]    ; ESP - User stack (attention: +8 car on a déjà pushé SS)
                            ; En fait on veut user_esp qui était à [esp+4] avant les push
    ; Correction: après le push de SS, l'ancien [esp+4] est maintenant [esp+8]
    ; Mais on a aussi besoin de recalculer...
    ; Plus simple: on a sauvé user_esp dans une variable
    
    ; Recommençons proprement
    ; On va utiliser la stack kernel actuelle pour construire la frame IRET
    
    ; D'abord, sauvegardons les valeurs
    mov ebx, [esp + 4 + 4]  ; EBX = user_esp (ajusté pour le push précédent)
    
    ; Annulons le push précédent
    add esp, 8              ; Retirer les 2 mauvais push
    
    ; Maintenant construisons correctement
    mov eax, [esp + 4]      ; EAX = user_esp  
    mov ecx, [esp + 8]      ; ECX = user_eip
    
    ; EFLAGS: on veut IF=1 (bit 9) pour réactiver les interruptions
    ; et IOPL=3 (bits 12-13) pour permettre l'accès I/O en Ring 3 (optionnel)
    ; Valeur de base: 0x200 (IF=1) ou 0x3200 (IF=1, IOPL=3)
    pushf                   ; Push EFLAGS actuel
    pop ebx                 ; EBX = EFLAGS
    or ebx, 0x200           ; Activer IF (Interrupt Flag)
    
    ; Construire la frame IRET
    push dword 0x23         ; SS - User Data Segment
    push eax                ; ESP - User stack pointer
    push ebx                ; EFLAGS avec IF=1
    push dword 0x1B         ; CS - User Code Segment (0x18 + RPL 3 = 0x1B)
    push ecx                ; EIP - User entry point
    
    ; Saut magique vers Ring 3 !
    iret