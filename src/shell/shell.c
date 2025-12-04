/* src/shell/shell.c - ALOS Command Shell Implementation */
#include "shell.h"
#include <stdint.h>
#include "commands.h"
#include "../kernel/console.h"
#include "../kernel/keyboard.h"
#include "../kernel/process.h"
#include "../kernel/klog.h"
#include "../include/string.h"
#include "../fs/vfs.h"
#include "../config/config.h"

/* Type signé pour les tailles (compatible 64 bits) */
typedef int64_t ssize_t;

/* Codes spéciaux clavier (définis dans keyboard.c) - castés en char pour éviter les warnings */
#define KEY_UP      ((char)0x80)
#define KEY_DOWN    ((char)0x81)
#define KEY_LEFT    ((char)0x82)
#define KEY_RIGHT   ((char)0x83)
#define KEY_CTRL_C  ((char)0x03)

/* Répertoire de travail courant */
static char cwd[SHELL_PATH_MAX] = "/";

/* Historique des commandes */
static char history[SHELL_HISTORY_SIZE][SHELL_LINE_MAX];
static size_t history_count = 0;      /* Nombre de commandes dans l'historique */
static size_t history_index = 0;      /* Position d'écriture (circulaire) */
static ssize_t history_nav = -1;      /* Position de navigation (-1 = pas de navigation) */

/* ========================================
 * Fonctions utilitaires internes
 * ======================================== */

/**
 * Ajoute une commande à l'historique.
 */
static void history_add(const char* line)
{
    /* Ne pas ajouter les lignes vides ou les doublons consécutifs */
    if (line[0] == '\0') {
        return;
    }
    
    /* Vérifier le doublon avec la dernière commande */
    if (history_count > 0) {
        int last = (history_index - 1 + SHELL_HISTORY_SIZE) % SHELL_HISTORY_SIZE;
        if (strcmp(history[last], line) == 0) {
            return;
        }
    }
    
    /* Copier la commande */
    strncpy(history[history_index], line, SHELL_LINE_MAX - 1);
    history[history_index][SHELL_LINE_MAX - 1] = '\0';
    
    /* Avancer l'index */
    history_index = (history_index + 1) % SHELL_HISTORY_SIZE;
    if (history_count < SHELL_HISTORY_SIZE) {
        history_count++;
    }
}

/**
 * Récupère une commande de l'historique.
 * @param offset  0 = dernière commande, 1 = avant-dernière, etc.
 * @return Pointeur vers la commande ou NULL
 */
static const char* history_get(ssize_t offset)
{
    if (offset < 0 || offset >= (ssize_t)history_count) {
        return NULL;
    }
    
    size_t idx = (history_index - 1 - (size_t)offset + SHELL_HISTORY_SIZE) % SHELL_HISTORY_SIZE;
    return history[idx];
}

/**
 * Lit une ligne de commande avec support de l'édition et de l'historique.
 */
static void shell_readline(char* buffer, size_t max_len)
{
    size_t pos = 0;
    size_t len = 0;
    history_nav = -1;  /* Reset de la navigation historique */
    
    buffer[0] = '\0';
    
    while (1) {
        char c = keyboard_getchar();
        
        switch (c) {
            case KEY_CTRL_C:
                /* CTRL+C - tuer les tâches et annuler la ligne */
                kill_all_user_tasks();
                console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                console_puts("^C");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                console_putc('\n');
                console_refresh();
                /* Retourner une ligne vide pour revenir au prompt */
                buffer[0] = '\0';
                return;
                
            case '\n':
                /* Enter - fin de la ligne */
                console_putc('\n');
                console_refresh();
                buffer[len] = '\0';
                return;
                
            case '\b':
                /* Backspace */
                if (pos > 0) {
                    /* Déplacer les caractères après le curseur */
                    for (size_t i = pos - 1; i < len - 1; i++) {
                        buffer[i] = buffer[i + 1];
                    }
                    pos--;
                    len--;
                    buffer[len] = '\0';
                    
                    /* Réafficher la ligne */
                    console_putc('\b');
                    for (size_t i = pos; i < len; i++) {
                        console_putc(buffer[i]);
                    }
                    console_putc(' ');  /* Effacer le dernier caractère */
                    /* Repositionner le curseur */
                    for (size_t i = pos; i <= len; i++) {
                        console_putc('\b');
                    }
                    console_refresh();
                }
                break;
                
            case KEY_UP:
                /* Flèche haut - commande précédente */
                if (history_nav < (ssize_t)history_count - 1) {
                    history_nav++;
                    const char* hist = history_get(history_nav);
                    if (hist) {
                        /* Effacer la ligne actuelle: retour au début */
                        while (pos > 0) {
                            console_putc('\b');
                            pos--;
                        }
                        /* Effacer avec des espaces */
                        for (size_t i = 0; i < len; i++) {
                            console_putc(' ');
                        }
                        /* Retour au début */
                        for (size_t i = 0; i < len; i++) {
                            console_putc('\b');
                        }
                        
                        /* Copier et afficher la commande historique */
                        strncpy(buffer, hist, max_len - 1);
                        buffer[max_len - 1] = '\0';
                        len = strlen(buffer);
                        pos = len;
                        console_puts(buffer);
                        console_refresh();
                    }
                }
                break;
                
            case KEY_DOWN:
                /* Flèche bas - commande suivante */
                if (history_nav >= 0) {
                    /* Effacer la ligne actuelle: retour au début */
                    while (pos > 0) {
                        console_putc('\b');
                        pos--;
                    }
                    /* Effacer avec des espaces */
                    for (size_t i = 0; i < len; i++) {
                        console_putc(' ');
                    }
                    /* Retour au début */
                    for (size_t i = 0; i < len; i++) {
                        console_putc('\b');
                    }
                    
                    history_nav--;
                    if (history_nav >= 0) {
                        const char* hist = history_get(history_nav);
                        if (hist) {
                            strncpy(buffer, hist, max_len - 1);
                            buffer[max_len - 1] = '\0';
                            len = strlen(buffer);
                            pos = len;
                            console_puts(buffer);
                        }
                    } else {
                        /* Revenir à une ligne vide */
                        buffer[0] = '\0';
                        len = 0;
                        pos = 0;
                    }
                    console_refresh();
                }
                break;
                
            case KEY_LEFT:
            case KEY_RIGHT:
                /* TODO: Déplacement du curseur dans la ligne (optionnel) */
                break;
                
            default:
                /* Caractère normal */
                if (c >= 32 && c < 127 && len < max_len - 1) {
                    /* Insérer le caractère à la position courante */
                    for (size_t i = len; i > pos; i--) {
                        buffer[i] = buffer[i - 1];
                    }
                    buffer[pos] = c;
                    pos++;
                    len++;
                    buffer[len] = '\0';
                    
                    /* Afficher le caractère */
                    console_putc(c);
                    
                    /* Réafficher la suite si nécessaire */
                    for (size_t i = pos; i < len; i++) {
                        console_putc(buffer[i]);
                    }
                    /* Repositionner le curseur */
                    for (size_t i = pos; i < len; i++) {
                        console_putc('\b');
                    }
                    console_refresh();
                }
                break;
        }
    }
}

/**
 * Parse une ligne de commande en arguments (tokenization).
 * @param line  Ligne à parser (sera modifiée)
 * @param argv  Tableau de pointeurs vers les arguments
 * @param max   Nombre maximum d'arguments
 * @return Nombre d'arguments (argc)
 */
static int shell_parse(char* line, char** argv, int max)
{
    int argc = 0;
    char* token = strtok(line, " \t");
    
    while (token != NULL && argc < max) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    
    return argc;
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

const char* shell_get_cwd(void)
{
    return cwd;
}

int shell_set_cwd(const char* path)
{
    char resolved[SHELL_PATH_MAX];
    
    /* Résoudre le chemin */
    if (shell_resolve_path(path, resolved, sizeof(resolved)) != 0) {
        return -1;
    }
    
    /* Vérifier que le répertoire existe */
    vfs_node_t* node = vfs_resolve_path(resolved);
    if (node == NULL) {
        return -1;
    }
    
    /* Vérifier que c'est un répertoire */
    if (node->type != VFS_DIRECTORY) {
        return -1;
    }
    
    /* Mettre à jour le cwd */
    strncpy(cwd, resolved, SHELL_PATH_MAX - 1);
    cwd[SHELL_PATH_MAX - 1] = '\0';
    
    return 0;
}

int shell_resolve_path(const char* path, char* result, size_t size)
{
    if (path == NULL || result == NULL || size == 0) {
        return -1;
    }
    
    char temp[SHELL_PATH_MAX];
    
    /* Chemin absolu */
    if (path[0] == '/') {
        strncpy(temp, path, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
    } else {
        /* Chemin relatif - concaténer avec cwd */
        size_t cwd_len = strlen(cwd);
        
        /* Copier le cwd */
        strncpy(temp, cwd, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        
        /* Ajouter un '/' si nécessaire */
        if (cwd_len > 0 && cwd[cwd_len - 1] != '/' && cwd_len < sizeof(temp) - 1) {
            temp[cwd_len] = '/';
            temp[cwd_len + 1] = '\0';
        }
        
        /* Ajouter le chemin relatif */
        size_t temp_len = strlen(temp);
        size_t path_len = strlen(path);
        if (temp_len + path_len < sizeof(temp)) {
            strcat(temp, path);
        } else {
            return -1;  /* Chemin trop long */
        }
    }
    
    /* Normaliser le chemin (résoudre . et ..) */
    char* components[64];  /* Max 64 niveaux de profondeur */
    int depth = 0;
    
    /* Tokenizer le chemin */
    char* p = temp;
    while (*p == '/') p++;  /* Sauter les / initiaux */
    
    while (*p != '\0') {
        /* Début du composant */
        char* start = p;
        
        /* Trouver la fin du composant */
        while (*p != '/' && *p != '\0') p++;
        
        /* Calculer la longueur */
        size_t comp_len = p - start;
        
        /* Terminer temporairement le composant */
        char saved = *p;
        *p = '\0';
        
        if (comp_len == 0 || (comp_len == 1 && start[0] == '.')) {
            /* Composant vide ou "." - ignorer */
        } else if (comp_len == 2 && start[0] == '.' && start[1] == '.') {
            /* ".." - remonter d'un niveau */
            if (depth > 0) {
                depth--;
            }
        } else {
            /* Composant normal - ajouter */
            if (depth < 64) {
                components[depth++] = start;
            }
        }
        
        /* Restaurer et avancer */
        *p = saved;
        while (*p == '/') p++;
    }
    
    /* Reconstruire le chemin normalisé */
    if (depth == 0) {
        /* Racine */
        if (size >= 2) {
            result[0] = '/';
            result[1] = '\0';
        } else {
            return -1;
        }
    } else {
        size_t pos = 0;
        for (int i = 0; i < depth; i++) {
            /* Ajouter / */
            if (pos < size - 1) {
                result[pos++] = '/';
            }
            /* Ajouter le composant - s'arrêter au / ou \0 */
            const char* comp = components[i];
            while (*comp != '\0' && *comp != '/' && pos < size - 1) {
                result[pos++] = *comp++;
            }
        }
        result[pos] = '\0';
    }
    
    return 0;
}

void shell_init(void)
{
    /* Initialiser le cwd à la racine */
    strcpy(cwd, "/");
    
    /* Initialiser l'historique */
    history_count = 0;
    history_index = 0;
    history_nav = -1;
    
    /* Charger l'historique persistant depuis /config/history */
    int loaded = config_load_history(history, SHELL_HISTORY_SIZE, SHELL_LINE_MAX);
    if (loaded > 0) {
        history_count = (size_t)loaded;
        history_index = (size_t)loaded % SHELL_HISTORY_SIZE;
        console_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        console_puts("[Shell] Loaded ");
        console_put_dec(loaded);
        console_puts(" history entries\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
    
    /* Initialiser les commandes */
    commands_init();
}

void shell_save_history(void)
{
    /* Calculer l'index de départ pour l'historique circulaire */
    size_t start_index = 0;
    if (history_count >= SHELL_HISTORY_SIZE) {
        start_index = history_index;  /* Buffer plein, commencer à l'index courant */
    }
    
    config_save_history(history, (int)history_count, (int)start_index, SHELL_LINE_MAX);
}

void shell_run(void)
{
    char line[SHELL_LINE_MAX];
    char* argv[SHELL_ARGS_MAX];
    
    /* AUTO-START: Run server command for testing */
    {
        char* test_argv[] = { "server", NULL };
        command_execute(1, test_argv);
    }
    
    while (1) {
        /* Afficher le prompt avec le cwd */
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("alos:");
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts(cwd);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        console_puts("$ ");
        console_refresh();
        
        KLOG_INFO("SHELL", "Prompt displayed, waiting for input...");
        
        /* Lire une ligne */
        shell_readline(line, sizeof(line));
        
        /* Ignorer les lignes vides */
        if (line[0] == '\0') {
            continue;
        }
        
        /* Ajouter à l'historique */
        history_add(line);
        shell_save_history();  /* Sauvegarde immédiate pour persistance */

        /* Parser la ligne */
        int argc = shell_parse(line, argv, SHELL_ARGS_MAX);
        
        if (argc > 0) {
            /* Exécuter la commande */
            int ret = command_execute(argc, argv);
            (void)ret;  /* Ignorer le code de retour pour l'instant */
        }
        
        /* Rafraîchir l'affichage */
        console_refresh();
    }
}
