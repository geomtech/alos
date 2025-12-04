/* src/config/config.c - Configuration File Manager Implementation */
#include "config.h"
#include "../fs/vfs.h"
#include "../kernel/console.h"
#include "../kernel/klog.h"
#include "../net/core/netdev.h"
#include "../include/string.h"
#include "../shell/commands.h"

/* ========================================
 * Utilitaires internes
 * ======================================== */

/**
 * Supprime les espaces en début et fin de chaîne (in-place)
 */
static char* trim(char* str)
{
    /* Espaces en début */
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    if (*str == '\0') {
        return str;
    }
    
    /* Espaces en fin */
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    end[1] = '\0';
    
    return str;
}

/**
 * Vérifie si une ligne est un commentaire ou vide
 */
static int is_comment_or_empty(const char* line)
{
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return (*line == '#' || *line == ';' || *line == '\0' || *line == '\n');
}

/* ========================================
 * Initialisation
 * ======================================== */

int config_init(void)
{
    /* Vérifier si /config existe */
    vfs_node_t* config_dir = vfs_open(CONFIG_DIR, VFS_O_RDONLY);
    
    if (config_dir == NULL) {
        /* Créer le répertoire /config */
        if (vfs_mkdir(CONFIG_DIR) != 0) {
            KLOG_ERROR("CONFIG", "Failed to create /config directory");
            return -1;
        }
        KLOG_INFO("CONFIG", "Created /config directory");
    } else {
        vfs_close(config_dir);
    }
    
    KLOG_INFO("CONFIG", "Configuration system initialized");
    return 0;
}

/* ========================================
 * Parsing de configuration
 * ======================================== */

int config_parse_line(const char* line, char* key, char* value)
{
    /* Chercher le '=' */
    const char* eq = line;
    while (*eq && *eq != '=') {
        eq++;
    }
    
    if (*eq != '=') {
        return -1;  /* Pas de '=' trouvé */
    }
    
    /* Copier la clé */
    size_t key_len = eq - line;
    if (key_len >= CONFIG_KEY_MAX) {
        key_len = CONFIG_KEY_MAX - 1;
    }
    strncpy(key, line, key_len);
    key[key_len] = '\0';
    
    /* Copier la valeur */
    eq++;  /* Sauter le '=' */
    strncpy(value, eq, CONFIG_VALUE_MAX - 1);
    value[CONFIG_VALUE_MAX - 1] = '\0';
    
    /* Trimmer les deux */
    char* trimmed_key = trim(key);
    char* trimmed_value = trim(value);
    
    /* Recopier si nécessaire (copie manuelle pour éviter memmove) */
    if (trimmed_key != key) {
        size_t len = strlen(trimmed_key);
        for (size_t i = 0; i <= len; i++) {
            key[i] = trimmed_key[i];
        }
    }
    if (trimmed_value != value) {
        size_t len = strlen(trimmed_value);
        for (size_t i = 0; i <= len; i++) {
            value[i] = trimmed_value[i];
        }
    }
    
    return 0;
}

int config_parse_ip(const char* str, uint8_t* ip)
{
    int parts[4] = {0, 0, 0, 0};
    int part_idx = 0;
    
    while (*str && part_idx < 4) {
        if (*str >= '0' && *str <= '9') {
            parts[part_idx] = parts[part_idx] * 10 + (*str - '0');
            if (parts[part_idx] > 255) {
                return -1;  /* Valeur trop grande */
            }
        } else if (*str == '.') {
            part_idx++;
        } else if (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
            /* Ignorer les espaces de fin */
            break;
        } else {
            return -1;  /* Caractère invalide */
        }
        str++;
    }
    
    if (part_idx != 3) {
        return -1;  /* Pas assez de parties */
    }
    
    ip[0] = (uint8_t)parts[0];
    ip[1] = (uint8_t)parts[1];
    ip[2] = (uint8_t)parts[2];
    ip[3] = (uint8_t)parts[3];
    
    return 0;
}

void config_ip_to_string(const uint8_t* ip, char* str)
{
    /* Format: "a.b.c.d" */
    int pos = 0;
    
    for (int i = 0; i < 4; i++) {
        uint8_t val = ip[i];
        
        if (val >= 100) {
            str[pos++] = '0' + (val / 100);
            val %= 100;
            str[pos++] = '0' + (val / 10);
            str[pos++] = '0' + (val % 10);
        } else if (val >= 10) {
            str[pos++] = '0' + (val / 10);
            str[pos++] = '0' + (val % 10);
        } else {
            str[pos++] = '0' + val;
        }
        
        if (i < 3) {
            str[pos++] = '.';
        }
    }
    
    str[pos] = '\0';
}

/* ========================================
 * Configuration réseau
 * ======================================== */

int config_load_network(network_config_t* config)
{
    if (config == NULL) {
        return -1;
    }
    
    /* Valeurs par défaut: DHCP activé */
    config->use_dhcp = 1;
    memset(config->ip_addr, 0, 4);
    memset(config->netmask, 0, 4);
    memset(config->gateway, 0, 4);
    memset(config->dns_server, 0, 4);
    
    /* Ouvrir le fichier de configuration */
    vfs_node_t* file = vfs_open(CONFIG_NETWORK_FILE, VFS_O_RDONLY);
    if (file == NULL) {
        /* Fichier absent, utiliser les valeurs par défaut (DHCP) */
        return -1;
    }
    
    /* Lire le fichier ligne par ligne */
    uint8_t buffer[1024];
    int bytes = vfs_read(file, 0, sizeof(buffer) - 1, buffer);
    vfs_close(file);
    
    if (bytes <= 0) {
        return -1;
    }
    
    buffer[bytes] = '\0';
    
    /* Parser chaque ligne */
    char line[CONFIG_LINE_MAX];
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VALUE_MAX];
    int line_start = 0;
    
    for (int i = 0; i <= bytes; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\0') {
            /* Extraire la ligne */
            int line_len = i - line_start;
            if (line_len > 0 && line_len < CONFIG_LINE_MAX) {
                strncpy(line, (char*)&buffer[line_start], line_len);
                line[line_len] = '\0';
                
                /* Ignorer les commentaires et lignes vides */
                if (!is_comment_or_empty(line)) {
                    if (config_parse_line(line, key, value) == 0) {
                        if (strcmp(key, "dhcp") == 0) {
                            config->use_dhcp = (strcmp(value, "yes") == 0 || strcmp(value, "1") == 0);
                        } else if (strcmp(key, "ip") == 0) {
                            config_parse_ip(value, config->ip_addr);
                        } else if (strcmp(key, "netmask") == 0) {
                            config_parse_ip(value, config->netmask);
                        } else if (strcmp(key, "gateway") == 0) {
                            config_parse_ip(value, config->gateway);
                        } else if (strcmp(key, "dns") == 0) {
                            config_parse_ip(value, config->dns_server);
                        }
                    }
                }
            }
            line_start = i + 1;
        }
    }
    
    KLOG_INFO("CONFIG", "Loaded network configuration");
    return 0;
}

int config_save_network(const network_config_t* config)
{
    if (config == NULL) {
        return -1;
    }
    
    /* Créer ou ouvrir le fichier */
    vfs_node_t* file = vfs_open(CONFIG_NETWORK_FILE, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (file == NULL) {
        /* Essayer de créer le fichier */
        if (vfs_create(CONFIG_NETWORK_FILE) != 0) {
            KLOG_ERROR("CONFIG", "Failed to create network.conf");
            return -1;
        }
        file = vfs_open(CONFIG_NETWORK_FILE, VFS_O_WRONLY);
        if (file == NULL) {
            return -1;
        }
    }
    
    /* Construire le contenu */
    char buffer[512];
    char ip_str[16];
    int offset = 0;
    
    /* Header */
    const char* header = "# ALOS Network Configuration\n# Edit this file to configure static IP\n\n";
    strcpy(buffer, header);
    offset = strlen(buffer);
    
    /* DHCP */
    const char* dhcp_line = config->use_dhcp ? "dhcp=yes\n\n" : "dhcp=no\n\n";
    strcpy(buffer + offset, dhcp_line);
    offset += strlen(dhcp_line);
    
    /* Configuration statique (si DHCP désactivé) */
    if (!config->use_dhcp) {
        strcpy(buffer + offset, "# Static IP configuration\n");
        offset += strlen("# Static IP configuration\n");
        
        config_ip_to_string(config->ip_addr, ip_str);
        strcpy(buffer + offset, "ip=");
        offset += 3;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
        
        config_ip_to_string(config->netmask, ip_str);
        strcpy(buffer + offset, "netmask=");
        offset += 8;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
        
        config_ip_to_string(config->gateway, ip_str);
        strcpy(buffer + offset, "gateway=");
        offset += 8;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
        
        config_ip_to_string(config->dns_server, ip_str);
        strcpy(buffer + offset, "dns=");
        offset += 4;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
    }
    
    buffer[offset] = '\0';
    
    /* Écrire dans le fichier */
    int written = vfs_write(file, 0, offset, (uint8_t*)buffer);
    vfs_close(file);
    
    if (written != offset) {
        KLOG_ERROR("CONFIG", "Failed to write network.conf");
        return -1;
    }
    
    KLOG_INFO("CONFIG", "Saved network configuration");
    return 0;
}

int config_apply_network(const network_config_t* config)
{
    if (config == NULL) {
        return -1;
    }
    
    NetInterface* netif = netif_get_default();
    if (netif == NULL) {
        KLOG_ERROR("CONFIG", "No network interface available");
        return -1;
    }
    
    if (config->use_dhcp) {
        /* DHCP sera géré par le code existant */
        KLOG_INFO("CONFIG", "Using DHCP configuration");
        netif->flags |= NETIF_FLAG_DHCP;
    } else {
        /* Configuration statique */
        netif->flags &= ~NETIF_FLAG_DHCP;
        
        netif->ip_addr = ip_bytes_to_u32(config->ip_addr);
        netif->netmask = ip_bytes_to_u32(config->netmask);
        netif->gateway = ip_bytes_to_u32(config->gateway);
        netif->dns_server = ip_bytes_to_u32(config->dns_server);
        
        KLOG_INFO("CONFIG", "Applied static IP configuration");
    }
    
    return 0;
}

/* ========================================
 * Configuration réseau par interface
 * ======================================== */

/**
 * Construit le chemin du fichier de config pour une interface.
 * Ex: "eth0" -> "/config/network-eth0.conf"
 */
static void build_iface_config_path(const char* iface, char* path, size_t path_size)
{
    strncpy(path, "/config/network-", path_size - 1);
    size_t len = strlen(path);
    strncpy(path + len, iface, path_size - len - 6);
    len = strlen(path);
    strncpy(path + len, ".conf", path_size - len - 1);
    path[path_size - 1] = '\0';
}

int config_load_network_iface(const char* iface, network_config_t* config)
{
    if (iface == NULL || config == NULL) {
        return -1;
    }
    
    /* Construire le chemin du fichier */
    char filepath[128];
    build_iface_config_path(iface, filepath, sizeof(filepath));
    
    /* Valeurs par défaut: DHCP activé */
    config->use_dhcp = 1;
    memset(config->ip_addr, 0, 4);
    memset(config->netmask, 0, 4);
    memset(config->gateway, 0, 4);
    memset(config->dns_server, 0, 4);
    
    /* Ouvrir le fichier de configuration */
    vfs_node_t* file = vfs_open(filepath, VFS_O_RDONLY);
    if (file == NULL) {
        /* Fichier absent, utiliser les valeurs par défaut (DHCP) */
        return -1;
    }
    
    /* Lire le fichier ligne par ligne */
    uint8_t buffer[1024];
    int bytes = vfs_read(file, 0, sizeof(buffer) - 1, buffer);
    vfs_close(file);
    
    if (bytes <= 0) {
        return -1;
    }
    
    buffer[bytes] = '\0';
    
    /* Parser chaque ligne */
    char line[CONFIG_LINE_MAX];
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VALUE_MAX];
    int line_start = 0;
    
    for (int i = 0; i <= bytes; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\0') {
            int line_len = i - line_start;
            if (line_len > 0 && line_len < CONFIG_LINE_MAX) {
                strncpy(line, (char*)&buffer[line_start], line_len);
                line[line_len] = '\0';
                
                /* Ignorer les commentaires et lignes vides */
                char* p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (*p != '#' && *p != ';' && *p != '\0' && *p != '\n') {
                    if (config_parse_line(line, key, value) == 0) {
                        if (strcmp(key, "dhcp") == 0) {
                            config->use_dhcp = (strcmp(value, "yes") == 0 || strcmp(value, "1") == 0);
                        } else if (strcmp(key, "ip") == 0) {
                            config_parse_ip(value, config->ip_addr);
                        } else if (strcmp(key, "netmask") == 0) {
                            config_parse_ip(value, config->netmask);
                        } else if (strcmp(key, "gateway") == 0) {
                            config_parse_ip(value, config->gateway);
                        } else if (strcmp(key, "dns") == 0) {
                            config_parse_ip(value, config->dns_server);
                        }
                    }
                }
            }
            line_start = i + 1;
        }
    }
    
    return 0;
}

int config_save_network_iface(const char* iface, const network_config_t* config)
{
    if (iface == NULL || config == NULL) {
        return -1;
    }
    
    /* Construire le chemin du fichier */
    char filepath[128];
    build_iface_config_path(iface, filepath, sizeof(filepath));
    
    /* Créer ou ouvrir le fichier */
    vfs_node_t* file = vfs_open(filepath, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (file == NULL) {
        /* Essayer de créer le fichier */
        if (vfs_create(filepath) != 0) {
            KLOG_ERROR("CONFIG", "Failed to create config file");
            return -1;
        }
        file = vfs_open(filepath, VFS_O_WRONLY);
        if (file == NULL) {
            return -1;
        }
    }
    
    /* Construire le contenu */
    char buffer[512];
    char ip_str[16];
    int offset = 0;
    
    /* Header */
    strcpy(buffer, "# ALOS Network Configuration for ");
    offset = strlen(buffer);
    strcpy(buffer + offset, iface);
    offset += strlen(iface);
    strcpy(buffer + offset, "\n\n");
    offset += 2;
    
    /* DHCP */
    const char* dhcp_line = config->use_dhcp ? "dhcp=yes\n\n" : "dhcp=no\n\n";
    strcpy(buffer + offset, dhcp_line);
    offset += strlen(dhcp_line);
    
    /* Configuration statique (si DHCP désactivé) */
    if (!config->use_dhcp) {
        strcpy(buffer + offset, "# Static IP configuration\n");
        offset += strlen("# Static IP configuration\n");
        
        config_ip_to_string(config->ip_addr, ip_str);
        strcpy(buffer + offset, "ip=");
        offset += 3;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
        
        config_ip_to_string(config->netmask, ip_str);
        strcpy(buffer + offset, "netmask=");
        offset += 8;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
        
        config_ip_to_string(config->gateway, ip_str);
        strcpy(buffer + offset, "gateway=");
        offset += 8;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
        
        config_ip_to_string(config->dns_server, ip_str);
        strcpy(buffer + offset, "dns=");
        offset += 4;
        strcpy(buffer + offset, ip_str);
        offset += strlen(ip_str);
        buffer[offset++] = '\n';
    }
    
    buffer[offset] = '\0';
    
    /* Écrire dans le fichier */
    int written = vfs_write(file, 0, offset, (uint8_t*)buffer);
    vfs_close(file);
    
    if (written != offset) {
        KLOG_ERROR("CONFIG", "Failed to write config file");
        return -1;
    }
    
    return 0;
}

int config_apply_network_iface(const char* iface, const network_config_t* config)
{
    if (iface == NULL || config == NULL) {
        return -1;
    }
    
    NetInterface* netif = netif_get_by_name(iface);
    if (netif == NULL) {
        KLOG_ERROR("CONFIG", "Interface not found");
        return -1;
    }
    
    if (config->use_dhcp) {
        KLOG_INFO("CONFIG", "Using DHCP for interface");
        netif->flags |= NETIF_FLAG_DHCP;
    } else {
        netif->flags &= ~NETIF_FLAG_DHCP;
        
        netif->ip_addr = ip_bytes_to_u32(config->ip_addr);
        netif->netmask = ip_bytes_to_u32(config->netmask);
        netif->gateway = ip_bytes_to_u32(config->gateway);
        netif->dns_server = ip_bytes_to_u32(config->dns_server);
        
        KLOG_INFO("CONFIG", "Applied static IP to interface");
    }
    
    return 0;
}

/* ========================================
 * Scripts
 * ======================================== */

int config_run_script(const char* path)
{
    if (path == NULL) {
        return -1;
    }
    
    /* Ouvrir le fichier script */
    vfs_node_t* file = vfs_open(path, VFS_O_RDONLY);
    if (file == NULL) {
        return -1;
    }
    
    /* Lire le contenu */
    uint8_t buffer[4096];
    int bytes = vfs_read(file, 0, sizeof(buffer) - 1, buffer);
    vfs_close(file);
    
    if (bytes <= 0) {
        return -1;
    }
    
    buffer[bytes] = '\0';
    
    /* Parser et exécuter chaque ligne */
    char line[CONFIG_LINE_MAX];
    char* argv[16];
    int line_start = 0;
    int line_count = 0;
    
    for (int i = 0; i <= bytes; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\0') {
            int line_len = i - line_start;
            
            if (line_len > 0 && line_len < CONFIG_LINE_MAX) {
                strncpy(line, (char*)&buffer[line_start], line_len);
                line[line_len] = '\0';
                
                /* Ignorer les commentaires et lignes vides */
                char* trimmed = trim(line);
                if (!is_comment_or_empty(trimmed)) {
                    line_count++;
                    
                    /* Parser la ligne en arguments */
                    int argc = 0;
                    char* token = strtok(trimmed, " \t");
                    while (token != NULL && argc < 16) {
                        argv[argc++] = token;
                        token = strtok(NULL, " \t");
                    }
                    
                    if (argc > 0) {
                        /* Exécuter la commande */
                        command_execute(argc, argv);
                    }
                }
            }
            line_start = i + 1;
        }
    }
    
    return 0;
}

int config_run_startup_script(void)
{
    return config_run_script(CONFIG_STARTUP_SCRIPT);
}

/* ========================================
 * Historique persistant
 * ======================================== */

int config_load_history(char history[][256], int max_entries, int line_max)
{
    /* Ouvrir le fichier d'historique */
    vfs_node_t* file = vfs_open(CONFIG_HISTORY_FILE, VFS_O_RDONLY);
    if (file == NULL) {
        return 0;  /* Pas d'historique existant */
    }
    
    /* Lire le contenu */
    uint8_t buffer[8192];  /* Max 8KB d'historique */
    int bytes = vfs_read(file, 0, sizeof(buffer) - 1, buffer);
    vfs_close(file);
    
    if (bytes <= 0) {
        return 0;
    }
    
    buffer[bytes] = '\0';
    
    /* Parser chaque ligne */
    int count = 0;
    int line_start = 0;
    
    for (int i = 0; i <= bytes && count < max_entries; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\0') {
            int line_len = i - line_start;
            
            if (line_len > 0 && line_len < line_max) {
                strncpy(history[count], (char*)&buffer[line_start], line_len);
                history[count][line_len] = '\0';
                count++;
            }
            line_start = i + 1;
        }
    }
    
    KLOG_INFO("CONFIG", "Loaded shell history");
    return count;
}

int config_save_history(char history[][256], int count, int start_index, int line_max)
{
    (void)line_max;  /* Paramètre non utilisé pour l'instant */
    
    if (count <= 0) {
        return 0;  /* Rien à sauvegarder */
    }
    
    /* Créer le fichier si nécessaire */
    vfs_node_t* file = vfs_open(CONFIG_HISTORY_FILE, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (file == NULL) {
        if (vfs_create(CONFIG_HISTORY_FILE) != 0) {
            KLOG_ERROR("CONFIG", "Failed to create history file");
            return -1;
        }
        file = vfs_open(CONFIG_HISTORY_FILE, VFS_O_WRONLY);
        if (file == NULL) {
            return -1;
        }
    }
    
    /* Construire le contenu */
    uint8_t buffer[8192];
    int offset = 0;
    int max_size = sizeof(buffer) - 1;
    
    /* Écrire les entrées dans l'ordre chronologique */
    int actual_count = count;
    int max_entries = 16;  /* SHELL_HISTORY_SIZE */
    
    for (int i = 0; i < actual_count && offset < max_size; i++) {
        int idx = (start_index + i) % max_entries;
        int len = strlen(history[idx]);
        
        if (len > 0 && offset + len + 1 < max_size) {
            memcpy(buffer + offset, history[idx], len);
            offset += len;
            buffer[offset++] = '\n';
        }
    }
    
    /* Écrire dans le fichier */
    int written = vfs_write(file, 0, offset, buffer);
    vfs_close(file);
    
    if (written != offset) {
        KLOG_ERROR("CONFIG", "Failed to write history file");
        return -1;
    }
    
    return 0;
}
