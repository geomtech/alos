; src/arch/x86/switch.s - Context Switch Assembly
; 
; Ce fichier contient les fonctions de context switch.
; Format unifié: interrupt_frame_t (pusha + eip/cs/eflags)

[BITS 32]

section .text

; ============================================================
; uint32_t switch_context(uint32_t* old_esp_ptr, uint32_t new_esp)
; ============================================================
; 
; Context switch coopératif (yield).
; Sauvegarde le contexte au format interrupt_frame_t.
;
; Paramètres:
;   [ESP+4]  = old_esp_ptr : Pointeur où sauvegarder l'ESP actuel
;   [ESP+8]  = new_esp     : Nouvel ESP à charger
;
; Retour: ne retourne pas directement, on "retourne" via popa+iretd
;
global switch_context
switch_context:
    ; Désactiver les interruptions pendant le switch
    cli
    
    ; Récupérer les arguments avant de modifier la stack
    mov eax, [esp + 4]      ; EAX = old_esp_ptr
    mov ecx, [esp + 8]      ; ECX = new_esp
    
    ; Construire un interrupt_frame_t sur la stack actuelle
    ; Format: [EFLAGS] [CS] [EIP] [EAX] [ECX] [EDX] [EBX] [ESP] [EBP] [ESI] [EDI]
    
    ; Pousser ce qui sera restauré par iretd
    pushf                   ; EFLAGS (avec IF=1 pour réactiver les interrupts)
    push dword 0x08         ; CS = kernel code segment
    push dword .return_point ; EIP = où continuer après le switch
    
    ; Pousser les registres (format pusha, mais dans le bon ordre pour popa)
    push eax                ; EAX (sera écrasé)
    push ecx                ; ECX (sera écrasé)
    push edx                ; EDX
    push ebx                ; EBX
    push esp                ; ESP (ignoré par popa)
    push ebp                ; EBP
    push esi                ; ESI
    push edi                ; EDI
    
    ; Sauvegarder ESP dans old_esp_ptr
    ; EAX contient toujours old_esp_ptr
    mov [eax], esp
    
    ; Basculer vers la nouvelle stack
    mov esp, ecx            ; ESP = new_esp
    
    ; Restaurer le contexte du nouveau thread (format interrupt_frame)
    popa                    ; Restaure EDI, ESI, EBP, (skip ESP), EBX, EDX, ECX, EAX
    iretd                   ; Restaure EIP, CS, EFLAGS (et réactive les interrupts)

.return_point:
    ; On arrive ici quand ce thread est reschedulé
    ; Les interruptions sont réactivées par iretd
    ret


; ============================================================
; void switch_task(uint32_t* old_esp_ptr, uint32_t new_esp, uint32_t new_cr3)
; ============================================================
; 
; Context switch avec support optionnel de changement d'espace d'adressage.
; Si new_cr3 != 0, change le Page Directory (CR3) pour l'isolation mémoire.
;
; Format de stack (sans segments):
;   [SS]       <- seulement pour user (iretd vers Ring 3)
;   [ESP_user] <- seulement pour user (iretd vers Ring 3)
;   [EFLAGS]
;   [CS]
;   [EIP]
;   [EAX] [ECX] [EDX] [EBX] [ESP_dummy] [EBP] [ESI] [EDI]  <- pusha/popa
;
; Paramètres:
;   [ESP+4]  = old_esp_ptr : Pointeur où sauvegarder l'ESP actuel
;   [ESP+8]  = new_esp     : Nouvel ESP à charger
;   [ESP+12] = new_cr3     : Nouveau Page Directory (0 = pas de changement)
;
global switch_task
switch_task:
    ; Désactiver les interruptions pendant le switch
    cli
    
    ; Récupérer les arguments avant de modifier la stack
    mov eax, [esp + 4]      ; EAX = old_esp_ptr
    mov ecx, [esp + 8]      ; ECX = new_esp
    mov edx, [esp + 12]     ; EDX = new_cr3
    
    ; Pousser ce qui sera restauré par iretd
    pushf                   ; EFLAGS (avec IF=1 pour réactiver les interrupts)
    push dword 0x08         ; CS = kernel code segment
    push dword .return_point ; EIP = où continuer après le switch
    
    ; Pousser les registres (format pusha, mais dans le bon ordre pour popa)
    push eax                ; EAX (sera écrasé)
    push ecx                ; ECX (sera écrasé)
    push edx                ; EDX (sera écrasé)
    push ebx                ; EBX
    push esp                ; ESP (ignoré par popa)
    push ebp                ; EBP
    push esi                ; ESI
    push edi                ; EDI
    
    ; Sauvegarder ESP dans old_esp_ptr
    ; EAX contient toujours old_esp_ptr
    mov [eax], esp
    
    ; Changer le Page Directory si new_cr3 != 0
    ; EDX contient new_cr3
    test edx, edx
    jz .skip_cr3_switch
    
    ; Charger le nouveau Page Directory
    mov cr3, edx
    
.skip_cr3_switch:
    ; Basculer vers la nouvelle stack
    mov esp, ecx            ; ESP = new_esp
    
    ; Restaurer le contexte du nouveau thread
    popa                    ; Restaure EDI, ESI, EBP, (skip ESP), EBX, EDX, ECX, EAX
    
    ; Vérifier si on retourne vers Ring 3 (CS sur la stack a RPL=3)
    ; CS est à [ESP+4] après popa
    test dword [esp + 4], 0x03
    jz .kernel_return
    
    ; Retour vers user mode: charger les segments user
    push eax
    mov ax, 0x23            ; User data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax
    
.kernel_return:
    iretd                   ; Restaure EIP, CS, EFLAGS (et SS/ESP si Ring 3)

.return_point:
    ; On arrive ici quand ce thread est reschedulé
    ; Les interruptions sont réactivées par iretd
    ret

; ============================================================
; void jump_to_user(uint32_t esp, uint32_t cr3)
; ============================================================
; 
; Saute vers un thread user sans sauvegarder le contexte actuel.
; Utilisé pour le PREMIER switch vers un thread user.
;
; Paramètres:
;   [ESP+4]  = esp : ESP du thread user (pointe vers le frame initial)
;   [ESP+8]  = cr3 : Page Directory du processus (0 = pas de changement)
;
global jump_to_user
jump_to_user:
    cli
    
    mov ecx, [esp + 4]      ; ECX = new_esp
    mov edx, [esp + 8]      ; EDX = new_cr3
    
    ; Changer le Page Directory si cr3 != 0
    test edx, edx
    jz .skip_cr3_user
    mov cr3, edx
.skip_cr3_user:
    
    ; Charger la nouvelle stack
    mov esp, ecx
    
    ; Restaurer les registres (le frame a été créé par thread_create_user)
    popa
    
    ; Charger les segments user (on sait qu'on va vers Ring 3)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Sauter en user mode
    iretd

; void task_entry_point(void)
; ============================================================
;
; Point d'entrée pour les nouveaux threads.
; Appelé via iretd depuis le scheduler préemptif.
;
; À l'entrée (après popa + iretd):
;   EAX = adresse de la fonction entry
;   ECX = argument (void *arg)
;
; Cette fonction appelle entry(arg) puis thread_exit().
;
extern thread_exit

global task_entry_point
task_entry_point:
    ; Les interruptions sont déjà activées (EFLAGS.IF=1 via iretd)
    
    ; Pousser l'argument sur la stack pour la convention cdecl
    push ecx                ; arg sur la stack
    
    ; Appeler la fonction du thread
    call eax                ; entry(arg)
    
    ; Nettoyer l'argument
    add esp, 4
    
    ; Si la fonction retourne, terminer proprement le thread
    ; EAX contient le code de retour de entry()
    push eax                ; status = valeur de retour
    call thread_exit        ; thread_exit(status)
    
    ; On ne devrait jamais arriver ici (thread_exit ne retourne pas)
    cli
.hang:
    hlt
    jmp .hang
