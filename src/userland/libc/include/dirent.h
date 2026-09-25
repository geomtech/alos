#ifndef _DIRENT_H
#define _DIRENT_H

/* Types d'entrée de répertoire (doivent correspondre à VFS_FILE/VFS_DIRECTORY
 * côté kernel, cf. src/fs/vfs.h). */
#define DT_FILE 0x01
#define DT_DIR 0x02

/*
 * ALOS n'a pas (encore) de vrai opendir()/readdir()/closedir() avec un
 * descripteur DIR* : le syscall SYS_READDIR est indexé par position sur un
 * chemin donné. Cette structure et cette fonction reflètent directement
 * cette API kernel plutôt que d'émuler une sémantique POSIX complète.
 */
struct dirent {
  char d_name[256];
  unsigned int d_type; /* DT_FILE ou DT_DIR */
  unsigned int d_size; /* Taille du fichier (0 pour les répertoires) */
};

/**
 * readdir() - Lit l'entrée d'index `index` du répertoire `path`.
 *
 * @return 0 si une entrée a été lue, 1 en fin de répertoire, -1 en cas
 *         d'erreur (chemin invalide ou n'est pas un répertoire).
 */
int readdir(const char *path, unsigned int index, struct dirent *entry);

#endif
