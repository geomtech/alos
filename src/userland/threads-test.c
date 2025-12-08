/* test_race.c - Test de concurrence pour ALOS */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Variable globale partagée entre les threads */
/* 'volatile' dit au compilateur : "Ne mets pas ça en cache dans un registre,
   relis la RAM à chaque fois". C'est crucial pour voir le bug. */
volatile int shared_counter = 0;

/* Verrou rudimentaire (0 = libre, 1 = pris) */
/* volatile int lock = 0; */

void worker(void *arg) {
  int id = (int)(long)arg;
  printf("Thread %d demarre...\n", id);

  /* Incrémentation massive pour forcer la préemption au milieu du calcul */
  for (int i = 0; i < 100000; i++) {
    /*
     * LA SECTION CRITIQUE
     * shared_counter++ n'est pas atomique. En assembleur, c'est :
     * 1. MOV EAX, [addr]   (Lire la valeur courante)
     * 2. INC EAX           (Ajouter 1)
     * -- Si le scheduler coupe ici, le thread B lit la VIEILLE valeur --
     * 3. MOV [addr], EAX   (Sauvegarder)
     */
    shared_counter++;
  }

  printf("Thread %d termine.\n", id);
  exit(0);
}

int main() {
  printf("=== Test de Race Condition ===\n");

  /* On alloue des stacks séparées */
  void *stack1 = malloc(4096);
  void *stack2 = malloc(4096);

  if (!stack1 || !stack2) {
    printf("Erreur allocation stack\n");
    return 1;
  }

  /* On lance 2 threads qui vont se battre pour incrémenter le compteur */
  /* Objectif théorique : 100 000 + 100 000 = 200 000 */
  extern int thread_create(void (*func)(void *), void *stack, void *arg);
  thread_create(worker, (char *)stack1 + 4096, (void *)1);
  thread_create(worker, (char *)stack2 + 4096, (void *)2);

  /* Attente active (très sale, mais on n'a pas encore wait_thread/join) */
  /* On attend que les threads aient fini (environ) */
  printf("Main: Attente des threads...\n");
  sleep(3); // Attendre 3 secondes (suppose que sleep() fonctionne)

  printf("Compteur final : %d (Attendu: 200000)\n", shared_counter);

  if (shared_counter < 200000) {
    printf("⚠️  RACE CONDITION DETECTEE ! (C'est une bonne nouvelle)\n");
    printf("   Des increments ont ete perdus a cause des interruptions.\n");
  } else {
    printf(
        "✅ Compte exact (Suspect... soit pas de preemption, soit chanceux)\n");
  }

  return 0;
}