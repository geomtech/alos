/* src/kernel/linux_compat.h - Linux Binary Compatibility Layer */
#ifndef LINUX_COMPAT_H
#define LINUX_COMPAT_H

#include <stdint.h>
#include "syscall.h"

/* ========================================
 * Numéros de syscalls Linux (i386)
 * Référence: Linux kernel arch/x86/entry/syscalls/syscall_32.tbl
 * ======================================== */

/* Process management */
#define LINUX_SYS_EXIT          1
#define LINUX_SYS_FORK          2
#define LINUX_SYS_READ          3
#define LINUX_SYS_WRITE         4
#define LINUX_SYS_OPEN          5
#define LINUX_SYS_CLOSE         6
#define LINUX_SYS_WAITPID       7
#define LINUX_SYS_EXECVE        11
#define LINUX_SYS_CHDIR         12
#define LINUX_SYS_TIME          13
#define LINUX_SYS_GETPID        20
#define LINUX_SYS_GETUID        24
#define LINUX_SYS_ACCESS        33
#define LINUX_SYS_KILL          37
#define LINUX_SYS_MKDIR         39
#define LINUX_SYS_RMDIR         40
#define LINUX_SYS_BRK           45
#define LINUX_SYS_GETGID        47
#define LINUX_SYS_GETEUID       49
#define LINUX_SYS_GETEGID       50
#define LINUX_SYS_IOCTL         54
#define LINUX_SYS_FCNTL         55

/* Memory management */
#define LINUX_SYS_MUNMAP        91
#define LINUX_SYS_MMAP          90
#define LINUX_SYS_MMAP2         192

/* File operations */
#define LINUX_SYS_GETCWD        183
#define LINUX_SYS_STAT          106
#define LINUX_SYS_LSTAT         107
#define LINUX_SYS_FSTAT         108
#define LINUX_SYS_READDIR       89
#define LINUX_SYS_GETDENTS      141
#define LINUX_SYS_GETDENTS64    220

/* Socket operations (Linux socketcall multiplex) */
#define LINUX_SYS_SOCKETCALL    102

/* Subcodes for socketcall */
#define LINUX_SYS_SOCKET        1
#define LINUX_SYS_BIND          2
#define LINUX_SYS_CONNECT       3
#define LINUX_SYS_LISTEN        4
#define LINUX_SYS_ACCEPT        5
#define LINUX_SYS_GETSOCKNAME   6
#define LINUX_SYS_GETPEERNAME   7
#define LINUX_SYS_SOCKETPAIR    8
#define LINUX_SYS_SEND          9
#define LINUX_SYS_RECV          10
#define LINUX_SYS_SENDTO        11
#define LINUX_SYS_RECVFROM      12
#define LINUX_SYS_SHUTDOWN      13
#define LINUX_SYS_SETSOCKOPT    14
#define LINUX_SYS_GETSOCKOPT    15
#define LINUX_SYS_SENDMSG       16
#define LINUX_SYS_RECVMSG       17

/* Signal handling */
#define LINUX_SYS_SIGNAL        48
#define LINUX_SYS_SIGACTION     67
#define LINUX_SYS_SIGRETURN     119
#define LINUX_SYS_RT_SIGACTION  174
#define LINUX_SYS_RT_SIGRETURN  173

/* Other */
#define LINUX_SYS_UNAME         122
#define LINUX_SYS_NANOSLEEP     162
#define LINUX_SYS_CLOCK_GETTIME 265
#define LINUX_SYS_EXIT_GROUP    252

/* ========================================
 * Flags compatibilité Linux
 * ======================================== */

/* Open flags (compatible avec Linux) */
#define LINUX_O_RDONLY      00000000
#define LINUX_O_WRONLY      00000001
#define LINUX_O_RDWR        00000002
#define LINUX_O_CREAT       00000100
#define LINUX_O_EXCL        00000200
#define LINUX_O_TRUNC       00001000
#define LINUX_O_APPEND      00002000
#define LINUX_O_NONBLOCK    00004000
#define LINUX_O_DIRECTORY   00200000

/* Socket domains */
#define LINUX_AF_UNIX       1
#define LINUX_AF_INET       2
#define LINUX_AF_INET6      10

/* Socket types */
#define LINUX_SOCK_STREAM   1
#define LINUX_SOCK_DGRAM    2
#define LINUX_SOCK_RAW      3

/* ========================================
 * Structures Linux
 * ======================================== */

/* Structure stat Linux (simplifié) */
struct linux_stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
};

/* Structure utsname Linux */
struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

/* Linux dirent structure */
struct linux_dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char d_name[256];
};

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise la couche de compatibilité Linux.
 */
void linux_compat_init(void);

/**
 * Dispatcher pour les syscalls Linux.
 * Appelé depuis syscall_dispatcher quand un binaire Linux est détecté.
 * 
 * @param regs  Pointeur vers les registres sauvegardés
 * @return Valeur de retour du syscall (ou code d'erreur négatif)
 */
int32_t linux_syscall_handler(syscall_regs_t* regs);

/**
 * Configure le processus courant comme "Linux mode".
 * Cela active le mapping des syscalls Linux.
 * 
 * @param enable  1 pour activer, 0 pour désactiver
 */
void linux_compat_set_mode(int enable);

/**
 * Vérifie si le processus courant est en mode Linux.
 * 
 * @return 1 si mode Linux actif, 0 sinon
 */
int linux_compat_is_active(void);

#endif /* LINUX_COMPAT_H */
