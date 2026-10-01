/* ALOS errno-aware adaptation of musl 1.2.6 __math_invalidf. */
#include <errno.h>
#include "libm.h"

float __math_invalidf(float x)
{
	if (!isnan(x)) errno = EDOM;
	return (x - x) / (x - x);
}
