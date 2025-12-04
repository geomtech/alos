/* src/kernel/keyboard.c - Keyboard driver with input buffer */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../arch/x86_64/io.h"
#include "console.h"
#include "keyboard.h"
#include "keymap.h"
#include "thread.h"
#include "sync.h"
#include "klog.h"

/* Sémaphore pour synchronisation avec l'IRQ clavier */
semaphore_t keyboard_sem;
static bool keyboard_sem_initialized = false;

/* Scancodes spéciaux */
#define SCANCODE_UP_ARROW    0x48
#define SCANCODE_DOWN_ARROW  0x50
#define SCANCODE_LEFT_ARROW  0x4B
#define SCANCODE_RIGHT_ARROW 0x4D
#define SCANCODE_PAGE_UP     0x49
#define SCANCODE_PAGE_DOWN   0x51
#define SCANCODE_ENTER       0x1C
#define SCANCODE_BACKSPACE   0x0E
#define SCANCODE_LCTRL       0x1D
#define SCANCODE_LSHIFT      0x2A
#define SCANCODE_RSHIFT      0x36
#define SCANCODE_LALT        0x38
#define SCANCODE_CAPSLOCK    0x3A
#define SCANCODE_E0_PREFIX   0xE0  /* Préfixe pour touches étendues */

/* Codes spéciaux pour le shell (non-ASCII) */
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_CTRL_C  0x03   /* ASCII ETX (End of Text) */
#define KEY_CTRL_D  0x04   /* ASCII EOT (End of Transmission) */

/* État des modificateurs */
static volatile bool ctrl_pressed = false;
static volatile bool shift_pressed = false;
static volatile bool alt_pressed = false;
static volatile bool altgr_pressed = false;  /* Alt droit (AltGr) */
static volatile bool capslock_active = false;
static volatile bool e0_prefix = false;      /* Préfixe E0 reçu */

/* État des dead keys (touches mortes) */
static volatile unsigned char pending_dead_key = 0;

/* Buffer circulaire pour les caractères */
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile size_t kb_head = 0;  /* Position d'écriture */
static volatile size_t kb_tail = 0;  /* Position de lecture */

/**
 * Ajoute un caractère dans le buffer circulaire.
 * Signale le sémaphore pour réveiller les threads en attente.
 */
static void keyboard_buffer_put(char c)
{
    size_t next_head = (kb_head + 1) % KEYBOARD_BUFFER_SIZE;
    
    /* Si le buffer est plein, on ignore le caractère */
    if (next_head != kb_tail) {
        keyboard_buffer[kb_head] = c;
        kb_head = next_head;
        
        /* Signaler qu'une touche est disponible.
         * sem_post() est safe depuis IRQ context. */
        if (keyboard_sem_initialized) {
            sem_post(&keyboard_sem);
        }
    }
}

/**
 * Lit un caractère du buffer (non-bloquant).
 * @return Le caractère ou 0 si buffer vide
 */
static char keyboard_buffer_get(void)
{
    if (kb_head == kb_tail) {
        return 0;  /* Buffer vide */
    }
    
    char c = keyboard_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

/**
 * Vérifie si un caractère est disponible.
 */
bool keyboard_has_char(void)
{
    return kb_head != kb_tail;
}

/**
 * Vide le buffer clavier.
 */
void keyboard_clear_buffer(void)
{
    kb_head = 0;
    kb_tail = 0;
}

/**
 * Lit un caractère du buffer (bloquant).
 * Bloque le thread jusqu'à ce qu'une touche soit disponible.
 * Utilise un sémaphore pour une synchronisation efficace avec l'IRQ.
 */
char keyboard_getchar(void)
{
    /* Initialiser le sémaphore si pas encore fait.
     * On ne peut pas le faire dans keyboard_init() car sync n'est pas encore prêt. */
    if (!keyboard_sem_initialized) {
        semaphore_init(&keyboard_sem, 0, KEYBOARD_BUFFER_SIZE);
        keyboard_sem_initialized = true;
    }
    
    /* Attendre qu'une touche soit disponible.
     * sem_wait() bloque le thread et libère le CPU aux autres threads.
     * L'IRQ clavier appellera sem_post() pour nous réveiller. */
    sem_wait(&keyboard_sem);
    
    /* Une touche est maintenant disponible */
    return keyboard_buffer_get();
}

/**
 * Lit un caractère du buffer (non-bloquant).
 * @return Le caractère lu, ou 0 si aucun caractère disponible
 */
char keyboard_getchar_nonblock(void)
{
    if (keyboard_has_char()) {
        return keyboard_buffer_get();
    }
    return 0;
}

/**
 * Change le layout clavier actif.
 * @param name Nom du layout ("qwerty", "azerty", etc.)
 * @return true si le layout a été changé, false si non trouvé
 */
bool keyboard_set_layout(const char* name)
{
    const keymap_t* km = keymap_find_by_name(name);
    if (km != NULL) {
        keymap_set(km);
        /* Réinitialiser les dead keys en attente */
        pending_dead_key = 0;
        return true;
    }
    return false;
}

/**
 * Récupère le nom du layout clavier actif.
 * @return Nom du layout actuel
 */
const char* keyboard_get_layout(void)
{
    const keymap_t* km = keymap_get_current();
    return km ? km->name : "unknown";
}

/**
 * Handler d'interruption clavier (IRQ1).
 * Stocke les caractères dans le buffer au lieu de les afficher.
 * Supporte: layouts multiples (via keymap), Caps Lock, AltGr, dead keys
 */
void keyboard_handler_c(void)
{
    /* 1. Lire le scancode */
    uint8_t scancode = inb(0x60);
    
    /* Debug: log keyboard IRQ (disabled to reduce spam) */
    /* KLOG_INFO_HEX("KBD", "IRQ scancode: ", scancode); */

    /* 2. Gérer le préfixe E0 (touches étendues: AltGr, flèches, etc.) */
    if (scancode == SCANCODE_E0_PREFIX) {
        e0_prefix = true;
        return;  /* EOI sent by irq_handler */
    }

    /* 3. Gérer les modificateurs (appui et relâchement) */
    
    /* Relâchement de touche (bit 7 = 1) */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        
        if (e0_prefix) {
            /* Touche étendue relâchée */
            if (released == SCANCODE_LALT) {
                altgr_pressed = false;  /* AltGr (Alt droit) relâché */
            }
            e0_prefix = false;
        } else {
            switch (released) {
                case SCANCODE_LCTRL:
                    ctrl_pressed = false;
                    break;
                case SCANCODE_LSHIFT:
                case SCANCODE_RSHIFT:
                    shift_pressed = false;
                    break;
                case SCANCODE_LALT:
                    alt_pressed = false;
                    break;
            }
        }
        /* EOI sent by irq_handler */
        return;
    }
    
    /* Appui de touche (bit 7 = 0) */
    
    /* Gérer AltGr (Alt droit avec préfixe E0) */
    if (e0_prefix) {
        if (scancode == SCANCODE_LALT) {
            altgr_pressed = true;
            e0_prefix = false;
            return;  /* EOI sent by irq_handler */
        }
        /* Autres touches étendues (flèches, etc.) */
        e0_prefix = false;
    }
    
    switch (scancode) {
        case SCANCODE_LCTRL:
            ctrl_pressed = true;
            break;
            
        case SCANCODE_LSHIFT:
        case SCANCODE_RSHIFT:
            shift_pressed = true;
            break;
            
        case SCANCODE_LALT:
            alt_pressed = true;
            break;
            
        case SCANCODE_CAPSLOCK:
            capslock_active = !capslock_active;  /* Toggle Caps Lock */
            break;
            
        case SCANCODE_UP_ARROW:
            keyboard_buffer_put(KEY_UP);
            break;
            
        case SCANCODE_DOWN_ARROW:
            keyboard_buffer_put(KEY_DOWN);
            break;
            
        case SCANCODE_LEFT_ARROW:
            keyboard_buffer_put(KEY_LEFT);
            break;
            
        case SCANCODE_RIGHT_ARROW:
            keyboard_buffer_put(KEY_RIGHT);
            break;
            
        case SCANCODE_PAGE_UP:
            console_scroll_up();
            break;
            
        case SCANCODE_PAGE_DOWN:
            console_scroll_down();
            break;
            
        default:
            /* Touche normale - utiliser la keymap active */
            if (scancode < 128) {
                const keymap_t* km = keymap_get_current();
                unsigned char c;
                
                /* Sélectionner la table selon les modificateurs */
                if (altgr_pressed && km->altgr[scancode] != 0) {
                    c = km->altgr[scancode];
                } else if (shift_pressed) {
                    c = km->shift[scancode];
                } else {
                    c = km->normal[scancode];
                }
                
                if (c != 0) {
                    /* Vérifier si c'est une dead key */
                    if (c >= DEAD_KEY_CIRCUMFLEX && c <= DEAD_KEY_TILDE) {
                        pending_dead_key = c;
                        /* Ne pas mettre dans le buffer, attendre le prochain caractère */
                    }
                    /* Vérifier CTRL+C */
                    else if (ctrl_pressed && (c == 'c' || c == 'C')) {
                        keyboard_buffer_put(KEY_CTRL_C);
                    }
                    /* Vérifier CTRL+D */
                    else if (ctrl_pressed && (c == 'd' || c == 'D')) {
                        keyboard_buffer_put(KEY_CTRL_D);
                    }
                    else {
                        /* Appliquer dead key si en attente */
                        if (pending_dead_key != 0) {
                            c = keymap_resolve_dead_key(pending_dead_key, c);
                            pending_dead_key = 0;
                        }
                        
                        /* Gérer Caps Lock pour les lettres */
                        if (c >= 'a' && c <= 'z') {
                            /* Caps Lock inverse l'état shift pour les lettres */
                            if (capslock_active != shift_pressed) {
                                c = c - 32;  /* Convertir en majuscule */
                            }
                        } else if (c >= 'A' && c <= 'Z') {
                            /* Déjà majuscule (shift pressé), Caps Lock l'inverse */
                            if (capslock_active) {
                                c = c + 32;  /* Convertir en minuscule */
                            }
                        }
                        
                        keyboard_buffer_put(c);
                    }
                }
            }
            break;
    }

    /* Note: EOI is sent by irq_handler() in idt.c, not here */
}
