/* src/shell/commands.c - Shell Commands Implementation */
#include "commands.h"
#include "shell.h"
#include "../kernel/console.h"
#include "../kernel/keyboard.h"
#include "../kernel/keymap.h"
#include "../kernel/process.h"
#include "../kernel/elf.h"
#include "../include/string.h"
#include "../net/l3/icmp.h"
#include "../net/core/netdev.h"
#include "../arch/x86/usermode.h"
#include "../fs/vfs.h"
#include "../config/config.h"

/* ========================================
 * Déclarations des handlers de commandes
 * ======================================== */

static int cmd_help(int argc, char** argv);
static int cmd_ping(int argc, char** argv);
static int cmd_tasks(int argc, char** argv);
static int cmd_ps(int argc, char** argv);
static int cmd_usermode(int argc, char** argv);
static int cmd_exec(int argc, char** argv);
static int cmd_elfinfo(int argc, char** argv);
static int cmd_netinfo(int argc, char** argv);
static int cmd_keymap(int argc, char** argv);
static int cmd_script(int argc, char** argv);
static int cmd_netconf(int argc, char** argv);
static int cmd_savehist(int argc, char** argv);

/* TODO: Implémenter ces commandes */
// static int cmd_clear(int argc, char** argv);
// static int cmd_ls(int argc, char** argv);
// static int cmd_cat(int argc, char** argv);
// static int cmd_cd(int argc, char** argv);
// static int cmd_pwd(int argc, char** argv);
// static int cmd_mkdir(int argc, char** argv);
// static int cmd_touch(int argc, char** argv);
// static int cmd_echo(int argc, char** argv);
// static int cmd_meminfo(int argc, char** argv);

/* ========================================
 * Table des commandes
 * ======================================== */

static shell_command_t commands[] = {
    /* Commandes implémentées */
    { "help",     "Display available commands",              cmd_help },
    { "ping",     "Ping a host (IP or hostname)",           cmd_ping },
    { "tasks",    "Test multitasking (launches 2 threads)",  cmd_tasks },
    { "ps",       "List running processes",                  cmd_ps },
    { "usermode", "Test User Mode (Ring 3) - EXPERIMENTAL",  cmd_usermode },
    { "exec",     "Execute an ELF program",                  cmd_exec },
    { "elfinfo",  "Display ELF file information",            cmd_elfinfo },
    { "netinfo",  "Display network configuration",           cmd_netinfo },
    { "keymap",   "Set keyboard layout (qwerty, azerty)",    cmd_keymap },
    { "script",   "Run a script file (/config/startup.sh)",  cmd_script },
    { "netconf",  "Configure network interface (eth0, etc.)", cmd_netconf },
    { "savehist", "Save command history to disk",            cmd_savehist },
    
    /* TODO: Commandes à implémenter */
    // { "clear",   "Clear the screen",                       cmd_clear },
    // { "ls",      "List directory contents",                cmd_ls },
    // { "cat",     "Display file contents",                  cmd_cat },
    // { "cd",      "Change directory",                       cmd_cd },
    // { "pwd",     "Print working directory",                cmd_pwd },
    // { "mkdir",   "Create a directory",                     cmd_mkdir },
    // { "touch",   "Create an empty file",                   cmd_touch },
    // { "echo",    "Display a message",                      cmd_echo },
    // { "meminfo", "Display memory information",             cmd_meminfo },
    
    /* Marqueur de fin */
    { NULL, NULL, NULL }
};

/* ========================================
 * Fonctions publiques
 * ======================================== */

void commands_init(void)
{
    /* Rien à initialiser pour l'instant */
}

int command_execute(int argc, char** argv)
{
    if (argc == 0 || argv[0] == NULL) {
        return -1;
    }
    
    /* Chercher la commande */
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            return commands[i].handler(argc, argv);
        }
    }
    
    /* Commande non trouvée - essayer d'exécuter un ELF dans /bin/ */
    char bin_path[256];
    
    /* Si la commande commence par '/', c'est un chemin absolu */
    if (argv[0][0] == '/') {
        strncpy(bin_path, argv[0], sizeof(bin_path) - 1);
        bin_path[sizeof(bin_path) - 1] = '\0';
    } else {
        /* Sinon, chercher dans /bin/ */
        strcpy(bin_path, "/bin/");
        strncpy(bin_path + 5, argv[0], sizeof(bin_path) - 6);
        bin_path[sizeof(bin_path) - 1] = '\0';
    }
    
    /* Vérifier si le fichier existe dans le VFS */
    vfs_node_t* node = vfs_resolve_path(bin_path);
    if (node != NULL && (node->type & VFS_FILE)) {
        /* Fichier trouvé - l'exécuter */
        console_puts("\n");
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts("=== Executing ELF Program ===");
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        int result = process_exec_and_wait(bin_path);
        
        if (result < 0) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("Failed to execute program.\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
        
        return result;
    }
    
    /* Commande vraiment non trouvée */
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("Unknown command: ");
    console_puts(argv[0]);
    console_puts("\nType 'help' for available commands.\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return -1;
}

/* ========================================
 * Implémentation des commandes
 * ======================================== */

/**
 * Commande: help
 * Affiche la liste des commandes disponibles.
 */
static int cmd_help(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("\nAvailable commands:\n");
    console_puts("-------------------\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    for (int i = 0; commands[i].name != NULL; i++) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("  ");
        console_puts(commands[i].name);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        /* Padding pour aligner les descriptions */
        int name_len = strlen(commands[i].name);
        for (int j = name_len; j < 12; j++) {
            console_putc(' ');
        }
        
        console_puts("- ");
        console_puts(commands[i].description);
        console_putc('\n');
    }
    
    console_puts("\n");
    
    /* Afficher les commandes à venir (TODO) */
    console_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    console_puts("Coming soon: clear, ls, cat, cd, pwd, mkdir, touch, echo, meminfo\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}

/**
 * Parse une adresse IP du format "x.x.x.x" vers 4 octets.
 * @return 0 si succès, -1 si erreur de format
 */
static int parse_ip(const char* str, uint8_t* ip)
{
    int octet = 0;
    int value = 0;
    int digits = 0;
    
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            value = value * 10 + (str[i] - '0');
            digits++;
            if (value > 255) {
                return -1;  /* Octet trop grand */
            }
        } else if (str[i] == '.') {
            if (digits == 0 || octet >= 3) {
                return -1;  /* Format invalide */
            }
            ip[octet++] = (uint8_t)value;
            value = 0;
            digits = 0;
        } else {
            return -1;  /* Caractère invalide */
        }
    }
    
    /* Dernier octet */
    if (digits == 0 || octet != 3) {
        return -1;
    }
    ip[octet] = (uint8_t)value;
    
    return 0;
}

/**
 * Commande: ping <host>
 * Envoie un ping ICMP vers un hôte (IP ou hostname).
 */
static int cmd_ping(int argc, char** argv)
{
    if (argc < 2) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Usage: ping <ip_address|hostname>\n");
        console_puts("Examples:\n");
        console_puts("  ping 10.0.2.2\n");
        console_puts("  ping google.com\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    const char* target = argv[1];
    uint8_t ip[4];
    
    /* Essayer de parser comme une adresse IP */
    if (parse_ip(target, ip) == 0) {
        /* C'est une adresse IP */
        return ping_ip(ip);
    } else {
        /* C'est probablement un hostname - utiliser la résolution DNS */
        return ping(target);
    }
}

/**
 * Commande: netinfo
 * Affiche la configuration réseau de toutes les interfaces.
 */
static int cmd_netinfo(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    /* Utilise la fonction existante de netdev qui affiche 
     * toutes les informations réseau de manière formatée */
    netdev_ipconfig_display();
    
    return 0;
}

/* ========================================
 * TODO: Commandes à implémenter
 * ======================================== */

/*
 * static int cmd_clear(int argc, char** argv)
 * {
 *     // TODO: Effacer l'écran
 *     // Utiliser console_clear(VGA_COLOR_BLACK);
 *     return 0;
 * }
 */

/*
 * static int cmd_ls(int argc, char** argv)
 * {
 *     // TODO: Lister le contenu d'un répertoire
 *     // 1. Déterminer le chemin (argv[1] ou cwd)
 *     // 2. Appeler shell_resolve_path() pour obtenir le chemin absolu
 *     // 3. Utiliser vfs_resolve_path() pour obtenir le noeud
 *     // 4. Parcourir avec vfs_readdir() et afficher les entrées
 *     // 5. Afficher le type (fichier/dossier) et éventuellement la taille
 *     return 0;
 * }
 */

/*
 * static int cmd_cat(int argc, char** argv)
 * {
 *     // TODO: Afficher le contenu d'un fichier
 *     // 1. Vérifier qu'un fichier est spécifié (argv[1])
 *     // 2. Résoudre le chemin avec shell_resolve_path()
 *     // 3. Ouvrir avec vfs_open()
 *     // 4. Lire par blocs avec vfs_read() et afficher
 *     // 5. Fermer avec vfs_close()
 *     return 0;
 * }
 */

/*
 * static int cmd_cd(int argc, char** argv)
 * {
 *     // TODO: Changer de répertoire
 *     // 1. Si pas d'argument, aller à "/"
 *     // 2. Sinon utiliser shell_set_cwd(argv[1])
 *     // 3. Afficher erreur si le répertoire n'existe pas
 *     return 0;
 * }
 */

/*
 * static int cmd_pwd(int argc, char** argv)
 * {
 *     // TODO: Afficher le répertoire courant
 *     // Utiliser shell_get_cwd() et afficher
 *     return 0;
 * }
 */

/*
 * static int cmd_mkdir(int argc, char** argv)
 * {
 *     // TODO: Créer un répertoire
 *     // 1. Vérifier qu'un chemin est spécifié
 *     // 2. Résoudre le chemin
 *     // 3. Appeler vfs_mkdir()
 *     return 0;
 * }
 */

/*
 * static int cmd_touch(int argc, char** argv)
 * {
 *     // TODO: Créer un fichier vide
 *     // 1. Vérifier qu'un chemin est spécifié
 *     // 2. Résoudre le chemin
 *     // 3. Appeler vfs_create()
 *     return 0;
 * }
 */

/*
 * static int cmd_echo(int argc, char** argv)
 * {
 *     // TODO: Afficher un message
 *     // Concaténer argv[1..argc-1] avec des espaces et afficher
 *     return 0;
 * }
 */

/*
 * static int cmd_meminfo(int argc, char** argv)
 * {
 *     // TODO: Afficher les informations mémoire
 *     // Inclure mm/kheap.h et appeler:
 *     // - kheap_get_total_size()
 *     // - kheap_get_free_size()
 *     // - kheap_get_block_count()
 *     // - kheap_get_free_block_count()
 *     return 0;
 * }
 */

/* ========================================
 * Fonctions de test pour le multitasking
 * ======================================== */

/* Compteurs pour les tâches de test */
static volatile int task_a_counter = 0;
static volatile int task_b_counter = 0;

/**
 * Délai actif (busy wait)
 */
static void loop_delay(void)
{
    for (volatile int i = 0; i < 5000000; i++);
}

/**
 * Tâche A - Affiche 'A' périodiquement
 */
static void task_a(void)
{
    while (!should_exit()) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_putc('A');
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        task_a_counter++;
        
        /* Afficher le compteur tous les 10 itérations */
        if (task_a_counter % 10 == 0) {
            console_putc('[');
            console_put_dec(task_a_counter);
            console_putc(']');
        }
        
        loop_delay();
    }
    /* Le thread se termine proprement via process_exit() */
}

/**
 * Tâche B - Affiche 'B' périodiquement
 */
static void task_b(void)
{
    while (!should_exit()) {
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_putc('B');
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        task_b_counter++;
        
        /* Afficher le compteur tous les 10 itérations */
        if (task_b_counter % 10 == 0) {
            console_putc('[');
            console_put_dec(task_b_counter);
            console_putc(']');
        }
        
        loop_delay();
    }
    /* Le thread se termine proprement via process_exit() */
}

/**
 * Commande: tasks
 * Lance deux threads de test pour démontrer le multitasking.
 */
static int cmd_tasks(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_puts("\n=== Multitasking Test ===\n");
    console_puts("Creating two kernel threads...\n");
    console_puts("Press Ctrl+C to stop (not implemented yet)\n\n");
    
    /* Réinitialiser les compteurs */
    task_a_counter = 0;
    task_b_counter = 0;
    
    /* Créer les threads */
    process_t* thread_a = create_kernel_thread(task_a, "task_A");
    process_t* thread_b = create_kernel_thread(task_b, "task_B");
    
    if (thread_a == NULL || thread_b == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("ERROR: Failed to create threads!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    console_puts("Threads created! You should see ABABAB...\n");
    console_puts("(Green=A, Cyan=B)\n\n");
    
    /* Note: Le shell continue, mais les threads tourneront en arrière-plan */
    /* grâce au scheduler appelé par le timer. */
    
    return 0;
}

/**
 * Commande: ps
 * Affiche la liste des processus.
 */
static int cmd_ps(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    process_list_debug();
    
    return 0;
}

/**
 * Commande: usermode
 * Teste le passage en mode utilisateur (Ring 3).
 * 
 * ATTENTION: C'est un test expérimental !
 * Une fois en Ring 3, on ne peut plus revenir (pas de syscalls encore).
 */
static int cmd_usermode(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("=== WARNING: User Mode Test ===\n");
    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    console_puts("This will jump to Ring 3 (User Mode).\n");
    console_puts("There's NO WAY BACK (no syscalls yet)!\n");
    console_puts("If you see a spinner in the top-right corner,\n");
    console_puts("it means User Mode is working!\n\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("Press 'y' to continue, any other key to cancel: ");
    console_refresh();
    
    char c = keyboard_getchar();
    console_putc(c);
    console_putc('\n');
    
    if (c != 'y' && c != 'Y') {
        console_puts("Cancelled.\n");
        return 0;
    }
    
    console_puts("\nJumping to User Mode...\n");
    console_refresh();
    
    /* Le grand saut ! */
    jump_to_usermode(user_mode_test, NULL);
    
    /* On ne devrait JAMAIS arriver ici */
    console_puts("ERROR: Returned from User Mode!?\n");
    return -1;
}

/**
 * Commande: exec <filename>
 * Exécute un programme ELF.
 */
static int cmd_exec(int argc, char** argv)
{
    if (argc < 2) {
        console_puts("Usage: exec <filename>\n");
        console_puts("Example: exec /bin/hello\n");
        return -1;
    }
    
    const char* filename = argv[1];
    
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("=== Executing ELF Program ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Exécuter le programme (bloquant) */
    int result = process_exec_and_wait(filename);
    
    if (result < 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Failed to execute program.\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
    
    return result;
}

/**
 * Commande: elfinfo <filename>
 * Affiche les informations d'un fichier ELF.
 */
static int cmd_elfinfo(int argc, char** argv)
{
    if (argc < 2) {
        console_puts("Usage: elfinfo <filename>\n");
        console_puts("Example: elfinfo /bin/hello\n");
        return -1;
    }
    
    const char* filename = argv[1];
    
    elf_info(filename);
    
    return 0;
}

/**
 * Commande: keymap [list|<layout>]
 * Affiche ou change le layout clavier.
 * 
 * Usage:
 *   keymap        - Affiche le layout actuel
 *   keymap list   - Liste les layouts disponibles
 *   keymap azerty - Change vers le layout AZERTY
 *   keymap qwerty - Change vers le layout QWERTY
 */
static int cmd_keymap(int argc, char** argv)
{
    /* Sans argument: afficher le layout actuel */
    if (argc < 2) {
        const keymap_t* km = keymap_get_current();
        console_puts("Current keyboard layout: ");
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts(km->name);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        console_puts(" (");
        console_puts(km->description);
        console_puts(")\n");
        console_puts("Use 'keymap list' to see available layouts.\n");
        return 0;
    }
    
    /* "keymap list" - lister les layouts disponibles */
    if (strcmp(argv[1], "list") == 0) {
        int count = 0;
        const keymap_t** keymaps = keymap_list_all(&count);
        const keymap_t* current = keymap_get_current();
        
        console_puts("\nAvailable keyboard layouts:\n");
        console_puts("---------------------------\n");
        
        for (int i = 0; i < count; i++) {
            /* Marquer le layout actif avec une étoile */
            if (keymaps[i] == current) {
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                console_puts("* ");
            } else {
                console_puts("  ");
            }
            
            console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            console_puts(keymaps[i]->name);
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            console_puts("\t- ");
            console_puts(keymaps[i]->description);
            console_puts("\n");
        }
        
        console_puts("\nUse 'keymap <name>' to switch layout.\n");
        return 0;
    }
    
    /* "keymap <layout>" - changer de layout */
    const char* layout_name = argv[1];
    
    if (keyboard_set_layout(layout_name)) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("Keyboard layout changed to: ");
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts(layout_name);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        console_puts("\n");
        return 0;
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Unknown layout: ");
        console_puts(layout_name);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        console_puts("Use 'keymap list' to see available layouts.\n");
        return -1;
    }
}

/**
 * Commande: script [path]
 * Exécute un fichier script contenant des commandes shell.
 * 
 * Usage:
 *   script                 - Exécute /config/startup.sh
 *   script /config/test.sh - Exécute le script spécifié
 */
static int cmd_script(int argc, char** argv)
{
    const char* path = CONFIG_STARTUP_SCRIPT;
    
    if (argc >= 2) {
        path = argv[1];
    }
    
    console_puts("\n");
    int result = config_run_script(path);
    
    if (result != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Failed to run script: ");
        console_puts(path);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    return 0;
}

/**
 * Commande: netconf [dhcp|static IP NETMASK GATEWAY DNS]
 * Configure les paramètres réseau.
 * 
 * Usage:
 *   netconf              - Affiche la configuration actuelle
 *   netconf dhcp         - Active DHCP
 *   netconf static 10.0.2.15 255.255.255.0 10.0.2.2 10.0.2.3
 */
static int cmd_netconf(int argc, char** argv)
{
    network_config_t config;
    const char* iface = NULL;
    
    /* Sans argument: afficher l'aide */
    if (argc < 2) {
        console_puts("\nUsage: netconf <interface> [options]\n");
        console_puts("\nOptions:\n");
        console_puts("  netconf eth0                - Show eth0 configuration\n");
        console_puts("  netconf eth0 dhcp           - Configure eth0 for DHCP\n");
        console_puts("  netconf eth0 static <ip> <netmask> <gateway> <dns>\n");
        console_puts("\nExamples:\n");
        console_puts("  netconf eth0 dhcp\n");
        console_puts("  netconf eth0 static 192.168.1.100 255.255.255.0 192.168.1.1 8.8.8.8\n");
        return 0;
    }
    
    /* Premier argument = nom de l'interface */
    iface = argv[1];
    
    /* Vérifier que l'interface existe */
    NetInterface* netif = netif_get_by_name(iface);
    if (netif == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Interface not found: ");
        console_puts(iface);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        console_puts("Use 'netinfo' to list available interfaces.\n");
        return -1;
    }
    
    /* "netconf eth0" - afficher la configuration de l'interface */
    if (argc == 2) {
        if (config_load_network_iface(iface, &config) == 0) {
            console_puts("\nConfiguration for ");
            console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            console_puts(iface);
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            console_puts(":\n");
            console_puts("----------------------------------\n");
            
            if (config.use_dhcp) {
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                console_puts("  Mode: DHCP (automatic)\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            } else {
                console_puts("  Mode: Static IP\n");
                char ip_str[16];
                
                console_puts("  IP:      ");
                config_ip_to_string(config.ip_addr, ip_str);
                console_puts(ip_str);
                console_puts("\n");
                
                console_puts("  Netmask: ");
                config_ip_to_string(config.netmask, ip_str);
                console_puts(ip_str);
                console_puts("\n");
                
                console_puts("  Gateway: ");
                config_ip_to_string(config.gateway, ip_str);
                console_puts(ip_str);
                console_puts("\n");
                
                console_puts("  DNS:     ");
                config_ip_to_string(config.dns_server, ip_str);
                console_puts(ip_str);
                console_puts("\n");
            }
        } else {
            console_puts("\nNo configuration file for ");
            console_puts(iface);
            console_puts(".\nUsing DHCP by default.\n");
        }
        return 0;
    }
    
    /* "netconf eth0 dhcp" - activer DHCP */
    if (strcmp(argv[2], "dhcp") == 0) {
        config.use_dhcp = 1;
        memset(config.ip_addr, 0, 4);
        memset(config.netmask, 0, 4);
        memset(config.gateway, 0, 4);
        memset(config.dns_server, 0, 4);
        
        if (config_save_network_iface(iface, &config) == 0) {
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            console_puts(iface);
            console_puts(" configured for DHCP.\n");
            console_puts("Reboot to apply changes.\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return 0;
        } else {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("Failed to save configuration for ");
            console_puts(iface);
            console_puts(".\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return -1;
        }
    }
    
    /* "netconf eth0 static IP NETMASK GATEWAY DNS" */
    if (strcmp(argv[2], "static") == 0) {
        if (argc < 7) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("Usage: netconf ");
            console_puts(iface);
            console_puts(" static <ip> <netmask> <gateway> <dns>\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return -1;
        }
        
        config.use_dhcp = 0;
        
        if (config_parse_ip(argv[3], config.ip_addr) != 0 ||
            config_parse_ip(argv[4], config.netmask) != 0 ||
            config_parse_ip(argv[5], config.gateway) != 0 ||
            config_parse_ip(argv[6], config.dns_server) != 0) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("Invalid IP address format.\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return -1;
        }
        
        if (config_save_network_iface(iface, &config) == 0) {
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            console_puts("Static IP configuration saved for ");
            console_puts(iface);
            console_puts(".\n");
            console_puts("Reboot to apply changes.\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return 0;
        } else {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("Failed to save configuration for ");
            console_puts(iface);
            console_puts(".\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return -1;
        }
    }
    
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("Unknown option: ");
    console_puts(argv[2]);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return -1;
}

/**
 * Commande: savehist
 * Sauvegarde manuellement l'historique des commandes dans /config/history.
 */
static int cmd_savehist(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    shell_save_history();
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("Command history saved to ");
    console_puts(CONFIG_HISTORY_FILE);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}
