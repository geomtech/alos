/* src/config/config.h - Configuration File Manager */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

/* Chemins des fichiers de configuration */
#define CONFIG_DIR              "/config"
#define CONFIG_NETWORK_FILE     "/config/network.conf"
#define CONFIG_HISTORY_FILE     "/config/history"
#define CONFIG_STARTUP_SCRIPT   "/config/startup.sh"

/* Tailles maximales */
#define CONFIG_LINE_MAX         256
#define CONFIG_KEY_MAX          64
#define CONFIG_VALUE_MAX        192

/* ========================================
 * Structure de configuration réseau
 * ======================================== */
typedef struct {
    int use_dhcp;               /* 1 = DHCP, 0 = statique */
    uint8_t ip_addr[4];         /* Adresse IP statique */
    uint8_t netmask[4];         /* Masque de sous-réseau */
    uint8_t gateway[4];         /* Passerelle par défaut */
    uint8_t dns_server[4];      /* Serveur DNS */
} network_config_t;

/* ========================================
 * Initialisation
 * ======================================== */

/**
 * Initialise le système de configuration.
 * Crée le répertoire /config si nécessaire.
 * @return 0 si succès, -1 si erreur
 */
int config_init(void);

/* ========================================
 * Configuration réseau
 * ======================================== */

/**
 * Charge la configuration réseau depuis /config/network.conf
 * @param config Structure à remplir
 * @return 0 si succès, -1 si erreur ou fichier absent
 */
int config_load_network(network_config_t* config);

/**
 * Sauvegarde la configuration réseau dans /config/network.conf
 * @param config Structure à sauvegarder
 * @return 0 si succès, -1 si erreur
 */
int config_save_network(const network_config_t* config);

/**
 * Charge la configuration réseau pour une interface spécifique.
 * Fichier: /config/network-<iface>.conf (ex: /config/network-eth0.conf)
 * @param iface Nom de l'interface (ex: "eth0")
 * @param config Structure à remplir
 * @return 0 si succès, -1 si erreur ou fichier absent
 */
int config_load_network_iface(const char* iface, network_config_t* config);

/**
 * Sauvegarde la configuration réseau pour une interface spécifique.
 * @param iface Nom de l'interface (ex: "eth0")
 * @param config Structure à sauvegarder
 * @return 0 si succès, -1 si erreur
 */
int config_save_network_iface(const char* iface, const network_config_t* config);

/**
 * Applique la configuration réseau à l'interface.
 * @param config Configuration à appliquer
 * @return 0 si succès, -1 si erreur
 */
int config_apply_network(const network_config_t* config);

/**
 * Applique la configuration réseau à une interface spécifique.
 * @param iface Nom de l'interface
 * @param config Configuration à appliquer
 * @return 0 si succès, -1 si erreur
 */
int config_apply_network_iface(const char* iface, const network_config_t* config);

/* ========================================
 * Scripts
 * ======================================== */

/**
 * Exécute un script de commandes.
 * Lit le fichier ligne par ligne et exécute chaque commande.
 * @param path Chemin du script
 * @return 0 si succès, -1 si erreur
 */
int config_run_script(const char* path);

/**
 * Exécute le script de démarrage /config/startup.sh
 * @return 0 si succès, -1 si fichier absent ou erreur
 */
int config_run_startup_script(void);

/* ========================================
 * Historique persistant
 * ======================================== */

/**
 * Charge l'historique du shell depuis /config/history
 * @param history Tableau de chaînes pour l'historique
 * @param max_entries Nombre maximum d'entrées
 * @param line_max Taille maximale d'une ligne
 * @return Nombre d'entrées chargées
 */
int config_load_history(char history[][256], int max_entries, int line_max);

/**
 * Sauvegarde l'historique du shell dans /config/history
 * @param history Tableau de chaînes de l'historique
 * @param count Nombre d'entrées
 * @param start_index Index de début (pour historique circulaire)
 * @param line_max Taille maximale d'une ligne
 * @return 0 si succès, -1 si erreur
 */
int config_save_history(char history[][256], int count, int start_index, int line_max);

/* ========================================
 * Utilitaires
 * ======================================== */

/**
 * Parse une ligne de configuration "key=value"
 * @param line Ligne à parser
 * @param key Buffer pour la clé
 * @param value Buffer pour la valeur
 * @return 0 si succès, -1 si format invalide
 */
int config_parse_line(const char* line, char* key, char* value);

/**
 * Parse une adresse IP "a.b.c.d" en tableau d'octets
 * @param str Chaîne IP
 * @param ip Buffer de 4 octets
 * @return 0 si succès, -1 si format invalide
 */
int config_parse_ip(const char* str, uint8_t* ip);

/**
 * Convertit une adresse IP en chaîne
 * @param ip Tableau de 4 octets
 * @param str Buffer de sortie (au moins 16 caractères)
 */
void config_ip_to_string(const uint8_t* ip, char* str);

#endif /* CONFIG_H */
