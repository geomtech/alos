/* ALOS errno-aware adaptation of musl 1.2.6 __math_xflow. */
#include <errno.h>
#include "libm.h"

double __math_xflow(uint32_t sign, double y)
{
	errno = ERANGE;
	return eval_as_double(fp_barrier(sign ? -y : y) * y);
}
