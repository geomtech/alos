/* src/arch/x86/usermode.h - User Mode (Ring 3) Support */
#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

/* ========================================
 * Constantes
 * ======================================== */

#define USER_STACK_SIZE     8192    /* Taille de la stack utilisateur (8 KiB = 2 pages) */
#define USER_CODE_SEGMENT   0x1B    /* Sélecteur User Code (index 3, RPL=3) */
#define USER_DATA_SEGMENT   0x23    /* Sélecteur User Data (index 4, RPL=3) */

/* ========================================
 * Fonctions ASM externes
 * ======================================== */

/**
 * Effectue le saut vers le mode utilisateur (Ring 3).
 * 
 * @param user_esp  Stack pointer pour le mode utilisateur
 * @param user_eip  Adresse de la fonction à exécuter en Ring 3
 */
extern void enter_usermode(uint32_t user_esp, uint32_t user_eip);

/* ========================================
 * Fonctions C
 * ======================================== */

/**
 * Initialise le support User Mode (TSS, etc.).
 * À appeler après init_gdt().
 */
void init_usermode(void);

/**
 * Lance une fonction en mode utilisateur.
 * Alloue une stack, configure le TSS, et saute en Ring 3.
 * 
 * @param function  Fonction à exécuter en mode utilisateur
 * @param user_esp  Pointeur de stack utilisateur (NULL = allocation automatique)
 */
void jump_to_usermode(void (*function)(void), void* user_esp);

/**
 * Fonction de test pour le mode utilisateur.
 * Fait une simple boucle infinie (pas de syscall pour l'instant).
 */
void user_mode_test(void);

#endif /* USERMODE_H */
