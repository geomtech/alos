/* src/shell/commands.c - Shell Commands Implementation */
#include "commands.h"
#include "shell.h"
#include "../kernel/console.h"
#include "../kernel/keyboard.h"
#include "../kernel/process.h"
#include "../include/string.h"
#include "../net/l3/icmp.h"
#include "../arch/x86/usermode.h"

/* ========================================
 * Déclarations des handlers de commandes
 * ======================================== */

static int cmd_help(int argc, char** argv);
static int cmd_ping(int argc, char** argv);
static int cmd_tasks(int argc, char** argv);
static int cmd_ps(int argc, char** argv);
static int cmd_usermode(int argc, char** argv);

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
// static int cmd_netinfo(int argc, char** argv);

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
    // { "netinfo", "Display network configuration",          cmd_netinfo },
    
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
    
    /* Commande non trouvée */
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
    console_puts("Coming soon: clear, ls, cat, cd, pwd, mkdir, touch, echo, meminfo, netinfo\n");
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

/*
 * static int cmd_netinfo(int argc, char** argv)
 * {
 *     // TODO: Afficher la configuration réseau
 *     // Inclure les headers réseau et afficher:
 *     // - Adresse MAC (netif->mac_addr)
 *     // - Adresse IP (netif->ip_addr)
 *     // - Masque (netif->netmask)
 *     // - Passerelle (netif->gateway)
 *     // - Serveur DNS
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
    jump_to_usermode(user_mode_test);
    
    /* On ne devrait JAMAIS arriver ici */
    console_puts("ERROR: Returned from User Mode!?\n");
    return -1;
}
