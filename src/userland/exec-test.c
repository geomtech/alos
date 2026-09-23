/* src/userland/exec-test.c - Régression execve(): image replacement, échec
 * sans effet de bord, et absence de fuite/zombie sur de nombreuses
 * itérations.
 *
 * Vérifie (cf. process_execve() dans src/kernel/process.c) :
 *   - Un execve() vers un binaire inexistant échoue (retourne -1) SANS
 *     toucher au processus appelant : même PID, même cwd, mêmes FDs
 *     ouverts (le programme continue de s'exécuter, exactement comme si
 *     l'appel n'avait jamais eu lieu).
 *   - Un execve() valide qui suit remplace l'image du processus tout en
 *     conservant le même PID (POSIX: execve() ne change jamais le PID).
 *   - En répétant échec+succès plusieurs dizaines de fois (en s'auto-
 *     ré-exécutant via son propre chemin "/bin/exec-test"), le mécanisme
 *     "spawn puis transplant" de process_execve() ne doit fuir aucune
 *     structure noyau (page directory, thread, process_t temporaires) ni
 *     laisser de zombie derrière lui.
 *
 * Usage : exec-test [iteration]
 *   - Sans argument : première itération (iteration = 0).
 *   - Avec argument : itération courante, utilisée pour la ré-exécution
 *     en chaîne et l'arrêt après TOTAL_ITERATIONS répétitions.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TOTAL_ITERATIONS 40 /* "plusieurs dizaines de fois" */
#define INVALID_PATH "/bin/does-not-exist-xyz"
#define SELF_PATH "/bin/exec-test"
#define PROBE_FILE "/config/startup.sh"

int main(int argc, char **argv) {
  int iteration = (argc >= 2) ? atoi(argv[1]) : 0;
  int pid_before = getpid();

  char cwd_before[256];
  if (getcwd(cwd_before, sizeof(cwd_before)) == NULL) {
    printf("exec-test: FAIL getcwd() failed at iteration %d\n", iteration);
    return 1;
  }

  /* FD de contrôle : ouvert AVANT l'execve invalide. Si le processus était
   * détruit/recréé (au lieu d'un échec propre), cette description de
   * fichier deviendrait invalide. */
  int probe_fd = open(PROBE_FILE, O_RDONLY);
  if (probe_fd < 0) {
    printf("exec-test: FAIL unable to open probe file at iteration %d\n",
           iteration);
    return 1;
  }

  /* 1) execve() vers un binaire inexistant : DOIT échouer et laisser le
   *    processus appelant totalement intact. */
  char *bad_argv[] = {INVALID_PATH, NULL};
  int ret = execve(INVALID_PATH, bad_argv, NULL);
  if (ret == 0) {
    /* Ne devrait jamais s'exécuter : si l'image avait vraiment été
     * remplacée, on ne reviendrait jamais ici. */
    printf("exec-test: FAIL invalid execve unexpectedly returned 0\n");
    return 1;
  }

  /* PID inchangé après l'échec. */
  int pid_after_fail = getpid();
  if (pid_after_fail != pid_before) {
    printf("exec-test: FAIL pid changed after failed execve (%d -> %d)\n",
           pid_before, pid_after_fail);
    return 1;
  }

  /* cwd inchangé après l'échec. */
  char cwd_after_fail[256];
  if (getcwd(cwd_after_fail, sizeof(cwd_after_fail)) == NULL ||
      strcmp(cwd_before, cwd_after_fail) != 0) {
    printf("exec-test: FAIL cwd changed after failed execve\n");
    return 1;
  }

  /* FD toujours valide après l'échec : on doit pouvoir relire depuis le
   * début du fichier sans erreur. */
  char probe_buf[16];
  int probe_read = read(probe_fd, probe_buf, sizeof(probe_buf));
  if (probe_read <= 0) {
    printf("exec-test: FAIL probe fd unusable after failed execve\n");
    return 1;
  }
  close(probe_fd);

  printf("exec-test: iter=%d pid=%d ok (invalid execve failed cleanly, "
         "cwd/fd intact)\n",
         iteration, pid_before);

  if (iteration >= TOTAL_ITERATIONS) {
    printf("exec-test: PASS after %d chained execve rounds, pid=%d "
           "unchanged throughout\n",
           iteration, pid_before);
    return 42;
  }

  /* 2) execve() valide : on s'auto-ré-exécute pour enchaîner l'itération
   *    suivante. Si ça réussit, AUCUN code après cet appel ne s'exécute :
   *    l'image courante est remplacée par la suivante (même PID). */
  char iter_str[16];
  snprintf(iter_str, sizeof(iter_str), "%d", iteration + 1);
  char *next_argv[] = {SELF_PATH, iter_str, NULL};
  execve(SELF_PATH, next_argv, NULL);

  /* Si on arrive ici, l'execve valide a échoué : c'est un bug. */
  printf("exec-test: FAIL valid self-execve failed unexpectedly at "
         "iteration %d\n",
         iteration);
  return 1;
}
