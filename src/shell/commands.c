/* src/shell/commands.c - Shell Commands Implementation */
#include "commands.h"
#include "shell.h"
#include "../kernel/console.h"
#include "../kernel/keyboard.h"
#include "../kernel/keymap.h"
#include "../kernel/process.h"
#include "../kernel/thread.h"
#include "../kernel/elf.h"
#include "../include/string.h"
#include "../net/l3/icmp.h"
#include "../net/core/netdev.h"
#include "../arch/x86/usermode.h"
#include "../fs/vfs.h"
#include "../config/config.h"
#include "../mm/kheap.h"

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

/* Nouvelles commandes filesystem et système */
static int cmd_clear(int argc, char** argv);
static int cmd_ls(int argc, char** argv);
static int cmd_cat(int argc, char** argv);
static int cmd_cd(int argc, char** argv);
static int cmd_pwd(int argc, char** argv);
static int cmd_mkdir(int argc, char** argv);
static int cmd_touch(int argc, char** argv);
static int cmd_echo(int argc, char** argv);
static int cmd_meminfo(int argc, char** argv);
static int cmd_rm(int argc, char** argv);
static int cmd_rmdir(int argc, char** argv);
static int cmd_threads(int argc, char** argv);

/* ========================================
 * Table des commandes
 * ======================================== */

static shell_command_t commands[] = {
    /* Commandes implémentées */
    { "help",     "Display available commands",              cmd_help },
    { "ping",     "Ping a host (IP or hostname)",           cmd_ping },
    { "tasks",    "Test multitasking (launches 2 threads)",  cmd_tasks },
    { "threads",  "Test new multithreading with priorities", cmd_threads },
    { "ps",       "List running processes",                  cmd_ps },
    { "usermode", "Test User Mode (Ring 3) - EXPERIMENTAL",  cmd_usermode },
    { "exec",     "Execute an ELF program",                  cmd_exec },
    { "elfinfo",  "Display ELF file information",            cmd_elfinfo },
    { "netinfo",  "Display network configuration",           cmd_netinfo },
    { "keymap",   "Set keyboard layout (qwerty, azerty)",    cmd_keymap },
    { "script",   "Run a script file (/config/startup.sh)",  cmd_script },
    { "netconf",  "Configure network interface (eth0, etc.)", cmd_netconf },
    { "savehist", "Save command history to disk",            cmd_savehist },
    
    /* Commandes filesystem et système */
    { "clear",   "Clear the screen",                        cmd_clear },
    { "ls",      "List directory contents",                 cmd_ls },
    { "cat",     "Display file contents",                   cmd_cat },
    { "cd",      "Change directory",                        cmd_cd },
    { "pwd",     "Print working directory",                 cmd_pwd },
    { "mkdir",   "Create a directory",                      cmd_mkdir },
    { "touch",   "Create an empty file",                    cmd_touch },
    { "echo",    "Display a message",                       cmd_echo },
    { "meminfo", "Display memory information",              cmd_meminfo },
    { "rm",      "Remove a file",                           cmd_rm },
    { "rmdir",   "Remove an empty directory",               cmd_rmdir },
    
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
        
        /* Passer tous les arguments au programme */
        int result = process_exec_and_wait(bin_path, argc, argv);
        
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
        console_puts("Usage: exec <filename> [args...]\n");
        console_puts("Example: exec /bin/hello\n");
        console_puts("Example: exec /server.elf -p 80\n");
        return -1;
    }
    
    const char* filename = argv[1];
    
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("=== Executing ELF Program ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Préparer les arguments pour le programme (argv[1:]) */
    int prog_argc = argc - 1;  /* Nombre d'arguments pour le programme */
    char** prog_argv = &argv[1];  /* argv[0] du programme = filename */
    
    /* Exécuter le programme (bloquant) avec ses arguments */
    int result = process_exec_and_wait(filename, prog_argc, prog_argv);
    
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

/* ========================================
 * Commandes Filesystem et Système
 * ======================================== */

/**
 * Commande: clear
 * Efface l'écran.
 */
static int cmd_clear(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_clear(VGA_COLOR_BLACK);
    return 0;
}

/**
 * Commande: pwd
 * Affiche le répertoire courant.
 */
static int cmd_pwd(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    const char* cwd = shell_get_cwd();
    console_puts(cwd);
    console_puts("\n");
    
    return 0;
}

/**
 * Commande: cd [path]
 * Change le répertoire courant.
 */
static int cmd_cd(int argc, char** argv)
{
    const char* path = "/";  /* Par défaut, aller à la racine */
    
    if (argc >= 2) {
        path = argv[1];
    }
    
    if (shell_set_cwd(path) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("cd: ");
        console_puts(path);
        console_puts(": No such directory\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    return 0;
}

/**
 * Commande: ls [path]
 * Liste le contenu d'un répertoire.
 */
static int cmd_ls(int argc, char** argv)
{
    char path[SHELL_PATH_MAX];
    
    if (argc >= 2) {
        /* Utiliser le chemin fourni */
        if (shell_resolve_path(argv[1], path, sizeof(path)) != 0) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("ls: ");
            console_puts(argv[1]);
            console_puts(": Invalid path\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return -1;
        }
    } else {
        /* Utiliser le répertoire courant */
        strncpy(path, shell_get_cwd(), sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    
    /* Ouvrir le répertoire */
    vfs_node_t* dir = vfs_resolve_path(path);
    if (dir == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("ls: ");
        console_puts(path);
        console_puts(": No such file or directory\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (!(dir->type & VFS_DIRECTORY)) {
        /* C'est un fichier, afficher juste son nom */
        console_puts(path);
        console_puts("\n");
        return 0;
    }
    
    /* Parcourir le répertoire */
    uint32_t index = 0;
    vfs_dirent_t* entry;
    int count = 0;
    
    console_puts("\n");
    
    while ((entry = vfs_readdir(dir, index)) != NULL) {
        /* Type indicator */
        vfs_node_t* child = vfs_finddir(dir, entry->name);
        
        if (child != NULL && (child->type & VFS_DIRECTORY)) {
            console_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            console_puts("[DIR]  ");
        } else {
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            console_puts("[FILE] ");
        }
        
        /* Taille (alignée sur 8 caractères) */
        if (child != NULL && !(child->type & VFS_DIRECTORY)) {
            char size_buf[12];
            int size = (int)child->size;
            int i = 0;
            
            if (size == 0) {
                size_buf[i++] = '0';
            } else {
                char tmp[12];
                int j = 0;
                while (size > 0) {
                    tmp[j++] = '0' + (size % 10);
                    size /= 10;
                }
                while (j > 0) {
                    size_buf[i++] = tmp[--j];
                }
            }
            size_buf[i] = '\0';
            
            /* Padding */
            int pad = 8 - i;
            while (pad-- > 0) console_putc(' ');
            console_puts(size_buf);
        } else {
            console_puts("       -");
        }
        
        console_puts("  ");
        
        /* Nom */
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        console_puts(entry->name);
        console_puts("\n");
        
        index++;
        count++;
    }
    
    console_puts("\nTotal: ");
    console_put_dec(count);
    console_puts(" items\n");
    
    return 0;
}

/**
 * Commande: cat <file>
 * Affiche le contenu d'un fichier.
 */
static int cmd_cat(int argc, char** argv)
{
    if (argc < 2) {
        console_puts("Usage: cat <filename>\n");
        return -1;
    }
    
    char path[SHELL_PATH_MAX];
    if (shell_resolve_path(argv[1], path, sizeof(path)) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("cat: ");
        console_puts(argv[1]);
        console_puts(": Invalid path\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    vfs_node_t* file = vfs_open(path, VFS_O_RDONLY);
    if (file == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("cat: ");
        console_puts(path);
        console_puts(": No such file\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (file->type & VFS_DIRECTORY) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("cat: ");
        console_puts(path);
        console_puts(": Is a directory\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vfs_close(file);
        return -1;
    }
    
    /* Lire et afficher le contenu */
    uint8_t buf[513];
    uint32_t offset = 0;
    int bytes_read;
    
    console_puts("\n");
    
    while ((bytes_read = vfs_read(file, offset, 512, buf)) > 0) {
        buf[bytes_read] = '\0';
        console_puts((char*)buf);
        offset += bytes_read;
    }
    
    console_puts("\n");
    vfs_close(file);
    
    return 0;
}

/**
 * Commande: mkdir <path>
 * Crée un répertoire.
 */
static int cmd_mkdir(int argc, char** argv)
{
    if (argc < 2) {
        console_puts("Usage: mkdir <dirname>\n");
        return -1;
    }
    
    char path[SHELL_PATH_MAX];
    if (shell_resolve_path(argv[1], path, sizeof(path)) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("mkdir: ");
        console_puts(argv[1]);
        console_puts(": Invalid path\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (vfs_mkdir(path) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("mkdir: cannot create directory '");
        console_puts(path);
        console_puts("'\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("Directory created: ");
    console_puts(path);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}

/**
 * Commande: touch <file>
 * Crée un fichier vide.
 */
static int cmd_touch(int argc, char** argv)
{
    if (argc < 2) {
        console_puts("Usage: touch <filename>\n");
        return -1;
    }
    
    char path[SHELL_PATH_MAX];
    if (shell_resolve_path(argv[1], path, sizeof(path)) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("touch: ");
        console_puts(argv[1]);
        console_puts(": Invalid path\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    /* Vérifier si le fichier existe déjà */
    vfs_node_t* existing = vfs_resolve_path(path);
    if (existing != NULL) {
        /* Le fichier existe déjà - juste retourner succès (comme touch Unix) */
        return 0;
    }
    
    if (vfs_create(path) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("touch: cannot create file '");
        console_puts(path);
        console_puts("'\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    return 0;
}

/**
 * Commande: echo [args...]
 * Affiche les arguments.
 */
static int cmd_echo(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        console_puts(argv[i]);
        if (i < argc - 1) {
            console_putc(' ');
        }
    }
    console_puts("\n");
    
    return 0;
}

/**
 * Commande: meminfo
 * Affiche les informations mémoire.
 */
static int cmd_meminfo(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    size_t total = kheap_get_total_size();
    size_t free_mem = kheap_get_free_size();
    size_t used = total - free_mem;
    size_t blocks = kheap_get_block_count();
    size_t free_blocks = kheap_get_free_block_count();
    
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("============================================\n");
    console_puts("         ALOS Memory Information           \n");
    console_puts("============================================\n\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Heap total */
    console_puts("  Heap Total Size:    ");
    console_put_dec((int)(total / 1024));
    console_puts(" KB (");
    console_put_dec((int)total);
    console_puts(" bytes)\n");
    
    /* Heap libre */
    console_puts("  Heap Free Size:     ");
    console_put_dec((int)(free_mem / 1024));
    console_puts(" KB (");
    console_put_dec((int)free_mem);
    console_puts(" bytes)\n");
    
    /* Heap utilisé */
    console_puts("  Heap Used Size:     ");
    console_put_dec((int)(used / 1024));
    console_puts(" KB (");
    console_put_dec((int)used);
    console_puts(" bytes)\n");
    
    console_puts("\n");
    
    /* Statistiques des blocs */
    console_puts("  Total Blocks:       ");
    console_put_dec((int)blocks);
    console_puts("\n");
    
    console_puts("  Free Blocks:        ");
    console_put_dec((int)free_blocks);
    console_puts("\n");
    
    console_puts("  Used Blocks:        ");
    console_put_dec((int)(blocks - free_blocks));
    console_puts("\n");
    
    /* Pourcentage d'utilisation */
    console_puts("\n");
    if (total > 0) {
        int percent_used = (int)((used * 100) / total);
        console_puts("  Memory Usage:       ");
        console_put_dec(percent_used);
        console_puts("%\n");
    }
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("\n============================================\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}

/* ========================================
 * Commande rm - Supprimer un fichier
 * ======================================== */
static int cmd_rm(int argc, char** argv)
{
    if (argc < 2) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Usage: rm <file>\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    /* Résoudre le chemin */
    char path[SHELL_PATH_MAX];
    if (shell_resolve_path(argv[1], path, sizeof(path)) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("rm: invalid path\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    /* Vérifier que ce n'est pas un répertoire */
    vfs_node_t* node = vfs_resolve_path(path);
    if (node == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts(path);
        console_puts(": No such file\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    if (node->type == VFS_DIRECTORY) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts(path);
        console_puts(": Is a directory (use rmdir)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    /* Supprimer le fichier */
    if (vfs_unlink(path) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("rm: failed to remove '");
        console_puts(path);
        console_puts("'\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    return 0;
}

/* ========================================
 * Commande rmdir - Supprimer un répertoire vide
 * ======================================== */
static int cmd_rmdir(int argc, char** argv)
{
    if (argc < 2) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Usage: rmdir <directory>\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    /* Résoudre le chemin */
    char path[SHELL_PATH_MAX];
    if (shell_resolve_path(argv[1], path, sizeof(path)) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("rmdir: invalid path\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    /* Vérifier que c'est bien un répertoire */
    vfs_node_t* node = vfs_resolve_path(path);
    if (node == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts(path);
        console_puts(": No such directory\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    if (node->type != VFS_DIRECTORY) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts(path);
        console_puts(": Not a directory (use rm)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    /* Supprimer le répertoire */
    if (vfs_rmdir(path) != 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("rmdir: failed to remove '");
        console_puts(path);
        console_puts("' (directory not empty?)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    
    return 0;
}

/* ========================================
 * Commande: threads
 * Teste le nouveau système de multithreading avec priorités
 * ======================================== */

/* Variables pour les threads de test */
static volatile int thread_high_counter = 0;
static volatile int thread_normal_counter = 0;
static volatile int thread_low_counter = 0;

static void high_priority_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 20; i++) {
        if (thread_should_exit()) break;
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("H");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        thread_high_counter++;
        thread_sleep_ms(50);  /* Courte pause */
    }
    thread_exit(0);
}

static void normal_priority_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 30; i++) {
        if (thread_should_exit()) break;
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("N");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        thread_normal_counter++;
        thread_sleep_ms(100);
    }
    thread_exit(0);
}

static void low_priority_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 40; i++) {
        if (thread_should_exit()) break;
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts("L");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        thread_low_counter++;
        thread_sleep_ms(150);
    }
    thread_exit(0);
}

static int cmd_threads(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_puts("\n=== New Multithreading Test ===\n");
    console_puts("Testing thread priorities:\n");
    console_puts("  ");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("H");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts(" = HIGH priority (UI)\n");
    console_puts("  ");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("N");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts(" = NORMAL priority\n");
    console_puts("  ");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("L");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts(" = LOW (background) priority\n\n");
    
    /* Réinitialiser les compteurs */
    thread_high_counter = 0;
    thread_normal_counter = 0;
    thread_low_counter = 0;
    
    /* Créer les threads avec différentes priorités */
    thread_t *high = thread_create("thread_high", high_priority_task, NULL,
                                   0, THREAD_PRIORITY_UI);
    thread_t *normal = thread_create("thread_normal", normal_priority_task, NULL,
                                     0, THREAD_PRIORITY_NORMAL);
    thread_t *low = thread_create("thread_low", low_priority_task, NULL,
                                  0, THREAD_PRIORITY_BACKGROUND);
    
    if (!high || !normal || !low) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("ERROR: Failed to create threads!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    console_puts("Threads created! Output: ");
    
    /* Attendre quelques secondes pour voir le résultat */
    for (int i = 0; i < 50; i++) {
        thread_sleep_ms(100);
    }
    
    console_puts("\n\n=== Results ===\n");
    console_puts("High priority iterations:   ");
    console_put_dec(thread_high_counter);
    console_puts("\n");
    console_puts("Normal priority iterations: ");
    console_put_dec(thread_normal_counter);
    console_puts("\n");
    console_puts("Low priority iterations:    ");
    console_put_dec(thread_low_counter);
    console_puts("\n");
    
    /* Afficher la liste des threads */
    thread_list_debug();
    
    return 0;
}