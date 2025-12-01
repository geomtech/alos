/* src/kernel/timer.h - PIT Timer Driver */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

/* ===========================================
 * PIT (Programmable Interval Timer) Ports
 * =========================================== */
#define PIT_CHANNEL0    0x40    /* Channel 0 data port (IRQ 0) */
#define PIT_CHANNEL1    0x41    /* Channel 1 data port */
#define PIT_CHANNEL2    0x42    /* Channel 2 data port (PC Speaker) */
#define PIT_COMMAND     0x43    /* Mode/Command register */

/* PIT oscille à environ 1.193182 MHz */
#define PIT_FREQUENCY   1193182

/* Fréquence cible pour les ticks (1000 Hz = 1 tick par ms) */
#define TIMER_FREQUENCY 1000

/* ===========================================
 * RTC (Real-Time Clock) Ports
 * =========================================== */
#define RTC_INDEX       0x70    /* Index register */
#define RTC_DATA        0x71    /* Data register */

/* RTC Registers */
#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_WEEKDAY     0x06
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_CENTURY     0x32    /* Peut ne pas exister sur certains systèmes */
#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B

/* ===========================================
 * Structures
 * =========================================== */

/**
 * Structure représentant une date/heure.
 */
typedef struct {
    uint8_t  second;    /* 0-59 */
    uint8_t  minute;    /* 0-59 */
    uint8_t  hour;      /* 0-23 */
    uint8_t  day;       /* 1-31 */
    uint8_t  month;     /* 1-12 */
    uint16_t year;      /* Année complète (ex: 2025) */
    uint8_t  weekday;   /* 1-7 (Dimanche = 1 ou dépend du BIOS) */
} datetime_t;

/**
 * Structure pour un timestamp Unix.
 */
typedef struct {
    uint32_t seconds;       /* Secondes depuis epoch (1 Jan 1970 00:00:00 UTC) */
    uint32_t milliseconds;  /* Millisecondes additionnelles */
} timestamp_t;

/* ===========================================
 * Fonctions publiques - Timer
 * =========================================== */

/**
 * Initialise le PIT avec la fréquence spécifiée.
 * @param frequency Fréquence en Hz (typiquement 100-1000)
 */
void timer_init(uint32_t frequency);

/**
 * Retourne le nombre de ticks depuis le démarrage.
 * @return Nombre de ticks
 */
uint64_t timer_get_ticks(void);

/**
 * Retourne le temps écoulé depuis le démarrage en millisecondes.
 * @return Millisecondes depuis le boot
 */
uint64_t timer_get_uptime_ms(void);

/**
 * Retourne le temps écoulé depuis le démarrage en secondes.
 * @return Secondes depuis le boot
 */
uint32_t timer_get_uptime_seconds(void);

/**
 * Attend un nombre spécifié de millisecondes.
 * @param ms Nombre de millisecondes à attendre
 */
void timer_sleep_ms(uint32_t ms);

/**
 * Attend un nombre spécifié de ticks.
 * @param ticks Nombre de ticks à attendre
 */
void timer_sleep_ticks(uint64_t ticks);

/* ===========================================
 * Fonctions publiques - RTC
 * =========================================== */

/**
 * Lit la date/heure actuelle depuis le RTC.
 * @param dt Pointeur vers la structure à remplir
 */
void rtc_read_datetime(datetime_t* dt);

/**
 * Retourne le timestamp Unix actuel.
 * Combine RTC (pour la date/heure) et PIT (pour les millisecondes).
 * @return Timestamp Unix
 */
timestamp_t timestamp_now(void);

/**
 * Convertit une datetime en timestamp Unix.
 * @param dt Date/heure à convertir
 * @return Secondes depuis l'epoch Unix
 */
uint32_t datetime_to_unix(const datetime_t* dt);

/**
 * Convertit un timestamp Unix en datetime.
 * @param unix_time Secondes depuis l'epoch
 * @param dt Structure à remplir
 */
void unix_to_datetime(uint32_t unix_time, datetime_t* dt);

/**
 * Formate une datetime en chaîne lisible.
 * @param dt Date/heure à formater
 * @param buffer Buffer de destination (au moins 20 caractères)
 * @param format 0 = "YYYY-MM-DD HH:MM:SS", 1 = "DD/MM/YYYY HH:MM:SS"
 */
void datetime_format(const datetime_t* dt, char* buffer, int format);

/**
 * Affiche la date/heure actuelle sur la console.
 */
void timestamp_print_now(void);

#endif /* TIMER_H */
