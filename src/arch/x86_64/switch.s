; src/arch/x86_64/switch.s - Context Switch for x86-64
;
; This file contains context switch routines for the scheduler.
; Uses the System V AMD64 ABI calling convention.

[BITS 64]

section .text

; ============================================
; void switch_context(uint64_t* old_rsp_ptr, uint64_t new_rsp)
; ============================================
; Cooperative context switch (yield).
; Saves current context and switches to new thread.
;
; Parameters (System V ABI):
;   RDI = old_rsp_ptr : Pointer to save current RSP
;   RSI = new_rsp     : New RSP to load
;
global switch_context
switch_context:
    ; CRITICAL: Save RFLAGS FIRST to prevent TF/IF leakage between threads
    pushfq
    
    ; Save callee-saved registers (System V ABI)
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; Save current RSP to *old_rsp_ptr
    mov [rdi], rsp
    
    ; Load new RSP
    mov rsp, rsi
    
    ; Restore callee-saved registers (LIFO order)
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    
    ; CRITICAL: Restore RFLAGS LAST (matches push order)
    popfq
    
    ; Return to new thread
    ret

; ============================================
; void switch_task(uint64_t* old_rsp_ptr, uint64_t new_rsp, uint64_t new_cr3)
; ============================================
; Context switch with FORMAT IRQ UNIFIÉ.
; Sauvegarde le contexte au même format que les IRQ handlers pour permettre
; la préemption transparente.
;
; Parameters (System V ABI):
;   RDI = old_rsp_ptr : Pointer to save current RSP
;   RSI = new_rsp     : New RSP to load
;   RDX = new_cr3     : New page table (0 = no change)
;
; Stack layout sauvegardé pour un contexte Ring 0:
;   [RFLAGS, CS, RIP, error_code, int_no, RAX...R15]
;
; IMPORTANT: pour un retour IRETQ Ring0->Ring0, le CPU ne dépile PAS RSP/SS.
; Les frames utilisateur initiaux conservent bien [RSP, SS], car IRETQ les
; dépile lors d'un changement de privilège Ring0->Ring3.
;
global switch_task
switch_task:
    ; Disable interrupts during switch
    cli
    
    ; Sauvegarder les paramètres avant de modifier la stack
    mov r8, rdi             ; r8 = old_rsp_ptr
    mov r9, rsi             ; r9 = new_rsp
    mov r10, rdx            ; r10 = new_cr3
    
    ; === Construire un fake IRETQ frame Ring 0 ===
    ; On simule un contexte interrompu en Ring 0.
    ; Pour un IRETQ Ring0->Ring0, seules RIP, CS et RFLAGS sont dépilées.
    ; Ajouter RSP/SS ici décale la pile de 16 octets à la reprise et finit
    ; par corrompre l'adresse de retour du code C.
    
    ; Lire RFLAGS actuel
    pushfq
    pop r11                 ; r11 = RFLAGS
    
    ; Le RIP de retour est déjà sur la stack (poussé par CALL).
    pop rax                 ; rax = return address (RIP)
    
    ; Frame IRETQ Ring 0: RFLAGS, CS, RIP
    push r11                ; RFLAGS
    push qword 0x08         ; CS (kernel code)
    push rax                ; RIP (adresse de retour)
    
    ; error_code et int_no (dummy)
    push qword 0            ; error_code
    push qword 0xFF         ; int_no (0xFF = yield, pour debug)
    
    ; === PUSH_ALL (15 registres, même ordre que interrupts.s) ===
    push rax                ; RAX (déjà utilisé, mais on le sauvegarde)
    push rcx                ; RCX
    push rdx                ; RDX (original, pas r10)
    push rbx                ; RBX
    push rbp                ; RBP
    push rsi                ; RSI (original, pas r9)
    push rdi                ; RDI (original, pas r8)
    push r8                 ; R8 (contient old_rsp_ptr mais on le sauvegarde)
    push r9                 ; R9 (contient new_rsp mais on le sauvegarde)
    push r10                ; R10 (contient new_cr3 mais on le sauvegarde)
    push r11                ; R11 (contient RFLAGS mais on le sauvegarde)
    push r12                ; R12
    push r13                ; R13
    push r14                ; R14
    push r15                ; R15
    
    ; Sauvegarder RSP actuel dans *old_rsp_ptr
    mov [r8], rsp
    
    ; Changer CR3 si nécessaire
    test r10, r10
    jz .skip_cr3
    mov cr3, r10
.skip_cr3:
    
    ; Charger le nouveau RSP
    mov rsp, r9
    
    ; === POP_ALL (15 registres, ordre inverse) ===
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
    
    ; Skip int_no et error_code
    add rsp, 16
    
    ; Si la frame cible retourne en Ring 3, charger les segments utilisateur.
    ; Après POP_ALL + skip, [rsp+8] contient CS.
    test qword [rsp + 8], 0x03
    jz .iret_target
    push rax
    mov ax, 0x1B            ; User Data
    mov ds, ax
    mov es, ax
    pop rax

.iret_target:
    ; IRETQ dépile RIP/CS/RFLAGS et, seulement lors d'un changement de
    ; privilège, RSP/SS.
    iretq

; ============================================
; void jump_to_user(uint64_t rsp, uint64_t rip, uint64_t cr3)
; ============================================
; Jump to user mode without saving current context.
; Used for initial process startup.
;
; Parameters (System V ABI):
;   RDI = rsp : User stack pointer
;   RSI = rip : User instruction pointer
;   RDX = cr3 : User page table (0 = no change)
;
global jump_to_user
jump_to_user:
    cli
    
    ; Debug: write to serial port BEFORE cr3 change
    push rax
    push rdx
    mov al, 'B'
    mov dx, 0x3F8
    out dx, al
    mov al, 'C'
    out dx, al
    mov al, 'R'
    out dx, al
    mov al, '3'
    out dx, al
    mov al, 10
    out dx, al
    pop rdx
    pop rax
    
    ; Change page table if cr3 != 0
    test rdx, rdx
    jz .skip_cr3_user
    mov cr3, rdx
    
.skip_cr3_user:
    
    ; Build iretq frame
    ; Stack layout for iretq:
    ;   [RSP+32] SS
    ;   [RSP+24] RSP
    ;   [RSP+16] RFLAGS
    ;   [RSP+8]  CS
    ;   [RSP+0]  RIP
    
    push qword 0x1B         ; SS = User Data (0x18 | 3)
    push rdi                ; RSP = User stack
    push qword 0x202        ; RFLAGS = IF=1 (interrupts enabled)
    push qword 0x23         ; CS = User Code (0x20 | 3)
    push rsi                ; RIP = User entry point
    
    ; Clear registers for security
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    ; Load user data segments
    mov ax, 0x1B            ; User Data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Debug: write to serial port before iretq
    mov al, 'U'
    mov dx, 0x3F8
    out dx, al
    mov al, 'S'
    out dx, al
    mov al, 'R'
    out dx, al
    mov al, 10
    out dx, al
    
    ; Jump to user mode!
    iretq

; ============================================
; void task_entry_point(void)
; ============================================
; Entry point for new kernel threads.
; Called after context switch with:
;   R12 = entry function pointer
;   R13 = argument (void*)
;
global task_entry_point
extern thread_exit

task_entry_point:
    ; Enable interrupts
    sti
    
    ; Call entry(arg)
    mov rdi, r13            ; First argument
    call r12                ; Call entry function
    
    ; If function returns, exit thread
    mov rdi, rax            ; Exit status = return value
    call thread_exit
    
    ; Should never reach here
    cli
.hang:
    hlt
    jmp .hang

; ============================================
; uint64_t read_rsp(void)
; ============================================
; Returns current RSP value.
;
global read_rsp
read_rsp:
    mov rax, rsp
    ret

; ============================================
; uint64_t read_rbp(void)
; ============================================
; Returns current RBP value.
;
global read_rbp
read_rbp:
    mov rax, rbp
    ret
