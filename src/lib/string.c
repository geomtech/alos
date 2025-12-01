/* src/lib/string.c - String utilities implementation */
#include "../include/string.h"

/* Variable statique pour strtok */
static char* strtok_saveptr = NULL;

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strcpy(char* dest, const char* src)
{
    char* ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    char* ret = dest;
    while (n && (*dest++ = *src++)) {
        n--;
    }
    while (n--) {
        *dest++ = '\0';
    }
    return ret;
}

char* strcat(char* dest, const char* src)
{
    char* ret = dest;
    while (*dest) {
        dest++;
    }
    while ((*dest++ = *src++));
    return ret;
}

char* strtok(char* str, const char* delim)
{
    char* token_start;
    
    /* Si str est NULL, continuer depuis la position précédente */
    if (str == NULL) {
        str = strtok_saveptr;
    }
    
    if (str == NULL) {
        return NULL;
    }
    
    /* Ignorer les délimiteurs au début */
    while (*str) {
        const char* d = delim;
        int is_delim = 0;
        while (*d) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim) {
            break;
        }
        str++;
    }
    
    /* Si on est à la fin de la chaîne */
    if (*str == '\0') {
        strtok_saveptr = NULL;
        return NULL;
    }
    
    /* Début du token */
    token_start = str;
    
    /* Trouver la fin du token */
    while (*str) {
        const char* d = delim;
        while (*d) {
            if (*str == *d) {
                *str = '\0';
                strtok_saveptr = str + 1;
                return token_start;
            }
            d++;
        }
        str++;
    }
    
    /* Fin de la chaîne */
    strtok_saveptr = NULL;
    return token_start;
}

int atoi(const char* str)
{
    int result = 0;
    int sign = 1;
    
    /* Ignorer les espaces */
    while (isspace(*str)) {
        str++;
    }
    
    /* Gérer le signe */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    /* Convertir les chiffres */
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

int isspace(int c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

int isalpha(int c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

void* memset(void* ptr, int value, size_t n)
{
    unsigned char* p = (unsigned char*)ptr;
    while (n--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}
