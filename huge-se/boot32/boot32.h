#ifndef BOOT32_H
#define BOOT32_H

/* boot32 — born-32-bit early-boot routines that replace 24-bit-only code in
 * the Mac SE ROM for the huge-se (32-bit, 32MB, PMMU IS=0) configuration.
 *
 * These are compiled with Retro68 (m68k-apple-macos-gcc, -m68030) and embedded
 * into the ROM mirror at $40870000.  Thin ASM trampolines in boot32_glue.S
 * adapt the SE ROM's diagnostic-dispatcher ABI (result in D6, return via
 * jmp (a6)) to the C calling convention. */

/* Top of RAM for the huge-se: flat 32MB.  The SE ROM's own sizing routine
 * ($40802928) caps detection at 2MB, so we report the real size here. */
#define BOOT32_MEMTOP 0x02000000UL

/* Replacement for the ROM RAM-sizing routine ($40802928).
 * Returns the top of RAM (the caller installs it as the initial SP). */
unsigned long boot32_memsize(void);

/* Replacement for the ROM destructive RAM test ($408026F0).  The original
 * sweeps a fill pattern across the tested range — including the low exception
 * vector table — which is fatal under born-32 (live vectors).  RAM is real Pi
 * memory; report success (noErr) without touching it. */
unsigned long boot32_ramtest(void);

/* Build the born-32 (IS=0) PMMU page tables in tested RAM and enable
 * translation.  Called after the ROM RAM test ($408026F0) so the destructive
 * sweep can't clobber the tables (the old emulator-side setup at the $48 seam
 * put them inside the swept range). */
void boot32_pmmu_setup(void);

#endif /* BOOT32_H */
