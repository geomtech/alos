/* src/shell/shell.h - ALOS Command Shell */
#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

/* Taille maximale d'une ligne de commande */
#define SHELL_LINE_MAX      256

/* Nombre maximum d'arguments par commande */
#define SHELL_ARGS_MAX      16

/* Taille de l'historique des commandes */
#define SHELL_HISTORY_SIZE  16

/* Taille maximale du chemin courant */
#define SHELL_PATH_MAX      256

/**
 * Initialise le shell.
 * Configure le répertoire courant à "/" et l'historique.
 */
void shell_init(void);

/**
 * Lance la boucle principale du shell.
 * Ne retourne jamais (boucle infinie).
 */
void shell_run(void);

/**
 * Retourne le chemin du répertoire courant.
 */
const char* shell_get_cwd(void);

/**
 * Change le répertoire courant.
 * @param path Chemin absolu ou relatif
 * @return 0 si succès, -1 si erreur
 */
int shell_set_cwd(const char* path);

/**
 * Résout un chemin relatif en chemin absolu.
 * @param path   Chemin à résoudre (peut être relatif ou absolu)
 * @param result Buffer pour stocker le résultat
 * @param size   Taille du buffer
 * @return 0 si succès, -1 si erreur
 */
int shell_resolve_path(const char* path, char* result, size_t size);

#endif /* SHELL_H */
