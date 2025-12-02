/* src/kernel/keymap.h - Abstraction des layouts clavier */
#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdint.h>
#include <stdbool.h>

/* Nombre maximum de keymaps enregistrées */
#define MAX_KEYMAPS 8

/* Caractère spécial pour les dead keys (touches mortes) */
#define DEAD_KEY_CIRCUMFLEX  0xF0  /* ^ (accent circonflexe) */
#define DEAD_KEY_DIAERESIS   0xF1  /* ¨ (tréma) */
#define DEAD_KEY_GRAVE       0xF2  /* ` (accent grave) */
#define DEAD_KEY_TILDE       0xF3  /* ~ (tilde) */

/**
 * Structure représentant un layout de clavier (keymap).
 * Contient les tables de mapping scancode -> caractère.
 */
typedef struct {
    const char* name;           /* Nom du layout (ex: "qwerty", "azerty") */
    const char* description;    /* Description (ex: "US QWERTY", "French AZERTY") */
    unsigned char normal[128];  /* Mapping sans modificateur */
    unsigned char shift[128];   /* Mapping avec Shift */
    unsigned char altgr[128];   /* Mapping avec AltGr (Alt droit) */
} keymap_t;

/**
 * Initialise le système de keymaps.
 * Enregistre les layouts par défaut (QWERTY, AZERTY).
 */
void keymap_init(void);

/**
 * Définit la keymap active.
 * @param map Pointeur vers la keymap à activer
 */
void keymap_set(const keymap_t* map);

/**
 * Récupère la keymap actuellement active.
 * @return Pointeur vers la keymap active
 */
const keymap_t* keymap_get_current(void);

/**
 * Recherche une keymap par son nom.
 * @param name Nom de la keymap (ex: "qwerty", "azerty")
 * @return Pointeur vers la keymap ou NULL si non trouvée
 */
const keymap_t* keymap_find_by_name(const char* name);

/**
 * Récupère la liste de toutes les keymaps disponibles.
 * @param count Pointeur pour stocker le nombre de keymaps
 * @return Tableau de pointeurs vers les keymaps
 */
const keymap_t** keymap_list_all(int* count);

/**
 * Enregistre une nouvelle keymap.
 * @param map Pointeur vers la keymap à enregistrer
 * @return true si l'enregistrement a réussi, false si la table est pleine
 */
bool keymap_register(const keymap_t* map);

/**
 * Résout une dead key avec le caractère suivant.
 * @param dead_key Le code de la dead key (DEAD_KEY_*)
 * @param c Le caractère de base
 * @return Le caractère accentué ou le caractère de base si pas de combinaison
 */
unsigned char keymap_resolve_dead_key(unsigned char dead_key, unsigned char c);

/* Keymaps prédéfinies (disponibles après keymap_init) */
extern const keymap_t keymap_qwerty_us;
extern const keymap_t keymap_azerty_fr;

#endif /* KEYMAP_H */
