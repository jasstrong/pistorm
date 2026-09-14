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

/* boot32_pmmu_setup — point the PMMU at the born-32 page tables and enable it.
 * Called (via boot32_glue's trampoline) AFTER the reinstated ROM RAM test at
 * $408026F0 completes.
 *
 * The tables are static, ROM-resident data (boot32_glue.S: boot32_l1/boot32_l2),
 * derived only from BOOT32_MEMTOP.  History: the emulator-side setup at the $48
 * seam put them at $01010000, inside the RAM-test range (swept); boot32 then built
 * them at MemTop-128KB, above every sweep -- but that is BufPtr territory, and an
 * INIT that lowers BufPtr (AppleShare's .AFPTranslator) BlockMoved code over L1.
 * ROM is out of reach of both.
 *
 * Layout:
 *   L1 (256 x 16MB):
 *     [$00]=RAM 0-16M identity  [$01]=RAM 16-32M identity (32MB config)
 *     [$40]->L2                 [else]=dirty alias to $0
 *   L2 (16 x 1MB, for the $40000000 window):
 *     [0-7]=strip $40 -> RAM    [5]=SCSI I/O (CI)
 *     [8]=ROM+SCSI ($40800000, no CI)   [9-F]=I/O ($409-$40F, CI)
 *   TC = $80F08450: E=1, PS=15(32KB), IS=0, TIA=8 TIB=4 TIC=5
 *   CRP/SRP = (limit $7FFF0002, aptr boot32_l1) */
extern const unsigned long boot32_l1[];	/* ROM-resident tables (boot32_glue.S) */

void boot32_pmmu_setup(void)
{
	/* The page tables are static ROM data (boot32_glue.S: boot32_l1/boot32_l2),
	 * not built in RAM: at MemTop-128KB they sat in BufPtr territory and an INIT
	 * that lowered BufPtr (AppleShare .AFPTranslator) BlockMoved code over them. */

	/* low-memory MMU globals so the OS knows an MMU is present + where. */
	*(volatile unsigned char *)0x0CB1UL = 4;            /* MMUType = 68030 */
	*(volatile unsigned char *)0x0B73UL = 0;            /* mode flag (0) */
	*(volatile unsigned long *)0x0CB4UL = (unsigned long)boot32_l1; /* MMUTbl base (ROM) */
	*(volatile unsigned long *)0x0CB8UL = 256UL * 4 + 16UL * 4; /* MMUTbl size */

	/* Fig_InitMemMgr (SuperMario OS/MemoryMgr/FigmentSources/MemMgrBoot.a) is NOT
	 * ported into born-32 figment — MemMgrBoot.a isn't compiled into figment.bin —
	 * so figment's MMFlags config byte is never established and the System comes up
	 * mis-configured.  Replicate its ForROM MMFlags setup here:
	 *   MMStartMode(0)|MMMixed(1)|MMSysheap(2)|MMROZheap(3)|mmFigEnable(5) = $2F
	 *   (32-bit addressing + 32-bit system/ROZ heaps + Figment enabled;
	 *    mmHighSysHeap(4) cleared).  The $0B73 Systemis24bit/Sysheapis24bit bits
	 *   that Fig_InitMemMgr also clears are already 0 (set above). */
	*(volatile unsigned char *)0x1EFCUL = 0x2F;         /* MMFlags */
	/* ...and the rest of Fig_InitMemMgr that figment's C actually reads.
	 * FakeHandleRange ($1E10) is LOAD-BEARING: figment compares heap/block ptrs
	 * against it constantly (LMGetFakeHandleRange, e.g. `if (curHeap >
	 * FakeHandleRange)`) to detect heaps above RealMemTop; left 0 it makes every
	 * such test true -> figment mishandles every heap.  Set FakeHandleRange =
	 * RealMemTop = MemTop.  Zero the grow-zone roots (GZRootHnd/Ptr/MoveHnd) as
	 * Fig_InitMemMgr does.  (MoveBytes/ClearBytes are direct C funcs in figment,
	 * not lomem vectors, so no vMoveBytes/vClearBytes needed for born-32.) */
	*(volatile unsigned long *)0x1EF4UL = BOOT32_MEMTOP;   /* RealMemTop */
	*(volatile unsigned long *)0x1E10UL = BOOT32_MEMTOP;   /* FakeHandleRange */
	*(volatile unsigned long *)0x0328UL = 0;               /* GZRootHnd */
	*(volatile unsigned long *)0x032CUL = 0;               /* GZRootPtr */
	*(volatile unsigned long *)0x0330UL = 0;               /* GZMoveHnd */
	/* vMoveBytes/vClearBytes: jump vectors old-MM callers reach via JSR([$1E00])/
	 * JSR([$1E04]).  Point them at figment's exported routines (the born-32 "hack":
	 * figment provides these as real 32-bit C funcs).  NB figment-build-specific
	 * addresses (memmove/ClearBytes in figment_stubs.c, figment linked @$40840000) —
	 * revisit if figment's layout changes. */
	*(volatile unsigned long *)0x1E00UL = 0x40846D52UL;  /* vMoveBytes -> figment memmove */
	*(volatile unsigned long *)0x1E04UL = 0x40846D2CUL;  /* vClearBytes -> figment ClearBytes */
	*(volatile unsigned long *)0x1E0CUL = 0;             /* vTrashQTMemList */
	/* nil handle/window safety pointers: $0/$4 = ROMBase+$10000 (a safe deref
	 * target so a nil-handle deref reads harmless ROM).  asm barrier defeats the
	 * compiler's null-deref optimization on the literal-0 store. */
	{
		volatile unsigned long *nilp = (volatile unsigned long *)0;
		__asm__ __volatile__("" : "+a"(nilp));
		nilp[0] = 0x40810000UL;   /* $0 nil handle */
		nilp[1] = 0x40810000UL;   /* $4 nil window ptr */
	}

	/* Root pointers + translation control.  Writing TC with bit 31 set turns
	 * on translation; the emulator's PMOVE handler validates + enables. */
	{
		unsigned long crp[2];
		static const unsigned long tc = 0x80F08450UL;
		crp[0] = 0x7FFF0002UL;
		crp[1] = (unsigned long)boot32_l1;	/* ROM-resident L1 */
		__asm__ __volatile__("pmove (%0),%%crp" : : "a"(crp) : "memory");
		__asm__ __volatile__("pmove (%0),%%srp" : : "a"(crp) : "memory");
		__asm__ __volatile__("pmove (%0),%%tc"  : : "a"(&tc) : "memory");
	}
}
