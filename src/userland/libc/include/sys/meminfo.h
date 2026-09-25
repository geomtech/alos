#ifndef _SYS_MEMINFO_H
#define _SYS_MEMINFO_H

#include <stdint.h>

/* Doit correspondre à meminfo_t côté kernel (src/kernel/syscall.c). */
struct meminfo {
  uint32_t total_size;
  uint32_t free_size;
  uint32_t block_count;
  uint32_t free_block_count;
};

/**
 * meminfo() - Récupère les statistiques du tas noyau via SYS_MEMINFO.
 *
 * @return 0 si succès, -1 si erreur.
 */
int meminfo(struct meminfo *info);

#endif
