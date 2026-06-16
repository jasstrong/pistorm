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
