/* src/userland/cmd/sh.c - ALOS Userland Shell */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define SHELL_LINE_MAX     256
#define SHELL_MAX_ARGS     32
#define SHELL_HISTORY_SIZE 32
#define MAX_MATCHES        32

/* Special key codes */
#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_CTRL_C 0x03
#define KEY_CTRL_D 0x04

/* Command history */
static char history[SHELL_HISTORY_SIZE][SHELL_LINE_MAX];
static int history_count = 0;

static void history_add(const char *cmd) {
  if (cmd == NULL || cmd[0] == '\0') {
    return;
  }
  /* Ne pas dupliquer si identique à la dernière commande */
  if (history_count > 0) {
    int last_idx = (history_count - 1) % SHELL_HISTORY_SIZE;
    if (strcmp(history[last_idx], cmd) == 0) {
      return;
    }
  }
  int idx = history_count % SHELL_HISTORY_SIZE;
  strncpy(history[idx], cmd, SHELL_LINE_MAX - 1);
  history[idx][SHELL_LINE_MAX - 1] = '\0';
  history_count++;
}

static const char *history_get(int offset_from_end) {
  if (offset_from_end < 0 || offset_from_end >= history_count) {
    return NULL;
  }
  if (offset_from_end >= SHELL_HISTORY_SIZE) {
    return NULL;
  }
  int idx = (history_count - 1 - offset_from_end) % SHELL_HISTORY_SIZE;
  if (idx < 0) {
    idx += SHELL_HISTORY_SIZE;
  }
  return history[idx];
}

static void print_prompt(void) {
  char cwd[256];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    strcpy(cwd, "/");
  }
  printf("alos:%s$ ", cwd);
}

/* Builtin list for autocompletion */
static const char *builtins[] = {
  "cd", "pwd", "clear", "exit", "help", NULL
};

/**
 * Autocomplétion de commande et de fichier (TAB).
 */
static void autocomplete(char *buffer, int *pos_ptr, int *len_ptr) {
  int pos = *pos_ptr;
  int len = *len_ptr;

  /* Trouver le début du mot courant */
  int word_start = pos;
  while (word_start > 0 && buffer[word_start - 1] != ' ') {
    word_start--;
  }

  char word[128];
  int word_len = pos - word_start;
  if (word_len >= (int)sizeof(word)) {
    return;
  }
  memcpy(word, buffer + word_start, word_len);
  word[word_len] = '\0';

  /* Vérifier si on complète la commande (premier mot) ou un argument */
  int is_command = 1;
  for (int i = 0; i < word_start; i++) {
    if (buffer[i] != ' ') {
      is_command = 0;
      break;
    }
  }

  char matches[MAX_MATCHES][64];
  int is_dir[MAX_MATCHES];
  int match_count = 0;

  if (is_command && strchr(word, '/') == NULL) {
    /* 1. Compléter les builtins */
    for (int i = 0; builtins[i] != NULL && match_count < MAX_MATCHES; i++) {
      if (strncmp(builtins[i], word, word_len) == 0) {
        strncpy(matches[match_count], builtins[i], sizeof(matches[0]) - 1);
        matches[match_count][sizeof(matches[0]) - 1] = '\0';
        is_dir[match_count] = 0;
        match_count++;
      }
    }

    /* 2. Compléter depuis /bin/ */
    userspace_dirent_t entry;
    uint32_t idx = 0;
    while (match_count < MAX_MATCHES) {
      long ret = syscall3(SYS_READDIR, (long)"/bin", idx++, (long)&entry);
      if (ret != 0) break;
      if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) continue;

      if (strncmp(entry.name, word, word_len) == 0) {
        /* Éviter doublon si déjà dans les builtins */
        int exists = 0;
        for (int m = 0; m < match_count; m++) {
          if (strcmp(matches[m], entry.name) == 0) {
            exists = 1;
            break;
          }
        }
        if (!exists) {
          strncpy(matches[match_count], entry.name, sizeof(matches[0]) - 1);
          matches[match_count][sizeof(matches[0]) - 1] = '\0';
          is_dir[match_count] = (entry.type & 0x02) ? 1 : 0;
          match_count++;
        }
      }
    }
  } else {
    /* Compléter un chemin de fichier/répertoire */
    char dir_path[256];
    char prefix[128];
    char *last_slash = strrchr(word, '/');
    if (last_slash != NULL) {
      int dlen = (int)(last_slash - word);
      if (dlen == 0) {
        strcpy(dir_path, "/");
      } else {
        strncpy(dir_path, word, dlen);
        dir_path[dlen] = '\0';
      }
      strncpy(prefix, last_slash + 1, sizeof(prefix) - 1);
      prefix[sizeof(prefix) - 1] = '\0';
    } else {
      strcpy(dir_path, ".");
      strncpy(prefix, word, sizeof(prefix) - 1);
      prefix[sizeof(prefix) - 1] = '\0';
    }

    int prefix_len = strlen(prefix);
    userspace_dirent_t entry;
    uint32_t idx = 0;
    while (match_count < MAX_MATCHES) {
      long ret = syscall3(SYS_READDIR, (long)dir_path, idx++, (long)&entry);
      if (ret != 0) break;

      /* Masquer . et .. sauf si demandé */
      if (prefix[0] != '.' && entry.name[0] == '.') continue;
      if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) continue;

      if (strncmp(entry.name, prefix, prefix_len) == 0) {
        strncpy(matches[match_count], entry.name, sizeof(matches[0]) - 1);
        matches[match_count][sizeof(matches[0]) - 1] = '\0';
        is_dir[match_count] = (entry.type & 0x02) ? 1 : 0;
        match_count++;
      }
    }
  }

  if (match_count == 0) {
    return;
  }

  if (match_count == 1) {
    /* Une seule correspondance : insérer le reste */
    const char *completed_name = matches[0];
    int skip_len = is_command && strchr(word, '/') == NULL ? word_len : 0;
    if (!is_command || strchr(word, '/') != NULL) {
      char *last_slash = strrchr(word, '/');
      skip_len = last_slash ? strlen(last_slash + 1) : word_len;
    }

    const char *suffix = completed_name + skip_len;
    while (*suffix && len < SHELL_LINE_MAX - 2) {
      buffer[len++] = *suffix;
      putchar(*suffix);
      pos++;
      suffix++;
    }

    /* Ajouter '/' pour un dossier, ou un espace pour un fichier */
    if (is_dir[0]) {
      buffer[len++] = '/';
      putchar('/');
      pos++;
    } else {
      buffer[len++] = ' ';
      putchar(' ');
      pos++;
    }
    buffer[len] = '\0';

    *pos_ptr = pos;
    *len_ptr = len;
  } else {
    /* Plusieurs correspondances : trouver le plus long préfixe commun */
    int skip_len = is_command && strchr(word, '/') == NULL ? word_len : 0;
    if (!is_command || strchr(word, '/') != NULL) {
      char *last_slash = strrchr(word, '/');
      skip_len = last_slash ? strlen(last_slash + 1) : word_len;
    }

    int p = skip_len;
    while (1) {
      char c = matches[0][p];
      if (c == '\0') break;
      int all_match = 1;
      for (int i = 1; i < match_count; i++) {
        if (matches[i][p] != c) {
          all_match = 0;
          break;
        }
      }
      if (!all_match) break;
      p++;
    }

    /* Si on peut étendre au moins d'un caractère commun */
    if (p > skip_len) {
      for (int i = skip_len; i < p && len < SHELL_LINE_MAX - 1; i++) {
        buffer[len++] = matches[0][i];
        putchar(matches[0][i]);
        pos++;
      }
      buffer[len] = '\0';
      *pos_ptr = pos;
      *len_ptr = len;
    } else {
      /* Sinon lister toutes les possibilités */
      putchar('\n');
      for (int i = 0; i < match_count; i++) {
        printf("  %s%s", matches[i], is_dir[i] ? "/" : "");
      }
      putchar('\n');
      print_prompt();
      for (int i = 0; i < len; i++) {
        putchar(buffer[i]);
      }
    }
  }
}

/**
 * Lecture interactive d'une ligne de commande.
 * Retourne 0 pour ligne lue, -1 pour Ctrl+C, -2 pour Ctrl+D (EOF).
 */
static int read_line(char *buffer, size_t max_len) {
  int pos = 0;
  int len = 0;
  int hist_nav = -1;

  buffer[0] = '\0';

  while (1) {
    int ic = getchar();
    if (ic < 0) {
      continue;
    }
    unsigned char c = (unsigned char)ic;

    switch (c) {
      case KEY_CTRL_C:
        printf("^C\n");
        buffer[0] = '\0';
        return -1;

      case KEY_CTRL_D:
        if (len == 0) {
          return -2; /* EOF */
        }
        break;

      case '\n':
      case '\r':
        putchar('\n');
        buffer[len] = '\0';
        return 0;

      case '\b':
      case 0x7F:
        if (pos > 0) {
          for (int i = pos - 1; i < len - 1; i++) {
            buffer[i] = buffer[i + 1];
          }
          pos--;
          len--;
          buffer[len] = '\0';

          putchar('\b');
          for (int i = pos; i < len; i++) {
            putchar(buffer[i]);
          }
          putchar(' ');
          for (int i = pos; i <= len; i++) {
            putchar('\b');
          }
        }
        break;

      case '\t':
        autocomplete(buffer, &pos, &len);
        break;

      case KEY_UP:
        if (hist_nav + 1 < history_count && hist_nav + 1 < SHELL_HISTORY_SIZE) {
          hist_nav++;
          const char *h = history_get(hist_nav);
          if (h != NULL) {
            /* Effacer la ligne courante */
            while (pos > 0) {
              putchar('\b');
              pos--;
            }
            for (int i = 0; i < len; i++) {
              putchar(' ');
            }
            for (int i = 0; i < len; i++) {
              putchar('\b');
            }
            /* Insérer la commande de l'historique */
            strncpy(buffer, h, max_len - 1);
            buffer[max_len - 1] = '\0';
            len = strlen(buffer);
            pos = len;
            for (int i = 0; i < len; i++) {
              putchar(buffer[i]);
            }
          }
        }
        break;

      case KEY_DOWN:
        if (hist_nav > 0) {
          hist_nav--;
          const char *h = history_get(hist_nav);
          if (h != NULL) {
            while (pos > 0) {
              putchar('\b');
              pos--;
            }
            for (int i = 0; i < len; i++) {
              putchar(' ');
            }
            for (int i = 0; i < len; i++) {
              putchar('\b');
            }
            strncpy(buffer, h, max_len - 1);
            buffer[max_len - 1] = '\0';
            len = strlen(buffer);
            pos = len;
            for (int i = 0; i < len; i++) {
              putchar(buffer[i]);
            }
          }
        } else if (hist_nav == 0) {
          hist_nav = -1;
          while (pos > 0) {
            putchar('\b');
            pos--;
          }
          for (int i = 0; i < len; i++) {
            putchar(' ');
          }
          for (int i = 0; i < len; i++) {
            putchar('\b');
          }
          buffer[0] = '\0';
          len = 0;
          pos = 0;
        }
        break;

      default:
        if (c >= 32 && c < 127 && len < (int)max_len - 1) {
          if (pos == len) {
            buffer[pos++] = (char)c;
            len++;
            buffer[len] = '\0';
            putchar(c);
          } else {
            for (int i = len; i > pos; i--) {
              buffer[i] = buffer[i - 1];
            }
            buffer[pos] = (char)c;
            len++;
            pos++;
            buffer[len] = '\0';
            for (int i = pos - 1; i < len; i++) {
              putchar(buffer[i]);
            }
            for (int i = pos; i < len; i++) {
              putchar('\b');
            }
          }
        }
        break;
    }
  }
}

static int parse_args(char *line, char **argv, int max_args) {
  int argc = 0;
  char *p = line;

  while (*p && argc < max_args - 1) {
    while (*p == ' ' || *p == '\t') {
      *p++ = '\0';
    }
    if (*p == '\0') {
      break;
    }
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') {
      p++;
    }
  }
  argv[argc] = NULL;
  return argc;
}

static int execute_line(char *line) {
  /* Ignorer les lignes vides ou contenant uniquement des espaces */
  char *trimmed = line;
  while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r' || *trimmed == '\n') {
    trimmed++;
  }
  if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
    return 0;
  }

  /* Découper les arguments */
  char *cmd_argv[SHELL_MAX_ARGS];
  int cmd_argc = parse_args(trimmed, cmd_argv, SHELL_MAX_ARGS);
  if (cmd_argc == 0) {
    return 0;
  }

  /* 1. Commandes internes (builtins) */
  if (strcmp(cmd_argv[0], "cd") == 0) {
    const char *path = (cmd_argc > 1) ? cmd_argv[1] : "/";
    if (strcmp(path, "~") == 0) {
      path = "/";
    }
    if (chdir(path) != 0) {
      printf("cd: %s: No such file or directory\n", path);
    }
    return 0;
  }

  if (strcmp(cmd_argv[0], "pwd") == 0) {
    char cwd_buf[256];
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) {
      printf("%s\n", cwd_buf);
    } else {
      printf("pwd: error retrieving current directory\n");
    }
    return 0;
  }

  if (strcmp(cmd_argv[0], "clear") == 0) {
    syscall0(SYS_CLEAR);
    return 0;
  }

  if (strcmp(cmd_argv[0], "exit") == 0) {
    int exit_code = (cmd_argc > 1) ? atoi(cmd_argv[1]) : 0;
    exit(exit_code);
  }

  if (strcmp(cmd_argv[0], "help") == 0) {
    printf("ALOS Userland Shell Built-in Commands:\n");
    printf("  cd <path>    Change current working directory\n");
    printf("  pwd          Print current working directory\n");
    printf("  clear        Clear screen\n");
    printf("  exit [code]  Exit the shell\n");
    printf("  help         Display this help message\n\n");
    printf("External commands available in /bin:\n");
    printf("  ls, cat, echo, mkdir, touch, rm, rmdir,\n");
    printf("  meminfo, ps, ping, wget, httpd, etc.\n");
    return 0;
  }

  /* 2. Exécution d'un programme externe via SYS_SPAWN_WAIT */
  int ret = spawn_wait(cmd_argv[0], cmd_argc, cmd_argv);
  if (ret < 0) {
    if (cmd_argv[0][0] == '/' || cmd_argv[0][0] == '.') {
      printf("sh: %s: No such file or directory\n", cmd_argv[0]);
    } else {
      printf("sh: %s: command not found\n", cmd_argv[0]);
    }
  } else if (ret >= 128) {
    printf("sh: %s terminated by signal (exit code %d)\n", cmd_argv[0], ret);
  }
  return ret;
}

static int run_script(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("sh: %s: no such file or directory\n", path);
    return 127;
  }

  char buf[SHELL_LINE_MAX];
  int pos = 0;
  char c;
  while (read(fd, &c, 1) > 0) {
    if (c == '\n') {
      buf[pos] = '\0';
      execute_line(buf);
      pos = 0;
    } else if (c != '\r') {
      if (pos < SHELL_LINE_MAX - 1) {
        buf[pos++] = c;
      }
    }
  }
  if (pos > 0) {
    buf[pos] = '\0';
    execute_line(buf);
  }
  close(fd);
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2) {
    return run_script(argv[1]);
  }

  printf("\n=== ALOS Userland Shell (/bin/sh) ===\n");
  printf("Type 'help' for builtins or run commands from /bin.\n\n");

  char line[SHELL_LINE_MAX];

  while (1) {
    print_prompt();

    int res = read_line(line, sizeof(line));
    if (res == -2) {
      /* Ctrl+D sur ligne vide */
      printf("exit\n");
      break;
    }
    if (res == -1) {
      /* Ctrl+C */
      continue;
    }

    char *trimmed = line;
    while (*trimmed == ' ' || *trimmed == '\t') {
      trimmed++;
    }
    if (*trimmed == '\0') {
      continue;
    }

    history_add(trimmed);
    execute_line(line);
  }

  return 0;
}
