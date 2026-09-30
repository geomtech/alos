#include <stdlib.h>
int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }
long long llabs(long long value) { return value < 0 ? -value : value; }
div_t div(int a, int b) { return (div_t){a / b, a % b}; }
ldiv_t ldiv(long a, long b) { return (ldiv_t){a / b, a % b}; }
lldiv_t lldiv(long long a, long long b) { return (lldiv_t){a / b, a % b}; }
