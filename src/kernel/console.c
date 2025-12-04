/* src/console.c - Console wrapper for framebuffer console */
#include "console.h"
#include "fb_console.h"
#include "thread.h"

/* Spinlock pour protéger l'accès concurrent à la console */
static spinlock_t console_lock;
static bool initialized = false;

/* Helpers pour la gestion des interruptions */
static inline uint64_t save_flags(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

static inline void restore_flags(uint64_t flags) {
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

static inline void local_cli(void) {
    __asm__ volatile("cli");
}

/* Legacy function - no longer needed */
void console_set_hhdm_offset(uint64_t hhdm_offset) {
    (void)hhdm_offset;
}

/* Initialize framebuffer console */
void console_init_fb(struct limine_framebuffer *fb) {
    if (fb != NULL) {
        fb_console_init(fb);
        initialized = true;
    }
}

void console_init(void)
{
    spinlock_init(&console_lock);
}

void console_clear(uint8_t bg_color)
{
    if (!initialized) return;
    
    uint64_t flags = save_flags();
    local_cli();
    spinlock_lock(&console_lock);
    
    fb_console_clear(vga_to_fb_color[bg_color & 0x0F]);
    
    spinlock_unlock(&console_lock);
    restore_flags(flags);
}

void console_set_color(uint8_t fg, uint8_t bg)
{
    if (!initialized) return;
    fb_console_set_vga_color(fg, bg);
}

void console_putc(char c)
{
    if (!initialized) return;
    
    uint64_t flags = save_flags();
    local_cli();
    spinlock_lock(&console_lock);
    
    fb_console_putc(c);
    
    spinlock_unlock(&console_lock);
    restore_flags(flags);
}

void console_puts(const char* str)
{
    if (!initialized) return;
    
    uint64_t flags = save_flags();
    local_cli();
    spinlock_lock(&console_lock);
    
    fb_console_puts(str);
    
    spinlock_unlock(&console_lock);
    restore_flags(flags);
}

void console_put_hex(uint32_t value)
{
    const char hex_chars[] = "0123456789ABCDEF";
    console_putc('0');
    console_putc('x');
    
    for (int i = 7; i >= 0; i--) {
        console_putc(hex_chars[(value >> (i * 4)) & 0xF]);
    }
}

void console_put_hex_byte(uint8_t value)
{
    const char hex_chars[] = "0123456789ABCDEF";
    console_putc(hex_chars[(value >> 4) & 0xF]);
    console_putc(hex_chars[value & 0xF]);
}

void console_put_dec(uint32_t value)
{
    if (value == 0) {
        console_putc('0');
        return;
    }
    
    char buffer[12];
    int i = 0;
    
    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    while (i > 0) {
        console_putc(buffer[--i]);
    }
}

void console_scroll_up(void)
{
    /* Not implemented for framebuffer */
}

void console_scroll_down(void)
{
    /* Not implemented for framebuffer */
}

void console_refresh(void)
{
    if (!initialized) return;
    fb_console_refresh();
}

int console_get_view_line(void)
{
    return 0;
}

int console_get_current_line(void)
{
    return 0;
}

void console_disable_hw_cursor(void)
{
    /* No hardware cursor with framebuffer */
}

void console_show_cursor(int show)
{
    (void)show;
}

void console_update_cursor(void)
{
    /* Cursor handled by fb_console */
}
