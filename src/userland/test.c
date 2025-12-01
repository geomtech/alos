/* userland/test.c */
int main()
{
    /* On suppose que le syscall write est le n°4 et stdout le n°1 */
    /* Attention : il faudra peut-être adapter selon votre implémentation syscall */
    char *msg = "Hello from ELF executable!\n";

    /* Syscall inline (ou includez votre lib syscall si vous l'avez sortie) */
    asm volatile(
        "int $0x80"
        :
        : "a"(4), "b"(1), "c"(msg), "d"(27));

    /* Syscall exit */
    asm volatile("int $0x80" : : "a"(1), "b"(0));

    return 0;
}