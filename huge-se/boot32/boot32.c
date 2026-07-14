/* boot32.c — born-32-bit early-boot replacements for SE ROM 24-bit code.
 * See boot32.h for the architecture overview. */

#include "boot32.h"

unsigned long boot32_memsize(void)
{
	/* The huge-se is born-32-bit with a flat 32MB physical space (PMMU IS=0).
	 * The SE ROM's aliasing probe ($40802928) caps detection at 2MB, so it
	 * would size RAM far too small and set MemTop/SP into the low megabytes.
	 * We know the real top, so report it directly. */
	return BOOT32_MEMTOP;
}

unsigned long boot32_ramtest(void)
{
	/* Non-destructive: the backing store is real Pi RAM, and the ROM's
	 * destructive sweep clobbers the exception vector table.  Report noErr. */
	return 0;
}

/* boot32_pmmu_setup — build the IS=0 born-32 page tables in tested RAM and
 * enable the PMMU.  Called (via boot32_glue's trampoline) AFTER the reinstated
 * ROM RAM test at $408026F0 completes, so the destructive sweep can't clobber
 * the tables.  This replaces the old emulator-side setup at the $48 seam, whose
 * tables at $01010000 sat inside the RAM-test range and got overwritten.
 *
 * Tables live at $01FE0000 — the 64KB just below the $01FF0000 WTC video
 * buffer, ABOVE both RAM-sweep ranges (the RAM test at $26F0 and the memsize
 * fill both cap at $01FE0000).  The diagnostic dispatcher calls the RAM test
 * more than once, so a mid-RAM table (the old $01010000) got swept and clobbered
 * by a later RAM-test pass after this setup ran; keeping the tables above every
 * sweep makes them survive.
 *
 * Layout:
 *   L1 @ $01FE0000 (256 x 16MB):
 *     [$00]=RAM 0-16M identity  [$01]=RAM 16-32M identity
 *     [$40]->L2                 [else]=dirty alias to $0
 *   L2 @ $01FE0400 (16 x 1MB, for the $40000000 window):
 *     [0-7]=strip $40 -> RAM    [5]=SCSI I/O (CI)
 *     [8]=ROM+SCSI ($40800000, no CI)   [9-F]=I/O ($409-$40F, CI)
 *   TC = $80F08450: E=1, PS=15(32KB), IS=0, TIA=8 TIB=4 TIC=5
 *   CRP/SRP = (limit $7FFF0002, aptr $01FE0000) */
/* BOOT32_L1 / BOOT32_L2 now derived from BOOT32_MEMTOP in boot32.h. */
void boot32_pmmu_setup(void)
{
	volatile unsigned long *l1 = (volatile unsigned long *)BOOT32_L1;
	volatile unsigned long *l2 = (volatile unsigned long *)BOOT32_L2;
	int i;

	/* Default every L1 entry to a base-$0 early-termination page descriptor: a
	 * "dirty alias" that maps its 16MB slot onto physical $0-$16MB, i.e. STRIPS
	 * the top byte of any address landing there (born-32's 24-bit-dirty-pointer
	 * tolerance). */
	for (i = 0; i < 256; i++)
		l1[i] = 0x00000019UL;                 /* DT=1, base $0 (dirty alias) */
	/* Identity-map exactly the L1 slots backed by REAL RAM — one 16MB slot per
	 * $01000000 of MemTop — so accesses to real RAM pass straight through.
	 * Everything above stays a base-$0 alias, so a dirty (bit-24+) pointer over
	 * the RAM top is stripped into low RAM rather than hitting non-existent
	 * physical.  Derived from BOOT32_MEMTOP so 16MB/32MB are both correct
	 * (16MB: only slot 0 identity; 32MB: slots 0-1).  For MemTop < 16MB the loop
	 * is empty and slot 0's base-$0 alias already identity-maps the low 16MB. */
	{
		int ram_slots = (int)(BOOT32_MEMTOP >> 24);
		for (i = 0; i < ram_slots; i++)
			l1[i] = ((unsigned long)i << 24) | 0x19UL;   /* RAM identity */
	}
	l1[0x40] = (BOOT32_L2 & 0xFFFFFFFCUL) | 0x02UL;  /* DT=2 -> L2 */

	for (i = 0; i < 8; i++)
		l2[i] = ((unsigned long)i << 20) | 0x19UL;      /* strip $40 -> RAM */
	l2[5] = 0x40000000UL | (5UL << 20) | 0x59UL;        /* SCSI I/O, CI */
	l2[8] = 0x40800019UL;                               /* ROM + SCSI, no CI */
	for (i = 9; i <= 15; i++)
		l2[i] = 0x40000000UL | ((unsigned long)i << 20) | 0x59UL;  /* I/O, CI */

	/* low-memory MMU globals so the OS knows an MMU is present + where. */
	*(volatile unsigned char *)0x0CB1UL = 4;            /* MMUType = 68030 */
	*(volatile unsigned char *)0x0B73UL = 0;            /* mode flag (0) */
	*(volatile unsigned long *)0x0CB4UL = BOOT32_L1;    /* MMUTbl base */
	*(volatile unsigned long *)0x0CB8UL = 256UL * 4 + 16UL * 4; /* MMUTbl size */

	/* Root pointers + translation control.  Writing TC with bit 31 set turns
	 * on translation; the emulator's PMOVE handler validates + enables. */
	{
		static const unsigned long crp[2] = { 0x7FFF0002UL, BOOT32_L1 };
		static const unsigned long tc = 0x80F08450UL;
		__asm__ __volatile__("pmove (%0),%%crp" : : "a"(crp) : "memory");
		__asm__ __volatile__("pmove (%0),%%srp" : : "a"(crp) : "memory");
		__asm__ __volatile__("pmove (%0),%%tc"  : : "a"(&tc) : "memory");
	}
}
