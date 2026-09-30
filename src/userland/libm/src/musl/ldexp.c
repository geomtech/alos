#include <math.h>
#include <errno.h>
#include <float.h>
#include <stdint.h>

double ldexp(double x, int n)
{
	double result = scalbn(x, n);
	if (isfinite(x) && x != 0.0) {
		int exponent;
		frexp(x, &exponent);
		int64_t scaled_exponent = (int64_t)exponent + n;
		if (scaled_exponent > DBL_MAX_EXP || scaled_exponent < DBL_MIN_EXP)
			errno = ERANGE;
	}
	return result;
}
