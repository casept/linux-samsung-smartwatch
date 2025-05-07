/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_TSC_H
#define _ASM_UM_TSC_H

#include <asm/asm.h>

/**
 * rdtsc() - returns the current TSC without ordering constraints
 *
 * rdtsc() returns the result of RDTSC as a 64-bit integer.  The
 * only ordering constraint it supplies is the ordering implied by
 * "asm volatile": it will put the RDTSC in the place you expect.  The
 * CPU can and will speculatively execute that RDTSC, though, so the
 * results can be non-monotonic if compared on different CPUs.
 */
static __always_inline u64 rdtsc(void)
{
	EAX_EDX_DECLARE_ARGS(val, low, high);

	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high);
}

#endif /* _ASM_UM_TSC_H */
