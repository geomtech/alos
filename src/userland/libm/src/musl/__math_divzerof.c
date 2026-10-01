/* ALOS errno-aware adaptation of musl 1.2.6 __math_divzerof. */
#include <errno.h>
#include "libm.h"

float __math_divzerof(uint32_t sign)
{
	errno = ERANGE;
	return fp_barrierf(sign ? -1.0f : 1.0f) / 0.0f;
}
