#ifndef BOOT32_H
#define BOOT32_H

/* boot32 — born-32-bit early-boot routines that replace 24-bit-only code in
 * the Mac SE ROM for the huge-se (32-bit, 32MB, PMMU IS=0) configuration.
 *
 * These are compiled with Retro68 (m68k-apple-macos-gcc, -m68030) and embedded
 * into the ROM mirror at $40870000.  Thin ASM trampolines in boot32_glue.S
 * adapt the SE ROM's diagnostic-dispatcher ABI (result in D6, return via
 * jmp (a6)) to the C calling convention. */

/* Top of RAM for the huge-se.  CONFIGURABLE: change this one value (and the
 * matching sysram/vidram sizes in huge-se.cfg + patch-rom.py reads it) to run
 * born-32 at a different RAM size.  The SE ROM's own sizing routine ($40802928)
 * caps detection at 2MB, so we report the real size here.  Everything else in
 * the RAM layout derives from this: */
/* No UL suffix: this header is #included by boot32_glue.S (gas) too. */
#define BOOT32_MEMTOP 0x02000000
/* Derived layout (all relative to the top of RAM):
 *   video (WTC) buffer : MEMTOP - 64KB  (mirrors to SE-bus $3F0000)
 *   RAM fill cap       : MEMTOP - 128KB (formerly the PMMU tables; those are
 *                        ROM-resident now: boot32_l1/boot32_l2 in boot32_glue.S)
 *   ScrnBase           : MEMTOP - $5900 (Mac screen base, inside the video buf)
 *   RAM-test A1 cap     : MEMTOP - $100  (protect the caller's stack-saved regs) */
#define BOOT32_VIDEO  (BOOT32_MEMTOP - 0x10000)
#define BOOT32_L1     (BOOT32_MEMTOP - 0x20000)
#define BOOT32_L2     (BOOT32_L1 + 0x400)

#ifndef __ASSEMBLER__
/* Replacement for the ROM RAM-sizing routine ($40802928).
 * Returns the top of RAM (the caller installs it as the initial SP). */
unsigned long boot32_memsize(void);

/* Replacement for the ROM destructive RAM test ($408026F0).  The original
 * sweeps a fill pattern across the tested range — including the low exception
 * vector table — which is fatal under born-32 (live vectors).  RAM is real Pi
 * memory; report success (noErr) without touching it. */
unsigned long boot32_ramtest(void);

/* Point the PMMU at the ROM-resident born-32 (IS=0) page tables (boot32_l1 in
 * boot32_glue.S) and enable translation.  Called after the ROM RAM test
 * ($408026F0). */
void boot32_pmmu_setup(void);
#endif /* __ASSEMBLER__ */

#endif /* BOOT32_H */
