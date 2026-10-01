/* ALOS errno-aware adaptation of musl 1.2.6 __math_divzero. */
#include <errno.h>
#include "libm.h"

double __math_divzero(uint32_t sign)
{
	errno = ERANGE;
	return fp_barrier(sign ? -1.0 : 1.0) / 0.0;
}
