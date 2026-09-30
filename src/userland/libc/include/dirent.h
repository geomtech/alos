#ifndef _DIRENT_H
#define _DIRENT_H

#include <stdint.h>
#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

typedef struct alos_directory_stream DIR;
struct dirent {
  uint64_t d_ino;
  off_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};

/* ABI historique indexee par chemin : conservee sous un nom distinct. */
#define ALOS_DT_FILE 0x01
#define ALOS_DT_DIR 0x02
struct alos_dirent {
  char d_name[256];
  unsigned int d_type; /* ALOS_DT_FILE ou ALOS_DT_DIR */
  unsigned int d_size; /* Taille du fichier (0 pour les répertoires) */
};

#ifdef __cplusplus
extern "C" {
#endif
DIR *opendir(const char *);
DIR *fdopendir(int);
struct dirent *readdir(DIR *);
int closedir(DIR *);
int dirfd(DIR *);

/**
 * alos_readdir() - Lit l'entrée d'index `index` du répertoire `path`.
 *
 * @return 0 si une entrée a été lue, 1 en fin de répertoire, -1 en cas
 *         d'erreur (chemin invalide ou n'est pas un répertoire).
 */
int alos_readdir(const char *path, unsigned int index, struct alos_dirent *entry);

#ifdef __cplusplus
}
#endif
#endif
