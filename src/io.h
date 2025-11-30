/* src/io.h */
#ifndef IO_H
#define IO_H

#include <stdint.h>

// Écrire un octet sur un port
static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Lire un octet depuis un port
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0"
                 : "=a"(ret)
                 : "Nd"(port));
    return ret;
}

// Petite pause pour les vieux matériels (io_wait)
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif