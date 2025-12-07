/* src/include/string.h - String utilities */
#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

/**
 * Calcule la longueur d'une chaîne.
 */
size_t strlen(const char* str);

/**
 * Compare deux chaînes.
 * @return 0 si égales, <0 si s1 < s2, >0 si s1 > s2
 */
int strcmp(const char* s1, const char* s2);

/**
 * Compare les n premiers caractères de deux chaînes.
 */
int strncmp(const char* s1, const char* s2, size_t n);

/**
 * Copie une chaîne.
 */
char* strcpy(char* dest, const char* src);

/**
 * Copie au maximum n caractères.
 */
char* strncpy(char* dest, const char* src, size_t n);

/**
 * Concatène deux chaînes.
 */
char* strcat(char* dest, const char* src);

/**
 * Tokenize une chaîne (similaire à strtok).
 * @param str     Chaîne à tokenizer (ou NULL pour continuer)
 * @param delim   Délimiteurs
 * @return Pointeur vers le prochain token ou NULL
 * @note Cette fonction n'est pas thread-safe. Utiliser strtok_r à la place.
 */
char* strtok(char* str, const char* delim);

/**
 * Tokenize une chaîne de manière réentrante (thread-safe).
 * @param str     Chaîne à tokenizer (ou NULL pour continuer)
 * @param delim   Délimiteurs
 * @param saveptr Pointeur pour sauvegarder l'état entre les appels
 * @return Pointeur vers le prochain token ou NULL
 */
char* strtok_r(char* str, const char* delim, char** saveptr);

/**
 * Convertit une chaîne en entier.
 */
int atoi(const char* str);

/**
 * Vérifie si un caractère est un espace.
 */
int isspace(int c);

/**
 * Vérifie si un caractère est un chiffre.
 */
int isdigit(int c);

/**
 * Vérifie si un caractère est une lettre.
 */
int isalpha(int c);

/**
 * Remplit une zone mémoire avec une valeur.
 */
void* memset(void* ptr, int value, size_t n);

/**
 * Copie une zone mémoire.
 */
void* memcpy(void* dest, const void* src, size_t n);

/**
 * Compare deux zones mémoire.
 */
int memcmp(const void* s1, const void* s2, size_t n);

/**
 * Recherche la première occurrence d'un caractère dans une chaîne.
 */
char* strchr(const char* str, int c);

/**
 * Recherche la dernière occurrence d'un caractère dans une chaîne.
 */
char* strrchr(const char* str, int c);

/**
 * Recherche une sous-chaîne dans une chaîne.
 */
char* strstr(const char* haystack, const char* needle);

/**
 * Concatène deux chaînes avec une limite de taille.
 */
char* strncat(char* dest, const char* src, size_t n);

/**
 * Déplace une zone mémoire (sécurisé pour les chevauchements).
 */
void* memmove(void* dest, const void* src, size_t n);

/**
 * Duplique une chaîne.
 * @note L'appelant doit libérer la mémoire
 */
char* strdup(const char* str);

/**
 * Recherche un caractère dans une zone mémoire.
 */
void* memchr(const void* ptr, int c, size_t n);

#endif /* STRING_H */
