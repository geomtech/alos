/* src/gui/vsync.h - Synchronisation VSYNC pour le rendu
 *
 * Ce module fournit une interface pour la synchronisation
 * avec le rafraîchissement vertical de l'écran (VSYNC).
 * Cela permet d'éliminer le tearing et d'obtenir un rendu fluide.
 */

#ifndef VSYNC_H
#define VSYNC_H

#include <stdbool.h>
#include <stdint.h>

/* Initialise le système de synchronisation VSYNC */
void vsync_init(void);

/* Active ou désactive la synchronisation VSYNC */
void vsync_set_enabled(bool enabled);

/* Vérifie si la synchronisation VSYNC est activée */
bool vsync_is_enabled(void);

/* Attend le prochain signal VSYNC */
void vsync_wait(void);

/* Attend le prochain signal VSYNC avec timeout */
bool vsync_wait_timeout(uint32_t timeout_ms);

/* Retourne le taux de rafraîchissement actuel en Hz */
float vsync_get_refresh_rate(void);

/* Retourne l'intervalle entre deux VSYNC en microsecondes */
uint32_t vsync_get_interval_us(void);

/* Met à jour les statistiques de synchronisation */
void vsync_update_stats(void);

/* Retourne le nombre de frames rendues */
uint32_t vsync_get_frame_count(void);

/* Retourne le temps moyen entre les frames en microsecondes */
uint32_t vsync_get_avg_frame_time_us(void);

#endif /* VSYNC_H */