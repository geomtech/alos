/* src/kernel/syscall.h - System Calls Interface */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* ========================================
 * Numéros des Syscalls (Convention Linux-like)
 * ======================================== */

#define SYS_EXIT        1       /* Terminer le processus */
#define SYS_READ        3       /* Lire depuis un fichier/stdin */
#define SYS_WRITE       4       /* Écrire vers un fichier/stdout */
#define SYS_OPEN        5       /* Ouvrir un fichier */
#define SYS_GETPID      20      /* Obtenir le PID du processus courant */

/* Filesystem syscalls */
#define SYS_CLOSE       6       /* Fermer un file descriptor */
#define SYS_CHDIR       12      /* Changer de répertoire */
#define SYS_MKDIR       39      /* Créer un répertoire */
#define SYS_READDIR     89      /* Lire une entrée de répertoire */
#define SYS_GETCWD      183     /* Obtenir le répertoire courant */
#define SYS_CREATE      85      /* Créer un fichier */

/* Socket syscalls (BSD-like numbers) */
#define SYS_SOCKET      41      /* Créer un socket */
#define SYS_BIND        49      /* Lier un socket à une adresse */
#define SYS_LISTEN      50      /* Mettre un socket en écoute */
#define SYS_ACCEPT      43      /* Accepter une connexion */
#define SYS_SEND        44      /* Envoyer des données */
#define SYS_RECV        45      /* Recevoir des données */

/* System syscalls */
#define SYS_KBHIT       100     /* Vérifier si une touche est disponible (non-bloquant) */
#define SYS_CLEAR       101     /* Effacer l'écran */
#define SYS_MEMINFO     102     /* Obtenir les infos mémoire */

/* Nombre maximum de syscalls */
#define MAX_SYSCALLS    256

/* ========================================
 * Structure des registres pour syscall
 * ======================================== */

/* 
 * Structure passée au dispatcher depuis l'ASM.
 * Correspond à l'ordre des push sur la stack.
 */
typedef struct {
    /* Registres sauvegardés par pusha (ordre inverse) */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;     /* ESP sauvegardé par pusha (pas utilisé) */
    uint32_t ebx;           /* Argument 1 */
    uint32_t edx;           /* Argument 3 */
    uint32_t ecx;           /* Argument 2 */
    uint32_t eax;           /* Numéro du syscall / Valeur de retour */
    
    /* Segments sauvegardés */
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    
    /* Pushé par le CPU lors de l'interruption (depuis Ring 3) */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;      /* ESP utilisateur (seulement si changement de ring) */
    uint32_t user_ss;       /* SS utilisateur (seulement si changement de ring) */
} syscall_regs_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le système de syscalls.
 * Enregistre l'interruption 0x80 dans l'IDT.
 */
void syscall_init(void);

/**
 * Dispatcher principal des syscalls.
 * Appelé depuis syscall_handler_asm.
 * 
 * @param regs  Pointeur vers les registres sauvegardés
 */
void syscall_dispatcher(syscall_regs_t* regs);

#endif /* SYSCALL_H */
