# huge-se born-32 boot — living state & de-hack plan

**Goal:** a genuinely 32-bit-clean SE boot. No functionality stubbed just to reach
the next blocker; minimal emulator-side band-aids. The stubs/hacks are the *cause*
of the corruption cascade — each skips real setup later code depends on.

**This is the single living boot-state doc.** Update it every session. It is the
Phase-1 inventory. (Full debugging history lives in the memory notes
`figment_born32_heap_wip.md` and `pistorm_hugese_boot_progress.md`.)

---

## Current boot status (2026-07-08)
Best working state (ROM built 04ffaa6a, validationFlags ON, gpclk loaded,
`timeout -s INT 55`): **boot LOADS the System past "Welcome to Macintosh", then
HANGS** = figment `_CheckHeap` infinite loop.

- **Active bug:** `_CheckHeap` loops on a **zero-size block at `$25604`** in SysZone,
  because block `$24004`'s size (`$2400C`) was clobbered to `$1600` by a
  `ConsumeFreeSpaceLow` stat-RMW (`$40842D00 add.l d3,$a4(a3)`) running with a
  **garbage `curHeap = $23F68`** (`$23F68+$A4 = $2400C`).
- **OPEN QUESTION (the real next step):** where does `curHeap = $23F68` come from?
  Plan: durable write-watch on `$2400C` (a3/curHeap + return-addr) → trace origin
  (stale `TheZone` lowmem `$118`? a 24-bit-dirty zone arg into an allocator?).
- **Dead theories (disproven — do not revisit):** stripped-pointer/branch-strip,
  heap-grow (`c_GrowSysZone` is not the root), zone-overlap, wedged-BlueSCSI (was a
  log-capped counter mirage), `validationFlags=0`, `$1CFE`=32MB (provably inert),
  a "DSAT/diagnostic serial" hang (misread).

---

## De-hack roadmap (Phase 1) — inventory tagged KEEP / FIX / FIGMENT / REVERT

### ROM patches (huge-se/patch-rom.py)
| # | patch | tag | note |
|---|-------|-----|------|
| 2,4 | autopatch/brute-force `$00400000` refs → relocated | **FIX-AT-ORIGIN** | jas: track each to origin by disassembly, don't brute-force/alias |
| 3 | exception-vector table `$19E8` relocate | KEEP | |
| 5 | "let all tests run, override MemTop" | **FIX** | should come from real sizing, not override |
| 5b | move system zone above Toolbox trap table | FIGMENT | for figment zone |
| 5c | enlarge resource-map alloc for figment overhead | FIGMENT | |
| 5d | NOP RM back-pointer write that corrupts sys zone | FIGMENT | |
| 5g | InitRSRCMgr → SuperMario version | FIGMENT | |
| 5h | ROM-resident `.SCSIHD` driver | DRIVER | |
| 6 | `$0762` JMP to figment trap installer | FIGMENT | the MM-replacement hook |
| — | SERD lock before ROM JSRs | KEEP | |
| — | boot-icon dest = runtime ScrnBase | KEEP | |
| — | **boot32: STUB the 24-bit RAM test/sizing** (`$25FA`→memsize, `$26F0`/`$2928`→ramtest) | **ELIMINATE** | ★ Phase-1 #1: make real 32-bit sizing/test work |
| — | Lo3Bytes `$031A` = `$FFFFFFFF` (24-bit mask no-op) | KEEP | needed for flat 32-bit |
| — | FXM MapFBlock `add.w`→`add.l` (SuperMario fix) | KEEP | legit 32-bit-clean fix |
| — | route sys-heap-grow `$AE20` → `c_GrowSysZone` | FIGMENT | |
| — | checksum bypass `$416A` (disk driver boot cksum) | REVIEW | |
| — | **`$1CFE` MemTop 8MB→32MB + un-stub `$26F0`/`$2928`** | **REVERT** | my 2026-07-08 red-herring; undo (git checkout patch-rom.py restores committed) |

### Emulator-side hacks
| hack | where | tag | note |
|------|-------|-----|------|
| MemTop force at `$48` seam | emulator.c ~1959 | **FIX** | replace with real ROM sizing (couples with the RAM-test elimination) |
| blind-zero all 32MB RAM at startup | config_file.c:260 | **FIX** | real HW isn't zeroed; RAM test should establish state |
| region-0 dirty-rescan autopatch (`R0_MARK_DIRTY`) | m68kcpu.h | **FIX-AT-ORIGIN** | same as ROM #2/#4 — fix the dirty pointers at source |
| iomap (SE-bus remap) | huge-se.cfg | KEEP | platform requirement |
| fake_gusd / patch_gusd | mac68k-platform.c | REVIEW | until ROM gusd is 32-bit-clean |
| figment MM trap intercept | emulator.c | FIGMENT | MM-replacement mechanism |
| SCSI bus `/RST` pulse at startup | ps_protocol.c | KEEP | good practice; un-wedges drive |
| branch ring | m68kcpu.h/.c | **KEEP (tooling)** | committed keeper |
| `[CHKHEAP]`@`$46610`, `$2400C` write-watch | emulator.c/m68kcpu.h | KEEP→PROMOTE | fold into the committed tracer (Phase 0.2) |
| DACK-RAW, DSAT, DIAG-FAIL, PCHIST-PC, D7-BIT26, DIRTY-ALIAS, CHIME-* | emulator.c/m68kmmu.h/ps_protocol.c | **REVERT** | my 2026-07-08 SCSI/32MB debug churn — strip |

---

## Phase 0 — foundation (in progress)
- **0.1 reproducible pipeline** (TODO): one command `stock ROM → build figment (Retro68) → patch-rom.py (recomputes checksum, line 666) → se-rom-huge.bin → rsync to Pi `~/pistorm/huge-se/` → run`. Kills the unknown/clobbered/stale-ROM class of bugs. Patcher input = stock SE ROM (`big-se/se-rom.bin` = `system.rom`, cksum `$B2E362A8`). figment: `make` in `huge-se/figment/` (needs `~/Retro68-build/toolchain` + SuperMario headers).
- **0.2 committed flag-gated boot tracer** (TODO): boot-stage checkpoints + zone/heap-globals + branch ring + figment `_CheckHeap` validator, in-tree behind a runtime switch — not disposable printfs.
- **0.3 determinism** (TODO): fix rapid-relaunch segfault (settle 12-15s currently; worse after many runs) + run variance.
- **0.4 this doc** (DONE, keep updating).

## Ops / gotchas
- Build: rsync source to Pi, `make PLATFORM=PI4_AARCH64`; **`rm m68kops.o m68kcpu.o emulator.o` after any m68kcpu.h edit** (inlines into m68kops.o too).
- Run: config reads `pistorm/huge-se/se-rom-huge.bin` (NOT `pistorm/se-rom-huge.bin`). Settle ~12-15s between launches (rapid-relaunch segfault). **Mute is SIGINT-only:** `timeout -s INT N ./emulator` or `pkill -INT -x emulator` (zeros SE sound buffer). Never leave a run buzzing.
- gpclk: `sudo insmod ~/pistorm/gpio/gpclk.ko` after any Pi reboot (creates /dev/pistorm-gpio).
- gcc emits `_CheckHeap.part.0`/.constprop clones at addrs≠nm symbol → function-entry probes miss; use write-watches.
