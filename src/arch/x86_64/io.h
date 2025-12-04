/* src/arch/x86_64/io.h - Port I/O functions for x86-64 */
#ifndef X86_64_IO_H
#define X86_64_IO_H

#include <stdint.h>

/* ========================================
 * Port I/O - Inline Assembly
 * ======================================== */

/**
 * Write a byte to an I/O port.
 */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Read a byte from an I/O port.
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Write a word (16 bits) to an I/O port.
 */
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Read a word (16 bits) from an I/O port.
 */
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Write a dword (32 bits) to an I/O port.
 */
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Read a dword (32 bits) from an I/O port.
 */
static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Wait for an I/O operation to complete.
 * Uses port 0x80 (POST diagnostic port) which is safe.
 */
static inline void io_wait(void)
{
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

/* ========================================
 * String I/O Operations
 * ======================================== */

/**
 * Read multiple words from an I/O port.
 */
static inline void insw(uint16_t port, void *addr, uint32_t count)
{
    __asm__ volatile("rep insw"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/**
 * Write multiple words to an I/O port.
 */
static inline void outsw(uint16_t port, const void *addr, uint32_t count)
{
    __asm__ volatile("rep outsw"
                     : "+S"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/**
 * Read multiple dwords from an I/O port.
 */
static inline void insl(uint16_t port, void *addr, uint32_t count)
{
    __asm__ volatile("rep insl"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/**
 * Write multiple dwords to an I/O port.
 */
static inline void outsl(uint16_t port, const void *addr, uint32_t count)
{
    __asm__ volatile("rep outsl"
                     : "+S"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/* ========================================
 * CPU Control
 * ======================================== */

/**
 * Halt the CPU until the next interrupt.
 */
static inline void cpu_halt(void)
{
    __asm__ volatile("hlt");
}

/**
 * Disable interrupts.
 */
static inline void cli(void)
{
    __asm__ volatile("cli");
}

/**
 * Enable interrupts.
 */
static inline void sti(void)
{
    __asm__ volatile("sti");
}

/**
 * Read the RFLAGS register.
 */
static inline uint64_t read_rflags(void)
{
    uint64_t rflags;
    __asm__ volatile("pushfq; popq %0" : "=r"(rflags));
    return rflags;
}

/**
 * Check if interrupts are enabled.
 */
static inline int interrupts_enabled(void)
{
    return (read_rflags() & 0x200) != 0;
}

#endif /* X86_64_IO_H */
