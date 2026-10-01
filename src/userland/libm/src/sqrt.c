/* Racines carrees IEEE-754 correctement arrondies par les instructions x86-64.
 * Comme musl x86_64, errno n'est pas modifie : math_errhandling repose sur
 * l'exception FE_INVALID levee par l'instruction pour un argument negatif. */
#include <math.h>

double sqrt(double x) {
  __asm__("sqrtsd %1, %0" : "=x"(x) : "x"(x));
  return x;
}

float sqrtf(float x) {
  __asm__("sqrtss %1, %0" : "=x"(x) : "x"(x));
  return x;
}

long double sqrtl(long double x) {
  __asm__("fsqrt" : "+t"(x));
  return x;
}
