/* src/lib/string.c - String utilities implementation */
#include "../include/string.h"
#include "umalloc.h"

/* Variable statique pour strtok (obsolète, utiliser strtok_r) */
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
    /* Version obsolète - utiliser strtok_r à la place */
    static char* saveptr = NULL;
    return strtok_r(str, delim, &saveptr);
}

char* strtok_r(char* str, const char* delim, char** saveptr)
{
    char* token_start;

    /* Si str est NULL, continuer depuis la position précédente */
    if (str == NULL) {
        str = *saveptr;
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
        *saveptr = NULL;
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
                *saveptr = str + 1;
                return token_start;
            }
            d++;
        }
        str++;
    }

    /* Fin de la chaîne */
    *saveptr = NULL;
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
    uint8_t* p = (uint8_t*)ptr;
    uint8_t val = (uint8_t)value;

    // Si la taille est suffisante et l'adresse alignée sur 64 bits
    if (n >= 8 && ((uintptr_t)p & 7) == 0) {
        size_t n64 = n / 8;
        uint64_t val64 = val * 0x0101010101010101ULL; // Répète le byte sur 8 octets
        uint64_t* p64 = (uint64_t*)p;

        while (n64--) {
            *p64++ = val64;
        }

        // Mise à jour du pointeur pour le reste
        p = (uint8_t*)p64;
        n %= 8;
    }

    while (n--) {
        *p++ = val;
    }
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    // Si la taille est suffisante et les adresses alignées sur 64 bits
    if (n >= 8 && ((uintptr_t)d & 7) == 0 && ((uintptr_t)s & 7) == 0) {
        size_t n64 = n / 8;
        uint64_t* d64 = (uint64_t*)d;
        const uint64_t* s64 = (const uint64_t*)s;

        while (n64--) {
            *d64++ = *s64++;
        }

        // Mise à jour des pointeurs pour le reste
        d = (uint8_t*)d64;
        s = (const uint8_t*)s64;
        n %= 8;
    }

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

/**
 * Recherche la première occurrence d'un caractère dans une chaîne.
 * @param str Chaîne à parcourir
 * @param c Caractère à rechercher
 * @return Pointeur vers le caractère trouvé ou NULL
 */
char* strchr(const char* str, int c)
{
    while (*str != '\0') {
        if (*str == (char)c) {
            return (char*)str;
        }
        str++;
    }
    return NULL;
}

/**
 * Recherche la dernière occurrence d'un caractère dans une chaîne.
 * @param str Chaîne à parcourir
 * @param c Caractère à rechercher
 * @return Pointeur vers le caractère trouvé ou NULL
 */
char* strrchr(const char* str, int c)
{
    char* last = NULL;
    while (*str != '\0') {
        if (*str == (char)c) {
            last = (char*)str;
        }
        str++;
    }
    if ((char)c == '\0') {
        return (char*)str;
    }
    return last;
}

/**
 * Recherche une sous-chaîne dans une chaîne.
 * @param haystack Chaîne principale
 * @param needle Sous-chaîne à rechercher
 * @return Pointeur vers le début de la sous-chaîne ou NULL
 */
char* strstr(const char* haystack, const char* needle)
{
    if (*needle == '\0') {
        return (char*)haystack;
    }

    while (*haystack != '\0') {
        const char* h = haystack;
        const char* n = needle;

        while (*n != '\0' && *h == *n) {
            h++;
            n++;
        }

        if (*n == '\0') {
            return (char*)haystack;
        }

        haystack++;
    }

    return NULL;
}

/**
 * Concatène deux chaînes avec une limite de taille.
 * @param dest Chaîne de destination
 * @param src Chaîne source
 * @param n Nombre maximum de caractères à concaténer
 * @return Pointeur vers la chaîne de destination
 */
char* strncat(char* dest, const char* src, size_t n)
{
    char* ret = dest;

    /* Aller à la fin de dest */
    while (*dest) {
        dest++;
    }

    /* Copier au maximum n caractères de src */
    while (n && (*dest++ = *src++)) {
        n--;
    }

    /* Ajouter le terminateur nul */
    *dest = '\0';

    return ret;
}

/**
 * Déplace une zone mémoire (sécurisé pour les chevauchements).
 * @param dest Destination
 * @param src Source
 * @param n Nombre d'octets à copier
 * @return Pointeur vers la destination
 */
void* memmove(void* dest, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (d < s) {
        /* Copie vers l'avant */
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* Copie vers l'arrière pour éviter l'écrasement */
        d += n;
        s += n;
        size_t i = n;
        while (i--) {
            *--d = *--s;
        }
    }

    return dest;
}

/**
 * Duplique une chaîne.
 * @param str Chaîne à dupliquer
 * @return Pointeur vers la nouvelle chaîne ou NULL en cas d'erreur
 * @note L'appelant doit libérer la mémoire
 */
char* strdup(const char* str)
{
    size_t len = strlen(str) + 1;
    char* new_str = (char*)umalloc(len);
    if (new_str) {
        memcpy(new_str, str, len);
    }
    return new_str;
}

/**
 * Recherche un caractère dans une zone mémoire.
 * @param ptr Zone mémoire à parcourir
 * @param c Caractère à rechercher
 * @param n Nombre d'octets à parcourir
 * @return Pointeur vers le caractère trouvé ou NULL
 */
void* memchr(const void* ptr, int c, size_t n)
{
    const unsigned char* p = (const unsigned char*)ptr;
    unsigned char uc = (unsigned char)c;

    while (n--) {
        if (*p == uc) {
            return (void*)p;
        }
        p++;
    }

    return NULL;
}
