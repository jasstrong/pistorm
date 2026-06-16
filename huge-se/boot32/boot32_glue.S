/* boot32_glue.S — ROM-ABI trampolines for the boot32 C routines.
 *
 * The SE ROM power-on diagnostic dispatcher calls each test routine with:
 *   A6 = return address   (routine ends with `jmp (a6)`)
 *   D7 = test number       (preserved across the call)
 *   D6 = result            (0 = pass; nonzero -> Sad Mac)
 *   A0/A1 = range args (for the RAM test)
 *
 * The first two longwords of the blob form a fixed-offset jump table so
 * patch-rom.py can redirect the ROM routine entries without symbol lookup:
 *   blob+0 -> RAM sizing replacement  (patched over $40802928)
 *   blob+6 -> RAM test   replacement  (patched over $408026F0)
 */
	.text
	.even

	.globl	boot32_table
boot32_table:
	jmp	boot32_memsize_rom	/* +0: 6 bytes (jmp xxx.l) */
	jmp	boot32_ramtest_rom	/* +6: 6 bytes */

/* ---- RAM sizing: In: A7=SP estimate.  Out: D6 = top of RAM. ------------- */
	.globl	boot32_memsize_rom
boot32_memsize_rom:
	movem.l	%d1-%d5/%a0-%a5,-(%sp)	/* preserve; D0 holds the C result */
	jsr	boot32_memsize
	move.l	%d0,%d6
	movem.l	(%sp)+,%d1-%d5/%a0-%a5
	jmp	(%a6)

/* ---- RAM test: In: A0/A1 (ignored).  Out: D6 = 0 (noErr). --------------- */
	.globl	boot32_ramtest_rom
boot32_ramtest_rom:
	movem.l	%d1-%d5/%a0-%a5,-(%sp)
	jsr	boot32_ramtest
	move.l	%d0,%d6
	movem.l	(%sp)+,%d1-%d5/%a0-%a5
	jmp	(%a6)
