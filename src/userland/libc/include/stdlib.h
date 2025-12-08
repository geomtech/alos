#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);

void exit(int status);
int atoi(const char *nptr);
char *itoa(int value, char *str, int base);

#endif
