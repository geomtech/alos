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
; LEGACY: Gardé pour compatibilité, appelle switch_context.
;
global switch_task
switch_task:
    ; Ignorer new_cr3 (tous les threads kernel partagent le même espace)
    ; Juste appeler switch_context
    push dword [esp + 8]    ; new_esp
    push dword [esp + 8]    ; old_esp_ptr (offset +4 car on a pushé)
    call switch_context
    add esp, 8
    ret
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
