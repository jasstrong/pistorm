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

    if (CHKVAR("figment")) {
        extern int figment_enabled;
        figment_enabled = 1;
        printf("[MAC68K] Figment MM trap intercept enabled\n");
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

    index = get_named_mapped_item(cfg, "sysram");
    if (index != -1) {
        /* Remove all RAM ranges: base pointer (from config parser)
         * and tracked pointers (from previous OVL remap) */
        m68k_remove_range(cfg->map_data[index]);
        if (ram_range_ptr && ram_range_ptr != cfg->map_data[index]) {
            m68k_remove_range(ram_range_ptr);
        }
        if (ram_wtc_ptr && ram_wtc_ptr != cfg->map_data[index] && ram_wtc_ptr != ram_range_ptr) {
            m68k_remove_range(ram_wtc_ptr);
        }
        ram_range_ptr = NULL;
        ram_wtc_ptr = NULL;

        if (ovl) {
            /* OVL on: ROM overlays $0 to ovl_decode_size (128KB on Mac SE).
             * RAM above the overlay is still accessible.
             * Keep config range zeroed so handle_mapped_write doesn't
             * suppress write-through GPIO writes. */
            uint32_t ram_start = ovl_decode_size;
            uint32_t ram_end = cfg->map_size[index];

            if (ram_start < ram_end) {
                uint32_t wtc_start = ram_end - WTC_REGION_SIZE;
                /* Huge SE: WTC at $7F0000 during 24-bit boot */
                if (ovl_sysrom_pos >= 0x40000000)
                    wtc_start = 0x7F0000;
                if (wtc_start > ram_start) {
                    ram_range_ptr = cfg->map_data[index] + ram_start;
                    m68k_add_ram_range(ram_start, wtc_start, ram_range_ptr);
                    ram_wtc_ptr = cfg->map_data[index] + wtc_start;
                    m68k_add_ram_range_wtc(wtc_start, wtc_start + WTC_REGION_SIZE, ram_wtc_ptr);
                    printf("[MAC68K] RAM at %08X-%08X fast, %08X-%08X wtc (OVL covers 0-%08X)\n",
                           ram_start, wtc_start, wtc_start, wtc_start + WTC_REGION_SIZE, ovl_decode_size);
                } else {
                    ram_range_ptr = cfg->map_data[index] + ram_start;
                    m68k_add_ram_range_wtc(ram_start, ram_end, ram_range_ptr);
                    printf("[MAC68K] RAM at %08X-%08X wtc (OVL covers 0-%08X)\n",
                           ram_start, ram_end, ovl_decode_size);
                }
            }
            cfg->map_offset[index] = 0;
            cfg->map_high[index] = 0;
        } else {
            /* OVL off: RAM at 0x000000 */
            uint32_t ram_end = cfg->map_size[index];
            uint32_t wtc_start = ram_end - WTC_REGION_SIZE;

            /* Huge SE: WTC at $7F0000 (top of visible 8MB), not $01FF0000
             * (top of 32MB).  PMMU identity-maps everything — no entry 7
             * remap.  MODE32 will move WTC to $01FF0000 via pseudovirt trap. */
            if (ovl_sysrom_pos >= 0x40000000)
                wtc_start = 0x7F0000;

            /* Set video buffer virtual address — 24-bit masked, because
             * custom_write sees addresses after slow-path 24-bit mask. */
            if (ovl_sysrom_pos >= 0x800000)
                bigse_vbuf_virt = wtc_start & 0x00FFFFFF;

            ram_range_ptr = cfg->map_data[index];
            cfg->map_offset[index] = 0x0;
            cfg->map_high[index] = cfg->map_size[index];
            m68k_add_ram_range(0x0, wtc_start, cfg->map_data[index]);
            ram_wtc_ptr = cfg->map_data[index] + wtc_start;
            m68k_add_ram_range_wtc(wtc_start, wtc_start + WTC_REGION_SIZE, ram_wtc_ptr);
            /* RAM above $800000 is not visible to CPU in 24-bit mode
             * (PMMU entries 8+ map to $40xxxxxx I/O/ROM).
             * Do NOT add a fast-path range — it would shadow the ROM alias. */
            printf("[MAC68K] RAM at 00000000-%08X fast, %08X-%08X wtc\n",
                   wtc_start, wtc_start, wtc_start + WTC_REGION_SIZE);
        }
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

/* Big/Huge SE: video buffer remap — see bigse_vbuf_virt declared above */
#define BIGSE_VBUF_SIZE  0x010000
#define BIGSE_VBUF_PHYS  0x3F0000

int custom_read_mac68k(struct emulator_config *cfg, unsigned int addr,
                       unsigned int *val, unsigned char type) {
    if (cfg) {}
    /* SCSI/IWM I/O handler.
     * Big SE:  ROM uses $880000+ → remap to $580000+ on SE bus.
     * Huge SE: ROM uses $40580000+ → slow-path masks to $580000+ → direct.
     * Both cases: phys ends up as $580000-$5FFFFF. */
    /* Huge SE: ROM exclusion zone reads land here after 24-bit masking.
     * $4081Axxx → masked $81Axxx. On big-se, handle_mapped_read catches
     * these (ROM at $800000). On huge-se (ROM at $40800000), they fall
     * through — serve from ROM buffer directly. */
    if (ovl_sysrom_pos >= 0x40000000) {
        uint32_t rom_masked = ovl_sysrom_pos & 0x00FFFFFF;  /* $800000 for $40800000 */
        if (addr >= rom_masked && addr < rom_masked + 0x80000) {
            /* Read from ROM buffer */
            extern struct emulator_config *cfg;
            int32_t rom_idx = get_named_mapped_item(cfg, "sysrom");
            if (rom_idx >= 0) {
                uint32_t off = addr - rom_masked;
                unsigned char *rom_data = cfg->map_data[rom_idx];
                if (type == 0) *val = rom_data[off];
                else if (type == 1) *val = (rom_data[off] << 8) | rom_data[off+1];
                else *val = (rom_data[off] << 24) | (rom_data[off+1] << 16) |
                            (rom_data[off+2] << 8) | rom_data[off+3];
                return 1;
            }
        }
    }

    uint32_t phys = 0;
    if (ovl_sysrom_pos >= 0x800000 &&
        addr >= BIGSE_SCSI_VIRT && addr < BIGSE_SCSI_VIRT + BIGSE_SCSI_SIZE) {
        phys = addr - BIGSE_SCSI_VIRT + BIGSE_SCSI_PHYS;
    } else if (ovl_sysrom_pos >= 0x40000000 &&
               addr >= 0x580000 && addr < 0x600000) {
        phys = addr;
    }
    if (phys) {
        if (phys >= 0x5FF000) {
            /* IWM needs paced IO — BBU state machine must advance
             * between accesses for the handshake to complete. */
            static uint32_t iwm_poll_count = 0;
            static uint32_t iwm_last_addr = 0;
            static uint64_t iwm_total_reads = 0;
            iwm_total_reads++;
            { static uint32_t iwm_rd = 0;
              if (++iwm_rd <= 10)
                printf("[IWM-RD] addr=$%06X val=$%02X #%u\n", phys, *val, iwm_rd);
            }
            *val = ps_read_8_paced(phys);
        } else {
            *val = ps_read_8(phys);
        }
        (void)type;
        return 1;
    }
    return -1;
}

int custom_write_mac68k(struct emulator_config *cfg, unsigned int addr,
                        unsigned int val, unsigned char type) {
    if (cfg) {}
    if (ovl_sysrom_pos >= 0x800000) {
        /* SCSI + IWM write handler (same logic as custom_read) */
        uint32_t phys = 0;
        if (addr >= BIGSE_SCSI_VIRT && addr < BIGSE_SCSI_VIRT + BIGSE_SCSI_SIZE)
            phys = addr - BIGSE_SCSI_VIRT + BIGSE_SCSI_PHYS;
        else if (ovl_sysrom_pos >= 0x40000000 && addr >= 0x580000 && addr < 0x600000)
            phys = addr;
        if (phys) {
            if (phys >= 0x5FF000) {
                static uint32_t iwm_wr = 0;
                if (++iwm_wr <= 10)
                    printf("[IWM-WR] addr=$%06X val=$%02X #%u PC=$%08X\n", phys, val & 0xFF, iwm_wr, m68k_get_reg(NULL, M68K_REG_PC));
                ps_write_8_paced(phys, val);
            } else
                ps_write_8(phys, val);
            (void)type;
            return 1;
        }
        /* Video buffer remap — WTC already wrote to Pi RAM buffer;
         * now send the write to the physical SE bus for the BBU/CRT. */
        if (addr >= bigse_vbuf_virt && addr < bigse_vbuf_virt + BIGSE_VBUF_SIZE) {
            uint32_t phys = addr - bigse_vbuf_virt + BIGSE_VBUF_PHYS;
            /* type: 0=byte, 1=word, 2=longword (enum map_op_types) */
            if (type >= 2) {  /* longword */
                ps_write_16(phys, val >> 16);
                ps_write_16(phys + 2, val & 0xFFFF);
            } else if (type == 1) {  /* word */
                ps_write_16(phys, val);
            } else {
                ps_write_8(phys, val);
            }
            return 1;
        }
        /* Block heap writes to physical video/sound buffer range.
         * The Mac thinks $3F0000-$3FFFFF is regular heap (8MB space),
         * but the BBU reads it for video DMA.  Video writes come
         * through the $7F0000 path above; everything else is heap
         * data that must NOT reach the physical display buffer. */
        if (addr >= BIGSE_VBUF_PHYS && addr < BIGSE_VBUF_PHYS + BIGSE_VBUF_SIZE) {
            return 1;  /* swallow — WTC buffer has it, don't corrupt CRT */
        }
        /* ROM write handler: apply writes to the ROM buffer.
         * ROM is read-only in the fast-path, but the SE ROM's MM code
         * does CLR.L to ROM addresses and expects reads to reflect the
         * change.  On real hardware, the BBU aliases these to RAM. */
        {
            uint32_t rom_masked = ovl_sysrom_pos & 0x00FFFFFF;
            if (addr >= rom_masked && addr < rom_masked + 0x80000) {
                extern struct emulator_config *cfg;
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
