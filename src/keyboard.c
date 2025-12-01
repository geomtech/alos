#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "io.h"
#include "console.h"
#include "drivers/pcnet.h"

/* Scancodes spéciaux pour les flèches */
#define SCANCODE_UP_ARROW    0x48
#define SCANCODE_DOWN_ARROW  0x50
#define SCANCODE_LEFT_ARROW  0x4B
#define SCANCODE_RIGHT_ARROW 0x4D
#define SCANCODE_PAGE_UP     0x49
#define SCANCODE_PAGE_DOWN   0x51
#define SCANCODE_ENTER       0x1C

static uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;

// Position actuelle du curseur à l'écran
static int terminal_col = 0;
static int terminal_row = 0;

// Table de correspondance Scancode Set 1 -> ASCII (Layout QWERTY US simplifié)
unsigned char kbdus[128] =
    {
        0,
        27,
        '1',
        '2',
        '3',
        '4',
        '5',
        '6',
        '7',
        '8', /* 9 */
        '9',
        '0',
        '-',
        '=',
        '\b', /* Backspace */
        '\t', /* Tab */
        'q',
        'w',
        'e',
        'r', /* 19 */
        't',
        'y',
        'u',
        'i',
        'o',
        'p',
        '[',
        ']',
        '\n', /* Enter key */
        0,    /* 29   - Control */
        'a',
        's',
        'd',
        'f',
        'g',
        'h',
        'j',
        'k',
        'l',
        ';', /* 39 */
        '\'',
        '`',
        0, /* Left shift */
        '\\',
        'z',
        'x',
        'c',
        'v',
        'b',
        'n', /* 49 */
        'm',
        ',',
        '.',
        '/',
        0, /* Right shift */
        '*',
        0,   /* Alt */
        ' ', /* Space bar */
        0,   /* Caps lock */
        0,   /* 59 - F1 key ... > */
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0, /* < ... F10 */
        0, /* 69 - Num lock*/
        0, /* Scroll Lock */
        0, /* Home key */
        0, /* Up Arrow */
        0, /* Page Up */
        '-',
        0, /* Left Arrow */
        0,
        0, /* Right Arrow */
        '+',
        0, /* 79 - End key*/
        0, /* Down Arrow */
        0, /* Page Down */
        0, /* Insert Key */
        0, /* Delete Key */
        0,
        0,
        0,
        0, /* F11 Key */
        0, /* F12 Key */
        0, /* All other keys are undefined */
};

// Fonction pour afficher un caractère et avancer le curseur
void terminal_putc(char c)
{
    if (c == '\n')
    {
        terminal_col = 0;
        terminal_row++;
    }
    else if (c == '\b')
    {
        if (terminal_col > 0)
            terminal_col--;
        const size_t index = terminal_row * VGA_WIDTH + terminal_col;
        VGA_MEMORY[index] = (uint16_t)' ' | (uint16_t)0x07 << 8; // Efface
    }
    else
    {
        const size_t index = terminal_row * VGA_WIDTH + terminal_col;
        VGA_MEMORY[index] = (uint16_t)c | (uint16_t)0x07 << 8; // 0x07 = Gris clair sur noir
        terminal_col++;
    }

    // Gestion du retour à la ligne automatique
    if (terminal_col >= VGA_WIDTH)
    {
        terminal_col = 0;
        terminal_row++;
    }

    // Si on arrive en bas, on remonte (scroll très basique : on repart en haut pour l'instant)
    if (terminal_row >= VGA_HEIGHT)
    {
        terminal_row = 0;
        terminal_col = 0;
    }
}

void keyboard_handler_c(void)
{
    // 1. Lire le scancode
    uint8_t scancode = inb(0x60);

    // 2. Vérifier si c'est un appui (Bit de poids fort à 0) ou un relâchement (Bit à 1)
    if (scancode & 0x80)
    {
        // C'est un "Break Code" (touche relâchée).
        // On ne fait RIEN ici.
    }
    else
    {
        // C'est un "Make Code" (touche appuyée).
        // Gérer les touches spéciales (flèches)
        switch (scancode) {
            case SCANCODE_UP_ARROW:
            case SCANCODE_PAGE_UP:
                console_scroll_up();
                break;
            case SCANCODE_DOWN_ARROW:
            case SCANCODE_PAGE_DOWN:
                console_scroll_down();
                break;
            case SCANCODE_ENTER:
                {
                    /* Envoyer un paquet broadcast de test */
                    PCNetDevice* pcnet = pcnet_get_device();
                    if (pcnet != NULL && pcnet->initialized) {
                        uint8_t packet[64];
                        
                        /* Destination MAC: broadcast */
                        packet[0] = 0xFF; packet[1] = 0xFF; packet[2] = 0xFF;
                        packet[3] = 0xFF; packet[4] = 0xFF; packet[5] = 0xFF;
                        
                        /* Source MAC */
                        for (int i = 0; i < 6; i++) {
                            packet[6 + i] = pcnet->mac_addr[i];
                        }
                        
                        /* EtherType: 0x0800 */
                        packet[12] = 0x08;
                        packet[13] = 0x00;
                        
                        /* Payload */
                        const char* msg = "ALOS Broadcast!";
                        for (int i = 0; msg[i] != '\0' && i < 46; i++) {
                            packet[14 + i] = msg[i];
                        }
                        
                        /* Padding */
                        for (int i = 14 + 15; i < 64; i++) {
                            packet[i] = 0;
                        }
                        
                        if (pcnet_send(pcnet, packet, 64)) {
                            console_puts("\n[Broadcast sent!]\n");
                        } else {
                            console_puts("\n[Broadcast FAILED]\n");
                        }
                        console_refresh();
                    } else {
                        terminal_putc('\n');
                    }
                }
                break;
            default:
                // Touche normale
                if (scancode < 128)
                {
                    char c = kbdus[scancode];
                    if (c != 0)
                    {
                        terminal_putc(c);
                    }
                }
                break;
        }
    }

    // 3. Acquitter l'interruption (EOI)
    outb(0x20, 0x20);
}