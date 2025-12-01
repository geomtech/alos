/* userland/hello.c - Simple Hello World pour ALOS */

/* Numéros de syscalls ALOS */
#define SYS_EXIT    1
#define SYS_WRITE   4

/* Fonction syscall inline */
static inline int syscall(int num, int arg1, int arg2, int arg3)
{
    int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}

/* Afficher une chaîne (null-terminated) */
static void print(const char* str)
{
    syscall(SYS_WRITE, 0, (int)str, 0);
}

/* Terminer le programme */
static void exit(int code)
{
    syscall(SYS_EXIT, code, 0, 0);
    /* Ne retourne jamais */
    while(1);
}

/* Point d'entrée */
int main(void)
{
    print("\n");
    print("  *************************************\n");
    print("  *                                   *\n");
    print("  *   Hello from ALOS User Space!    *\n");
    print("  *                                   *\n");
    print("  *   This ELF was loaded by the     *\n");
    print("  *   kernel's ELF loader and is     *\n");
    print("  *   running in Ring 3 (User Mode)  *\n");
    print("  *                                   *\n");
    print("  *************************************\n");
    print("\n");
    
    exit(0);
    
    return 0;  /* Jamais atteint */
}
