/* src/shell/commands.h - Shell Commands */
#ifndef SHELL_COMMANDS_H
#define SHELL_COMMANDS_H

/**
 * Structure d'une commande shell.
 */
typedef struct {
    const char* name;           /* Nom de la commande */
    const char* description;    /* Description courte */
    int (*handler)(int argc, char** argv);  /* Fonction handler */
} shell_command_t;

/**
 * Initialise les commandes.
 */
void commands_init(void);

/**
 * Exécute une commande.
 * @param argc  Nombre d'arguments
 * @param argv  Tableau des arguments (argv[0] = nom de la commande)
 * @return Code de retour de la commande (0 = succès)
 */
int command_execute(int argc, char** argv);

#endif /* SHELL_COMMANDS_H */
