/* src/lib/user.h - User Space Library */
#ifndef USER_H
#define USER_H

#include <stdint.h>

/* ========================================
 * Numéros des Syscalls
 * ======================================== */

#define SYS_EXIT        1
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_GETPID      20

/* ========================================
 * Fonction syscall générique
 * ======================================== */

/**
 * Déclenche un syscall via INT 0x80.
 * 
 * @param num   Numéro du syscall (dans EAX)
 * @param arg1  Premier argument (dans EBX)
 * @param arg2  Deuxième argument (dans ECX)
 * @param arg3  Troisième argument (dans EDX)
 * @return      Valeur de retour du syscall (dans EAX)
 */
static inline int syscall(int num, int arg1, int arg2, int arg3)
{
    int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)                         /* Output: EAX = retour */
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)  /* Inputs */
        : "memory"                          /* Clobbers */
    );
    return ret;
}

/* ========================================
 * Wrappers haut niveau
 * ======================================== */

/**
 * Termine le processus avec un code de sortie.
 * Ne retourne jamais.
 */
static inline void exit(int status)
{
    syscall(SYS_EXIT, status, 0, 0);
    /* Ne devrait jamais arriver ici */
    while (1);
}

/**
 * Affiche une chaîne de caractères null-terminated.
 */
static inline int print(const char* str)
{
    return syscall(SYS_WRITE, 0, (int)str, 0);
}

/**
 * Affiche exactement n caractères.
 */
static inline int write(int fd, const char* buf, uint32_t count)
{
    return syscall(SYS_WRITE, fd, (int)buf, (int)count);
}

/**
 * Retourne le PID du processus courant.
 */
static inline int getpid(void)
{
    return syscall(SYS_GETPID, 0, 0, 0);
}

#endif /* USER_H */
