/* src/kernel/klog.c - Kernel Logging System Implementation */
#include "klog.h"
#include "timer.h"
#include "console.h"
#include "../fs/vfs.h"
#include "../mm/kheap.h"
#include "../arch/x86_64/io.h"

/* ===========================================
 * Variables globales
 * =========================================== */

/* Buffer précoce pour les logs avant que le VFS soit prêt */
static char early_buffer[KLOG_BUFFER_SIZE];
static uint32_t early_buffer_pos = 0;
static int early_mode = 1;  /* 1 = buffer mémoire, 0 = fichier */

/* Fichier de log */
static vfs_node_t* log_file = NULL;
static uint32_t log_file_offset = 0;

/* Niveau de log actuel */
static klog_level_t current_level = LOG_DEBUG;

/* État d'initialisation */
static int initialized = 0;

/* ===========================================
 * Fonctions utilitaires internes
 * =========================================== */

static void* klog_memset(void* s, int c, size_t n)
{
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void* klog_memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

static size_t klog_strlen(const char* s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static char* klog_strcpy(char* dest, const char* src)
{
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

static char* klog_strcat(char* dest, const char* src)
{
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

/**
 * Convertit un nombre en chaîne décimale.
 */
static void uint_to_str(uint32_t value, char* buffer)
{
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    char tmp[12];
    int i = 0;
    
    while (value > 0) {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    /* Inverser */
    int j = 0;
    while (i > 0) {
        buffer[j++] = tmp[--i];
    }
    buffer[j] = '\0';
}

/**
 * Convertit un nombre en chaîne hexadécimale.
 */
static void uint_to_hex(uint32_t value, char* buffer)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    
    buffer[0] = '0';
    buffer[1] = 'x';
    
    for (int i = 7; i >= 0; i--) {
        buffer[2 + (7 - i)] = hex_chars[(value >> (i * 4)) & 0xF];
    }
    buffer[10] = '\0';
}

/**
 * Retourne le préfixe de niveau de log.
 */
static const char* level_to_string(klog_level_t level)
{
    switch (level) {
        case LOG_DEBUG: return "[DEBUG]";
        case LOG_INFO:  return "[INFO] ";
        case LOG_WARN:  return "[WARN] ";
        case LOG_ERROR: return "[ERROR]";
        default:        return "[?????]";
    }
}

/**
 * Écrit dans le buffer précoce.
 */
static void write_to_early_buffer(const char* str)
{
    size_t len = klog_strlen(str);
    
    /* Vérifier qu'il y a assez d'espace */
    if (early_buffer_pos + len >= KLOG_BUFFER_SIZE - 1) {
        /* Buffer plein - on écrase les anciens logs */
        /* Simple: on recommence au début */
        early_buffer_pos = 0;
    }
    
    klog_memcpy(early_buffer + early_buffer_pos, str, len);
    early_buffer_pos += len;
    early_buffer[early_buffer_pos] = '\0';
}

/**
 * Écrit dans le fichier de log.
 */
static void write_to_file(const char* str)
{
    if (log_file == NULL) return;
    
    size_t len = klog_strlen(str);
    int written = vfs_write(log_file, log_file_offset, len, (const uint8_t*)str);
    
    if (written > 0) {
        log_file_offset += written;
    }
    if (written > 0) {
        log_file_offset += written;
    }
}

/* ===========================================
 * Serial Port Support (COM1)
 * =========================================== */
#define PORT_COM1 0x3F8

static void serial_init(void)
{
    outb(PORT_COM1 + 1, 0x00);    /* Disable all interrupts */
    outb(PORT_COM1 + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(PORT_COM1 + 0, 0x03);    /* Set divisor to 3 (lo byte) 38400 baud */
    outb(PORT_COM1 + 1, 0x00);    /*                  (hi byte) */
    outb(PORT_COM1 + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(PORT_COM1 + 2, 0xC7);    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(PORT_COM1 + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
}

static int serial_is_transmit_empty(void)
{
    return inb(PORT_COM1 + 5) & 0x20;
}

static void serial_write_char(char a)
{
    while (serial_is_transmit_empty() == 0);
    outb(PORT_COM1, a);
}

static void serial_write_str(const char* str)
{
    while (*str) {
        serial_write_char(*str++);
    }
}

/**
 * Formate et écrit un message de log.
 */
static void do_log(klog_level_t level, const char* module, const char* msg, 
                   const char* suffix)
{
    if (level < current_level) return;
    
    /* Construire le message formaté */
    char formatted[KLOG_MAX_MSG_LEN];
    char timestamp[24];
    
    /* Timestamp: [SSSSSS.mmm] où S=secondes depuis boot, m=millisecondes */
    uint32_t uptime = (uint32_t)timer_get_uptime_ms();  /* Cast en 32-bit pour éviter __udivdi3 */
    uint32_t seconds = uptime / 1000;
    uint32_t ms = uptime % 1000;
    
    char sec_str[12], ms_str[4];
    uint_to_str(seconds, sec_str);
    uint_to_str(ms, ms_str);
    
    /* Padding pour les millisecondes (toujours 3 chiffres) */
    timestamp[0] = '[';
    int pos = 1;
    
    /* Copier les secondes */
    for (int i = 0; sec_str[i]; i++) {
        timestamp[pos++] = sec_str[i];
    }
    timestamp[pos++] = '.';
    
    /* Padding millisecondes */
    if (ms < 10) {
        timestamp[pos++] = '0';
        timestamp[pos++] = '0';
    } else if (ms < 100) {
        timestamp[pos++] = '0';
    }
    for (int i = 0; ms_str[i]; i++) {
        timestamp[pos++] = ms_str[i];
    }
    timestamp[pos++] = ']';
    timestamp[pos++] = ' ';
    timestamp[pos] = '\0';
    
    /* Construire le message complet */
    formatted[0] = '\0';
    klog_strcat(formatted, timestamp);
    klog_strcat(formatted, level_to_string(level));
    klog_strcat(formatted, " ");
    
    if (module != NULL && module[0] != '\0') {
        klog_strcat(formatted, "[");
        klog_strcat(formatted, module);
        klog_strcat(formatted, "] ");
    }
    
    klog_strcat(formatted, msg);
    
    if (suffix != NULL && suffix[0] != '\0') {
        klog_strcat(formatted, suffix);
    }
    
    klog_strcat(formatted, "\n");
    
    /* Écrire selon le mode */
    if (early_mode) {
        write_to_early_buffer(formatted);
    } else {
        write_to_file(formatted);
    }
    
    /* Toujours écrire sur le port série pour le debug */
    serial_write_str(formatted);
}

/* ===========================================
 * Fonctions publiques
 * =========================================== */

void klog_early_init(void)
{
    klog_memset(early_buffer, 0, KLOG_BUFFER_SIZE);
    early_buffer_pos = 0;
    early_mode = 1;
    current_level = LOG_INFO;
    
    /* Initialize serial port for debug output */
    serial_init();
    
    /* Premier message de log */
    klog(LOG_INFO, "KLOG", "Early logging initialized (memory buffer + serial)");
}

int klog_init(void)
{
    if (initialized) {
        return 0;  /* Déjà initialisé */
    }
    
    klog(LOG_INFO, "KLOG", "Initializing file-based logging...");
    
    /* Créer le dossier /system s'il n'existe pas */
    vfs_node_t* system_dir = vfs_resolve_path(KLOG_SYSTEM_DIR);
    if (system_dir == NULL) {
        klog(LOG_INFO, "KLOG", "Creating /system directory...");
        if (vfs_mkdir(KLOG_SYSTEM_DIR) != 0) {
            klog(LOG_ERROR, "KLOG", "Failed to create /system directory");
            return -1;
        }
    }
    
    /* Créer le dossier /system/logs s'il n'existe pas */
    vfs_node_t* logs_dir = vfs_resolve_path(KLOG_LOGS_DIR);
    if (logs_dir == NULL) {
        klog(LOG_INFO, "KLOG", "Creating /system/logs directory...");
        if (vfs_mkdir(KLOG_LOGS_DIR) != 0) {
            klog(LOG_ERROR, "KLOG", "Failed to create /system/logs directory");
            return -1;
        }
    }
    
    /* Créer le fichier de log s'il n'existe pas, sinon l'ouvrir */
    log_file = vfs_open(KLOG_FILE_PATH, VFS_O_RDWR | VFS_O_CREAT);
    if (log_file == NULL) {
        klog(LOG_ERROR, "KLOG", "Failed to open/create log file");
        return -1;
    }
    
    /* Se positionner à la fin du fichier pour append */
    log_file_offset = log_file->size;
    
    /* Ajouter un séparateur pour la nouvelle session */
    const char* separator = "\n========== NEW BOOT SESSION ==========\n";
    vfs_write(log_file, log_file_offset, klog_strlen(separator), (const uint8_t*)separator);
    log_file_offset += klog_strlen(separator);
    
    /* Basculer en mode fichier */
    early_mode = 0;
    initialized = 1;
    
    /* Vider le buffer précoce dans le fichier */
    if (early_buffer_pos > 0) {
        write_to_file(early_buffer);
    }
    
    klog(LOG_INFO, "KLOG", "File-based logging active");
    
    return 0;
}

void klog_shutdown(void)
{
    if (log_file != NULL) {
        klog(LOG_INFO, "KLOG", "Shutting down logging system");
        klog_flush();
        vfs_close(log_file);
        log_file = NULL;
    }
    
    early_mode = 1;
    initialized = 0;
}

void klog_set_level(klog_level_t level)
{
    current_level = level;
}

klog_level_t klog_get_level(void)
{
    return current_level;
}

void klog(klog_level_t level, const char* module, const char* msg)
{
    do_log(level, module, msg, NULL);
}

void klog_dec(klog_level_t level, const char* module, const char* msg, uint32_t value)
{
    char suffix[16];
    uint_to_str(value, suffix);
    do_log(level, module, msg, suffix);
}

void klog_hex(klog_level_t level, const char* module, const char* msg, uint32_t value)
{
    char suffix[16];
    uint_to_hex(value, suffix);
    do_log(level, module, msg, suffix);
}

void klog_flush(void)
{
    /* Pour l'instant, les écritures sont synchrones */
    /* Dans une future version, on pourrait bufferiser et flush ici */
    (void)0;
}
