/* src/gui/gui.h - Point d'entrée principal du GUI ALOS
 * 
 * Ce module coordonne tous les composants de l'interface graphique :
 * - Initialisation du système de rendu
 * - Gestion du compositeur
 * - Window manager
 * - Menu bar et dock
 * - Boucle d'événements
 */
#ifndef GUI_H
#define GUI_H

#include "gui_types.h"
#include "render.h"
#include "font.h"
#include "compositor.h"
#include "wm.h"
#include "menubar.h"
#include "dock.h"
#include "events.h"
#include "../include/limine.h"

/* État du GUI */
typedef enum {
    GUI_STATE_UNINITIALIZED,
    GUI_STATE_RUNNING,
    GUI_STATE_PAUSED,
    GUI_STATE_SHUTDOWN
} gui_state_t;

/**
 * Initialise le système GUI complet.
 * 
 * @param fb Framebuffer Limine
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int gui_init(struct limine_framebuffer* fb);

/**
 * Libère toutes les ressources du GUI.
 */
void gui_shutdown(void);

/**
 * Retourne l'état actuel du GUI.
 */
gui_state_t gui_get_state(void);

/**
 * Met en pause le GUI (arrête le rendu).
 */
void gui_pause(void);

/**
 * Reprend le GUI après une pause.
 */
void gui_resume(void);

/**
 * Traite les événements en attente.
 * Doit être appelé régulièrement.
 */
void gui_process_events(void);

/**
 * Met à jour les animations.
 * 
 * @param delta_time Temps écoulé depuis la dernière frame (en secondes)
 */
void gui_update(float delta_time);

/**
 * Effectue le rendu complet de l'interface (appelé au démarrage).
 */
void gui_render_full(void);

/**
 * Met à jour le curseur souris (rendu léger).
 */
void gui_render(void);

/**
 * Boucle principale du GUI (bloquante).
 * Combine process_events, update et render.
 */
void gui_main_loop(void);

/**
 * Demande l'arrêt de la boucle principale.
 */
void gui_request_quit(void);

/**
 * Configure le fond d'écran.
 */
void gui_set_wallpaper_color(uint32_t color);
void gui_set_wallpaper_gradient(rgba_t color1, rgba_t color2, gradient_direction_t dir);

/**
 * Crée une fenêtre de démonstration.
 */
window_t* gui_create_demo_window(const char* title, int32_t x, int32_t y);

/**
 * Ajoute des applications de démonstration au dock.
 */
void gui_setup_demo_dock(void);

/**
 * Configure les menus de démonstration.
 */
void gui_setup_demo_menus(void);

/**
 * Callback pour les événements souris (appelé par le driver souris).
 * Doit correspondre au type mouse_callback_t.
 */
#include "../kernel/mouse.h"
void gui_mouse_callback(const mouse_state_t* state);

#endif /* GUI_H */
