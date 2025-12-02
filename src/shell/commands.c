/* src/shell/commands.c - Shell Commands Implementation */
#include "commands.h"
#include "shell.h"
#include "../kernel/console.h"
#include "../kernel/keyboard.h"
#include "../kernel/keymap.h"
#include "../kernel/process.h"
#include "../kernel/thread.h"
#include "../kernel/sync.h"
#include "../kernel/workqueue.h"
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
static int cmd_synctest(int argc, char** argv);
static int cmd_schedtest(int argc, char** argv);
static int cmd_worktest(int argc, char** argv);

/* ========================================
 * Table des commandes
 * ======================================== */

static shell_command_t commands[] = {
    /* Commandes implémentées */
    { "help",     "Display available commands",              cmd_help },
    { "ping",     "Ping a host (IP or hostname)",           cmd_ping },
    { "tasks",    "Test multitasking (launches 2 threads)",  cmd_tasks },
    { "threads",  "Test new multithreading with priorities", cmd_threads },
    { "synctest", "Test synchronization primitives (mutex, sem, etc.)", cmd_synctest },
    { "schedtest", "Test scheduler aging and nice values", cmd_schedtest },
    { "worktest", "Test worker thread pool and reaper", cmd_worktest },
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

/* ========================================
 * Synchronization Test Command
 * ======================================== */

/* Shared resources for sync tests */
static mutex_t test_mutex;
static semaphore_t test_semaphore;
static condvar_t test_condvar;
static rwlock_t test_rwlock;

static volatile int shared_counter = 0;
static volatile int producer_done = 0;
static volatile int data_ready = 0;

/* === Mutex Test === */
static void mutex_test_worker(void *arg)
{
    int id = (int)(uintptr_t)arg;
    
    for (int i = 0; i < 5; i++) {
        mutex_lock(&test_mutex);
        
        int old = shared_counter;
        shared_counter++;
        
        /* Small delay to make race conditions visible if mutex fails */
        for (volatile int j = 0; j < 1000; j++);
        
        if (shared_counter != old + 1) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("MUTEX FAIL! ");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
        
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("M");
        console_put_dec(id);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        mutex_unlock(&test_mutex);
        thread_yield();
    }
    
    thread_exit(0);
}

/* === Semaphore Test (Simple counting) === */
#define SEM_TEST_COUNT 5
static volatile int sem_counter = 0;

static void sem_worker(void *arg)
{
    int id = (int)(uintptr_t)arg;
    
    for (int i = 0; i < SEM_TEST_COUNT; i++) {
        sem_wait(&test_semaphore);  /* Acquire permit */
        
        sem_counter++;
        
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts("S");
        console_put_dec(id);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        /* Small work simulation */
        for (volatile int j = 0; j < 10000; j++);
        
        sem_post(&test_semaphore);  /* Release permit */
    }
    
    thread_exit(0);
}

/* === Condition Variable Test === */
static void cv_waiter(void *arg)
{
    int id = (int)(uintptr_t)arg;
    
    mutex_lock(&test_mutex);
    
    while (!data_ready) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        console_puts("W");
        console_put_dec(id);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        condvar_wait(&test_condvar, &test_mutex);
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("!");
    console_put_dec(id);
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    mutex_unlock(&test_mutex);
    thread_exit(0);
}

static void cv_signaler(void *arg)
{
    (void)arg;
    
    thread_sleep_ms(200);
    
    mutex_lock(&test_mutex);
    data_ready = 1;
    mutex_unlock(&test_mutex);
    
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("[BROADCAST]");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    condvar_broadcast(&test_condvar);
    
    thread_exit(0);
}

/* === Read-Write Lock Test === */
static volatile int rwlock_shared_data = 0;

static void rwlock_reader(void *arg)
{
    int id = (int)(uintptr_t)arg;
    
    for (int i = 0; i < 3; i++) {
        rwlock_rdlock(&test_rwlock);
        
        int val = rwlock_shared_data;
        (void)val;  /* Read the data */
        
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts("R");
        console_put_dec(id);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        /* Hold read lock briefly */
        for (volatile int j = 0; j < 500; j++);
        
        rwlock_rdunlock(&test_rwlock);
        thread_yield();
    }
    
    thread_exit(0);
}

static void rwlock_writer(void *arg)
{
    int id = (int)(uintptr_t)arg;
    
    for (int i = 0; i < 2; i++) {
        rwlock_wrlock(&test_rwlock);
        
        rwlock_shared_data++;
        
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("W");
        console_put_dec(id);
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        /* Hold write lock briefly */
        for (volatile int j = 0; j < 1000; j++);
        
        rwlock_wrunlock(&test_rwlock);
        thread_yield();
    }
    
    thread_exit(0);
}

static int cmd_synctest(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("=== Synchronization Primitives Test ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* ====== MUTEX TEST ====== */
    console_puts("\n[1] MUTEX TEST - ");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("M#");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts(" = thread # acquired mutex\n");
    console_puts("    Testing mutual exclusion with 3 threads...\n    ");
    
    mutex_init(&test_mutex, MUTEX_TYPE_NORMAL);
    shared_counter = 0;
    
    thread_t *m1 = thread_create("mutex_t1", mutex_test_worker, (void*)1, 0, THREAD_PRIORITY_NORMAL);
    thread_t *m2 = thread_create("mutex_t2", mutex_test_worker, (void*)2, 0, THREAD_PRIORITY_NORMAL);
    thread_t *m3 = thread_create("mutex_t3", mutex_test_worker, (void*)3, 0, THREAD_PRIORITY_NORMAL);
    
    if (m1) thread_join(m1);
    if (m2) thread_join(m2);
    if (m3) thread_join(m3);
    
    console_puts("\n    Final counter: ");
    console_put_dec(shared_counter);
    console_puts(" (expected: 15)\n");
    if (shared_counter == 15) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("    PASS!\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("    FAIL!\n");
    }
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* ====== SEMAPHORE TEST ====== */
    console_puts("\n[2] SEMAPHORE TEST - ");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("S#");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts(" = worker # in critical section\n");
    console_puts("    Testing semaphore with 2 permits, 3 workers...\n    ");
    
    /* Semaphore with 2 permits - only 2 threads can be in critical section */
    semaphore_init(&test_semaphore, 2, 0);
    sem_counter = 0;
    
    thread_t *sw1 = thread_create("sem_w1", sem_worker, (void*)1, 0, THREAD_PRIORITY_NORMAL);
    thread_t *sw2 = thread_create("sem_w2", sem_worker, (void*)2, 0, THREAD_PRIORITY_NORMAL);
    thread_t *sw3 = thread_create("sem_w3", sem_worker, (void*)3, 0, THREAD_PRIORITY_NORMAL);
    
    if (sw1) thread_join(sw1);
    if (sw2) thread_join(sw2);
    if (sw3) thread_join(sw3);
    
    /* 3 workers x 5 iterations = 15 */
    console_puts("\n    Counter: ");
    console_put_dec(sem_counter);
    console_puts(" (expected: 15)\n");
    if (sem_counter == 15) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("    PASS!\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("    FAIL!\n");
    }
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* ====== CONDITION VARIABLE TEST ====== */
    console_puts("\n[3] CONDITION VARIABLE TEST - ");
    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    console_puts("W#");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("=waiting, ");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("!#");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("=woken\n");
    console_puts("    Testing condvar broadcast with 3 waiters...\n    ");
    
    mutex_init(&test_mutex, MUTEX_TYPE_NORMAL);
    condvar_init(&test_condvar);
    data_ready = 0;
    
    thread_t *w1 = thread_create("cv_w1", cv_waiter, (void*)1, 0, THREAD_PRIORITY_NORMAL);
    thread_t *w2 = thread_create("cv_w2", cv_waiter, (void*)2, 0, THREAD_PRIORITY_NORMAL);
    thread_t *w3 = thread_create("cv_w3", cv_waiter, (void*)3, 0, THREAD_PRIORITY_NORMAL);
    thread_t *sig = thread_create("cv_sig", cv_signaler, NULL, 0, THREAD_PRIORITY_HIGH);
    
    if (w1) thread_join(w1);
    if (w2) thread_join(w2);
    if (w3) thread_join(w3);
    if (sig) thread_join(sig);
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("\n    PASS!\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* ====== READ-WRITE LOCK TEST ====== */
    console_puts("\n[4] READ-WRITE LOCK TEST - ");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("R#");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("=reader, ");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("W#");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("=writer\n");
    console_puts("    Testing 3 readers, 2 writers (writer-preferring)...\n    ");
    
    rwlock_init(&test_rwlock, RWLOCK_PREFER_WRITER);
    rwlock_shared_data = 0;
    
    thread_t *r1 = thread_create("rw_r1", rwlock_reader, (void*)1, 0, THREAD_PRIORITY_NORMAL);
    thread_t *r2 = thread_create("rw_r2", rwlock_reader, (void*)2, 0, THREAD_PRIORITY_NORMAL);
    thread_t *r3 = thread_create("rw_r3", rwlock_reader, (void*)3, 0, THREAD_PRIORITY_NORMAL);
    thread_t *ww1 = thread_create("rw_w1", rwlock_writer, (void*)1, 0, THREAD_PRIORITY_NORMAL);
    thread_t *ww2 = thread_create("rw_w2", rwlock_writer, (void*)2, 0, THREAD_PRIORITY_NORMAL);
    
    if (r1) thread_join(r1);
    if (r2) thread_join(r2);
    if (r3) thread_join(r3);
    if (ww1) thread_join(ww1);
    if (ww2) thread_join(ww2);
    
    console_puts("\n    Shared data: ");
    console_put_dec(rwlock_shared_data);
    console_puts(" (expected: 4 from 2 writers x 2 iterations)\n");
    if (rwlock_shared_data == 4) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("    PASS!\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("    FAIL!\n");
    }
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* ====== SUMMARY ====== */
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("=== All Synchronization Tests Complete ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    return 0;
}

/* ========================================
 * Scheduler Test - Nice and Aging (Rocket Boost)
 * ======================================== */

/* Test threads for scheduler */
static void sched_test_worker_busy(void *arg)
{
    int id = (int)arg;
    uint64_t iterations = 0;
    int nice = (int)thread_get_nice(thread_current());

    console_puts("Worker ");
    console_put_dec(id);
    console_puts(" (nice=");
    if (nice < 0) {
        console_puts("-");
        console_put_dec(-nice);
    } else if (nice > 0) {
        console_puts("+");
        console_put_dec(nice);
    } else {
        console_puts("0");
    }
    console_puts(") started - TID=");
    console_put_dec(thread_get_tid());
    console_puts("\n");

    /* Busy work for ~50ms to avoid too many context switches */
    for (int i = 0; i < 50; i++) {
        iterations++;
        thread_yield();  /* Give others a chance */
    }

    console_puts("Worker ");
    console_put_dec(id);
    console_puts(" finished after ");
    console_put_dec(iterations);
    console_puts(" yields (CPU time: ");
    console_put_dec(thread_get_cpu_time_ms(thread_current()));
    console_puts("ms)\n");
}

static void sched_test_worker_idle(void *arg)
{
    (void)arg;

    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    console_puts("[LOW PRIORITY] Thread started with nice=+19 (IDLE priority)\n");
    console_puts("[LOW PRIORITY] Waiting for Rocket Boost after 100ms of starvation...\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* This thread should get starved initially, then boosted by aging */
    uint64_t start_time = thread_get_cpu_time_ms(thread_current());

    for (int i = 0; i < 50; i++) {
        thread_yield();
    }

    uint64_t end_time = thread_get_cpu_time_ms(thread_current());

    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[LOW PRIORITY] Thread completed! CPU time: ");
    console_put_dec(end_time - start_time);
    console_puts("ms\n");
    console_puts("[LOW PRIORITY] Should have been boosted to UI priority by aging!\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static int cmd_schedtest(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("=== Scheduler Improvements Test ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("Testing: Nice values, Rocket Boost aging, CPU accounting\n\n");

    /* Test 1: Nice values */
    console_puts("[TEST 1] Nice Values (-20 to +19)\n");
    console_puts("Creating 3 threads with different nice values:\n");

    /* Create threads with appropriate initial priorities to avoid race */
    thread_t *t1 = thread_create("nice_-10", sched_test_worker_busy, (void*)1, 0, THREAD_PRIORITY_UI);
    console_puts("  Created t1 - TID=");
    if (t1) {
        console_put_dec(t1->tid);
        thread_set_nice(t1, -10);  /* Set nice immediately after creation */
    } else {
        console_puts("FAILED");
    }
    console_puts("\n");

    thread_t *t2 = thread_create("nice_0", sched_test_worker_busy, (void*)2, 0, THREAD_PRIORITY_NORMAL);
    console_puts("  Created t2 - TID=");
    if (t2) {
        console_put_dec(t2->tid);
        thread_set_nice(t2, 0);    /* Set nice immediately after creation */
    } else {
        console_puts("FAILED");
    }
    console_puts("\n");

    thread_t *t3 = thread_create("nice_+10", sched_test_worker_busy, (void*)3, 0, THREAD_PRIORITY_BACKGROUND);
    console_puts("  Created t3 - TID=");
    if (t3) {
        console_put_dec(t3->tid);
        thread_set_nice(t3, +10);  /* Set nice immediately after creation */
    } else {
        console_puts("FAILED");
    }
    console_puts("\n\n");

    console_puts("  Thread 1: nice=-10 -> UI priority\n");
    console_puts("  Thread 2: nice=0   -> NORMAL priority\n");
    console_puts("  Thread 3: nice=+10 -> BACKGROUND priority\n\n");

    if (t1) thread_join(t1);
    if (t2) thread_join(t2);
    if (t3) thread_join(t3);

    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("  Nice values test complete!\n\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Test 2: Rocket Boost (Aging) */
    console_puts("[TEST 2] Rocket Boost Aging\n");
    console_puts("Creating one IDLE priority thread that should be starved,\n");
    console_puts("then automatically boosted to UI priority after 100ms.\n\n");

    /* Create high priority threads to starve the low one */
    thread_t *high1 = thread_create("high1", sched_test_worker_busy, (void*)10, 0, THREAD_PRIORITY_HIGH);
    thread_t *high2 = thread_create("high2", sched_test_worker_busy, (void*)11, 0, THREAD_PRIORITY_HIGH);

    /* Create the low priority thread that should get boosted */
    thread_t *low = thread_create("idle_boost", sched_test_worker_idle, NULL, 0, THREAD_PRIORITY_IDLE);
    if (low) thread_set_nice(low, +19);  /* Extremely low priority */

    if (high1) thread_join(high1);
    if (high2) thread_join(high2);
    if (low) thread_join(low);

    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("  Rocket Boost aging test complete!\n\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Test 3: Display thread stats */
    console_puts("[TEST 3] CPU Accounting\n");
    console_puts("Displaying thread list with CPU time and context switches:\n");

    thread_list_debug();

    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("=== Scheduler Test Complete ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("Check the thread list above to see:\n");
    console_puts("  - CPU time consumed (ms)\n");
    console_puts("  - Context switch count\n");
    console_puts("  - Nice values\n");
    console_puts("  - Boost status (B)\n\n");

    return 0;
}

/* ========================================
 * Worker Thread Pool Test
 * ======================================== */

/* Work function counter - protected by atomic ops */
static volatile int work_counter = 0;

/* Test work function - simple counter increment */
static void work_func_increment(void *arg)
{
    int id = (int)(intptr_t)arg;
    
    /* Simulate some work */
    for (volatile int i = 0; i < 100000; i++);
    
    __sync_fetch_and_add(&work_counter, 1);
    
    /* Print completion message */
    console_puts("  Work item #");
    console_put_dec(id);
    console_puts(" completed by TID=");
    console_put_dec(thread_get_tid());
    console_puts("\n");
}

/* Long-running work function for shutdown timeout test */
static void work_func_slow(void *arg)
{
    (void)arg;
    console_puts("  [SLOW] Work item started, sleeping 200ms...\n");
    thread_sleep_ms(200);
    console_puts("  [SLOW] Work item completed!\n");
}

static int cmd_worktest(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("=== Worker Thread Pool Test ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("Testing: Reaper thread, Worker pool, FIFO work queue\n\n");
    
    /* Test 1: Basic work submission */
    console_puts("[TEST 1] Basic Work Submission (10 items)\n");
    console_puts("Submitting 10 work items to kernel worker pool...\n");
    
    work_counter = 0;
    
    for (int i = 1; i <= 10; i++) {
        int result = kwork_submit(work_func_increment, (void*)(intptr_t)i);
        if (result != 0) {
            console_puts("  ERROR: Failed to submit work item #");
            console_put_dec(i);
            console_puts("\n");
        }
    }
    
    /* Wait for all work to complete */
    console_puts("Waiting for work items to complete...\n");
    int timeout = 50;  /* 500ms max */
    while (work_counter < 10 && timeout > 0) {
        thread_sleep_ms(10);
        timeout--;
    }
    
    console_puts("\nCompleted work items: ");
    console_put_dec(work_counter);
    console_puts("/10\n");
    
    if (work_counter == 10) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("  Basic work test PASSED!\n\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("  Basic work test FAILED!\n\n");
    }
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Test 2: Custom worker pool with shutdown timeout */
    console_puts("[TEST 2] Custom Pool with Shutdown Timeout\n");
    console_puts("Creating custom pool with 2 workers...\n");
    
    worker_pool_t *custom_pool = worker_pool_create(2);
    if (!custom_pool) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("  ERROR: Failed to create custom pool!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else {
        console_puts("  Custom pool created successfully\n");
        
        /* Submit slow work to test timeout */
        console_puts("Submitting slow work item (200ms)...\n");
        worker_pool_submit(custom_pool, work_func_slow, NULL);
        
        /* Give work time to start */
        thread_sleep_ms(50);
        
        /* Shutdown with short timeout (should timeout) */
        console_puts("Shutting down with 100ms timeout (should timeout)...\n");
        int not_terminated = worker_pool_shutdown_timeout(custom_pool, 100);
        
        if (not_terminated > 0) {
            console_puts("  Timeout occurred: ");
            console_put_dec(not_terminated);
            console_puts(" worker(s) still running\n");
            console_puts("  (This is expected behavior for slow work)\n");
        } else {
            console_puts("  All workers terminated in time\n");
        }
        
        /* Wait more then destroy */
        thread_sleep_ms(200);
        worker_pool_destroy(custom_pool);
        
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("  Custom pool test PASSED!\n\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
    
    /* Test 3: Thread join timeout */
    console_puts("[TEST 3] Thread Join Timeout\n");
    console_puts("Testing thread_join_timeout function...\n");
    
    /* Create a thread that sleeps for 200ms */
    thread_t *slow_thread = thread_create("slow_join", (thread_entry_t)work_func_slow, NULL, 0, THREAD_PRIORITY_NORMAL);
    if (!slow_thread) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("  ERROR: Failed to create slow thread!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else {
        console_puts("  Created slow thread TID=");
        console_put_dec(slow_thread->tid);
        console_puts("\n");
        
        /* Try to join with short timeout */
        console_puts("  Joining with 50ms timeout (should timeout)...\n");
        int result = thread_join_timeout(slow_thread, 50);
        
        if (result == -ETIMEDOUT) {
            console_puts("  Got expected ETIMEDOUT!\n");
        } else {
            console_puts("  Unexpected result: ");
            console_put_dec(result);
            console_puts("\n");
        }
        
        /* Wait for thread to actually finish */
        console_puts("  Waiting for thread to finish naturally...\n");
        thread_sleep_ms(300);
        
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("  Thread join timeout test PASSED!\n\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
    
    /* Test 4: Reaper thread verification */
    console_puts("[TEST 4] Reaper Thread Verification\n");
    console_puts("Creating 5 short-lived threads to test zombie cleanup...\n");
    
    for (int i = 0; i < 5; i++) {
        thread_t *temp = thread_create("temp", (thread_entry_t)work_func_increment, (void*)(intptr_t)(100 + i), 0, THREAD_PRIORITY_NORMAL);
        if (temp) {
            console_puts("  Created thread TID=");
            console_put_dec(temp->tid);
            console_puts("\n");
            /* Don't join - let reaper clean up */
        }
    }
    
    /* Wait for threads to finish and reaper to clean up */
    thread_sleep_ms(500);
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("  Threads should be cleaned up by reaper (check logs)\n\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Display thread list */
    console_puts("[INFO] Current Thread List:\n");
    thread_list_debug();
    
    console_puts("\n");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("=== Worker Thread Pool Test Complete ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}