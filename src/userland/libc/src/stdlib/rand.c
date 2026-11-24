/* rand/srand : generateur pseudo-aleatoire C standard (deterministe, non
 * cryptographique : pour de l'entropie reelle utiliser getentropy). Etat par
 * thread pour eviter les courses ; graine par defaut 1 comme le standard. */
#include <stdlib.h>

static _Thread_local unsigned long long state = 1;

void srand(unsigned int seed) { state = seed ? seed : 1; }

int rand(void) {
  /* xorshift64* ; 31 bits utiles. */
  unsigned long long x = state ? state : 1;
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
  state = x;
  return (int)((x * 0x2545F4914F6CDD1Dull) >> 33) & RAND_MAX;
}
