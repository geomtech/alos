/* src/userland/libc/src/stdlib/exit.c */
#include <stdlib.h>
#include <unistd.h>

void exit(int status) { _exit(status); }
