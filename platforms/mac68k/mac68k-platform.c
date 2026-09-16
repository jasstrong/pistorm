// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "platforms/platforms.h"
#include "platforms/shared/rtc.h"
#include "gpio/ps_protocol.h"
#include "vnc/vnc.h"

//#define DEBUG_MAC_PLATFORM

#ifdef DEBUG_MAC_PLATFORM
#define DEBUG printf
#else
#define DEBUG(...)
#endif

#define min(a, b) (a < b) ? a : b
#define max(a, b) (a > b) ? a : b

extern void stop_cpu_emulation(uint8_t disasm_cur);

uint8_t iscsi_enabled;
uint8_t noscsi_enabled;

struct vnc_config vnc_cfg;

extern int kb_hook_enabled;
extern int mouse_hook_enabled;
extern unsigned int ovl;

uint32_t ovl_sysrom_pos = 0x400000;
uint32_t ovl_decode_size = 0x20000; /* 128KB OVL overlay on Mac SE */
uint32_t bigse_vbuf_virt = 0x7F0000;  /* video buffer virt addr, set by OVL handler */

/* Big/Huge SE: the SE video/sound buffer occupies the top 64KB of visible RAM;
 * its writes mirror to the SE-bus physical video/sound at $3F0000 (the real CRT
 * + BBU audio). */
#define BIGSE_VBUF_SIZE  0x010000
#define BIGSE_VBUF_PHYS  0x3F0000

/* Runtime fake-revert of the on-disk 'gusd' gestalt patch (bigSE only).  The
 * disk keeps machine 5/9 addressing-method = $0005 (the 32-bit-clean IIci
 * method, which hugeSE's System needs).  bigSE is 24-bit (cpu 68030_24) and
 * hangs at "Welcome to Macintosh" on that method, so we substitute the original
 * SE/SE-30 methods ($0002/$0003) as the resource streams off SCSI — no disk edit
 * needed, hugeSE unaffected.  Enabled by `setvar fake_gusd 1`. */
int fake_gusd_enabled = 0;
/* hugeSE: patch the loaded 'gusd' in RAM to the 32-bit methods (volume-independent
 * sibling of fake_gusd — works even when the boot volume's gusd is stock/compressed).
 * Enabled by `setvar patch_gusd 1`; the hook lives in emulator.c ([GUSD-RAM]). */
int patch_gusd_enabled = 0;

/*
 * SCSI driver ROM exclusion zone: addresses in this range bypass the
 * Musashi fast-path cache and go through real GPIO bus cycles, so the
 * BBU sees ROM reads (instruction fetches) between SCSI driver instructions.
 */
/* SCSI driver ROM exclusion zone — offset from ROM base.
 * Instruction fetches in this range must go through the SE bus
 * so the BBU sees real bus cycles for DMA timing. */
#define SCSI_ROM_OFFSET_LOW   0x1A000
#define SCSI_ROM_OFFSET_HIGH  0x1C000

/* Top of RAM that must be write-through for BBU video/sound DMA */
#define WTC_REGION_SIZE 0x10000  /* 64KB */

void adjust_ranges_mac68k(struct emulator_config *cfg) {
    cfg->mapped_high = 0;
    cfg->mapped_low = 0;
    cfg->custom_high = 0;
    cfg->custom_low = 0;

    // Set up the min/max ranges for mapped reads/writes
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
        if (cfg->map_type[i] != MAPTYPE_NONE) {
            if ((cfg->map_offset[i] != 0 && cfg->map_offset[i] < cfg->mapped_low) || cfg->mapped_low == 0)
                cfg->mapped_low = cfg->map_offset[i];
            if (cfg->map_offset[i] + cfg->map_size[i] > cfg->mapped_high)
                cfg->mapped_high = cfg->map_offset[i] + cfg->map_size[i];
        }
    }

    printf("[MAC68K] Platform custom range: %.8X-%.8X\n", cfg->custom_low, cfg->custom_high);
    printf("[MAC68K] Platform mapped range: %.8X-%.8X\n", cfg->mapped_low, cfg->mapped_high);
}


int setup_platform_mac68k(struct emulator_config *cfg) {
    printf("Performing setup for Mac68k platform.\n");

    if (strlen(cfg->platform->subsys)) {
        printf("Sub system %sd specified, but no handler is available for this.\n", cfg->platform->subsys);
    }
    else
        printf("No sub system specified.\n");

    handle_ovl_mappings_mac68k(cfg);

    return 0;
}

#define CHKVAR(a) (strcmp(var, a) == 0)

void setvar_mac68k(struct emulator_config *cfg, char *var, char *val) {
    if (!var)
        return;

    // FIXME: Silence unused variable warnings.
    if (var || cfg || val) {}

    if (CHKVAR("sysrom_pos") && val) {
        ovl_sysrom_pos = get_int(val);
        printf("[MAC68K] System ROM/RAM OVL position set to %.8X\n", ovl_sysrom_pos);
    }

    if (CHKVAR("iscsi") && !iscsi_enabled) {
        printf("[MAC68K] iSCSI Interface Enabled... well, not really.\n");
        iscsi_enabled = 1;
        //iscsi_init();
        //adjust_ranges_mac68k(cfg);
    }

    if (CHKVAR("noscsi")) {
        printf("[MAC68K] SCSI bypass enabled — 5380 accesses will be swallowed.\n");
        noscsi_enabled = 1;
    }

    if (CHKVAR("vnc")) {
        vnc_cfg.enabled = 1;
        vnc_cfg.port = (val && strlen(val)) ? (int)get_int(val) : 5900;
        printf("[MAC68K] VNC server enabled on port %d\n", vnc_cfg.port);
    }

    if (CHKVAR("mode32")) {
        extern int mode32_enabled;
        mode32_enabled = 1;
        printf("[MAC68K] MODE32 trap enabled\n");
    }
    if (CHKVAR("trace_all")) {
        extern int trace_all_enabled;
        trace_all_enabled = 1;
        printf("[MAC68K] TRACE-ALL enabled — armed on first loaded-code PC, dumps every instr + data access to /tmp/trace.txt\n");
    }

    if (CHKVAR("fake_gusd")) {
        extern int fake_gusd_enabled;
        fake_gusd_enabled = 1;
        printf("[MAC68K] Runtime 'gusd' gestalt fake-revert enabled (bigSE 24-bit)\n");
    }

    if (CHKVAR("patch_gusd")) {
        extern int patch_gusd_enabled;
        patch_gusd_enabled = 1;
        printf("[MAC68K] Runtime 'gusd' 32-bit-method patch enabled (hugeSE, in-RAM after load)\n");
    }

    if (CHKVAR("probes")) {
        extern int probes_enabled;
        probes_enabled = 1;
        printf("[MAC68K] Print-only boot probes enabled (per-instruction diagnostics; slow)\n");
    }

    if (CHKVAR("stlb")) {
        extern int pmmu_stlb_mode;
        pmmu_stlb_mode = val ? atoi(val) : 1;
        printf("[MAC68K] PMMU soft TLB mode %d (0 off, 1 on, N>1 on + re-check every Nth hit)\n", pmmu_stlb_mode);
    }

    if (CHKVAR("menutrace")) {
        extern int menutrace_enabled;
        menutrace_enabled = 1;
        printf("[MAC68K] Screen CopyBits / offscreen-buffer trace enabled ([MENU-CB] [MENU-QDX])\n");
    }

    if (CHKVAR("figment")) {
        extern int figment_enabled;
        extern int figment_verbose;
        extern int suppress_rom_patches;
        figment_enabled = 1;
        figment_verbose = (getenv("HUGESE_VERBOSE") != NULL);
        suppress_rom_patches = (getenv("HUGESE_NOPATCH") != NULL);
        printf("[MAC68K] Figment MM trap intercept enabled (verbose=%d nopatch=%d)\n",
               figment_verbose, suppress_rom_patches);
    }
}


void handle_ovl_mappings_mac68k(struct emulator_config *cfg) {
    int32_t index = -1;
    static unsigned char *ram_range_ptr = NULL;
    static unsigned char *ram_wtc_ptr = NULL;

    /*
     * Mac SE memory map:
     *   ROM is ALWAYS at 0x400000 (decoded by BBU).
     *   OVL adds a mirror of ROM at 0x000000 (covers ROM size).
     *   RAM is at 0x000000 when OVL is off.
     *   During OVL, RAM above the ROM overlay is still accessible.
     *
     * Musashi fast-path: ROM always at 0x400000.
     * Config map_offset: follows OVL state for handle_mapped_read
     * (so reads from 0x0 during OVL go through slow path to ROM).
     */

    /* Get ROM size for overlay calculation */
    int32_t rom_index = get_named_mapped_item(cfg, "sysrom");
    uint32_t rom_size = (rom_index != -1) ? cfg->map_size[rom_index] : 0x80000;

    if (rom_index != -1) {
        /* Config offset tracks OVL for the slow-path mapped read handler */
        cfg->map_offset[rom_index] = (ovl) ? 0x0 : ovl_sysrom_pos;
        cfg->map_high[rom_index] = cfg->map_offset[rom_index] + cfg->map_size[rom_index];
        /* Fast-path: ROM at sysrom_pos (read-only).
         * Writes to ROM range are caught by custom_write and applied to
         * the buffer — needed for the MM code at $AC6E which does CLR.L
         * to a ROM address and expects the next read to see zero. */
        m68k_remove_range(cfg->map_data[rom_index]);
        m68k_add_rom_range(ovl_sysrom_pos, ovl_sysrom_pos + cfg->map_size[rom_index], cfg->map_data[rom_index]);
        printf("[MAC68K] ROM at %08X (fast-path at %08X-%08X)\n",
               cfg->map_offset[rom_index], ovl_sysrom_pos, ovl_sysrom_pos + cfg->map_size[rom_index]);
    }

    /* sysram: plain fast Pi RAM (bulk).  Fast inline read+write, NO SE-bus
     * write-through.  During OVL the ROM overlays $0..ovl_decode_size, so the
     * fast RAM starts above the overlay. */
    index = get_named_mapped_item(cfg, "sysram");
    if (index != -1) {
        m68k_remove_range(cfg->map_data[index]);
        if (ram_range_ptr && ram_range_ptr != cfg->map_data[index])
            m68k_remove_range(ram_range_ptr);
        ram_range_ptr = NULL;

        uint32_t ram_start = ovl ? ovl_decode_size : 0;
        uint32_t ram_end = cfg->map_size[index];
        /* bigSE declares the WHOLE 8MB as one wtcram map with NO separate vidram
         * map.  Carve the top 64KB (the SE video/sound window; ScrnBase =
         * MemTop-$5900 lives here) as a relocating WTC that mirrors to the SE-bus
         * physical video/sound at $3F0000, so the framebuffer reaches the real
         * CRT (else the BBU shows garbage = "simasimac").  The rest stays plain
         * fast RAM.  This is CODE-SIDE (not a config split), and
         * m68k_add_ram_range_wtc sets a fast READABLE range, so the stock SE RAM
         * test still reads it back and counts it into MemTop (a config-level
         * vidram split does drop MemTop; this does not).  hugeSE is untouched:
         * its sysram is plain MAPTYPE_RAM and its video uses a separate vidram
         * map handled below, so this branch never fires for it. */
        int wtc_sysram = (cfg->map_type[index] == MAPTYPE_RAM_WTC)
                         && (get_named_mapped_item(cfg, "vidram") == -1);
        uint32_t vid_start = (ram_end >= BIGSE_VBUF_SIZE) ? ram_end - BIGSE_VBUF_SIZE : ram_end;
        if (wtc_sysram && ram_start < vid_start) {
            if (ram_wtc_ptr && ram_wtc_ptr != cfg->map_data[index]) {
                m68k_remove_range(ram_wtc_ptr);   /* drop a prior carve (reset re-run) */
                ram_wtc_ptr = NULL;
            }
            ram_range_ptr = cfg->map_data[index] + ram_start;
            m68k_add_ram_range(ram_start, vid_start, ram_range_ptr);
            ram_wtc_ptr = cfg->map_data[index] + vid_start;
            m68k_add_ram_range_wtc(vid_start, ram_end, ram_wtc_ptr, BIGSE_VBUF_PHYS);
            bigse_vbuf_virt = vid_start;
            printf("[MAC68K] bigSE video WTC %08X-%08X -> SE bus %08X (CRT mirror)\n",
                   (unsigned)vid_start, (unsigned)ram_end, (unsigned)BIGSE_VBUF_PHYS);
        } else if (ram_start < ram_end) {
            ram_range_ptr = cfg->map_data[index] + ram_start;
            m68k_add_ram_range(ram_start, ram_end, ram_range_ptr);
        }
        /* Zero the config range so handle_mapped_write doesn't intercept —
         * the fast range above serves all sysram accesses. */
        cfg->map_offset[index] = 0;
        cfg->map_high[index] = 0;
        printf("[MAC68K] sysram fast RAM %08X-%08X (OVL covers 0-%08X)\n",
               ram_start, ram_end, ovl ? ovl_decode_size : 0);
    }

    /* vidram: 64KB write-through-cache; writes mirror to the configured SE-bus
     * address (map_sebus) — the relocating WTC (emu top-of-RAM video window ->
     * SE top-of-RAM video/sound buffer). */
    index = get_named_mapped_item(cfg, "vidram");
    if (index != -1) {
        m68k_remove_range(cfg->map_data[index]);
        if (ram_wtc_ptr && ram_wtc_ptr != cfg->map_data[index])
            m68k_remove_range(ram_wtc_ptr);
        ram_wtc_ptr = cfg->map_data[index];
        m68k_add_ram_range_wtc(cfg->map_offset[index], cfg->map_high[index],
                               ram_wtc_ptr, (uint32_t)cfg->map_sebus[index]);
        bigse_vbuf_virt = cfg->map_offset[index];  /* for shutdown mute, etc. */
        printf("[MAC68K] vidram WTC %08X-%08X -> SE bus %08X\n",
               cfg->map_offset[index], cfg->map_high[index], (uint32_t)cfg->map_sebus[index]);
    }

    adjust_ranges_mac68k(cfg);
}

void handle_reset_mac68k(struct emulator_config *cfg) {
    DEBUG("[MAC68K] Reset handler.\n");

    if (iscsi_enabled) {
        //iscsi_refresh_drives();
    }

    handle_ovl_mappings_mac68k(cfg);
}

void shutdown_platform_mac68k(struct emulator_config *cfg) {
    printf("[MAC68K] Performing Mac68k platform shutdown.\n");
    if (cfg) {}

    /* Mute the BBU audio DMA on exit: the audio DMA free-runs off the SE's
     * physical sound buffer (top of the real ~4MB, $3FFD00-$400000) on its own
     * video-timed clock, independent of the CPU.  If we exit with garbage in
     * that buffer the speaker buzzes until power-cycle.  Zero it so a constant
     * sample = silence. */
    for (uint32_t a = 0x3F0000; a < 0x400000; a += 2)
        ps_write_16(a, 0);
    printf("[MAC68K] Zeroed SE physical video/sound buffer ($3F0000-$400000) to mute BBU.\n");

    if (cfg->platform->subsys) {
        free(cfg->platform->subsys);
        cfg->platform->subsys = NULL;
    }
    if (iscsi_enabled) {
        //iscsi_shutdown();
        iscsi_enabled = 0;
    }

    mouse_hook_enabled = 0;
    kb_hook_enabled = 0;

    printf("[MAC68K] Platform shutdown completed.\n");
}

/* Big SE: remap SCSI ($580000) and IWM ($5FF000) from virtual $88xxxx/$8FFxxx.
 * Range must NOT cover $81A000-$81FFFF — those are ROM exclusion zone reads
 * that alias through 24-bit masking and must reach the SE bus as ROM, not SCSI. */
#define BIGSE_SCSI_VIRT  0x880000
#define BIGSE_SCSI_SIZE  0x080000
#define BIGSE_SCSI_PHYS  0x580000

/* Big/Huge SE: video buffer remap — BIGSE_VBUF_SIZE/PHYS defined near the top */

/* SE-bus remap shim (configurable, underneath the core): translate a physical
 * address to its real SE-bus address via the cfg `iomap` windows.
 *   bus = bus_base + (phys - lo)   for phys in [lo, hi)
 * No window matches → identity pass-through (ordinary SE needs no windows).
 * This is an explicit affine remap, NOT a width mask — so a 68020 PiStorm
 * with 32-bit bus targets is just a different window table, no special path.
 *   ordinary SE : (no windows)                     identity
 *   bigSE       : iomap 0x880000  0x900000  0x580000
 *   hugeSE      : iomap 0x40000000 0x41000000 0x0
 */
static inline uint32_t se_bus_addr(struct emulator_config *cfg, uint32_t phys) {
    for (unsigned int i = 0; i < cfg->io_remap_count; i++) {
        if (phys >= cfg->io_remap_lo[i] && phys < cfg->io_remap_hi[i])
            return (uint32_t)(cfg->io_remap_bus[i] + (phys - cfg->io_remap_lo[i]));
    }
    return phys;  /* identity — no remap configured for this range */
}

/* Sliding-window matcher over the SCSI data-register byte stream.  Recognizes
 * the uncompressed 'gusd' resource by its 14-byte prefix (header + machine-4
 * pair + machine-5 number) and rewrites the two method low-bytes that the
 * 2026-06-18 patch changed:  machine-5 method @+$0F  $05 -> $02,
 *                            machine-9 method @+$1F  $05 -> $03.
 * Only the low byte differs ($0005 vs $0002/$0003); the high byte is $00 either
 * way, so we touch nothing else.  Byte-at-a-time (5380 data reg is 8-bit). */
static unsigned char gusd_filter_byte(unsigned char b) {
    static const unsigned char A[14] =
        {0x00,0x01,0xAE,0x5B,0x5E,0x75,0x00,0x6D,0x00,0x04,0x00,0x01,0x00,0x05};
    static int m = 0;      /* anchor bytes matched so far */
    static int post = -1;  /* >=0: byte index past a completed anchor */
    if (post >= 0) {
        unsigned char out = b;
        if (post == 1 && b == 0x05) { out = 0x02; printf("[GUSD] machine5 method $05->$02\n"); }
        else if (post == 17) {
            if (b == 0x05) { out = 0x03; printf("[GUSD] machine9 method $05->$03\n"); }
            post = -1; m = 0; return out;   /* last patched byte — done */
        }
        if (++post > 17) { post = -1; m = 0; }
        return out;
    }
    if (b == A[m]) {
        if (++m == (int)sizeof(A)) { m = 0; post = 0; }
    } else {
        m = (b == A[0]) ? 1 : 0;
    }
    return b;
}

/* Passive 'gusd' watcher (hugeSE diagnostics): same anchor as gusd_filter_byte
 * but log-only — reports whether the methods streaming off SCSI are the
 * 2026-06-18 patched values ($0005) or stock ($0002/$0003). */
static void gusd_watch_byte(unsigned char b) {
    static const unsigned char A[14] =
        {0x00,0x01,0xAE,0x5B,0x5E,0x75,0x00,0x6D,0x00,0x04,0x00,0x01,0x00,0x05};
    static int m = 0, post = -1;
    if (post >= 0) {
        if (post == 1)  printf("[GUSD-SEEN] machine5 method low-byte=$%02X (%s)\n", b, b==0x05?"PATCHED 32-bit":"stock/unpatched");
        if (post == 17) { printf("[GUSD-SEEN] machine9 method low-byte=$%02X (%s)\n", b, b==0x05?"PATCHED 32-bit":"stock/unpatched"); post=-1; m=0; return; }
        if (++post > 17) { post = -1; m = 0; }
        return;
    }
    if (b == A[m]) { if (++m == (int)sizeof(A)) { m = 0; post = 0; } }
    else m = (b == A[0]) ? 1 : 0;
}

#include <time.h>
/* wall-clock microseconds since the first SCSI bus access — for the [GPIO-*]
 * timing capture: measure inter-access gaps and spot interrupt-handler stalls
 * (a big jump between two consecutive 5380 cycles = an IRQ ran mid-selection). */
static double gpio_ts_us(void) {
    static struct timespec t0; static int set = 0;
    struct timespec tn; clock_gettime(CLOCK_MONOTONIC, &tn);
    if (!set) { t0 = tn; set = 1; }
    return (double)(tn.tv_sec - t0.tv_sec) * 1e6 + (double)(tn.tv_nsec - t0.tv_nsec) / 1e3;
}

int custom_read_mac68k(struct emulator_config *cfg, unsigned int addr,
                       unsigned int *val, unsigned char type) {
    /* SE-bus I/O: if a configured `iomap` window remaps this physical address,
     * it's real hardware — read it from the SE bus.  ROM and RAM are served by
     * the fast-path ranges before we ever get here, so a window hit is genuine
     * I/O (e.g. hugeSE $405FF010 → SE bus $5FF010; bigSE $88xxxx → $58xxxx). */
    uint32_t bus = se_bus_addr(cfg, addr);
    if (bus != addr) {
        /* SE-bus I/O ports are byte-wired: a real 68030 splits a word/long
         * access to an 8-bit port into N sequential single-byte cycles at
         * consecutive addresses (dynamic bus sizing), each asserting one byte
         * strobe (UDS for even A0, LDS for odd).  The BBU pseudo-DMA pump counts
         * exactly those per-byte strobes, so we MUST emit one ps_read_8 per byte
         * ascending — a single ps_read_8, or a ps_read_16 word cycle, would
         * under-strobe the pump and desync the transfer (→ $0035 catalog garbage
         * on huge-se; big-se survives only because 24-bit masking discards the
         * mis-pumped high byte). */
        int nbytes = (type == 2) ? 4 : (type == 1) ? 2 : 1;
        unsigned in_scsi = (bus >= 0x5FF000 && bus < 0x600000);
        unsigned r = (bus >> 4) & 7;
        int do_gusd = fake_gusd_enabled && in_scsi && (r == 0 || r == 6);
        uint32_t v = 0;
        for (int i = 0; i < nbytes; i++) {
            unsigned char b = (unsigned char)ps_read_8(bus + i);
            { extern uint32_t ovl_sysrom_pos;
              if (ovl_sysrom_pos >= 0x800000 && (bus + i) >= 0x580000 && (bus + i) < 0x600000) {
                /* SKIP the boring "waiting for BSY" poll reads ($5FF040==$03) so the
                 * capture spans MANY selection attempts and shows the moment BSY (or a
                 * phase change) actually appears — i.e. the SUCCESSFUL selection.
                 * Range widened to $580000 (was $5FF000) to capture bigSE's SCSI at
                 * bus $58xxxx AND the BBU pseudo-DMA region, for the bigSE-vs-hugeSE diff. */
                int _boring = (((bus + i) & 0xFFF0FF) == 0x580040 && b == 0x03) ||
                              ((bus + i) == 0x5FF040 && b == 0x03);
                static int _gr = 0;
                if (!_boring && _gr++ < 400) printf("[GPIO-RD] t=%10.1fus bus=$%06X val=$%02X  type=%d nb=%d i=%d  cpuaddr=$%08X PC=$%08X\n",
                                        gpio_ts_us(), (bus + i), b, (int)type, nbytes, i, addr, m68k_get_reg(NULL, M68K_REG_PC)); } }
            if (do_gusd) b = gusd_filter_byte(b);   /* runtime 'gusd' revert (big-se) */
            else if (in_scsi && (r == 0 || r == 6)) gusd_watch_byte(b);  /* hugeSE: log-only */
            v = (v << 8) | b;
        }
        *val = v;
        (void)in_scsi;  /* [PUMP] data-pump health probe removed — it ran on every SCSI byte (major slowdown) */
        return 1;
    }
    return -1;  /* not remapped I/O → fall through to mapped buffers (ROM/RAM) */
}

int custom_write_mac68k(struct emulator_config *cfg, unsigned int addr,
                        unsigned int val, unsigned char type) {
    if (cfg) {}
    /* SE-bus I/O write via the configured `iomap` window (same shim as the read). */
    uint32_t bus = se_bus_addr(cfg, addr);
    if (bus != addr) {
        /* Byte-wired SE-bus I/O: split word/long writes into N sequential
         * single-byte cycles (ascending addresses) so the BBU pump sees the
         * right per-byte strobes — mirror of the read path above. */
        int nbytes = (type == 2) ? 4 : (type == 1) ? 2 : 1;
        for (int i = 0; i < nbytes; i++) {
            uint8_t _b = (val >> (8 * (nbytes - 1 - i))) & 0xFF;
            { extern uint32_t ovl_sysrom_pos;
              if (ovl_sysrom_pos >= 0x800000 && (bus + i) >= 0x580000 && (bus + i) < 0x600000) {
                static int _gw = 0;
                if (_gw++ < 300) printf("[GPIO-WR] t=%10.1fus bus=$%06X val=$%02X  type=%d nb=%d i=%d  cpuaddr=$%08X PC=$%08X\n",
                                        gpio_ts_us(), (bus + i), _b, (int)type, nbytes, i, addr, m68k_get_reg(NULL, M68K_REG_PC)); } }
            ps_write_8(bus + i, _b);
        }
        return 1;
    }
    if (ovl_sysrom_pos >= 0x800000) {
        /* Video/sound buffer write-through: the WTC region already wrote the Pi
         * RAM buffer; mirror it to the physical SE bus for the BBU/CRT (display/
         * sound sit at the top of the 32 MB → SE bus $3F0000). */
        if (addr >= bigse_vbuf_virt && addr < bigse_vbuf_virt + BIGSE_VBUF_SIZE) {
            uint32_t phys = addr - bigse_vbuf_virt + BIGSE_VBUF_PHYS;
            if (type >= 2) { ps_write_16(phys, val >> 16); ps_write_16(phys + 2, val & 0xFFFF); }
            else if (type == 1) ps_write_16(phys, val);
            else ps_write_8(phys, val);
            return 1;
        }
        /* Swallow stray heap writes that land on the physical video/sound buffer. */
        if (addr >= BIGSE_VBUF_PHYS && addr < BIGSE_VBUF_PHYS + BIGSE_VBUF_SIZE)
            return 1;
        /* ROM-write-to-buffer (bigSE): the 24-bit SE-ROM Memory Manager does
         * CLR.L to ROM addresses and expects the read to reflect it (the BBU
         * aliases ROM writes to RAM).  Apply to the ROM buffer.  bigSE only:
         * hugeSE ROM writes ($40800000) hit the iomap window above first. */
        {
            uint32_t rom_masked = ovl_sysrom_pos & 0x00FFFFFF;
            if (addr >= rom_masked && addr < rom_masked + 0x80000) {
                int32_t ri = get_named_mapped_item(cfg, "sysrom");
                if (ri >= 0 && cfg->map_data[ri]) {
                    uint32_t off = addr - rom_masked;
                    unsigned char *rom_data = cfg->map_data[ri];
                    if (type == 0) {
                        rom_data[off] = val & 0xFF;
                    } else if (type == 1) {
                        rom_data[off]   = (val >> 8) & 0xFF;
                        rom_data[off+1] = val & 0xFF;
                    } else {
                        rom_data[off]   = (val >> 24) & 0xFF;
                        rom_data[off+1] = (val >> 16) & 0xFF;
                        rom_data[off+2] = (val >> 8)  & 0xFF;
                        rom_data[off+3] = val & 0xFF;
                    }
                    return 1;
                }
            }
        }
    }
    return -1;
}

void create_platform_mac68k(struct platform_config *cfg, char *subsys) {
    cfg->register_read = NULL;
    cfg->register_write = NULL;
    cfg->custom_read = custom_read_mac68k;
    cfg->custom_write = custom_write_mac68k;
    cfg->platform_initial_setup = setup_platform_mac68k;
    cfg->handle_reset = handle_reset_mac68k;
    cfg->shutdown = shutdown_platform_mac68k;

    cfg->setvar = setvar_mac68k;
    cfg->id = PLATFORM_MAC;

    if (subsys) {
        cfg->subsys = malloc(strlen(subsys) + 1);
        strcpy(cfg->subsys, subsys);
        for (unsigned int i = 0; i < strlen(cfg->subsys); i++) {
            cfg->subsys[i] = tolower(cfg->subsys[i]);
        }
    }
}
