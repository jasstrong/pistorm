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
	jmp	boot32_memsize_rom	/* +0:  6 bytes (jmp xxx.l) */
	jmp	boot32_ramtest_rom	/* +6:  6 bytes */
	jmp	boot32_pmmu_setup_rom	/* +12: 6 bytes — A6-trampoline target */

/* ---- RAM sizing: In: A7=SP estimate.  Out: D6 = top of RAM. -------------
 * Born-32 RAM INIT (the original sizing's lost side effect): the real ROM wrote
 * the RAM-test pattern ($6DB6DB6D) into most of physical RAM as it sized — leaving
 * RAM DEFINED before the OS ran.  born-32's memsize just returned the size and
 * wrote nothing, so the OS ran on emulator garbage that varies per boot → the
 * nondeterministic figment-heap corruption.  Fill the pattern HERE (sizing is
 * early, before POST builds RAM structures — the RAM-test point was too late):
 * $4000 (above vectors/low-mem) up to min($01FF0000, SP-128KB) so the live stack
 * and the high screen buffer survive.  Then return MemTop as before. */
	.globl	boot32_memsize_rom
boot32_memsize_rom:
	movem.l	%d0-%d5/%a0-%a5,-(%sp)
	/* Fill most of physical RAM with the Mac RAM-test pattern (the original sizing's
	 * lost side effect) so the OS runs on DEFINED memory.  Range: $4000 (above the
	 * vectors/low-mem) up to min(SP-128KB, $01FF0000).  The $01FF0000 cap is the WTC
	 * video boundary — RAM at/above it passes through the modified wtcram mapping to
	 * the SE physical video/sound, so we must NOT write there.  SP-clamp preserves the
	 * live stack. */
	move.l	%sp,%d2
	sub.l	#0x20000,%d2		/* d2 = SP - 128KB (preserve stack) */
	cmp.l	#0x01FE0000,%d2
	bcs.s	0f
	/* Cap at $01FE0000: the 64KB below the $01FF0000 WTC video buffer is
	 * reserved for the born-32 PMMU page tables (boot32_pmmu_setup builds them
	 * at $01FE0000).  The RAM test caps at the same boundary.  Filling above it
	 * would corrupt the descriptors (the POST re-sizes after the tables exist). */
	move.l	#0x01FE0000,%d2
0:	move.l	#0x4000,%a0
	move.l	#0x6DB6DB6D,%d1		/* authentic Mac RAM-test fill pattern */
1:	cmp.l	%d2,%a0
	bcc.s	2f
	move.l	%d1,(%a0)+
	bra.s	1b
2:	jsr	boot32_memsize		/* D0 = MemTop ($02000000) */
	move.l	%d0,%d6
	movem.l	(%sp)+,%d0-%d5/%a0-%a5
	jmp	(%a6)

/* ---- RAM test: In: A0/A1 (ignored).  Out: D6 = 0 (noErr). ---------------
 * No-op: a blanket clear here clobbers born-32 POST RAM structures (set before
 * the test, relied on after) → cascade.  RAM init, if needed, must happen before
 * the CPU runs, not at the (too-late) RAM-test point. */
	.globl	boot32_ramtest_rom
boot32_ramtest_rom:
	movem.l	%d1-%d5/%a0-%a5,-(%sp)
	jsr	boot32_ramtest
	move.l	%d0,%d6
	movem.l	(%sp)+,%d1-%d5/%a0-%a5
	jmp	(%a6)

/* ---- PMMU setup trampoline, reached after the LAST RAM-test call -----------
 * The RAM test sweeps essentially ALL of RAM across 4 calls; the LAST is
 * A0=$00200000,A1=$01FFA700. The $26F0 stub (patch-rom.py) detects that call,
 * saves the diagnostic return in A3, and points A6 here — so the test's
 * `jmp (a6)` lands here AFTER the final sweep. We build the born-32 page tables
 * in the now-stable RAM (nothing sweeps it again), enable translation + set the
 * MMU low-mem globals (so the System knows it's a 32-bit machine), then restore
 * A6 from A3 and return to the POST with the MMU on. A3 (not the stack) carries
 * the return because the RAM test overwrites the stack region; A3 is callee-saved
 * so the C helper preserves it. */
	.globl	boot32_pmmu_setup_rom
boot32_pmmu_setup_rom:
	movem.l	%d0-%d7/%a0-%a5,-(%sp)
	jsr	boot32_pmmu_setup	/* build tables + MMU globals + PMOVE CRP/SRP/TC (enable) */
	movem.l	(%sp)+,%d0-%d7/%a0-%a5
	move.l	%a3,%a6			/* the real diagnostic return the stub saved in A3 */
	jmp	(%a6)
