/* src/kernel/timer.c - PIT Timer & RTC Driver Implementation */
#include "timer.h"
#include "thread.h"
#include "../arch/x86_64/io.h"
#include "console.h"

/* ===========================================
 * Variables globales
 * =========================================== */

/* Compteur de ticks depuis le démarrage */
static volatile uint64_t g_timer_ticks = 0;

/* Fréquence actuelle du timer en Hz */
static uint32_t g_timer_frequency = TIMER_FREQUENCY;

/* Timestamp de référence au boot (lu depuis RTC) */
static uint32_t g_boot_timestamp = 0;

/* ===========================================
 * Fonctions internes
 * =========================================== */

/**
 * Convertit un nombre BCD en binaire.
 * Le RTC retourne les valeurs en BCD par défaut.
 */
static uint8_t bcd_to_binary(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/**
 * Lit un registre RTC.
 * Note: NMI est désactivé pendant la lecture.
 */
static uint8_t rtc_read_register(uint8_t reg)
{
    /* Disable NMI (bit 7) + select register */
    outb(RTC_INDEX, (1 << 7) | reg);
    io_wait();
    return inb(RTC_DATA);
}

/**
 * Vérifie si le RTC est en train de mettre à jour.
 */
static bool rtc_is_updating(void)
{
    outb(RTC_INDEX, RTC_STATUS_A);
    return (inb(RTC_DATA) & 0x80) != 0;
}

/**
 * Calcule si une année est bissextile.
 */
static bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * Retourne le nombre de jours dans un mois.
 */
static uint8_t days_in_month(uint8_t month, uint16_t year)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    if (month < 1 || month > 12) return 0;
    
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    
    return days[month - 1];
}

/* ===========================================
 * Handler d'interruption Timer (IRQ 0)
 * =========================================== */

/* Les fonctions scheduler sont déclarées dans thread.h */

/**
 * Nouveau handler avec support préemption.
 * Appelé directement depuis l'IRQ0 ASM.
 * @param frame Pointeur vers les registres sauvegardés (interrupt_frame_t)
 * @return Nouveau RSP si préemption, 0 sinon
 */
/* Flag pour activer les callbacks scheduler depuis le timer */
static bool g_timer_scheduling_enabled = false;

void timer_enable_scheduling(void)
{
    g_timer_scheduling_enabled = true;
}

uint64_t timer_handler_preempt(void *frame)
{
    g_timer_ticks++;
    
    /* Envoyer EOI au PIC (Important: avant le scheduler!) */
    outb(0x20, 0x20);
    
    /* Ne pas appeler le scheduler tant que le multitasking n'est pas prêt */
    if (!g_timer_scheduling_enabled) {
        return 0;
    }
    
    /* Gestion du temps et réveil des threads endormis */
    scheduler_tick();
    
    /* APPEL CRITIQUE : On demande au scheduler de préempter si besoin.
     * Il retourne 0 si pas de changement, ou le nouveau RSP si changement.
     * 
     * Le format de contexte est maintenant unifié : tous les threads utilisent
     * le format IRQ (15 registres + int_no/error_code + iret frame).
     */
    return scheduler_preempt((interrupt_frame_t *)frame);
}

/* ===========================================
 * Fonctions publiques - PIT Timer
 * =========================================== */

void timer_init(uint32_t frequency)
{
    if (frequency == 0) {
        frequency = TIMER_FREQUENCY;  /* Valeur par défaut si 0 */
    }
    
    g_timer_frequency = frequency;
    
    /* Calcul du diviseur pour obtenir la fréquence désirée */
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    /* Limiter le diviseur à 16 bits */
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;
    
    /* 
     * Commande: 
     * - Channel 0 (bits 7-6 = 00)
     * - Access mode: lobyte/hibyte (bits 5-4 = 11)
     * - Operating mode: Square Wave Generator (bits 3-1 = 011)
     * - Binary mode (bit 0 = 0)
     * = 0x36
     */
    outb(PIT_COMMAND, 0x36);
    io_wait();
    
    /* Envoyer le diviseur (low byte puis high byte) */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    io_wait();
    
    /* Lire le timestamp RTC au boot pour référence */
    datetime_t boot_time;
    rtc_read_datetime(&boot_time);
    g_boot_timestamp = datetime_to_unix(&boot_time);
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[TIMER] PIT initialized at ");
    console_put_dec(frequency);
    console_puts(" Hz\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

uint64_t timer_get_ticks(void)
{
    return g_timer_ticks;
}

uint64_t timer_get_uptime_ms(void)
{
    /* Éviter la division 64 bits en utilisant la fréquence connue */
    /* Si freq = 1000 Hz, alors ticks = ms directement */
    if (g_timer_frequency == 0) {
        return 0;  /* Sécurité contre division par zéro */
    }
    if (g_timer_frequency == 1000) {
        return g_timer_ticks;
    }
    /* Pour d'autres fréquences, approximation sans division 64 bits */
    uint32_t ticks_low = (uint32_t)g_timer_ticks;
    return (ticks_low * 1000) / g_timer_frequency;
}

uint32_t timer_get_uptime_seconds(void)
{
    /* Utiliser seulement les 32 bits bas pour éviter __udivdi3 */
    if (g_timer_frequency == 0) {
        return 0;  /* Sécurité contre division par zéro */
    }
    uint32_t ticks_low = (uint32_t)g_timer_ticks;
    return ticks_low / g_timer_frequency;
}

void timer_sleep_ms(uint32_t ms)
{
    if (g_timer_frequency == 0) {
        return;  /* Sécurité contre division par zéro */
    }
    uint64_t target = g_timer_ticks + (ms * g_timer_frequency) / 1000;
    
    while (g_timer_ticks < target) {
        /* Halt jusqu'à la prochaine interruption pour économiser le CPU */
        asm volatile("sti; hlt");
    }
}

void timer_sleep_ticks(uint64_t ticks)
{
    uint64_t target = g_timer_ticks + ticks;
    
    while (g_timer_ticks < target) {
        asm volatile("sti; hlt");
    }
}

/* ===========================================
 * Fonctions publiques - RTC
 * =========================================== */

void rtc_read_datetime(datetime_t* dt)
{
    /* Attendre que le RTC ne soit pas en train de mettre à jour */
    while (rtc_is_updating());
    
    /* Lire les registres */
    uint8_t second  = rtc_read_register(RTC_SECONDS);
    uint8_t minute  = rtc_read_register(RTC_MINUTES);
    uint8_t hour    = rtc_read_register(RTC_HOURS);
    uint8_t day     = rtc_read_register(RTC_DAY);
    uint8_t month   = rtc_read_register(RTC_MONTH);
    uint8_t year    = rtc_read_register(RTC_YEAR);
    uint8_t weekday = rtc_read_register(RTC_WEEKDAY);
    
    /* Lire le Status Register B pour déterminer le format */
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    
    /* Convertir de BCD en binaire si nécessaire (bit 2 de status B) */
    if (!(status_b & 0x04)) {
        second  = bcd_to_binary(second);
        minute  = bcd_to_binary(minute);
        hour    = bcd_to_binary(hour & 0x7F) | (hour & 0x80);  /* Préserver le bit PM */
        day     = bcd_to_binary(day);
        month   = bcd_to_binary(month);
        year    = bcd_to_binary(year);
    }
    
    /* Convertir de 12h en 24h si nécessaire (bit 1 de status B) */
    if (!(status_b & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }
    
    /* Calculer l'année complète (on suppose 2000+) */
    uint16_t full_year = 2000 + year;
    
    /* Remplir la structure */
    dt->second  = second;
    dt->minute  = minute;
    dt->hour    = hour;
    dt->day     = day;
    dt->month   = month;
    dt->year    = full_year;
    dt->weekday = weekday;
}

/* ===========================================
 * Fonctions publiques - Timestamp
 * =========================================== */

uint32_t datetime_to_unix(const datetime_t* dt)
{
    /* Jours depuis l'epoch (1 Jan 1970) */
    uint32_t days = 0;
    
    /* Années complètes depuis 1970 */
    for (uint16_t y = 1970; y < dt->year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    /* Mois complets de l'année courante */
    for (uint8_t m = 1; m < dt->month; m++) {
        days += days_in_month(m, dt->year);
    }
    
    /* Jours du mois courant (moins 1 car le jour commence à 1) */
    days += dt->day - 1;
    
    /* Convertir en secondes et ajouter heures/minutes/secondes */
    uint32_t seconds = days * 86400;  /* 24 * 60 * 60 */
    seconds += dt->hour * 3600;
    seconds += dt->minute * 60;
    seconds += dt->second;
    
    return seconds;
}

void unix_to_datetime(uint32_t unix_time, datetime_t* dt)
{
    uint32_t remaining = unix_time;
    
    /* Calculer les secondes, minutes, heures du jour */
    dt->second = remaining % 60;
    remaining /= 60;
    dt->minute = remaining % 60;
    remaining /= 60;
    dt->hour = remaining % 24;
    remaining /= 24;
    
    /* 'remaining' contient maintenant le nombre de jours depuis l'epoch */
    uint32_t days = remaining;
    
    /* Calculer le jour de la semaine (1 Jan 1970 était un Jeudi = 4) */
    dt->weekday = ((days + 4) % 7) + 1;  /* 1 = Dimanche */
    
    /* Trouver l'année */
    uint16_t year = 1970;
    while (1) {
        uint32_t days_in_year = is_leap_year(year) ? 366 : 365;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }
    dt->year = year;
    
    /* Trouver le mois */
    uint8_t month = 1;
    while (1) {
        uint8_t days_in_m = days_in_month(month, year);
        if (days < days_in_m) break;
        days -= days_in_m;
        month++;
    }
    dt->month = month;
    
    /* Le jour du mois */
    dt->day = days + 1;
}

timestamp_t timestamp_now(void)
{
    timestamp_t ts;
    
    /* Méthode 1: Utiliser le RTC + millisecondes depuis les ticks */
    datetime_t now;
    rtc_read_datetime(&now);
    ts.seconds = datetime_to_unix(&now);
    
    /* Les millisecondes sont calculées à partir des ticks (évite modulo 64 bits) */
    uint32_t uptime_ms = (uint32_t)timer_get_uptime_ms();
    ts.milliseconds = uptime_ms % 1000;
    
    return ts;
}

void datetime_format(const datetime_t* dt, char* buffer, int format)
{
    /* Helper pour écrire un nombre à 2 chiffres */
    #define WRITE_2DIGIT(buf, val) do { \
        (buf)[0] = '0' + ((val) / 10); \
        (buf)[1] = '0' + ((val) % 10); \
    } while(0)
    
    /* Helper pour écrire un nombre à 4 chiffres */
    #define WRITE_4DIGIT(buf, val) do { \
        (buf)[0] = '0' + ((val) / 1000); \
        (buf)[1] = '0' + (((val) / 100) % 10); \
        (buf)[2] = '0' + (((val) / 10) % 10); \
        (buf)[3] = '0' + ((val) % 10); \
    } while(0)
    
    if (format == 0) {
        /* Format ISO: "YYYY-MM-DD HH:MM:SS" */
        WRITE_4DIGIT(buffer, dt->year);
        buffer[4] = '-';
        WRITE_2DIGIT(buffer + 5, dt->month);
        buffer[7] = '-';
        WRITE_2DIGIT(buffer + 8, dt->day);
        buffer[10] = ' ';
        WRITE_2DIGIT(buffer + 11, dt->hour);
        buffer[13] = ':';
        WRITE_2DIGIT(buffer + 14, dt->minute);
        buffer[16] = ':';
        WRITE_2DIGIT(buffer + 17, dt->second);
        buffer[19] = '\0';
    } else {
        /* Format européen: "DD/MM/YYYY HH:MM:SS" */
        WRITE_2DIGIT(buffer, dt->day);
        buffer[2] = '/';
        WRITE_2DIGIT(buffer + 3, dt->month);
        buffer[5] = '/';
        WRITE_4DIGIT(buffer + 6, dt->year);
        buffer[10] = ' ';
        WRITE_2DIGIT(buffer + 11, dt->hour);
        buffer[13] = ':';
        WRITE_2DIGIT(buffer + 14, dt->minute);
        buffer[16] = ':';
        WRITE_2DIGIT(buffer + 17, dt->second);
        buffer[19] = '\0';
    }
    
    #undef WRITE_2DIGIT
    #undef WRITE_4DIGIT
}

void timestamp_print_now(void)
{
    datetime_t now;
    rtc_read_datetime(&now);
    
    char buffer[20];
    datetime_format(&now, buffer, 0);

    console_puts(buffer);
}

/* ===========================================
 * Fonctions utilitaires pour les logs
 * =========================================== */

/**
 * Affiche un message avec timestamp.
 * @param level Niveau de log ("INFO", "WARN", "ERROR", etc.)
 * @param msg Message à afficher
 */
void log_with_timestamp(const char* level, const char* msg)
{
    datetime_t now;
    rtc_read_datetime(&now);
    
    char buffer[20];
    datetime_format(&now, buffer, 0);
    
    console_puts("[");
    console_puts(buffer);
    console_puts("] [");
    console_puts(level);
    console_puts("] ");
    console_puts(msg);
    console_puts("\n");
}
