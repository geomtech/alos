/* ALOS errno-aware adaptation of musl 1.2.6 __math_xflowf. */
#include <errno.h>
#include "libm.h"

float __math_xflowf(uint32_t sign, float y)
{
	errno = ERANGE;
	return eval_as_float(fp_barrierf(sign ? -y : y) * y);
}
