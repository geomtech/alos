/* src/console.c - Console virtuelle avec scrolling */
#include "console.h"
#include "thread.h"
#include "../arch/x86/io.h"

/* Ports VGA pour le contrôle du curseur hardware */
#define VGA_CTRL_REG    0x3D4
#define VGA_DATA_REG    0x3D5

/* Helpers pour la gestion des interruptions */
static inline uint32_t save_flags(void) {
    uint32_t flags;
    asm volatile("pushfl; popl %0" : "=r"(flags));
    return flags;
}

static inline void restore_flags(uint32_t flags) {
    asm volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static inline void cli(void) {
    asm volatile("cli");
}

/* Spinlock pour protéger l'accès concurrent à la console */
static spinlock_t console_lock;

/* Buffer VGA physique */
static uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

/* Buffer virtuel de la console (100 lignes x 80 colonnes) */
static uint16_t console_buffer[CONSOLE_BUFFER_LINES * VGA_WIDTH];

/* Position d'écriture dans le buffer */
static int write_col = 0;
static int write_line = 0;

/* Ligne de début de la vue (pour le scrolling) */
static int view_start_line = 0;

/* Couleur courante */
static uint8_t current_color = 0x0F; /* Blanc sur noir par défaut */

/* État du curseur logiciel */
static int cursor_visible = 1;       /* Curseur visible par défaut */
static int cursor_char_saved = 0;    /* Caractère sous le curseur sauvegardé */
static uint16_t saved_char = 0;      /* Caractère/couleur sauvegardé */

/* Forward declaration */
void console_update_cursor(void);
void console_disable_hw_cursor(void);
void console_refresh(void);

/* Génère un octet de couleur */
static inline uint8_t make_color(uint8_t fg, uint8_t bg)
{
    return fg | (bg << 4);
}

/* Génère une entrée VGA (caractère + couleur) */
static inline uint16_t make_vga_entry(char c, uint8_t color)
{
    return (uint16_t)c | ((uint16_t)color << 8);
}

void console_init(void)
{
    /* Initialiser le spinlock de la console */
    spinlock_init(&console_lock);
    
    write_col = 0;
    write_line = 0;
    view_start_line = 0;
    current_color = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    cursor_visible = 1;
    cursor_char_saved = 0;
    
    /* Désactiver le curseur VGA hardware */
    console_disable_hw_cursor();
    
    /* Initialiser le buffer avec des espaces */
    for (int i = 0; i < CONSOLE_BUFFER_LINES * VGA_WIDTH; i++) {
        console_buffer[i] = make_vga_entry(' ', current_color);
    }
}

void console_clear(uint8_t bg_color)
{
    uint32_t flags = save_flags();
    cli();
    spinlock_lock(&console_lock);
    
    uint8_t color = make_color(VGA_COLOR_WHITE, bg_color);
    current_color = color;
    
    for (int i = 0; i < CONSOLE_BUFFER_LINES * VGA_WIDTH; i++) {
        console_buffer[i] = make_vga_entry(' ', color);
    }
    
    write_col = 0;
    write_line = 0;
    view_start_line = 0;
    
    console_refresh();
    
    spinlock_unlock(&console_lock);
    restore_flags(flags);
}

void console_set_color(uint8_t fg, uint8_t bg)
{
    current_color = make_color(fg, bg);
}

/* Version interne sans locking */
static void console_putc_unlocked(char c)
{
    if (c == '\n') {
        write_col = 0;
        write_line++;
    } else if (c == '\r') {
        write_col = 0;
    } else if (c == '\b') {
        /* Backspace - reculer d'une position */
        if (write_col > 0) {
            write_col--;
        }
    } else if (c == '\t') {
        write_col = (write_col + 8) & ~7;
    } else {
        int index = write_line * VGA_WIDTH + write_col;
        if (index < CONSOLE_BUFFER_LINES * VGA_WIDTH) {
            console_buffer[index] = make_vga_entry(c, current_color);
        }
        write_col++;
    }
    
    /* Retour à la ligne automatique */
    if (write_col >= VGA_WIDTH) {
        write_col = 0;
        write_line++;
    }
    
    /* Si on dépasse le buffer, on revient au début (circulaire simplifié) */
    if (write_line >= CONSOLE_BUFFER_LINES) {
        write_line = CONSOLE_BUFFER_LINES - 1;
        /* Scroll le buffer d'une ligne vers le haut */
        for (int i = 0; i < (CONSOLE_BUFFER_LINES - 1) * VGA_WIDTH; i++) {
            console_buffer[i] = console_buffer[i + VGA_WIDTH];
        }
        /* Effacer la dernière ligne */
        for (int i = 0; i < VGA_WIDTH; i++) {
            console_buffer[(CONSOLE_BUFFER_LINES - 1) * VGA_WIDTH + i] = 
                make_vga_entry(' ', current_color);
        }
    }
    
    /* Auto-scroll la vue pour suivre l'écriture */
    if (write_line >= view_start_line + VGA_HEIGHT) {
        view_start_line = write_line - VGA_HEIGHT + 1;
    }
}

void console_putc(char c)
{
    uint32_t flags = save_flags();
    cli();
    spinlock_lock(&console_lock);
    console_putc_unlocked(c);
    /* Note: putc ne fait pas de refresh complet pour perf, juste buffer */
    spinlock_unlock(&console_lock);
    restore_flags(flags);
}

void console_puts(const char* str)
{
    uint32_t flags = save_flags();
    cli();
    spinlock_lock(&console_lock);
    while (*str) {
        console_putc_unlocked(*str++);
    }
    console_refresh();
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
    if (view_start_line > 0) {
        view_start_line--;
        console_refresh();
    }
}

void console_scroll_down(void)
{
    /* Ne pas dépasser la ligne d'écriture courante */
    if (view_start_line < write_line - VGA_HEIGHT + 1) {
        view_start_line++;
        console_refresh();
    }
}

void console_refresh(void)
{
    /* Réinitialiser l'état du curseur car on va rafraîchir tout l'écran */
    cursor_char_saved = 0;
    
    for (int y = 0; y < VGA_HEIGHT; y++) {
        int buffer_line = view_start_line + y;
        
        for (int x = 0; x < VGA_WIDTH; x++) {
            int vga_index = y * VGA_WIDTH + x;
            int buffer_index = buffer_line * VGA_WIDTH + x;
            
            if (buffer_line < CONSOLE_BUFFER_LINES) {
                VGA_MEMORY[vga_index] = console_buffer[buffer_index];
            } else {
                VGA_MEMORY[vga_index] = make_vga_entry(' ', current_color);
            }
        }
    }
    
    /* Afficher le curseur logiciel */
    console_update_cursor();
}

int console_get_view_line(void)
{
    return view_start_line;
}

int console_get_current_line(void)
{
    return write_line;
}

void console_disable_hw_cursor(void)
{
    /* Désactiver le curseur VGA hardware en mettant le bit 5 du registre 0x0A */
    outb(VGA_CTRL_REG, 0x0A);
    outb(VGA_DATA_REG, 0x20);  /* Bit 5 = cursor disable */
}

void console_show_cursor(int show)
{
    cursor_visible = show;
    if (!show && cursor_char_saved) {
        /* Restaurer le caractère sous le curseur */
        int screen_line = write_line - view_start_line;
        if (screen_line >= 0 && screen_line < VGA_HEIGHT) {
            int vga_index = screen_line * VGA_WIDTH + write_col;
            VGA_MEMORY[vga_index] = saved_char;
        }
        cursor_char_saved = 0;
    }
}

void console_update_cursor(void)
{
    if (!cursor_visible) {
        return;
    }
    
    /* Restaurer l'ancien caractère si nécessaire */
    if (cursor_char_saved) {
        /* On ne restaure pas ici car le caractère a déjà été écrasé par putc */
        cursor_char_saved = 0;
    }
    
    /* Calculer la position écran du curseur */
    int screen_line = write_line - view_start_line;
    
    /* Vérifier que le curseur est visible à l'écran */
    if (screen_line >= 0 && screen_line < VGA_HEIGHT && write_col < VGA_WIDTH) {
        int vga_index = screen_line * VGA_WIDTH + write_col;
        
        /* Sauvegarder le caractère actuel */
        saved_char = VGA_MEMORY[vga_index];
        cursor_char_saved = 1;
        
        /* Afficher le curseur (caractère '_' ou bloc inversé) */
        /* On utilise un bloc plein avec couleur inversée */
        uint8_t cursor_color = ((current_color & 0x0F) << 4) | ((current_color >> 4) & 0x0F);
        VGA_MEMORY[vga_index] = (uint16_t)' ' | ((uint16_t)cursor_color << 8);
    }
}
