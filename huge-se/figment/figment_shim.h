/* figment_shim.h — compatibility shim for compiling Figment with Retro68/GCC */
#ifndef FIGMENT_SHIM_H
#define FIGMENT_SHIM_H

/* MPW unsigned type aliases */
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;

/* Missing error constant */
#ifndef memROZWarn
#define memROZWarn (-99)  /* read-only zone warning */
#endif

/* MPW pascal keyword — GCC doesn't need it for 68k */
#ifndef pascal
#define pascal
#endif

/* Provide ClearBytes and BlockMoveData as real functions.
 * The MPW inline asm "= {0x4EB0, 0x81E1, 0x1E04}" for ClearBytes
 * does JSR ([$1E04]) through an uninitialized vector — must replace. */
#include <string.h>
/* Debug print via pseudovirt trap $A0FE: A0=string, D0=value */
static inline void figment_debug(const char *msg, unsigned long val) {
    register const char *a0 __asm__("a0") = msg;
    register unsigned long d0 __asm__("d0") = val;
    __asm__ __volatile__(".word 0xA0FE" : : "a"(a0), "d"(d0) : "memory");
}
#define FIG_DBG(msg, val) figment_debug(msg, val)

/* ClearBytes/BlockMoveData/MoveBytes — provided as real functions in figment_stubs.c */

/* MPW inline asm: GetSP returns the current stack pointer.
 * MPW syntax "= {0x2E8F}" (MOVE.L A7,(SP)) doesn't work in GCC.
 * Provide a real function and skip the MPW definition. */
static inline unsigned long _figment_getsp(void) {
    unsigned long sp;
    __asm__ __volatile__ ("move.l %%sp,%0" : "=r"(sp));
    return sp;
}
#define GetSP(...) _figment_getsp()

/* GetCurrentHeap — used by FindHeap in patchedIn mode.
 * Returns the current zone (TheZone low-memory global). */
#define GetCurrentHeap() ((stdHeap*)(*(void**)0x118))

/* Suppress MPW #pragma parameter (register calling convention) */
#define __WANT_LONG_LONG__

#endif /* FIGMENT_SHIM_H */
