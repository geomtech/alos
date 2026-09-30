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
#define SYS_SPAWN_WAIT 201 /* Lancer un programme et attendre sa terminaison (shell userland) */


/* Filesystem syscalls */
#define SYS_CLOSE 6    /* Fermer un file descriptor */
#define SYS_LSEEK 19
#define SYS_FSTAT 28
#define SYS_STAT 106
#define SYS_UNLINK 10  /* Supprimer un fichier */
#define SYS_CHDIR 12   /* Changer de répertoire */
#define SYS_MKDIR 39   /* Créer un répertoire */
#define SYS_RMDIR 40   /* Supprimer un répertoire vide */
#define SYS_READDIR 89 /* Lire une entrée de répertoire */
#define SYS_MMAP 90
#define SYS_MUNMAP 91
#define SYS_MPROTECT 125
#define SYS_MADVISE 219
#define SYS_ERRNO_LOCATION 220
#define SYS_VMINFO 221
#define SYS_CLOCK_GETTIME 222
#define SYS_NANOSLEEP_POSIX 223
#define SYS_GET_FS 224
#define SYS_SET_FS 225
#define SYS_CONTEXT_SWITCHES 226
#define SYS_FUTEX_WAIT 227
#define SYS_FUTEX_WAKE 228
#define SYS_THREAD_JOIN 229
#define SYS_THREAD_DETACH 230
#define SYS_THREAD_REGISTER 231
#define SYS_EXIT_GROUP 232
#define SYS_ISATTY 233
#define SYS_THREAD_STACK 234 /* Bornes de la pile principale ELF du processus */
#define SYS_READDIR_FD 235
#define SYS_FCNTL 236
#define SYS_DUP 237
#define SYS_DUP2 238
#define SYS_UNAME 260
#define SYS_NATIVE_SOCKET 240
#define SYS_NATIVE_POLL 241
#define SYS_NATIVE_EPOLL 242
#define SYS_FSYNC 250
#define SYS_PIPE2 251
#define SYS_ACCESS 252
#define SYS_MKDIR_MODE 253
#define SYS_GETENTROPY 261
#define SYS_PREAD 262
#define SYS_PWRITE 263
#define SYS_FTRUNCATE 264
#define SYS_MINCORE 265
#define SYS_SYSTEM_INFO 266
#define SYS_RESOLVE_IPV4 267
#define SYS_READV 270
#define SYS_WRITEV 271
#define SYS_IOCTL 272
#define SYS_SHM_CREATE_NATIVE 280
#define SYS_SHM_READONLY 281
#define SYS_SHM_INFO 282
#define SYS_SHM_SAME 283
#define SYS_RESOURCE_LIMIT 290
#define SYS_RESOURCE_SET_LIMIT 291
#define SYS_THREAD_NICE 292
#define SYS_STATVFS 300
#define SYS_FUTIMES 301
#define SYS_SET_PROCESS_TITLE 302
#define SYS_MSYNC 303
#define SYS_PROCESS_TERMINATE 304
#define SYS_PROCESS_WAIT 305
#define SYS_PROCESS_QUERY 306
#define SYS_NATIVE_PATH 310
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
#define SYS_KBHIT 100                 /* Vérifier si une touche est disponible (non-bloquant) */
#define SYS_CLEAR 101                 /* Effacer l'écran */
#define SYS_MEMINFO 102               /* Obtenir les infos mémoire */
#define SYS_PS_INFO 103               /* Obtenir la liste des processus/threads */
#define SYS_PING 104                  /* Envoyer une requête ping ICMP */
#define SYS_WGET 105                  /* Télécharger un fichier HTTP */
#define SYS_HTTPD 107                 /* Contrôle du serveur HTTP */
#define SYS_GET_FRAMEBUFFER 110       /* Obtenir les infos framebuffer */
#define SYS_GET_EVENT 111             /* Obtenir un événement input */
#define SYS_WAIT_EVENT 112            /* Attendre un événement input (bloquant) */
#define SYS_BRK 120                   /* Modifier la taille du segment de données du processus */
#define SYS_NANOSLEEP 162             /* Dormir pendant un temps spécifié */
#define SYS_GET_MICROSECONDS 163      /* Obtenir le temps en microsecondes */
#define SYS_SLEEP_MICROS 164          /* Dormir pendant un temps spécifié en microsecondes */

/* Structure pour SYS_PS_INFO */
typedef struct {
  uint32_t pid;
  uint32_t tid;
  char name[32];
  uint32_t state;
  uint64_t rip;
  uint64_t rsp;
} proc_info_t;

/* Structure pour SYS_READDIR */
typedef struct {
  char name[256];
  uint32_t type;
  uint32_t size;
} userspace_dirent_t;

/* Structure pour SYS_MEMINFO */
typedef struct {
  uint32_t total_size;
  uint32_t free_size;
  uint32_t block_count;
  uint32_t free_block_count;
} meminfo_t;

/* IPC local, memoire partagee et serveur d'affichage */
#define SYS_IPC_LISTEN 202
#define SYS_IPC_CONNECT 203
#define SYS_IPC_ACCEPT 204
#define SYS_IPC_SEND 205
#define SYS_IPC_RECV 206
#define SYS_SHM_CREATE 207
#define SYS_SHM_MAP 208
#define SYS_SHM_UNMAP 209
#define SYS_DISPLAY_ACQUIRE 210
#define SYS_DISPLAY_RELEASE 211
#define SYS_SHM_SIZE 212

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

/**
 * Configure et lit le répertoire de travail courant pour les syscalls.
 */
void syscall_set_cwd(const char *path);
const char *syscall_get_cwd(void);

#endif /* SYSCALL_H */
