/* src/userland/sh.c - Shell userland ALOS (/bin/sh)
 *
 * Remplace progressivement le shell kernel (src/shell/shell.c). Tourne en
 * Ring 3 comme un programme ELF normal : lit le clavier via getchar()
 * (SYS_READ bloquant sur stdin), affiche via printf() (SYS_WRITE), et lance
 * les commandes externes via spawn_wait() (SYS_SPAWN_WAIT).
 *
 * Limitations phase 1 : pas de complétion TAB, pas de couleurs ANSI.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LINE_MAX 256
#define ARGV_MAX 16
#define HISTORY_MAX 16

#define KEY_BACKSPACE 0x08
#define KEY_DEL 0x7F
#define KEY_CTRL_C 0x03

static char history[HISTORY_MAX][LINE_MAX];
static int history_count = 0;

/**
 * Lit une ligne au clavier caractère par caractère.
 * Gère le backspace et Ctrl+C (annule la ligne courante).
 *
 * @return Longueur de la ligne lue, ou -1 si Ctrl+C a annulé la saisie.
 */
static int shell_readline(char *buf, int size) {
  int len = 0;

  for (;;) {
    int c = getchar();
    if (c < 0) {
      /* Rien à lire pour l'instant : on retente. */
      continue;
    }

    if (c == KEY_CTRL_C) {
      printf("^C\n");
      buf[0] = '\0';
      return -1;
    }

    if (c == '\n' || c == '\r') {
      putchar('\n');
      buf[len] = '\0';
      return len;
    }

    if (c == KEY_BACKSPACE || c == KEY_DEL) {
      if (len > 0) {
        len--;
        printf("\b \b");
      }
      continue;
    }

    /* Caractère imprimable normal */
    if (len < size - 1) {
      buf[len++] = (char)c;
      putchar(c);
    }
  }
}

/**
 * Tokenise la ligne en argc/argv (découpage sur les espaces).
 */
static int shell_parse(char *line, char **argv, int max_args) {
  int argc = 0;
  char *p = line;

  while (*p && argc < max_args) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '\0')
      break;

    argv[argc++] = p;

    while (*p && *p != ' ' && *p != '\t')
      p++;

    if (*p) {
      *p = '\0';
      p++;
    }
  }

  return argc;
}

static void history_add(const char *line) {
  if (line[0] == '\0')
    return;

  if (history_count < HISTORY_MAX) {
    strncpy(history[history_count], line, LINE_MAX - 1);
    history[history_count][LINE_MAX - 1] = '\0';
    history_count++;
  } else {
    /* Décale l'historique (le plus vieux est perdu) */
    for (int i = 1; i < HISTORY_MAX; i++) {
      strncpy(history[i - 1], history[i], LINE_MAX);
    }
    strncpy(history[HISTORY_MAX - 1], line, LINE_MAX - 1);
    history[HISTORY_MAX - 1][LINE_MAX - 1] = '\0';
  }
}

static void cmd_help(void) {
  printf("ALOS shell (/bin/sh) - commandes internes:\n");
  printf("  cd <dir>   - changer de repertoire\n");
  printf("  clear      - effacer l'ecran\n");
  printf("  history    - afficher l'historique des commandes\n");
  printf("  help       - afficher cette aide\n");
  printf("  exit       - quitter le shell\n");
  printf("Toute autre commande est cherchee dans /bin/<commande>.\n");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  char line[LINE_MAX];
  char *av[ARGV_MAX];
  char cwd[LINE_MAX];

  printf("ALOS userland shell - tapez 'help' pour la liste des commandes\n");

  for (;;) {
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      strcpy(cwd, "?");
    }
    printf("alos:%s$ ", cwd);

    int len = shell_readline(line, sizeof(line));
    if (len < 0) {
      /* Ctrl+C : on annule la ligne et on réaffiche le prompt */
      continue;
    }
    if (len == 0) {
      continue;
    }

    history_add(line);

    int ac = shell_parse(line, av, ARGV_MAX);
    if (ac == 0) {
      continue;
    }

    if (strcmp(av[0], "cd") == 0) {
      const char *dir = (ac > 1) ? av[1] : "/";
      if (chdir(dir) != 0) {
        printf("cd: %s: no such directory\n", dir);
      }
    } else if (strcmp(av[0], "exit") == 0) {
      return 0;
    } else if (strcmp(av[0], "clear") == 0) {
      syscall0(SYS_CLEAR);
    } else if (strcmp(av[0], "help") == 0) {
      cmd_help();
    } else if (strcmp(av[0], "history") == 0) {
      for (int i = 0; i < history_count; i++) {
        printf("%d  %s\n", i + 1, history[i]);
      }
    } else {
      /* Commande externe : /bin/<cmd> ou chemin absolu */
      char path[LINE_MAX];
      if (av[0][0] == '/') {
        strncpy(path, av[0], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
      } else {
        snprintf(path, sizeof(path), "/bin/%s", av[0]);
      }

      int ret = spawn_wait(path, ac, av);
      if (ret < 0) {
        printf("sh: %s: command not found\n", av[0]);
      }
    }
  }

  return 0;
}
