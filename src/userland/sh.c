/* src/userland/sh.c - Shell userland ALOS (/bin/sh)
 *
 * Remplace progressivement le shell kernel (src/shell/shell.c). Tourne en
 * Ring 3 comme un programme ELF normal : lit le clavier via getchar()
 * (SYS_READ bloquant sur stdin), affiche via printf() (SYS_WRITE), et lance
 * les commandes externes via fork()/execve()/waitpid().
 *
 * Deux modes d'exécution, qui partagent le MÊME tokenizer et le MÊME
 * dispatcher de commandes (shell_execute_line) :
 *
 *   - Mode interactif (argc == 1) : lit le clavier, affiche un prompt.
 *   - Mode script (argc >= 2)     : "sh <chemin>" lit un fichier via
 *     open()/read()/close() et exécute chaque ligne comme si elle avait été
 *     tapée au clavier. Utilisé par le kernel pour /config/startup.sh.
 *
 * Limitations phase 1 : pas de complétion TAB, pas de couleurs ANSI, pas de
 * pipes/redirections/variables d'environnement/globbing.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define LINE_MAX 256
#define ARGV_MAX 16
#define HISTORY_MAX 16

#define KEY_BACKSPACE 0x08
#define KEY_DEL 0x7F
#define KEY_CTRL_C 0x03

/* Taille du buffer de lecture par appel à read() en mode script. */
#define SCRIPT_READ_CHUNK 256

static char history[HISTORY_MAX][LINE_MAX];
static int history_count = 0;

/**
 * Résultat de l'exécution d'une ligne de commande (interactive ou script).
 */
typedef struct {
  int should_exit; /* Non-nul si la commande "exit" a été exécutée */
  int exit_code;   /* Code de sortie demandé par "exit [code]" */
} line_result_t;

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
  printf("  echo <...> - afficher les arguments\n");
  printf("  pwd        - afficher le repertoire courant\n");
  printf("  exit [n]   - quitter le shell (code de sortie optionnel)\n");
  printf("Toute autre commande est cherchee dans /bin/<commande>.\n");
  printf("Usage: sh [script]  - execute un script au lieu du mode interactif.\n");
}

/**
 * Exécute une ligne de commande déjà tokenisable (tokenizer + builtins +
 * commandes externes). Utilisée à la fois par la boucle interactive et par
 * l'exécuteur de script (shell_run_script), pour garantir un seul et même
 * chemin d'exécution.
 *
 * @param line  Ligne à exécuter (sera modifiée par le tokenizer).
 */
static line_result_t shell_execute_line(char *line) {
  line_result_t result = {0, 0};
  char *av[ARGV_MAX];

  int ac = shell_parse(line, av, ARGV_MAX);
  if (ac == 0) {
    return result;
  }

  if (strcmp(av[0], "cd") == 0) {
    const char *dir = (ac > 1) ? av[1] : "/";
    if (chdir(dir) != 0) {
      printf("cd: %s: no such directory\n", dir);
    }
  } else if (strcmp(av[0], "exit") == 0) {
    result.should_exit = 1;
    result.exit_code = (ac > 1) ? atoi(av[1]) : 0;
  } else if (strcmp(av[0], "clear") == 0) {
    syscall0(SYS_CLEAR);
  } else if (strcmp(av[0], "help") == 0) {
    cmd_help();
  } else if (strcmp(av[0], "history") == 0) {
    for (int i = 0; i < history_count; i++) {
      printf("%d  %s\n", i + 1, history[i]);
    }
  } else if (strcmp(av[0], "echo") == 0) {
    for (int i = 1; i < ac; i++) {
      printf("%s%s", (i > 1) ? " " : "", av[i]);
    }
    printf("\n");
  } else if (strcmp(av[0], "pwd") == 0) {
    char cwd[LINE_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
      printf("%s\n", cwd);
    } else {
      printf("pwd: unable to read current directory\n");
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

    pid_t child = fork();
    if (child < 0) {
      printf("sh: %s: unable to fork\n", av[0]);
    } else if (child == 0) {
      char *child_argv[ARGV_MAX + 1];
      for (int i = 0; i < ac; i++) {
        child_argv[i] = av[i];
      }
      child_argv[ac] = NULL;
      execve(path, child_argv, NULL);
      printf("sh: %s: command not found\n", av[0]);
      _exit(127);
    } else {
      int status;
      if (waitpid(child, &status, 0) < 0) {
        printf("sh: %s: wait failed\n", av[0]);
      }
    }
  }

  return result;
}

/**
 * Indique si une ligne (après trim des espaces de tête) est vide ou un
 * commentaire. Compatible avec l'ancien parseur kernel de config.c : une
 * ligne commençant par '#' ou ';' (après les espaces/tabulations de tête)
 * est ignorée.
 */
static int is_comment_or_empty(const char *line) {
  while (*line == ' ' || *line == '\t')
    line++;
  return (*line == '#' || *line == ';' || *line == '\0');
}

/**
 * Supprime les espaces/tabulations/CR/LF en fin de chaîne, in-place.
 * Gère les fins de ligne CRLF ("\r\n") en plus de LF ("\n").
 */
static void trim_trailing_ws(char *line) {
  int len = (int)strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                     line[len - 1] == ' ' || line[len - 1] == '\t')) {
    line[--len] = '\0';
  }
}

/**
 * Lecteur de script à tampon : évite de charger tout le fichier en mémoire
 * (contrairement à l'ancienne limite fixe de 4095 octets du parseur kernel)
 * en relisant le fichier par blocs de SCRIPT_READ_CHUNK octets via read().
 */
typedef struct {
  int fd;
  char buf[SCRIPT_READ_CHUNK];
  int len; /* Nombre d'octets valides dans buf */
  int pos; /* Position de lecture courante dans buf */
  int eof;
} script_reader_t;

static int script_reader_getc(script_reader_t *r) {
  if (r->pos >= r->len) {
    if (r->eof) {
      return -1;
    }
    int n = read(r->fd, r->buf, SCRIPT_READ_CHUNK);
    if (n <= 0) {
      r->eof = 1;
      return -1;
    }
    r->len = n;
    r->pos = 0;
  }
  return (unsigned char)r->buf[r->pos++];
}

/**
 * Lit une ligne (jusqu'à '\n' ou EOF) dans `line` (taille max `size`,
 * NUL-terminée). Une ligne trop longue pour `size` est tronquée sans
 * débordement : les octets excédentaires sont consommés et jetés jusqu'au
 * prochain '\n' (ou EOF).
 *
 * @return Longueur de la ligne lue (0 si ligne vide), ou -1 en cas d'EOF
 *         sans aucune donnée lue (fin du script).
 */
static int script_reader_getline(script_reader_t *r, char *line, int size,
                                 int *truncated) {
  int len = 0;
  int c;
  int got_any = 0;

  *truncated = 0;

  while ((c = script_reader_getc(r)) >= 0) {
    got_any = 1;
    if (c == '\n') {
      break;
    }
    if (len < size - 1) {
      line[len++] = (char)c;
    } else {
      *truncated = 1; /* Ligne trop longue : on jette le reste jusqu'à '\n' */
    }
  }

  if (!got_any) {
    return -1;
  }

  line[len] = '\0';
  return len;
}

/**
 * Exécute un script shell non-interactif ("sh <chemin>").
 *
 * Réutilise shell_execute_line() : chaque ligne du script est exécutée
 * exactement comme si elle avait été tapée au clavier (mêmes builtins,
 * même résolution de commande externe via fork()/execve()/waitpid()).
 *
 * Comportement (aligné sur l'ancien config_run_script() du kernel) :
 *   - lignes vides ou commentaires ('#' ou ';' en tête) ignorées,
 *   - CRLF et espaces de fin tolérés,
 *   - une commande externe qui retourne un code non-nul n'interrompt PAS
 *     le script (comportement legacy préservé),
 *   - "exit [code]" dans le script arrête le script et retourne ce code,
 *   - script introuvable : message + retour non-fatal (127),
 *   - ligne trop longue : avertissement avec numéro de ligne, on continue.
 *
 * @return Code de sortie du script (dernier "exit", ou 0/127 selon le cas).
 */
static int shell_run_script(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("sh: %s: no such file or directory\n", path);
    return 127;
  }

  script_reader_t reader;
  memset(&reader, 0, sizeof(reader));
  reader.fd = fd;

  char line[LINE_MAX];
  int line_no = 0;
  int status = 0;

  for (;;) {
    int truncated = 0;
    int len = script_reader_getline(&reader, line, sizeof(line), &truncated);
    if (len < 0) {
      break; /* EOF */
    }
    line_no++;

    if (truncated) {
      printf("sh: %s: line %d too long, truncated\n", path, line_no);
    }

    trim_trailing_ws(line);

    if (is_comment_or_empty(line)) {
      continue;
    }

    line_result_t result = shell_execute_line(line);
    if (result.should_exit) {
      status = result.exit_code;
      break;
    }
  }

  close(fd);
  return status;
}

/**
 * Boucle interactive : affiche le prompt, lit une ligne au clavier, l'exécute
 * via shell_execute_line(). Retourne le code de sortie demandé par "exit".
 */
static int shell_interactive_loop(void) {
  char line[LINE_MAX];
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

    line_result_t result = shell_execute_line(line);
    if (result.should_exit) {
      return result.exit_code;
    }
  }
}

int main(int argc, char **argv) {
  /* Mode script non-interactif : "sh <chemin>" (utilisé par le kernel pour
   * exécuter /config/startup.sh via /bin/sh, avant la boucle interactive). */
  if (argc >= 2) {
    return shell_run_script(argv[1]);
  }

  return shell_interactive_loop();
}
