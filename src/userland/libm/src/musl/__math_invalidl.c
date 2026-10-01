/* ALOS errno-aware adaptation of musl 1.2.6 __math_invalidl. */
#include <errno.h>
#include "libm.h"

long double __math_invalidl(long double x)
{
	if (!isnan(x)) errno = EDOM;
	return (x - x) / (x - x);
}
