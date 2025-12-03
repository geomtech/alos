/* src/net/netlog.h - Network Stack Logging Macros
 * 
 * Contrôle des logs de la stack réseau.
 * 
 * Usage dans le Makefile:
 *   make NET_DEBUG=1    -> Active les logs réseau
 *   make NET_DEBUG=0    -> Désactive les logs réseau (défaut)
 *   make                -> Désactive les logs réseau (défaut)
 * 
 * Ce header remplace les appels console_* par des versions conditionnelles.
 * Inclure ce header APRÈS console.h dans les fichiers réseau.
 */
#ifndef NET_LOG_H
#define NET_LOG_H

#include "../kernel/console.h"

/* ===========================================
 * Macros de logging conditionnelles
 * 
 * Ces macros remplacent les fonctions console_*
 * pour permettre de désactiver tous les logs
 * réseau d'un seul coup via NET_DEBUG.
 * =========================================== */

#ifdef NET_DEBUG

/* ========== Macros activées ========== */

/* Fonctions console wrapper - actives */
#define net_puts(s)             console_puts(s)
#define net_putc(c)             console_putc(c)
#define net_put_dec(v)          console_put_dec(v)
#define net_put_hex(v)          console_put_hex(v)
#define net_put_hex_byte(v)     console_put_hex_byte(v)
#define net_set_color(fg, bg)   console_set_color(fg, bg)
#define net_reset_color()       console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK)

/* Macros de log avec tag */
#define NET_LOG(tag, msg) do { \
    console_puts("[" tag "] " msg); \
} while(0)

#define NET_LOGLN(tag, msg) do { \
    console_puts("[" tag "] " msg "\n"); \
} while(0)

/* Log coloré (info - cyan) */
#define NET_LOG_INFO(tag, msg) do { \
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

#define NET_LOG_INFO_LN(tag, msg) do { \
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg "\n"); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

/* Log coloré (succès - vert) */
#define NET_LOG_OK(tag, msg) do { \
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

#define NET_LOG_OK_LN(tag, msg) do { \
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg "\n"); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

/* Log coloré (warning - jaune/marron) */
#define NET_LOG_WARN(tag, msg) do { \
    console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

#define NET_LOG_WARN_LN(tag, msg) do { \
    console_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg "\n"); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

/* Log coloré (erreur - rouge) */
#define NET_LOG_ERR(tag, msg) do { \
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

#define NET_LOG_ERR_LN(tag, msg) do { \
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); \
    console_puts("[" tag "] " msg "\n"); \
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
} while(0)

#else /* NET_DEBUG not defined */

/* ========== Macros désactivées ========== */

/* Fonctions console wrapper - désactivées */
#define net_puts(s)             ((void)0)
#define net_putc(c)             ((void)0)
#define net_put_dec(v)          ((void)0)
#define net_put_hex(v)          ((void)0)
#define net_put_hex_byte(v)     ((void)0)
#define net_set_color(fg, bg)   ((void)0)
#define net_reset_color()       ((void)0)

/* Macros de log avec tag - désactivées */
#define NET_LOG(tag, msg)           ((void)0)
#define NET_LOGLN(tag, msg)         ((void)0)
#define NET_LOG_INFO(tag, msg)      ((void)0)
#define NET_LOG_INFO_LN(tag, msg)   ((void)0)
#define NET_LOG_OK(tag, msg)        ((void)0)
#define NET_LOG_OK_LN(tag, msg)     ((void)0)
#define NET_LOG_WARN(tag, msg)      ((void)0)
#define NET_LOG_WARN_LN(tag, msg)   ((void)0)
#define NET_LOG_ERR(tag, msg)       ((void)0)
#define NET_LOG_ERR_LN(tag, msg)    ((void)0)

#endif /* NET_DEBUG */

#endif /* NET_LOG_H */
