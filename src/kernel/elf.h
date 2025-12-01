/* src/kernel/elf.h - ELF Loader Interface */
#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include "process.h"
#include "../include/elf.h"

/* ========================================
 * Structure de résultat du chargement ELF
 * ======================================== */

typedef struct {
    uint32_t entry_point;       /* Point d'entrée du programme */
    uint32_t base_addr;         /* Adresse de base (plus basse) */
    uint32_t top_addr;          /* Adresse la plus haute */
    uint32_t num_segments;      /* Nombre de segments chargés */
} elf_load_result_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Charge un fichier ELF en mémoire pour un processus.
 * 
 * @param filename  Chemin du fichier ELF
 * @param proc      Processus cible (pour l'allocation mémoire)
 * @param result    Structure pour stocker les résultats
 * @return          0 si succès, code d'erreur négatif sinon
 */
int elf_load_file(const char* filename, process_t* proc, elf_load_result_t* result);

/**
 * Vérifie si un fichier est un exécutable ELF valide.
 * 
 * @param filename  Chemin du fichier
 * @return          1 si ELF valide, 0 sinon
 */
int elf_is_valid(const char* filename);

/**
 * Affiche les informations d'un fichier ELF (debug).
 * 
 * @param filename  Chemin du fichier
 */
void elf_info(const char* filename);

#endif /* ELF_LOADER_H */
