/* src/io.h */
#ifndef IO_H
#define IO_H

#include <stdint.h>

/* Écrire un octet sur un port */
static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Lire un octet depuis un port */
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0"
                 : "=a"(ret)
                 : "Nd"(port));
    return ret;
}

/* Écrire un mot de 16 bits sur un port */
static inline void outw(uint16_t port, uint16_t val)
{
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Lire un mot de 16 bits depuis un port */
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    asm volatile("inw %1, %0"
                 : "=a"(ret)
                 : "Nd"(port));
    return ret;
}

/* Écrire un double mot de 32 bits sur un port */
static inline void outl(uint16_t port, uint32_t val)
{
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* Lire un double mot de 32 bits depuis un port */
static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    asm volatile("inl %1, %0"
                 : "=a"(ret)
                 : "Nd"(port));
    return ret;
}

/* Petite pause pour les vieux matériels (io_wait) */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif