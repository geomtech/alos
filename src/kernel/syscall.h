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
 * Structure des registres pour syscall (x86-64)
 * ======================================== */

/* 
 * Structure passée au dispatcher depuis l'ASM.
 * Correspond à l'ordre des push sur la stack en x86-64.
 * 
 * Convention System V AMD64 pour syscalls:
 * - RAX = numéro du syscall
 * - RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5, R9 = arg6
 * - Retour dans RAX
 */
typedef struct {
    /* Registres sauvegardés (ordre inverse des push) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;           /* Argument 4 */
    uint64_t r9;            /* Argument 6 */
    uint64_t r8;            /* Argument 5 */
    uint64_t rdi;           /* Argument 1 */
    uint64_t rsi;           /* Argument 2 */
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;           /* Argument 3 */
    uint64_t rcx;
    uint64_t rax;           /* Numéro du syscall / Valeur de retour */
    
    /* Pushé par le stub ISR */
    uint64_t int_no;
    uint64_t error_code;
    
    /* Pushé par le CPU lors de l'interruption */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;           /* RSP utilisateur */
    uint64_t ss;            /* SS utilisateur */
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

/* ========================================
 * Fonctions syscall exportées (pour linux_compat)
 * ======================================== */

/**
 * Syscalls exportés pour utilisation par linux_compat.
 * Ces fonctions sont des wrappers publics autour des implémentations internes.
 */
void syscall_do_exit(int status);
int syscall_do_read(int fd, void* buf, uint64_t count);
int syscall_do_write(int fd, const void* buf, uint64_t count);
int syscall_do_open(const char* path, uint64_t flags);
int syscall_do_close(int fd);
int syscall_do_getpid(void);
int syscall_do_getcwd(char* buf, uint64_t size);
int syscall_do_chdir(const char* path);
int syscall_do_mkdir(const char* path);

#endif /* SYSCALL_H */
