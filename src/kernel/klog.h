/* src/kernel/klog.h - Kernel Logging System */
#ifndef KLOG_H
#define KLOG_H

#include <stdint.h>
#include <stddef.h>

/* ===========================================
 * Configuration
 * =========================================== */
#define KLOG_BUFFER_SIZE    8192    /* 8 KiB de buffer précoce */
#define KLOG_MAX_MSG_LEN    512     /* Taille max d'un message */
#define KLOG_FILE_PATH      "/system/logs/kernel.log"
#define KLOG_SYSTEM_DIR     "/system"
#define KLOG_LOGS_DIR       "/system/logs"

/* ===========================================
 * Niveaux de log
 * =========================================== */
typedef enum {
    LOG_DEBUG = 0,      /* Messages de debug détaillés */
    LOG_INFO  = 1,      /* Informations générales */
    LOG_WARN  = 2,      /* Avertissements */
    LOG_ERROR = 3,      /* Erreurs */
    LOG_NONE  = 4       /* Désactive les logs */
} klog_level_t;

/* ===========================================
 * Fonctions publiques
 * =========================================== */

/**
 * Initialise le système de logs précoce (buffer en mémoire).
 * À appeler très tôt dans le boot, avant le VFS.
 */
void klog_early_init(void);

/**
 * Initialise le système de logs fichier.
 * À appeler après que le VFS soit monté.
 * Crée /system/logs si nécessaire et ouvre le fichier de log.
 * Vide le buffer précoce dans le fichier.
 * @return 0 si succès, -1 si erreur
 */
int klog_init(void);

/**
 * Ferme le fichier de log proprement.
 * À appeler avant l'arrêt du système.
 */
void klog_shutdown(void);

/**
 * Définit le niveau de log minimum.
 * Les messages en dessous de ce niveau seront ignorés.
 * @param level Niveau minimum (LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR)
 */
void klog_set_level(klog_level_t level);

/**
 * Récupère le niveau de log actuel.
 * @return Niveau de log actuel
 */
klog_level_t klog_get_level(void);

/**
 * Log un message avec le niveau spécifié.
 * @param level  Niveau de log
 * @param module Nom du module (ex: "VFS", "EXT2", "NET")
 * @param msg    Message à logger
 */
void klog(klog_level_t level, const char* module, const char* msg);

/**
 * Log un message avec une valeur entière.
 * @param level  Niveau de log
 * @param module Nom du module
 * @param msg    Message
 * @param value  Valeur à afficher après le message
 */
void klog_dec(klog_level_t level, const char* module, const char* msg, uint32_t value);

/**
 * Log un message avec une valeur hexadécimale.
 * @param level  Niveau de log
 * @param module Nom du module
 * @param msg    Message
 * @param value  Valeur hexa à afficher après le message
 */
void klog_hex(klog_level_t level, const char* module, const char* msg, uint32_t value);

/**
 * Force l'écriture des logs bufferisés sur le disque.
 */
void klog_flush(void);

/* ===========================================
 * Macros de commodité
 * =========================================== */
#define KLOG_DEBUG(module, msg)         klog(LOG_DEBUG, module, msg)
#define KLOG_INFO(module, msg)          klog(LOG_INFO, module, msg)
#define KLOG_WARN(module, msg)          klog(LOG_WARN, module, msg)
#define KLOG_ERROR(module, msg)         klog(LOG_ERROR, module, msg)

#define KLOG_DEBUG_DEC(module, msg, v)  klog_dec(LOG_DEBUG, module, msg, v)
#define KLOG_INFO_DEC(module, msg, v)   klog_dec(LOG_INFO, module, msg, v)
#define KLOG_WARN_DEC(module, msg, v)   klog_dec(LOG_WARN, module, msg, v)
#define KLOG_ERROR_DEC(module, msg, v)  klog_dec(LOG_ERROR, module, msg, v)

#define KLOG_DEBUG_HEX(module, msg, v)  klog_hex(LOG_DEBUG, module, msg, v)
#define KLOG_INFO_HEX(module, msg, v)   klog_hex(LOG_INFO, module, msg, v)
#define KLOG_WARN_HEX(module, msg, v)   klog_hex(LOG_WARN, module, msg, v)
#define KLOG_ERROR_HEX(module, msg, v)  klog_hex(LOG_ERROR, module, msg, v)

#endif /* KLOG_H */
