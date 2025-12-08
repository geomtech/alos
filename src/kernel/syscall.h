/* src/kernel/syscall.h - System Calls Interface */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* ========================================
 * Numéros des Syscalls (Convention Linux-like)
 * ======================================== */

#define SYS_EXIT 1     /* Terminer le processus */
#define SYS_READ 3     /* Lire depuis un fichier/stdin */
#define SYS_WRITE 4    /* Écrire vers un fichier/stdout */
#define SYS_OPEN 5     /* Ouvrir un fichier */
#define SYS_GETPID 20  /* Obtenir le PID du processus courant */
#define SYS_FORK 57    /* Créer un nouveau processus */
#define SYS_EXECVE 59  /* Exécuter un programme */
#define SYS_WAITPID 61 /* Attendre la fin d'un processus fils */
#define SYS_THREAD_CREATE                                                      \
  60 /* Créer un nouveau thread dans le processus courant */

/* Thread syscalls */
#define SYS_CLONE 56   /* Créer un thread/processus */
#define SYS_GETTID 186 /* Obtenir le Thread ID */
#define SYS_TKILL 200  /* Terminer un thread spécifique */

/* Filesystem syscalls */
#define SYS_CLOSE 6    /* Fermer un file descriptor */
#define SYS_CHDIR 12   /* Changer de répertoire */
#define SYS_MKDIR 39   /* Créer un répertoire */
#define SYS_READDIR 89 /* Lire une entrée de répertoire */
#define SYS_GETCWD 183 /* Obtenir le répertoire courant */
#define SYS_CREATE 85  /* Créer un fichier */

/* Socket syscalls (BSD-like numbers) */
#define SYS_SOCKET 41 /* Créer un socket */
#define SYS_BIND 49   /* Lier un socket à une adresse */
#define SYS_LISTEN 50 /* Mettre un socket en écoute */
#define SYS_ACCEPT 43 /* Accepter une connexion */
#define SYS_SEND 44   /* Envoyer des données */
#define SYS_RECV 45   /* Recevoir des données */

/* System syscalls */
#define SYS_KBHIT                                                              \
  100                 /* Vérifier si une touche est disponible (non-bloquant) \
                       */
#define SYS_CLEAR 101 /* Effacer l'écran */
#define SYS_MEMINFO 102         /* Obtenir les infos mémoire */
#define SYS_GET_FRAMEBUFFER 110 /* Obtenir les infos framebuffer */
#define SYS_GET_EVENT 111       /* Obtenir un événement input */

/* Nombre maximum de syscalls */
#define MAX_SYSCALLS 256

/* ========================================
 * Blocking Syscall Support
 * ======================================== */

/* État d'un syscall bloquant */
typedef enum {
  SYSCALL_STATE_RUNNING,   /* Syscall en cours */
  SYSCALL_STATE_BLOCKED,   /* Syscall bloqué, attente événement */
  SYSCALL_STATE_COMPLETED, /* Syscall terminé, résultat disponible */
} syscall_state_t;

/* Structure Info Framebuffer (pour SYS_GET_FRAMEBUFFER) */
typedef struct {
  uint64_t addr; /* Adresse virtuelle (mappée en userland) */
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint16_t bpp;
  uint16_t red_mask_size;
  uint16_t red_mask_shift;
  uint16_t green_mask_size;
  uint16_t green_mask_shift;
  uint16_t blue_mask_size;
  uint16_t blue_mask_shift;
} framebuffer_info_t;

/* Types d'événements */
#define EVENT_NONE 0
#define EVENT_KEY_DOWN 1
#define EVENT_KEY_UP 2
#define EVENT_MOUSE_MOVE 3
#define EVENT_MOUSE_BTN 4
#define EVENT_MOUSE_SCROLL 5

/* Structure Événement Input */
typedef struct {
  uint32_t type;
  uint32_t time;
  union {
    struct {
      uint32_t key;
      uint32_t scancode;
      uint32_t flags;
    } key;
    struct {
      int32_t x;
      int32_t y;
      int32_t dx;
      int32_t dy;
      uint32_t buttons;
    } mouse;
  } data;
} input_event_t;

/* Context de reprise pour syscalls bloquants */
typedef struct syscall_context {
  syscall_state_t state; /* État du syscall */
  uint32_t syscall_num;  /* Numéro du syscall */
  int result;            /* Résultat (quand completed) */

  /* Arguments sauvegardés pour reprise */
  uint64_t arg0;
  uint64_t arg1;
  uint64_t arg2;
  uint64_t arg3;

  /* Contexte spécifique au syscall (union) */
  union {
    struct {
      int listen_fd;
      uint16_t port;
    } accept_ctx;

    struct {
      int fd;
      uint8_t *buf;
      int len;
    } recv_ctx;
  };
} syscall_context_t;

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
  uint64_t r10; /* Argument 4 */
  uint64_t r9;  /* Argument 6 */
  uint64_t r8;  /* Argument 5 */
  uint64_t rdi; /* Argument 1 */
  uint64_t rsi; /* Argument 2 */
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rdx; /* Argument 3 */
  uint64_t rcx;
  uint64_t rax; /* Numéro du syscall / Valeur de retour */

  /* Pushé par le stub ISR */
  uint64_t int_no;
  uint64_t error_code;

  /* Pushé par le CPU lors de l'interruption */
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp; /* RSP utilisateur */
  uint64_t ss;  /* SS utilisateur */
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
void syscall_dispatcher(syscall_regs_t *regs);

/* ========================================
 * Fonctions syscall exportées (pour linux_compat)
 * ======================================== */

/**
 * Syscalls exportés pour utilisation par linux_compat.
 * Ces fonctions sont des wrappers publics autour des implémentations internes.
 */
void syscall_do_exit(int status);
int syscall_do_read(int fd, void *buf, uint64_t count);
int syscall_do_write(int fd, const void *buf, uint64_t count);
int syscall_do_open(const char *path, uint64_t flags);
int syscall_do_close(int fd);
int syscall_do_getpid(void);
int syscall_do_getcwd(char *buf, uint64_t size);
int syscall_do_chdir(const char *path);
int syscall_do_mkdir(const char *path);

/* Fonctions syscall exportées pour les nouveaux syscalls */
int syscall_do_fork(void);
int syscall_do_execve(const char *filename, char **argv, char **envp);
int syscall_do_waitpid(int pid, int *status, int options);

#endif /* SYSCALL_H */
