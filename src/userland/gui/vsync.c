/* src/gui/vsync.c - Implémentation de la synchronisation VSYNC
 *
 * Ce module fournit une implémentation de base pour la synchronisation
 * avec le rafraîchissement vertical de l'écran.
 */

#include "vsync.h"
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/time.h>

/* État de la synchronisation VSYNC */
static bool g_vsync_enabled = false;
static float g_refresh_rate = 60.0f; /* Taux de rafraîchissement par défaut */
static uint32_t g_vsync_interval_us = 16667; /* 16.667ms pour 60Hz */

/* Statistiques de performance */
static uint32_t g_frame_count = 0;
static uint32_t g_last_frame_time = 0;
static uint32_t g_total_frame_time = 0;
static uint32_t g_frame_times[60]; /* Historique des 60 derniers frames */
static int g_frame_time_index = 0;

void vsync_init(void) {
    /* Initialisation de base - pourrait être améliorée avec la détection matérielle */
    g_vsync_enabled = false;
    g_refresh_rate = 60.0f;
    g_vsync_interval_us = (uint32_t)(1000000.0f / g_refresh_rate);

    for (int i = 0; i < 60; i++) {
        g_frame_times[i] = g_vsync_interval_us;
    }

    printf("VSYNC: Initialized at %.2fHz (interval: %uus)\n",
           g_refresh_rate, g_vsync_interval_us);
}

void vsync_set_enabled(bool enabled) {
    g_vsync_enabled = enabled;
    printf("VSYNC: %s\n", enabled ? "Enabled" : "Disabled");
}

bool vsync_is_enabled(void) {
    return g_vsync_enabled;
}

void vsync_wait(void) {
    if (!g_vsync_enabled) {
        return;
    }

    /* Implémentation logicielle - dans un vrai système, cela utiliserait
     * un appel système ou une interruption matérielle pour attendre le VSYNC */

    /* Calculer le temps depuis le dernier frame */
    uint32_t current_time = syscall1(SYS_GET_MICROSECONDS, 0);
    uint32_t elapsed = current_time - g_last_frame_time;

    /* Si le temps écoulé est inférieur à l'intervalle VSYNC, attendre */
    if (elapsed < g_vsync_interval_us) {
        uint32_t sleep_time = g_vsync_interval_us - elapsed;
        syscall1(SYS_SLEEP_MICROS, sleep_time);
    }

    /* Mettre à jour le temps du dernier frame */
    g_last_frame_time = syscall1(SYS_GET_MICROSECONDS, 0);

    /* Mettre à jour les statistiques */
    g_frame_count++;
    g_total_frame_time += elapsed;

    /* Mettre à jour l'historique des temps de frame */
    g_frame_times[g_frame_time_index++] = elapsed;
    if (g_frame_time_index >= 60) {
        g_frame_time_index = 0;
    }
}

bool vsync_wait_timeout(uint32_t timeout_ms) {
    if (!g_vsync_enabled) {
        return true;
    }

    uint32_t start_time = syscall1(SYS_GET_MICROSECONDS, 0);
    uint32_t timeout_us = timeout_ms * 1000;

    while (1) {
        uint32_t current_time = syscall1(SYS_GET_MICROSECONDS, 0);
        uint32_t elapsed = current_time - g_last_frame_time;

        if (elapsed >= g_vsync_interval_us) {
            /* VSYNC atteint */
            g_last_frame_time = current_time;
            g_frame_count++;
            g_total_frame_time += elapsed;

            /* Mettre à jour l'historique */
            g_frame_times[g_frame_time_index++] = elapsed;
            if (g_frame_time_index >= 60) {
                g_frame_time_index = 0;
            }

            return true;
        }

        if (current_time - start_time >= timeout_us) {
            /* Timeout atteint */
            return false;
        }

        /* Attendre un court moment avant de vérifier à nouveau */
        syscall1(SYS_SLEEP_MICROS, 1000);
    }
}

float vsync_get_refresh_rate(void) {
    return g_refresh_rate;
}

uint32_t vsync_get_interval_us(void) {
    return g_vsync_interval_us;
}

void vsync_update_stats(void) {
    /* Calculer le taux de rafraîchissement réel basé sur l'historique */
    if (g_frame_count > 0) {
        uint32_t total_time = 0;
        for (int i = 0; i < 60; i++) {
            total_time += g_frame_times[i];
        }

        if (total_time > 0) {
            g_refresh_rate = 60.0f * 1000000.0f / (float)total_time;
            g_vsync_interval_us = (uint32_t)(1000000.0f / g_refresh_rate);
        }
    }
}

uint32_t vsync_get_frame_count(void) {
    return g_frame_count;
}

uint32_t vsync_get_avg_frame_time_us(void) {
    if (g_frame_count == 0) {
        return g_vsync_interval_us;
    }

    return g_total_frame_time / g_frame_count;
}

void vsync_print_stats(void) {
    printf("VSYNC Stats: %u frames, %.2fHz, avg frame time: %uus\n",
           g_frame_count, g_refresh_rate, vsync_get_avg_frame_time_us());
}