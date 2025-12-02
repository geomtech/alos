/* src/kernel/keymap.c - Implémentation des layouts clavier */
#include "keymap.h"
#include "../include/string.h"

/* ========================================
 * Keymaps prédéfinies
 * ======================================== */

/**
 * Layout QWERTY US (clavier américain standard)
 * Scancode Set 1 -> ASCII
 */
const keymap_t keymap_qwerty_us = {
    .name = "qwerty",
    .description = "US QWERTY",
    
    /* Table normale (sans modificateur) */
    .normal = {
        0,    27,   '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',   /* 0x00-0x09 */
        '9',  '0',  '-',  '=',  '\b', '\t', 'q',  'w',  'e',  'r',   /* 0x0A-0x13 */
        't',  'y',  'u',  'i',  'o',  'p',  '[',  ']',  '\n', 0,     /* 0x14-0x1D */
        'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',   /* 0x1E-0x27 */
        '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  'b',  'n',   /* 0x28-0x31 */
        'm',  ',',  '.',  '/',  0,    '*',  0,    ' ',  0,    0,     /* 0x32-0x3B */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x3C-0x45 (F1-F10) */
        0,    0,    0,    0,    '-',  0,    0,    0,    '+',  0,     /* 0x46-0x4F */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x50-0x59 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x5A-0x63 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x64-0x6D */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x6E-0x77 */
        0,    0,    0,    0,    0,    0,    0,    0                   /* 0x78-0x7F */
    },
    
    /* Table Shift (avec Shift enfoncé) */
    .shift = {
        0,    27,   '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',   /* 0x00-0x09 */
        '(',  ')',  '_',  '+',  '\b', '\t', 'Q',  'W',  'E',  'R',   /* 0x0A-0x13 */
        'T',  'Y',  'U',  'I',  'O',  'P',  '{',  '}',  '\n', 0,     /* 0x14-0x1D */
        'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',   /* 0x1E-0x27 */
        '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',  'B',  'N',   /* 0x28-0x31 */
        'M',  '<',  '>',  '?',  0,    '*',  0,    ' ',  0,    0,     /* 0x32-0x3B */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x3C-0x45 */
        0,    0,    0,    0,    '-',  0,    0,    0,    '+',  0,     /* 0x46-0x4F */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x50-0x59 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x5A-0x63 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x64-0x6D */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x6E-0x77 */
        0,    0,    0,    0,    0,    0,    0,    0                   /* 0x78-0x7F */
    },
    
    /* Table AltGr (pas utilisée en QWERTY US) */
    .altgr = {0}
};

/**
 * Layout AZERTY FR (clavier français standard)
 * Scancode Set 1 -> ASCII/caractères français
 * Utilise Code Page 437 pour les caractères accentués.
 * 
 * Codes CP437 utilisés:
 *   é=0x82, è=0x8A, ê=0x88, ë=0x89
 *   à=0x85, â=0x83, ä=0x84
 *   ù=0x97, û=0x96, ü=0x81
 *   ç=0x87, ô=0x93, î=0x8C, ï=0x8B
 * 
 * Particularités AZERTY:
 * - Chiffres accessibles avec Shift
 * - Caractères spéciaux (&, é, ", ', etc.) en mode normal sur la rangée du haut
 * - A et Q inversés, Z et W inversés, M déplacé
 */
const keymap_t keymap_azerty_fr = {
    .name = "azerty",
    .description = "French AZERTY",
    
    /* Table normale (sans modificateur) */
    .normal = {
        0,    27,   '&',  0x82, '"',  '\'', '(',  '-',  0x8A, '_',   /* 0x00-0x09: 1=&, 2=é, 3=", 4=', 5=(, 6=-, 7=è, 8=_ */
        0x87, 0x85, ')',  '=',  '\b', '\t', 'a',  'z',  'e',  'r',   /* 0x0A-0x13: 9=ç, 0=à, )=), ==, q→a, w→z */
        't',  'y',  'u',  'i',  'o',  'p',  DEAD_KEY_CIRCUMFLEX, '$', '\n', 0, /* 0x14-0x1D: ^=dead key, $=$ */
        'q',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  'm',   /* 0x1E-0x27: a→q, ;→m */
        0x97, '*',  0,    '*',  'w',  'x',  'c',  'v',  'b',  'n',   /* 0x28-0x31: '→ù, `→*, z→w */
        ',',  ';',  ':',  '!',  0,    '*',  0,    ' ',  0,    0,     /* 0x32-0x3B: m→,, ,→;, .→:, /→! */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x3C-0x45 (F1-F10) */
        0,    0,    0,    0,    '-',  0,    0,    0,    '+',  0,     /* 0x46-0x4F */
        0,    0,    0,    0,    0,    0,    '<',  0,    0,    0,     /* 0x50-0x59: 0x56 = < (touche à gauche du W) */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x5A-0x63 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x64-0x6D */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x6E-0x77 */
        0,    0,    0,    0,    0,    0,    0,    0                   /* 0x78-0x7F */
    },
    
    /* Table Shift (avec Shift enfoncé) - Chiffres et majuscules */
    .shift = {
        0,    27,   '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',   /* 0x00-0x09: Chiffres avec Shift */
        '9',  '0',  '.',  '+',  '\b', '\t', 'A',  'Z',  'E',  'R',   /* 0x0A-0x13 */
        'T',  'Y',  'U',  'I',  'O',  'P',  DEAD_KEY_DIAERESIS, '#', '\n', 0, /* 0x14-0x1D: ¨=dead key, #=# */
        'Q',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  'M',   /* 0x1E-0x27 */
        '%',  0x9C, 0,    '|',  'W',  'X',  'C',  'V',  'B',  'N',   /* 0x28-0x31: %=%, µ=0x9C (£ en CP437, proche) */
        '?',  '.',  '/',  '!',  0,    '*',  0,    ' ',  0,    0,     /* 0x32-0x3B */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x3C-0x45 */
        0,    0,    0,    0,    '-',  0,    0,    0,    '+',  0,     /* 0x46-0x4F */
        0,    0,    0,    0,    0,    0,    '>',  0,    0,    0,     /* 0x50-0x59: > avec Shift */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x5A-0x63 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x64-0x6D */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x6E-0x77 */
        0,    0,    0,    0,    0,    0,    0,    0                   /* 0x78-0x7F */
    },
    
    /* Table AltGr (Alt droit) - Caractères spéciaux */
    .altgr = {
        0,    0,    0,    '~',  '#',  '{',  '[',  '|',  '`',  '\\',  /* 0x00-0x09: ~, #, {, [, |, `, \ */
        '^',  '@',  ']',  '}',  0,    0,    0,    0,    0,    0,     /* 0x0A-0x13: ^, @, ], } */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x14-0x1D */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x1E-0x27 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x28-0x31 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x32-0x3B */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x3C-0x45 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x46-0x4F */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x50-0x59 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x5A-0x63 */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x64-0x6D */
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x6E-0x77 */
        0,    0,    0,    0,    0,    0,    0,    0                   /* 0x78-0x7F */
    }
};

/* ========================================
 * Variables globales
 * ======================================== */

/* Keymap actuellement active */
static const keymap_t* current_keymap = &keymap_qwerty_us;

/* Table des keymaps enregistrées */
static const keymap_t* registered_keymaps[MAX_KEYMAPS];
static int keymap_count = 0;

/* ========================================
 * Implémentation des fonctions
 * ======================================== */

/**
 * Initialise le système de keymaps.
 * Enregistre les layouts par défaut.
 */
void keymap_init(void)
{
    keymap_count = 0;
    
    /* Enregistrer les keymaps par défaut */
    keymap_register(&keymap_qwerty_us);
    keymap_register(&keymap_azerty_fr);
    
    /* QWERTY par défaut */
    current_keymap = &keymap_qwerty_us;
}

/**
 * Définit la keymap active.
 */
void keymap_set(const keymap_t* map)
{
    if (map != NULL) {
        current_keymap = map;
    }
}

/**
 * Récupère la keymap actuellement active.
 */
const keymap_t* keymap_get_current(void)
{
    return current_keymap;
}

/**
 * Recherche une keymap par son nom.
 */
const keymap_t* keymap_find_by_name(const char* name)
{
    if (name == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < keymap_count; i++) {
        if (strcmp(registered_keymaps[i]->name, name) == 0) {
            return registered_keymaps[i];
        }
    }
    
    return NULL;
}

/**
 * Récupère la liste de toutes les keymaps disponibles.
 */
const keymap_t** keymap_list_all(int* count)
{
    if (count != NULL) {
        *count = keymap_count;
    }
    return registered_keymaps;
}

/**
 * Enregistre une nouvelle keymap.
 */
bool keymap_register(const keymap_t* map)
{
    if (map == NULL || keymap_count >= MAX_KEYMAPS) {
        return false;
    }
    
    /* Vérifier qu'elle n'est pas déjà enregistrée */
    for (int i = 0; i < keymap_count; i++) {
        if (registered_keymaps[i] == map) {
            return true;  /* Déjà enregistrée */
        }
    }
    
    registered_keymaps[keymap_count++] = map;
    return true;
}

/**
 * Résout une dead key avec le caractère suivant.
 * Combine l'accent avec la lettre de base.
 * Utilise le Code Page 437 (ASCII étendu VGA) pour les caractères accentués.
 */
unsigned char keymap_resolve_dead_key(unsigned char dead_key, unsigned char c)
{
    switch (dead_key) {
        case DEAD_KEY_CIRCUMFLEX:  /* ^ (accent circonflexe) */
            switch (c) {
                case 'a': return 0x83;  /* â */
                case 'e': return 0x88;  /* ê */
                case 'i': return 0x8C;  /* î */
                case 'o': return 0x93;  /* ô */
                case 'u': return 0x96;  /* û */
                case 'A': return 0x83;  /* Â (pas de majuscule en CP437, utiliser minuscule) */
                case 'E': return 0x88;  /* Ê */
                case 'I': return 0x8C;  /* Î */
                case 'O': return 0x93;  /* Ô */
                case 'U': return 0x96;  /* Û */
                case ' ': return '^';   /* Espace après ^ = ^ seul */
                default:  return c;     /* Pas de combinaison, retourner le caractère */
            }
            break;
            
        case DEAD_KEY_DIAERESIS:  /* ¨ (tréma) */
            switch (c) {
                case 'a': return 0x84;  /* ä */
                case 'e': return 0x89;  /* ë */
                case 'i': return 0x8B;  /* ï */
                case 'o': return 0x94;  /* ö */
                case 'u': return 0x81;  /* ü */
                case 'y': return 0x98;  /* ÿ */
                case 'A': return 0x8E;  /* Ä */
                case 'E': return 0x89;  /* Ë (pas de majuscule en CP437) */
                case 'I': return 0x8B;  /* Ï */
                case 'O': return 0x99;  /* Ö */
                case 'U': return 0x9A;  /* Ü */
                case ' ': return '"';   /* Espace après ¨ = " */
                default:  return c;
            }
            break;
            
        case DEAD_KEY_GRAVE:  /* ` (accent grave) */
            switch (c) {
                case 'a': return 0x85;  /* à */
                case 'e': return 0x8A;  /* è */
                case 'i': return 0x8D;  /* ì */
                case 'o': return 0x95;  /* ò */
                case 'u': return 0x97;  /* ù */
                case 'A': return 0x85;  /* À (pas de majuscule en CP437) */
                case 'E': return 0x8A;  /* È */
                case 'I': return 0x8D;  /* Ì */
                case 'O': return 0x95;  /* Ò */
                case 'U': return 0x97;  /* Ù */
                case ' ': return '`';   /* Espace après ` = ` seul */
                default:  return c;
            }
            break;
            
        case DEAD_KEY_TILDE:  /* ~ (tilde) */
            switch (c) {
                case 'n': return 0xA4;  /* ñ */
                case 'N': return 0xA5;  /* Ñ */
                case 'a': return 'a';   /* ã - pas en CP437 */
                case 'o': return 'o';   /* õ - pas en CP437 */
                case 'A': return 'A';   /* Ã */
                case 'O': return 'O';   /* Õ */
                case ' ': return '~';   /* Espace après ~ = ~ seul */
                default:  return c;
            }
            break;
            
        default:
            return c;
    }
    
    return c;
}
