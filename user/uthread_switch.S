	.text

/* Switch from current_thread to next thread_thread, and make
 * next_thread the current_thread.  Use t0 as a temporary register,
 * which should be caller saved. */

/*
Caller-saved registers (AKA volatile registers, or call-clobbered) 
are used to hold temporary quantities that need not be preserved across calls.

Callee-saved registers (AKA non-volatile registers, or call-preserved) are 
used to hold long-lived values that should be preserved across calls.
*/

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
	sd ra, 0(a0)
	sd sp, 8(a0)
	sd s0, 16(a0)
	sd s1, 24(a0)
	sd s2, 32(a0)
	sd s3, 40(a0)
	sd s4, 48(a0)
	sd s5, 56(a0)
	sd s6, 64(a0)
	sd s7, 72(a0)
	sd s8, 80(a0)
	sd s9, 88(a0)
	sd s10, 96(a0)
	sd s11, 104(a0)
/*sf fs0, 112(a0)
	sd fs1, 120(a0)
	sd fs2, 128(a0)
	sd fs3, 136(a0)*/

	ld ra, 0(a1)
	ld sp, 8(a1)
	ld s0, 16(a1)
	ld s1, 24(a1)
	ld s2, 32(a1)
	ld s3, 40(a1)
	ld s4, 48(a1)
	ld s5, 56(a1)
	ld s6, 64(a1)
	ld s7, 72(a1)
	ld s8, 80(a1)
	ld s9, 88(a1)
	ld s10, 96(a1)
	ld s11, 104(a1)
/*ld fs0, 112(a1)
	ld fs1, 120(a1)
	ld fs2, 128(a1)
	ld fs3, 136(a1)*/
	
	ret    /* return to ra */
