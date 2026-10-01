/* ALOS errno-aware adaptation of musl 1.2.6 __math_invalid. */
#include <errno.h>
#include "libm.h"

double __math_invalid(double x)
{
	if (!isnan(x)) errno = EDOM;
	return (x - x) / (x - x);
}
