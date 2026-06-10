// SPDX-License-Identifier: MIT
// Minimal GDB Remote Serial Protocol stub for the PiStorm emulator.
//
// Usage: set env var GDBSTUB_PORT=1234 before launching the emulator.
// Then from a dev host with m68k-elf-gdb (or gdb-multiarch):
//     (gdb) set architecture m68k
//     (gdb) target remote <pi-host>:1234
//     (gdb) add-symbol-file huge-se/figment/figment.elf    0x40840000
//     (gdb) add-symbol-file huge-se/resmgr/ResourceMgr.elf 0x40848000
//
// Only a minimal subset of RSP is implemented: ? g G p P m M c s z0/Z0
// z1/Z1 qSupported qXfer:features:read qAttached H vMustReplyEmpty vCont?
// D k.  Watchpoints and multi-threading are not implemented.
#ifndef GDBSTUB_H
#define GDBSTUB_H

#include <stdint.h>

/* Start the stub.  Returns 0 on success.  Spawns a listener thread that
 * accepts a single gdb client on 0.0.0.0:port.  Safe to call before
 * m68k_init(); the thread won't block the CPU start. */
int  gdbstub_init(int port);

/* Instruction hook.  Hot path: returns immediately if no gdb client is
 * attached and no breakpoints are set.  Called from the m68k core before
 * every instruction executes via m68k_set_instr_hook_callback(). */
void gdbstub_instr_hook(unsigned int pc);

/* Memory watchpoint check.  Called from m68k_read_memory_* /
 * m68k_write_memory_* in emulator.c.  is_write=1 for writes, 0 for reads.
 * Hot path exits in a couple of instructions when no watchpoints are set.
 * When a watchpoint matches, records the hit and requests a stop at the
 * next instruction boundary (imprecise but adequate for debugging). */
extern int gdbstub_wp_count;                 /* lock-free hot-path short-circuit */
void gdbstub_watch_check_slow(unsigned int addr, int len, int is_write);
static inline void gdbstub_watch_check(unsigned int addr, int len, int is_write) {
    if (__builtin_expect(gdbstub_wp_count == 0, 1)) return;
    gdbstub_watch_check_slow(addr, len, is_write);
}

/* Optional: signal that the emulator is shutting down.  Closes sockets. */
void gdbstub_shutdown(void);

#endif /* GDBSTUB_H */
