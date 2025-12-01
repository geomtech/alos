/* src/kernel/keyboard.c - Keyboard driver with input buffer */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../arch/x86/io.h"
#include "console.h"
#include "keyboard.h"

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

/* Codes spéciaux pour le shell (non-ASCII) */
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_CTRL_C  0x03   /* ASCII ETX (End of Text) */

/* État des modificateurs */
static volatile bool ctrl_pressed = false;
static volatile bool shift_pressed = false;

/* Buffer circulaire pour les caractères */
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int kb_head = 0;  /* Position d'écriture */
static volatile int kb_tail = 0;  /* Position de lecture */

/* Table de correspondance Scancode Set 1 -> ASCII (Layout QWERTY US) */
static unsigned char kbdus[128] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',   /* 0x00-0x09 */
    '9',  '0',  '-',  '=',  '\b', '\t', 'q',  'w',  'e',  'r',   /* 0x0A-0x13 */
    't',  'y',  'u',  'i',  'o',  'p',  '[',  ']',  '\n', 0,     /* 0x14-0x1D (0x1C = Enter, 0x1D = Ctrl) */
    'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',   /* 0x1E-0x27 */
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  'b',  'n',   /* 0x28-0x31 (0x2A = LShift) */
    'm',  ',',  '.',  '/',  0,    '*',  0,    ' ',  0,    0,     /* 0x32-0x3B (0x38 = Alt, 0x3A = Caps) */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x3C-0x45 (F1-F10) */
    0,    0,    0,    0,    '-',  0,    0,    0,    '+',  0,     /* 0x46-0x4F (NumLock, etc.) */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x50-0x59 */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x5A-0x63 */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x64-0x6D */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,     /* 0x6E-0x77 */
    0,    0,    0,    0,    0,    0,    0,    0                   /* 0x78-0x7F */
};

/**
 * Ajoute un caractère dans le buffer circulaire.
 */
static void keyboard_buffer_put(char c)
{
    int next_head = (kb_head + 1) % KEYBOARD_BUFFER_SIZE;
    
    /* Si le buffer est plein, on ignore le caractère */
    if (next_head != kb_tail) {
        keyboard_buffer[kb_head] = c;
        kb_head = next_head;
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
 * Attend avec hlt jusqu'à ce qu'un caractère soit disponible.
 */
char keyboard_getchar(void)
{
    while (!keyboard_has_char()) {
        /* Activer les interruptions et attendre */
        asm volatile("sti");
        asm volatile("hlt");
    }
    return keyboard_buffer_get();
}

/**
 * Handler d'interruption clavier (IRQ1).
 * Stocke les caractères dans le buffer au lieu de les afficher.
 */
void keyboard_handler_c(void)
{
    /* 1. Lire le scancode */
    uint8_t scancode = inb(0x60);

    /* 2. Gérer les modificateurs (appui et relâchement) */
    
    /* Relâchement de touche (bit 7 = 1) */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        switch (released) {
            case SCANCODE_LCTRL:
                ctrl_pressed = false;
                break;
            case SCANCODE_LSHIFT:
            case SCANCODE_RSHIFT:
                shift_pressed = false;
                break;
        }
        /* Acquitter et sortir */
        outb(0x20, 0x20);
        return;
    }
    
    /* Appui de touche (bit 7 = 0) */
    switch (scancode) {
        case SCANCODE_LCTRL:
            ctrl_pressed = true;
            break;
            
        case SCANCODE_LSHIFT:
        case SCANCODE_RSHIFT:
            shift_pressed = true;
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
            /* Touche normale */
            if (scancode < 128) {
                char c = kbdus[scancode];
                if (c != 0) {
                    /* Vérifier CTRL+C */
                    if (ctrl_pressed && (c == 'c' || c == 'C')) {
                        /* Juste mettre CTRL+C dans le buffer - le shell gère le reste */
                        keyboard_buffer_put(KEY_CTRL_C);
                    }
                    /* Gérer majuscules avec Shift */
                    else if (shift_pressed && c >= 'a' && c <= 'z') {
                        keyboard_buffer_put(c - 32);  /* Convertir en majuscule */
                    }
                    else {
                        keyboard_buffer_put(c);
                    }
                }
            }
            break;
    }

    /* 3. Acquitter l'interruption (EOI au PIC) */
    outb(0x20, 0x20);
}
