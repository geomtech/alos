/* src/kernel.c */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "io.h"

/* Adresse physique du buffer VGA en mode texte couleur */
static uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

/* Génère un octet de couleur (4 bits fond, 4 bits texte) */
static inline uint8_t vga_color(uint8_t fg, uint8_t bg)
{
    return fg | bg << 4;
}

/* Génère un mot de 16 bits (caractère + couleur) pour la VRAM */
static inline uint16_t vga_entry(unsigned char uc, uint8_t color)
{
    return (uint16_t)uc | (uint16_t)color << 8;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

void timer_handler_c(void)
{
    // On ne fait rien pour l'instant, mais IL FAUT acquitter.
    // Envoyer EOI au PIC Maître
    outb(0x20, 0x20);
}

void kernel_main(void)
{
    init_gdt();
    init_idt();

    // 1. Nettoyage de l'écran (Fond Bleu, Texte Blanc pour le style "BSOD")
    uint8_t terminal_color = vga_color(15, 1); // 15=White, 1=Blue
    for (size_t y = 0; y < VGA_HEIGHT; y++)
    {
        for (size_t x = 0; x < VGA_WIDTH; x++)
        {
            const size_t index = y * VGA_WIDTH + x;
            VGA_MEMORY[index] = vga_entry(' ', terminal_color);
        }
    }

    // 2. Écriture du message
    const char *msg = "AuraOS Veteran here. Native mode engaged.";
    size_t msg_len = strlen(msg);

    // On écrit au milieu de l'écran environ
    size_t offset = (10 * VGA_WIDTH) + 20;

    for (size_t i = 0; i < msg_len; i++)
    {
        VGA_MEMORY[offset + i] = vga_entry(msg[i], terminal_color);
    }

    asm volatile("sti");

    while (1)
    {
        // Le CPU tourne ici en attendant que vous appuyiez sur une touche
        asm volatile("hlt"); // Met le CPU en pause pour économiser l'énergie
    }
}