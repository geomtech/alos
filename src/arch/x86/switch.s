; src/arch/x86/switch.s - Context Switch Assembly
; 
; Ce fichier contient la fonction magique qui permet de passer
; d'un processus à un autre en sauvegardant/restaurant les registres.

[BITS 32]

section .text

; ============================================================
; void switch_task(uint32_t* old_esp_ptr, uint32_t new_esp)
; ============================================================
; 
; Paramètres (cdecl calling convention):
;   [ESP+4]  = old_esp_ptr : Pointeur où sauvegarder l'ESP actuel
;   [ESP+8]  = new_esp     : Nouvel ESP à charger
;
; Cette fonction :
;   1. Push les registres callee-saved du processus actuel
;   2. Sauvegarde l'ESP dans *old_esp_ptr
;   3. Charge le new_esp dans ESP
;   4. Pop les registres du nouveau processus
;   5. Retourne (ret saute à l'EIP du nouveau processus)
;
; Après l'appel, on se retrouve dans le contexte du nouveau processus !
;
global switch_task
switch_task:
    ; ===== Sauvegarder le contexte du processus actuel =====
    
    ; Push les registres callee-saved (ceux qu'on doit préserver)
    ; L'ordre est important : on les pop dans l'ordre inverse
    push ebx
    push esi
    push edi
    push ebp
    
    ; ===== Sauvegarder l'ESP actuel =====
    
    ; Récupérer les arguments AVANT de modifier ESP
    ; Note: ESP a changé à cause des push, donc l'offset est différent
    ; [ESP]    = EBP (qu'on vient de push)
    ; [ESP+4]  = EDI
    ; [ESP+8]  = ESI
    ; [ESP+12] = EBX
    ; [ESP+16] = Adresse de retour (return address)
    ; [ESP+20] = old_esp_ptr (premier argument)
    ; [ESP+24] = new_esp (deuxième argument)
    
    mov eax, [esp + 20]     ; EAX = old_esp_ptr
    mov ecx, [esp + 24]     ; ECX = new_esp (sauvegarder AVANT de changer ESP!)
    mov [eax], esp          ; *old_esp_ptr = ESP actuel
    
    ; ===== Charger le contexte du nouveau processus =====
    
    ; Charger le nouvel ESP depuis ECX (pas depuis la stack!)
    mov esp, ecx            ; ESP = new_esp
                            ; On est maintenant sur la stack du nouveau processus
    
    ; Pop les registres du nouveau processus
    ; (dans l'ordre inverse de comment ils ont été push)
    pop ebp
    pop edi
    pop esi
    pop ebx
    
    ; ===== Retourner dans le nouveau processus =====
    
    ; ret va dépiler l'adresse de retour (EIP) de la stack
    ; et sauter à cette adresse.
    ; Pour un nouveau thread, c'est l'adresse de la fonction thread.
    ; Pour un thread existant, c'est l'adresse après son dernier schedule().
    ret


; ============================================================
; void task_entry_point(void)
; ============================================================
;
; Point d'entrée pour les nouveaux threads.
; Cette fonction est appelée quand un thread démarre pour la première fois.
; Elle appelle la fonction du thread, puis process_exit() à la fin.
;
extern process_exit

global task_entry_point
task_entry_point:
    ; À ce point, la stack contient :
    ; [ESP]   = Adresse de la fonction du thread
    ; [ESP+4] = Argument (si besoin, pour plus tard)
    
    ; Activer les interruptions (désactivées pendant le switch)
    sti
    
    ; Récupérer l'adresse de la fonction
    pop eax                 ; EAX = adresse de la fonction
    
    ; Appeler la fonction du thread
    call eax
    
    ; Si la fonction retourne, terminer proprement le thread
    call process_exit
    
    ; On ne devrait jamais arriver ici
    cli
.hang:
    hlt
    jmp .hang
