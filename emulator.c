// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "emulator.h"
#include "platforms/platforms.h"
#include "input/input.h"
#include "m68kcpu.h"

#include "gpio/ps_protocol.h"
#include "vnc/vnc.h"

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "m68kops.h"

/* #define DEBUG_MAC_IO */  /* Uncomment for per-access SCSI/VIA/IO debug spam */
/* #define DEBUG_DIAG */   /* Uncomment (or -DDEBUG_DIAG) for diagnostic counters & ring buffers */
#define KEY_POLL_INTERVAL_MSEC 5000

unsigned int ovl;
extern uint32_t ovl_sysrom_pos;

int kb_hook_enabled = 0;
int mouse_hook_enabled = 0;
int cpu_emulation_running = 1;
uint8_t mouse_dx = 0, mouse_dy = 0;
uint8_t mouse_buttons = 0;
uint8_t mouse_extra = 0;

extern volatile unsigned int *gpio;
extern volatile uint16_t srdata;
uint8_t realtime_graphics_debug = 0, emulator_exiting = 0;
extern uint32_t scsi_rom_low, scsi_rom_high;
extern uint8_t noscsi_enabled;
extern struct vnc_config vnc_cfg;
uint8_t realtime_disassembly, int2_enabled = 0;
uint32_t do_disasm = 0, old_level;
uint32_t last_irq = 0, last_last_irq = 0;

uint8_t ipl_enabled[8];

uint8_t end_signal = 0, load_new_config = 0;

char disasm_buf[4096];

/* PC trace: write every instruction's PC to a binary file for diffing.
 * Enable with PC_TRACE=<filename> env var.  Optional PC_TRACE_LIMIT=N
 * (default 10M instructions = 40MB). */
static uint32_t *pc_trace_buf = NULL;
static uint32_t pc_trace_count = 0;
static uint32_t pc_trace_limit = 0;
static const char *pc_trace_path = NULL;

static void pc_trace_flush(void) {
    if (!pc_trace_buf || !pc_trace_count) return;
    FILE *fp = fopen(pc_trace_path, "wb");
    if (fp) {
        fwrite(pc_trace_buf, 4, pc_trace_count, fp);
        fclose(fp);
        printf("[PC-TRACE] Wrote %u PCs to %s (%u MB)\n",
               pc_trace_count, pc_trace_path,
               (pc_trace_count * 4) >> 20);
    }
    free(pc_trace_buf);
    pc_trace_buf = NULL;
}

int mem_fd, mouse_fd = -1, keyboard_fd = -1;
int mem_fd_gpclk;
atomic_int irq = 0;

#ifdef DEBUG_DIAG
// Diagnostic counters for interrupt debugging
static atomic_uint dbg_ipl_assert = 0;
static unsigned int dbg_cpu_irq = 0;
static unsigned int dbg_cpu_deassert = 0;
static unsigned int dbg_irq_ack_count = 0;
#endif

// PC/address watchpoints
#ifdef DEBUG_MAC_IO
static unsigned int watch_jiodone = 0;  // writes to ioResult ($1FFC10)
#endif
// static int trace_writes_left = 0;  // post-DskErr write trace counter (disabled)

#ifdef DEBUG_DIAG
// VIA IFR bit counters — tracks which interrupt sources the ISR sees
static unsigned int via_ifr_bits[8] = {0};  // count per bit
static unsigned int via_ifr_reads = 0;
// VIA IER snapshot — last value read from IER
static unsigned int via_ier_last = 0;
static unsigned int via_ier_reads = 0;
// VIA IER write tracking — catch transient T1/T2 enable
static unsigned int via_ier_writes = 0;
static unsigned int via_ier_t1_enabled = 0;
static unsigned int via_ier_t2_enabled = 0;
static unsigned int via_t1cl_writes = 0;
static unsigned int via_t1ch_writes = 0;
// VIA register read ACK counters — track which IFR sources are actually cleared
static unsigned int via_t2cl_reads = 0;   // T2C-L read at $EFF1FE (clears T2 IFR)
static unsigned int via_ira_reads = 0;    // IRA read at $EFE3FE (clears CA1 IFR)
static unsigned int via_orb_reads = 0;    // ORB read at $EFE1FE (clears CB1/CB2 IFR)
static unsigned int via_sr_reads = 0;     // SR read at $EFF5FE (clears SR IFR)
// VIA IFR write tracking — handlers clear IFR by writing (not reading IRA)
static unsigned int via_ifr_writes = 0;   // total writes to IFR ($EFFBFE)
static unsigned int via_ifr_w_ca2 = 0;    // writes with bit 0 set (clear CA2)
static unsigned int via_ifr_w_ca1 = 0;    // writes with bit 1 set (clear CA1)
// m68k_read_memory_8 address counters
static unsigned int rd8_total = 0;
static unsigned int rd8_via = 0;
static unsigned int rd8_scsi = 0;
static unsigned int wr8_scsi = 0;
static unsigned int rd8_iwm = 0;
static unsigned int rd8_hi = 0;
#endif

#ifdef DEBUG_DIAG
// SCSI read/write log — ring buffer captures most recent N accesses
#define SCSI_LOG_SIZE 65536
struct scsi_log_entry { uint32_t addr; uint8_t val; uint32_t pc; uint8_t is_write; };
struct scsi_log_entry scsi_log_buf[SCSI_LOG_SIZE];
unsigned int scsi_log_head = 0;   // next write position (wraps)
unsigned int scsi_log_total = 0;  // total SCSI accesses logged
// Suppress repetitive CSBS polling — only log when value changes
static uint8_t scsi_csbs_last = 0xFF;    // last CSCS value (init to impossible)
unsigned int scsi_csbs_repeat = 0; // count of suppressed repeats

// CSBS transition tracker — captures phase transitions after DMA setup
#define CSBS_TRANS_MAX 64
struct csbs_transition {
  uint8_t csbs;       // new CSBS value
  uint8_t bsr;        // BSR at transition time
  uint8_t mr;         // MR at transition time
  uint32_t poll_count; // polls since last transition
};
static struct csbs_transition csbs_trans[CSBS_TRANS_MAX];
static unsigned int csbs_trans_count = 0;
static unsigned int csbs_trans_polls = 0;  // polls since last transition
static int csbs_tracking_active = 0;       // armed by DMA start write

void dump_scsi_log(const char *label) {
  if (scsi_log_total == 0) return;
  unsigned int n = (scsi_log_head < SCSI_LOG_SIZE) ? scsi_log_head : SCSI_LOG_SIZE;
  unsigned int start = (scsi_log_head < SCSI_LOG_SIZE) ? 0 : (scsi_log_head - SCSI_LOG_SIZE);
  FILE *f = fopen("/tmp/scsi_crash.log", "w");
  if (f) {
    fprintf(f, "[SCSI-LOG@%s] total=%u showing last %u\n", label, scsi_log_total, n);
    for (unsigned int i = 0; i < n; i++) {
      unsigned int si = (start + i) % SCSI_LOG_SIZE;
      fprintf(f, "%c8 %08X %02X %08X %u\n",
             scsi_log_buf[si].is_write ? 'W' : 'R',
             scsi_log_buf[si].addr, scsi_log_buf[si].val,
             scsi_log_buf[si].pc, start + i);
    }
    fclose(f);
    printf("[SCSI-LOG@%s] total=%u, %u entries written to /tmp/scsi_crash.log\n",
           label, scsi_log_total, n);
  }
}
#else
void dump_scsi_log(const char *label) { (void)label; }
#endif

#ifdef DEBUG_DIAG
// Direct memory read from wtcram/ROM buffer — safe to call from inside m68k callbacks
// (unlike m68k_read_memory_XX which is NOT re-entrant)
// NOTE: get_mapped_data_pointer_by_address excludes MAPTYPE_RAM_WTC, so we
// search cfg->map_data[] ourselves, accepting RAM, RAM_WTC, RAM_NOALLOC, and ROM.
static uint8_t *direct_get_ptr(uint32_t address) {
  extern struct emulator_config *cfg;
  if (!cfg) return NULL;
  address &= 0xFFFFFF;
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE || !cfg->map_data[i]) continue;
    if (address >= cfg->map_offset[i] && address < cfg->map_high[i]) {
      if (cfg->map_type[i] == MAPTYPE_RAM || cfg->map_type[i] == MAPTYPE_RAM_WTC ||
          cfg->map_type[i] == MAPTYPE_RAM_NOALLOC || cfg->map_type[i] == MAPTYPE_ROM)
        return cfg->map_data[i] + (address - cfg->map_offset[i]);
    }
  }
  return NULL;
}
static uint16_t direct_read_16(uint32_t address) {
  uint8_t *p = direct_get_ptr(address);
  if (p) return (p[0] << 8) | p[1];
  return 0xDEAD;
}
static uint32_t direct_read_32(uint32_t address) {
  uint8_t *p = direct_get_ptr(address);
  if (p) return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  return 0xDEADDEAD;
}

// IWM access log — ring buffer captures most recent N accesses (reads + writes)
#define IWM_LOG_SIZE 256
static struct { uint32_t addr; uint8_t val; uint32_t pc; uint32_t caller; uint8_t is_write; } iwm_log_buf[IWM_LOG_SIZE];
static unsigned int iwm_log_head = 0;   // next write position (wraps)
static unsigned int iwm_log_total = 0;  // total IWM accesses logged

// IWM mode tracking — persistent Q6/Q7 state + per-mode counters
// IWM register mapping: reg12=Q6off, reg13=Q6on, reg14=Q7off, reg15=Q7on
// Modes: Q6=0,Q7=0 → data; Q6=1,Q7=0 → status; Q6=0,Q7=1 → handshake; Q6=1,Q7=1 → write
static int iwm_q6 = 0, iwm_q7 = 0;
// Phase line state tracking (set/cleared by accessing phase registers)
static int iwm_ph[4] = {0};  // ph0-ph3 current state
static unsigned int iwm_mode_rd[4] = {0};  // [q7*2+q6] read counts
static unsigned int iwm_hshk_ready = 0;    // handshake reads with bit7=1 (byte ready)
static unsigned int iwm_hshk_notready = 0; // handshake reads with bit7=0
static unsigned int iwm_data_zero = 0;     // data reg reads returning $00
static unsigned int iwm_data_nonzero = 0;  // data reg reads returning non-$00
static unsigned int iwm_data_ff = 0;       // data reg reads returning $FF (no data)
static unsigned int iwm_data_hi = 0;       // data reg reads with bit7=1 (valid GCR byte)
static unsigned int iwm_data_lo = 0;       // data reg reads with bit7=0 (no valid byte yet)

// IWM sense line names indexed by {ph3,ph2,ph1,ph0}
static const char *iwm_sense_names[16] = {
  "DIRTN","CSTIN","STEP","WRPRT","MOTORON","TK0","EJECT","TACH",
  "RDDATA0","RDDATA1","?A","?B","SIDES","?D","?E","DRVIN"
};

static void iwm_update_state(unsigned int reg) {
  // Phase lines: reg0=ph0L, reg1=ph0H, reg2=ph1L, ..., reg7=ph3H
  if (reg < 8) {
    iwm_ph[reg / 2] = reg & 1;
  }
  // Q6/Q7: reg12=Q6L, reg13=Q6H, reg14=Q7L, reg15=Q7H
  if (reg == 12) iwm_q6 = 0;
  else if (reg == 13) iwm_q6 = 1;
  else if (reg == 14) iwm_q7 = 0;
  else if (reg == 15) iwm_q7 = 1;
}

static void iwm_log_access(unsigned int address, unsigned int val, int is_write) {
  static const char *iwm_reg_names[] = {
    "ph0L","ph0H","ph1L","ph1H","ph2L","ph2H","ph3L","ph3H",
    "mtrOff","mtrOn","intDrv","extDrv","Q6L","Q6H","Q7L","Q7H"
  };

  uint32_t iwm_pc = m68k_get_reg(NULL, M68K_REG_PC);
  unsigned int iwm_reg = (address - 0xDFE1FF) >> 9;

  // One-shot dump of RAM code when PC is in RAM (< $400000)
  // The code may be temporary (loaded then freed), so capture it while executing
  {
    static int ram_code_dumped = 0;
    if (!ram_code_dumped && (iwm_pc & 0xFFFFFF) < 0x400000 && (iwm_pc & 0xFFFFFF) >= 0x1000) {
      ram_code_dumped = 1;
      uint32_t pc24 = iwm_pc & 0xFFFFFF;
      uint32_t dump_start = (pc24 > 0x80) ? (pc24 - 0x80) : 0;
      uint32_t dump_end = pc24 + 0x80;
      printf("[IWM] *** RAM CODE DUMP (PC=%06X) ***\n", pc24);
      for (uint32_t a = dump_start; a < dump_end; a += 16) {
        printf("[IWM] %06X:", a);
        for (int j = 0; j < 16; j += 2)
          printf(" %04X", direct_read_16(a + j));
        printf("\n");
      }
      // Also dump key low-memory vectors while we're in a valid context
      printf("[IWM] LowMem: SonyPatch($B40)=%08X IWMBase($1E0)=%08X SonyVars($134)=%08X\n",
             direct_read_32(0xB40), direct_read_32(0x1E0), direct_read_32(0x134));
      printf("[IWM] *** END RAM CODE DUMP ***\n");
    }
  }

  // Update IWM state BEFORE logging (so logged state reflects this access)
  if (iwm_reg < 16) iwm_update_state(iwm_reg);

  unsigned int sense_sel = (iwm_ph[3] << 3) | (iwm_ph[2] << 2) | (iwm_ph[1] << 1) | iwm_ph[0];

  // Realtime log: only print on significant state transitions, not every access
  {
    static int last_mode = -1;  // q7*2+q6
    static unsigned int suppress_count = 0;
    static unsigned int last_summary_total = 0;
    int mode = iwm_q7 * 2 + iwm_q6;

    // Always print mode transitions (data→status→handshake→write)
    int mode_changed = (mode != last_mode);
    // Print phase/sense changes (drive polling state transitions)
    static unsigned int last_sense_log = ~0u;
    int sense_changed = (sense_sel != last_sense_log);
    // Print first few data reads to see what values come back
    static unsigned int data_rd_count = 0;
    int is_data_rd = (!is_write && mode == 0);  // Q6=0, Q7=0 = data register
    if (is_data_rd) data_rd_count++;
    int show_data = (is_data_rd && data_rd_count <= 20);

    if (mode_changed || sense_changed || show_data) {
      if (suppress_count > 0) {
        printf("[IWM]   ... %u accesses suppressed\n", suppress_count);
        suppress_count = 0;
      }
      printf("[IWM] #%u %s reg%u(%s) val=%02X ph=%d%d%d%d(%s) Q6=%d Q7=%d pc=%08X",
             iwm_log_total, is_write ? "WR" : "RD",
             iwm_reg, (iwm_reg < 16) ? iwm_reg_names[iwm_reg] : "?",
             val & 0xFF,
             iwm_ph[3], iwm_ph[2], iwm_ph[1], iwm_ph[0],
             iwm_sense_names[sense_sel],
             iwm_q6, iwm_q7, iwm_pc);
      printf("\n");
      last_mode = mode;
      last_sense_log = sense_sel;
    } else {
      suppress_count++;
    }

    // Periodic summary every 5000 accesses
    if (iwm_log_total - last_summary_total >= 5000) {
      last_summary_total = iwm_log_total;
      printf("[IWM] === SUMMARY at #%u: data_rd=%u(hi=%u lo=%u zero=%u ff=%u) hshk=%u(rdy=%u nrdy=%u) status=%u ===\n",
             iwm_log_total, iwm_mode_rd[0], iwm_data_hi, iwm_data_lo,
             iwm_data_zero, iwm_data_ff,
             iwm_mode_rd[2], iwm_hshk_ready, iwm_hshk_notready,
             iwm_mode_rd[1]);
    }
  }

  unsigned int idx = iwm_log_head % IWM_LOG_SIZE;
  iwm_log_buf[idx].addr = address;
  iwm_log_buf[idx].val = val;
  iwm_log_buf[idx].pc = iwm_pc;
  iwm_log_buf[idx].caller = 0;
  // For status register reads (reg14=Q7L with Q6 set), capture SP and stack
  if (iwm_reg == 14 && iwm_q6 == 1 && !is_write) {
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
    // Store SP in caller field, stack words in a separate small dump
    iwm_log_buf[idx].caller = sp;
  }
  iwm_log_buf[idx].is_write = is_write;
  iwm_log_head++;
  iwm_log_total++;

  // Count reads by current mode (only for reads)
  if (!is_write) {
    int mode = iwm_q7 * 2 + iwm_q6;
    iwm_mode_rd[mode]++;
    if (mode == 0) { // data register (Q6=0, Q7=0)
      if (val & 0x80) iwm_data_hi++;
      else iwm_data_lo++;
      if (val == 0x00) iwm_data_zero++;
      else if (val == 0xFF) iwm_data_ff++;
      else iwm_data_nonzero++;
    } else if (mode == 2) { // handshake register (Q6=0, Q7=1)
      if (val & 0x80) iwm_hshk_ready++;
      else iwm_hshk_notready++;
    }
  }
}
#endif

// Slow IO regions — addresses that need bus cycle delays to match real 68000 timing.
// Populated from MAPTYPE_SLOWIO entries in the config file.
#define MAX_SLOWIO_REGIONS 8
#define SLOWIO_DEFAULT_DELAY 150
static struct { uint32_t lo; uint32_t hi; int delay; } slowio[MAX_SLOWIO_REGIONS];
static int slowio_count = 0;

static inline int slowio_get_delay(uint32_t addr) {
  for (int i = 0; i < slowio_count; i++) {
    if (addr >= slowio[i].lo && addr < slowio[i].hi)
      return slowio[i].delay;
  }
  return 0;
}

// Populated from MAPTYPE_PACEDIO entries in the config file.
// Paced IO stretches the bus cycle itself (not inter-cycle delay like slowio).
#define MAX_PACEDIO_REGIONS 8
static struct { uint32_t lo; uint32_t hi; int extra_cycles; } pacedio[MAX_PACEDIO_REGIONS];
static int pacedio_count = 0;

// Returns 0 if not paced, or (1 + extra_cycles) if paced.
// extra_cycles comes from the "delay" config parameter on pacedio mappings.
static inline int is_pacedio(uint32_t addr) {
  for (int i = 0; i < pacedio_count; i++) {
    if (addr >= pacedio[i].lo && addr < pacedio[i].hi)
      return 1 + pacedio[i].extra_cycles;
  }
  return 0;
}

#ifdef DEBUG_DIAG
static unsigned int pacedio_hit_count = 0;
#endif

// Busy-wait to match real 68000 bus cycle timing.
// A real 68000 at 7.83MHz: tst.b d(An) = 8 cycles = ~1μs.
// But between sense line selection and status read, the ROM executes
// several instructions (~3-4μs total). Use a generous delay to ensure
// the IWM has fully settled before each bus access.
// PiStorm GPIO cycles are ~200ns — 5-20x too fast for slow peripherals.
static unsigned int slowio_hit_count = 0;
static inline void slowio_delay(int iterations) {
  slowio_hit_count++;
  for (volatile int dly = 0; dly < iterations; dly++) ;
}

#define MUSASHI_HAX

#ifdef MUSASHI_HAX
#include "m68kcpu.h"
extern m68ki_cpu_core m68ki_cpu;
extern volatile int m68ki_initial_cycles;
extern volatile int m68ki_remaining_cycles;

#define M68K_SET_IRQ(i) old_level = CPU_INT_LEVEL; \
	CPU_INT_LEVEL = (i << 8); \
	if(old_level != 0x0700 && CPU_INT_LEVEL == 0x0700) \
		m68ki_cpu.nmi_pending = TRUE;
#define M68K_END_TIMESLICE 	m68ki_initial_cycles = GET_CYCLES(); \
	SET_CYCLES(0);
#else
#define M68K_SET_IRQ m68k_set_irq
#define M68K_END_TIMESLICE m68k_end_timeslice()
#endif

#define NOP asm("nop"); asm("nop"); asm("nop"); asm("nop");

#define DEBUG_EMULATOR
#ifdef DEBUG_EMULATOR
#define DEBUG printf
#else
#define DEBUG(...)
#endif

// Configurable emulator options
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 300, irq_status = 0;
struct emulator_config *cfg = NULL;
char keyboard_file[256] = "/dev/input/event1";

// Gestalt trap ($A1AD) intercept for 68030/68040 emulation.
// Returns 1 if handled (caller should skip the A-line exception), 0 otherwise.
int gestalt_trap_intercept(void) {
  if (!cfg || cfg->platform->id != PLATFORM_MAC)
    return 0;

  uint32_t selector = m68k_get_reg(NULL, M68K_REG_D0);
  uint32_t result;

  int is_040 = (cpu_type == M68K_CPU_TYPE_68040 || cpu_type == M68K_CPU_TYPE_68040_24);
  int is_030 = (cpu_type == M68K_CPU_TYPE_68030 || cpu_type == M68K_CPU_TYPE_68030_24);
  int is_big_se = (ovl_sysrom_pos >= 0x800000);  /* Big SE: ROM at $800000 */

  if (selector == 0x70726F63) {  // 'proc'
    if (is_040) result = 5;
    else if (is_030) result = 4;
    else return 0;
  } else if (selector == 0x66707520) {  // 'fpu '
    if (is_040 || is_030) result = 3;
    else return 0;
  } else if (selector == 0x6D6D7520) {  // 'mmu '
    if (is_040) result = 3;
    else if (is_030) result = 2;
    else return 0;
  } else if (selector == 0x61646472) {  // 'addr' (gestaltAddressingModeAttr)
    if (cpu_type == M68K_CPU_TYPE_68030) {
      extern int mode32_active;
      result = mode32_active ? 0x07 : 0x04;
    } else if (is_030)
      result = 0x00;  /* 24-bit mode */
    else return 0;
  } else if (selector == 0x63707574) {  // 'cput' (gestaltNativeCPUtype)
    if (is_040) result = 0x104;
    else if (is_030) result = 0x103;
    else return 0;
  } else if (selector == 0x6D616368) {  // 'mach' (gestaltMachineType)
    if (is_big_se) result = 6;          // gestaltMacII — ROM at $800000 in 24-bit
    else if (is_040) result = 22;       // gestaltQuadra700
    else if (is_030) result = 9;        // gestaltMacSE030
    else return 0;
  } else {
    return 0;  // not handled — let the trap dispatcher run
  }
  m68k_set_reg(NULL, M68K_REG_D0, 0);       // noErr
  m68k_set_reg(NULL, M68K_REG_A0, result);
  return 1;
}

int mode32_active = 0;
int mode32_enabled = 0;  /* set by config: setvar mode32 1 */
volatile int mode32_trigger = 0;  /* set by SIGUSR1 */
int figment_enabled = 0;  /* set when Figment is embedded in ROM */

/* Figment trap table: maps OS trap number → binary offset of fig_XXX glue.
 * Auto-generated from figment.elf symbols. */
#define FIGMENT_ROM_OFF 0x40000  /* in the mirror half of 512K ROM */
#include "huge-se/figment/figment_offsets.h"

/* Figment trap lookup — no longer used for write interception (traps are
 * now baked into the ROM by patch-rom.py), but kept for diagnostics. */
uint32_t figment_lookup(uint32_t trap_addr) {
    if (!figment_enabled) return 0;
    uint32_t trap_num = (trap_addr - 0x0400) >> 2;
    if (trap_num > 0xFF) return 0;
    for (int i = 0; figment_trap_table[i].trap_num || figment_trap_table[i].offset; i++) {
        if (figment_trap_table[i].trap_num == trap_num) {
            return figment_trap_table[i].offset;
        }
    }
    return 0;
}

/* MODE32 pseudovirt trap — switch from IS=8 (24-bit) to IS=0 (32-bit).
 * Called from m68ki_exception_1010 when the 68k code executes DC.W $A0FF.
 * Atomically swaps PMMU tables, relocates WTC, updates globals. */
int mode32_trap_handler(m68ki_cpu_core *state) {
    uint32_t selector = REG_DA[0];
    if (selector != 0x4D333200)  /* 'M32\0' */
        return 0;

    if (mode32_active) {
        printf("[MODE32] Already active, ignoring\n");
        REG_DA[0] = 0;
        return 1;  /* PC already past A-line word */
    }

    printf("[MODE32] Switching to 32-bit addressing...\n");

    /* --- 1. Build IS=0 PMMU table --- */
    /* Two-level PMMU table: TIA=8 (256 entries) + TIB=4 (16 entries for $40)
     * TC = $80F08450: E=1, PS=15, IS=0, TIA=8, TIB=4, TIC=5, TID=0
     * Check: 0+8+4+5+0+15 = 32 ✓
     *
     * Level 1 (TIA=8): 256 entries, each 16MB. Full dirty byte stripping.
     *   $00: identity RAM (first 16MB)
     *   $01: identity RAM (second 16MB)
     *   $40: DT=2 → level 2 (per-megabyte I/O separation)
     *   all others: DT=1, base=$00000000 (dirty alias)
     *
     * Level 2 for $40 (TIB=4): 16 entries, each 1MB.
     *   0-7: $40000000-$407FFFFF → $00000000-$007FFFFF (strip $40)
     *     8: $40800000-$408FFFFF → $40800000 (ROM+SCSI, no CI)
     *   9-F: $40900000-$40FFFFFF → I/O identity (CI) */
    uint32_t level1_addr = 0x01010000;  /* 1KB */
    uint32_t level2_addr = 0x01010400;  /* 64 bytes */
    uint32_t level1[256];
    uint32_t level2[16];

    int32_t ri = get_named_mapped_item(cfg, "sysram");
    if (ri < 0 || !cfg->map_data[ri]) {
        printf("[MODE32] ERROR: no sysram\n");
        REG_DA[0] = -1;
        return 1;
    }
    unsigned char *ram = cfg->map_data[ri];

    /* Level 1: all dirty aliases except $00, $01, $40 */
    for (int i = 0; i < 256; i++)
        level1[i] = 0x00000019;  /* DT=1, base=$00000000 */
    level1[0x00] = 0x00000019;   /* RAM first 16MB (identity) */
    level1[0x01] = 0x01000019;   /* RAM second 16MB (identity) */
    level1[0x40] = (level2_addr & 0xFFFFFFFC) | 0x02;  /* DT=2 → level 2 */

    /* Level 2: per-megabyte split of $40xxxxxx */
    for (int i = 0; i < 8; i++)
        level2[i] = ((uint32_t)i << 20) | 0x19;        /* strip $40 → RAM */
    level2[8] = 0x40800019;                              /* ROM+SCSI, no CI */
    for (int i = 9; i <= 15; i++)
        level2[i] = (0x40000000 | ((uint32_t)i << 20)) | 0x59;  /* I/O, CI */

    /* Write tables to RAM buffer */
    for (int i = 0; i < 256; i++) {
        uint32_t off = level1_addr + i * 4;
        ram[off+0] = (level1[i] >> 24); ram[off+1] = (level1[i] >> 16);
        ram[off+2] = (level1[i] >> 8);  ram[off+3] = level1[i];
    }
    for (int i = 0; i < 16; i++) {
        uint32_t off = level2_addr + i * 4;
        ram[off+0] = (level2[i] >> 24); ram[off+1] = (level2[i] >> 16);
        ram[off+2] = (level2[i] >> 8);  ram[off+3] = level2[i];
    }

    printf("[MODE32] Two-level IS=0 table:\n");
    printf("  L1 at $%08X: [$00]=RAM [$01]=RAM [$40]→L2 [else]=dirty alias\n", level1_addr);
    printf("  L2 at $%08X: [0-7]=strip $40 [8]=ROM+SCSI [9-F]=I/O(CI)\n", level2_addr);

    /* --- 2. Swap PMMU registers --- */
    state->mmu_tc = 0x80F08450;  /* IS=0, TIA=8, TIB=4, TIC=5, PS=15 */
    state->mmu_crp_aptr = level1_addr;
    state->mmu_srp_aptr = level1_addr;
    /* CRP/SRP limit stays $7FFF0002 */

    /* --- 3. Flush all caches --- */
    state->code_translation_cache.lower = 0;
    state->code_translation_cache.upper = 0;
    state->fc_read_translation_cache.lower = 0;
    state->fc_read_translation_cache.upper = 0;
    state->fc_write_translation_cache.lower = 0;
    state->fc_write_translation_cache.upper = 0;
    /* Flush ATC */
    for (int i = 0; i < MMU_ATC_ENTRIES; i++) {
        state->mmu_atc_tag[i] = 0x8000;  /* invalid */
        state->mmu_atc_data[i] = 0;
    }

    /* --- 4. Relocate WTC from $7F0000 to $01FF0000 --- */
    memcpy(ram + 0x01FF0000, ram + 0x7F0000, 0x10000);

    /* Update globals that point into old WTC region */
    uint32_t old_scrnbase = (ram[0x824]<<24)|(ram[0x825]<<16)|(ram[0x826]<<8)|ram[0x827];
    uint32_t old_soundbase = (ram[0x266]<<24)|(ram[0x267]<<16)|(ram[0x268]<<8)|ram[0x269];
    uint32_t reloc_off = 0x01FF0000 - 0x7F0000;

    uint32_t new_scrnbase = old_scrnbase + reloc_off;
    ram[0x824] = (new_scrnbase >> 24); ram[0x825] = (new_scrnbase >> 16);
    ram[0x826] = (new_scrnbase >> 8);  ram[0x827] = new_scrnbase;

    if (old_soundbase >= 0x7F0000 && old_soundbase < 0x800000) {
        uint32_t new_soundbase = old_soundbase + reloc_off;
        ram[0x266] = (new_soundbase >> 24); ram[0x267] = (new_soundbase >> 16);
        ram[0x268] = (new_soundbase >> 8);  ram[0x269] = new_soundbase;
        printf("[MODE32] SoundBase: $%08X → $%08X\n", old_soundbase, new_soundbase);
    }

    /* Expand fast-path ranges in place */
    {
        m68ki_cpu_core *cpu = &m68ki_cpu;

        for (int i = 0; i < cpu->write_ranges; i++) {
            if (cpu->write_addr[i] == 0 && !cpu->write_through[i]) {
                cpu->write_upper[i] = 0x01FF0000;
                break;
            }
        }
        for (int i = 0; i < cpu->read_ranges; i++) {
            if (cpu->read_addr[i] == 0 && cpu->read_upper[i] <= 0x800000) {
                cpu->read_upper[i] = 0x01FF0000;
                break;
            }
        }
        for (int i = 0; i < cpu->write_ranges; i++) {
            if (cpu->write_through[i]) {
                cpu->write_addr[i] = 0x01FF0000;
                cpu->write_upper[i] = 0x02000000;
                cpu->write_data[i] = ram + 0x01FF0000;
                break;
            }
        }
        for (int i = 0; i < cpu->read_ranges; i++) {
            if (cpu->read_addr[i] >= 0x7F0000 && cpu->read_upper[i] <= 0x800000) {
                cpu->read_addr[i] = 0x01FF0000;
                cpu->read_upper[i] = 0x02000000;
                cpu->read_data[i] = ram + 0x01FF0000;
                break;
            }
        }

        cpu->code_translation_cache.lower = 0;
        cpu->code_translation_cache.upper = 0;
        cpu->fc_read_translation_cache.lower = 0;
        cpu->fc_read_translation_cache.upper = 0;
        cpu->fc_write_translation_cache.lower = 0;
        cpu->fc_write_translation_cache.upper = 0;
    }

    {
        extern uint32_t bigse_vbuf_virt;
        bigse_vbuf_virt = 0xFF0000;
    }

    /* MMU globals */
    ram[0x0CB4] = (level1_addr >> 24); ram[0x0CB5] = (level1_addr >> 16);
    ram[0x0CB6] = (level1_addr >> 8);  ram[0x0CB7] = level1_addr;
    /* $CB8 = MMUTblSize (not a second pointer!) */
    uint32_t tbl_size = 256 * 4 + 16 * 4;  /* L1 + L2 */
    ram[0x0CB8] = (tbl_size >> 24); ram[0x0CB9] = (tbl_size >> 16);
    ram[0x0CBA] = (tbl_size >> 8);  ram[0x0CBB] = tbl_size;

    printf("[MODE32] ScrnBase: $%08X → $%08X\n", old_scrnbase, new_scrnbase);
    printf("[MODE32] WTC: $7F0000 → $01FF0000, RAM: $0-$01FF0000\n");
    printf("[MODE32] TC=$%08X CRP=($7FFF0002,$%08X)\n",
           state->mmu_tc, level1_addr);

    /* Return success to 68k — PC already past the A-line word */
    REG_DA[0] = 0;
    return 1;
}

/* Update Gestalt 'addr' when mode32 active */

uint16_t irq_delay = 0;

void *ipl_task(void *args) {
  printf("IPL thread running\n");
  uint32_t value;
  while (1) {
    value = *(gpio + 13);
    if (value & (1 << PIN_TXN_IN_PROGRESS))
      goto noppers;

    if (!(value & (1 << PIN_IPL_ZERO))) {
      if (!atomic_load(&irq)) {
        M68K_END_TIMESLICE;
        atomic_store(&irq, 1);
#ifdef DEBUG_DIAG
        atomic_fetch_add(&dbg_ipl_assert, 1);
#endif
      }
    }
noppers:
    usleep(1);
  }
  return args;
}

/*
 * Inject a mouse event into the Mac OS event queue (EvQHdr at $014A).
 *
 * MUST be called from the CPU thread only — between emulated instructions
 * so the queue is in a consistent state.
 *
 * The SE ROM's PostEvent uses SysEvtBuf ($0146) as a CONSTANT pointer to
 * a circular buffer of 22-byte EvQEl entries.  Free slots have evtQWhat
 * ($FFFF) at offset 6.  We scan for a free slot, fill it in, and link it
 * into EventQueue ($014A) — exactly what the ROM's PostEvent does.
 *
 * EvQEl layout (standard, confirmed from ROM):
 *   +0  qLink        long   — queue linkage
 *   +4  qType        word   — 4 = evType
 *   +6  evtQWhat     word   — event code ($FFFF = free)
 *   +8  evtQMessage  long
 *  +12  evtQWhen     long   — Ticks
 *  +16  evtQWhere    Point  — {v, h}
 *  +20  evtQModifiers word  — hi: modifier keys, lo: MBState
 */
#define EVQHDR_HEAD    0x014C
#define EVQHDR_TAIL    0x0150
#define TICKS_ADDR     0x016A
#define EVQEL_SIZE     22       /* sizeof(EvQEl) */
#define SYSEVTBUF      0x0146   /* long: base of event buffer (CONSTANT) */
#define EVTBUFCNT      0x0154   /* word: number of buffer entries */


static inline uint32_t ram_read32(const uint8_t *ram, uint32_t addr) {
    return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr+1] << 16) |
           ((uint32_t)ram[addr+2] << 8) |  (uint32_t)ram[addr+3];
}

static inline void ram_write32(uint8_t *ram, uint32_t addr, uint32_t val) {
    ram[addr]   = (uint8_t)(val >> 24);
    ram[addr+1] = (uint8_t)(val >> 16);
    ram[addr+2] = (uint8_t)(val >> 8);
    ram[addr+3] = (uint8_t)val;
}

static inline void ram_write16(uint8_t *ram, uint32_t addr, uint16_t val) {
    ram[addr]   = (uint8_t)(val >> 8);
    ram[addr+1] = (uint8_t)val;
}

static inline uint16_t ram_read16(const uint8_t *ram, uint32_t addr) {
    return ((uint16_t)ram[addr] << 8) | ram[addr+1];
}

static int vnc_inject_mouse_event(uint8_t what) {
    uint8_t *ram = vnc_cfg.ram_base;
    if (!ram)
        return 0;

    /* EvQHdr.qFlags ($014A): if non-zero, queue is locked — retry later */
    if (ram[0x014A] || ram[0x014B])
        return 0;

    /* Find a free slot: scan event buffer for evtQWhat == $FFFF */
    uint32_t buf_base = ram_read32(ram, SYSEVTBUF);
    uint16_t buf_cnt  = ram_read16(ram, EVTBUFCNT);
    if (buf_base == 0 || buf_base >= vnc_cfg.ram_size)
        return 0;
    if (buf_cnt == 0 || buf_cnt > 100)
        buf_cnt = 5;  /* safety fallback */

    uint32_t slot = 0;
    for (uint16_t i = 0; i < buf_cnt; i++) {
        uint32_t entry = buf_base + (uint32_t)i * EVQEL_SIZE;
        if (entry + EVQEL_SIZE > vnc_cfg.ram_size)
            break;
        if (ram_read16(ram, entry + 6) == 0xFFFF) {
            slot = entry;
            break;
        }
    }
    if (!slot)
        return 0;  /* will retry */

    uint16_t ex = vnc_cfg.mouse_event_x;
    uint16_t ey = vnc_cfg.mouse_event_y;

    /* Fill in EvQEl — claiming the slot (evtQWhat != $FFFF) */
    ram_write32(ram, slot,      0);              /* qLink = NULL (will be queue tail) */
    ram_write16(ram, slot + 4,  4);              /* qType = evType */
    ram_write16(ram, slot + 6,  what);           /* evtQWhat: 1=mouseDown, 2=mouseUp */
    ram_write32(ram, slot + 8,  0);              /* evtQMessage */
    ram_write32(ram, slot + 12, ram_read32(ram, TICKS_ADDR)); /* evtQWhen */
    ram_write16(ram, slot + 16, ey);             /* evtQWhere.v */
    ram_write16(ram, slot + 18, ex);             /* evtQWhere.h */
    /* evtQModifiers: low byte = MBState ($0172), high byte = modifier keys */
    ram_write16(ram, slot + 20, (uint16_t)ram[0x0172]);

    /* Append to EventQueue */
    uint32_t tail = ram_read32(ram, EVQHDR_TAIL);
    if (tail == 0) {
        ram_write32(ram, EVQHDR_HEAD, slot);
    } else {
        ram_write32(ram, tail, slot);  /* old_tail->qLink = slot */
    }
    ram_write32(ram, EVQHDR_TAIL, slot);

    return 1;
}

static int vnc_inject_key_event(uint8_t mac_keycode, uint8_t mac_char,
                                uint8_t down, uint16_t modifiers) {
    uint8_t *ram = vnc_cfg.ram_base;
    if (!ram)
        return 0;

    /* EvQHdr.qFlags ($014A): if non-zero, queue is locked — retry later */
    if (ram[0x014A] || ram[0x014B])
        return 0;

    /* Find a free slot: scan event buffer for evtQWhat == $FFFF */
    uint32_t buf_base = ram_read32(ram, SYSEVTBUF);
    uint16_t buf_cnt  = ram_read16(ram, EVTBUFCNT);
    if (buf_base == 0 || buf_base >= vnc_cfg.ram_size)
        return 0;
    if (buf_cnt == 0 || buf_cnt > 100)
        buf_cnt = 5;

    uint32_t slot = 0;
    for (uint16_t i = 0; i < buf_cnt; i++) {
        uint32_t entry = buf_base + (uint32_t)i * EVQEL_SIZE;
        if (entry + EVQEL_SIZE > vnc_cfg.ram_size)
            break;
        if (ram_read16(ram, entry + 6) == 0xFFFF) {
            slot = entry;
            break;
        }
    }
    if (!slot)
        return 0;

    uint16_t what = down ? 3 : 5;  /* keyDown=3, keyUp=5 */

    ram_write32(ram, slot,      0);
    ram_write16(ram, slot + 4,  4);              /* qType = evType */
    ram_write16(ram, slot + 6,  what);
    /* evtQMessage: keycode in bits 8-15, char code in bits 0-7 */
    ram_write32(ram, slot + 8,  ((uint32_t)mac_keycode << 8) | mac_char);
    ram_write32(ram, slot + 12, ram_read32(ram, TICKS_ADDR));
    /* evtQWhere: current mouse position from MTemp */
    ram_write16(ram, slot + 16, ram_read16(ram, 0x0828));     /* MTemp.v */
    ram_write16(ram, slot + 18, ram_read16(ram, 0x0828 + 2)); /* MTemp.h */
    /* evtQModifiers: modifier flags | MBState */
    ram_write16(ram, slot + 20, (modifiers & 0xFF00) | ram[0x0172]);

    uint32_t tail = ram_read32(ram, EVQHDR_TAIL);
    if (tail == 0) {
        ram_write32(ram, EVQHDR_HEAD, slot);
    } else {
        ram_write32(ram, tail, slot);
    }
    ram_write32(ram, EVQHDR_TAIL, slot);

    return 1;
}

static inline void m68k_execute_bef(m68ki_cpu_core *state, int num_cycles)
{
	/* eat up any reset cycles */
	if (RESET_CYCLES) {
	    int rc = RESET_CYCLES;
	    RESET_CYCLES = 0;
	    num_cycles -= rc;
	    if (num_cycles <= 0)
		return;
	}

	/* Set our pool of clock cycles available */
	SET_CYCLES(num_cycles);
	m68ki_initial_cycles = num_cycles;

	/* See if interrupts came in */
	m68ki_check_interrupts(state);

	/* Make sure we're not stopped */
	if(!CPU_STOPPED)
	{
		/* Return point if we had an address error */
		m68ki_set_address_error_trap(state); /* auto-disable (see m68kcpu.h) */

#ifdef M68K_BUSERR_THING
		m68ki_check_bus_error_trap();
#endif

		/* Main loop.  Keep going until we run out of clock cycles */
		do
		{
			/* Set tracing according to T1. (T0 is done inside instruction) */
			m68ki_trace_t1(); /* auto-disable (see m68kcpu.h) */

			/* Set the address space for reads */
			m68ki_use_data_space(); /* auto-disable (see m68kcpu.h) */

			/* Call external hook to peek at CPU */
			m68ki_instr_hook(REG_PC); /* auto-disable (see m68kcpu.h) */

			/* PC trace for big-se vs huge-se comparison */
			if (pc_trace_buf && pc_trace_count < pc_trace_limit)
				pc_trace_buf[pc_trace_count++] = REG_PC;

			/* Debug: trace T2 load and disk subroutines */
			{
			  uint32_t rom_off = REG_PC - ovl_sysrom_pos;
			  if (rom_off == 0x1AA20) {
			    static int t2_dbg = 0;
			    if (t2_dbg++ < 3)
			      printf("[T2-LOAD] called! D0=$%08X\n", REG_DA[0]);
			  }
			  if (rom_off == 0x3370) {
			    static int sub_dbg = 0;
			    if (sub_dbg++ < 5)
			    {
			      { uint8_t orb = ps_read_8(0xEFE1FE);
			        printf("[DISK] $0CF8=$%08X ORB=$%02X\n",
			               m68ki_read_32(state, 0x0CF8), orb);
			      }
			    }
			  }
			  if (rom_off == 0x3588) {
			    static int sr_dbg = 0;
			    if (sr_dbg++ < 5)
			      printf("[SR-HANDLER] entered! #%d\n", sr_dbg);
			  }
			  /* Stuck loop diagnostic: ROM+$1A54A polling IWM */
			  if (rom_off == 0x1A54A) {
			    static int iwm_stuck = 0;
			    if (iwm_stuck++ % 100000 == 0) {
			      uint32_t ticks = m68ki_read_32(state, 0x16A);
			      uint8_t iwm_q6l = ps_read_8(0x5FF040);
			      printf("[IWM-STUCK] #%d Ticks=$%08X IWM_Q6L=$%02X D0=$%08X D1=$%08X\n",
			             iwm_stuck, ticks, iwm_q6l,
			             REG_DA[0], REG_DA[1]);
			    }
			  }
			  /* Catch jumps into WTC region after MODE32 relocation */
			  if (mode32_active && REG_PC >= 0x01FF0000 && REG_PC < 0x02000000) {
			    static int wtc_jump = 0;
			    if (wtc_jump++ < 3)
			      printf("[WTC-JUMP] PC=$%08X from PPC=$%08X  A0=$%08X A1=$%08X SP=$%08X\n",
			             REG_PC, REG_PPC, REG_DA[8], REG_DA[9], REG_DA[15]);
			  }
			  /* Keyboard SR callback: ROM+$368A is JMP (A0) where A0 was
			   * loaded from the driver's completion routine pointer. If the
			   * keyboard driver hasn't initialized yet, A0 is NULL → crash.
			   * Redirect to an RTS in ROM so the interrupt returns cleanly. */
			  if (rom_off == 0x368A && REG_DA[8+0] == 0) {
			    static int kbd_null = 0;
			    if (kbd_null++ < 5)
			      printf("[KBD-NULL] JMP (A0=0) at ROM+$368A → redirecting to RTS\n");
			    /* ROM+$3680 is an RTS right before this code block */
			    REG_DA[8+0] = ovl_sysrom_pos + 0x3680;
			  }
			  if (rom_off == 0x2A0E) {
			    /* MOVEQ #-1,D0 before BTST #1,$1A00(A5) poll loop — polls VIA IFR for CA1/VBL */
			    uint32_t a5 = REG_DA[8+5];
			    uint32_t addr = a5 + 0x1A00;
			    uint8_t val_pmmu = m68ki_read_8_fc(state, addr, FLAG_S | 5);
			    uint8_t val_bus = ps_read_8(0xEFFBFE);  /* direct SE bus: VIA IFR */
			    printf("[SND-POLL] ROM+$02A0E: A5=$%08X  target=$%08X  pmmu=$%02X  bus=$%02X  bit1_p=%d  bit1_b=%d\n",
			           a5, addr, val_pmmu, val_bus, (val_pmmu >> 1) & 1, (val_bus >> 1) & 1);
			  }
			  /* Trace what PMMU translates VIA IFR to */
			  if (rom_off == 0x2B3A) {  /* MOVE.B ($1A00,A1), D0 — IFR read */
			    static int ifr_dbg = 0;
			    if (ifr_dbg++ < 5) {
			      uint32_t virt = REG_DA[9] + 0x1A00;  /* A1 + $1A00 */
			      uint32_t phys = pmmu_translate_addr(&m68ki_cpu, virt, 1);
			      printf("[IFR-XLAT] virt=$%08X → phys=$%08X\n", virt, phys);
			    }
			  }
			  if (rom_off == 0x0048) {
			    static int dump_done = 0;
			    if (!dump_done) {
			      printf("[LOWMEM] Dump at +$0048 (after sizing):\n");
			      for (int k = 0; k < 0x400; k += 16) {
			        printf("  $%04X:", k);
			        for (int j = 0; j < 16; j += 4)
			          printf(" %08X", m68ki_read_32(state, k + j));
			        printf("\n");
			      }
			      printf("[LOWMEM] Registers: D6=$%08X D7=$%08X A6=$%08X SP=$%08X\n",
			             REG_DA[6], REG_DA[7], REG_DA[14], REG_DA[15]);
			      dump_done = 1;
			    }
			  }
			}
			{ static int iwm_dbg = 0;
			  uint32_t rom_off = REG_PC - ovl_sysrom_pos;
			  if (rom_off == 0x3370 && iwm_dbg < 10) {
			    uint32_t vec64 = m68ki_read_32(state, 0x64);
			    uint32_t stub = ovl_sysrom_pos + 0x1B62;
			    printf("[DISK] vec$64=$%08X (%s) $01D4=$%08X\n",
			           vec64,
			           (vec64 == stub) ? "BOOT STUB!" : "real handler",
			           m68ki_read_32(state, 0x01D4));
			    printf("[DISK] Lvl1DT: CA2=$%08X CA1=$%08X SR=$%08X CB2=$%08X CB1=$%08X T2=$%08X T1=$%08X IRQ=$%08X\n",
			           m68ki_read_32(state, 0x0192),
			           m68ki_read_32(state, 0x0196),
			           m68ki_read_32(state, 0x019A),
			           m68ki_read_32(state, 0x019E),
			           m68ki_read_32(state, 0x01A2),
			           m68ki_read_32(state, 0x01A6),
			           m68ki_read_32(state, 0x01AA),
			           m68ki_read_32(state, 0x01AE));
			  }
			}
			{
			  uint32_t rom_off = REG_PC - ovl_sysrom_pos;
			  if (rom_off == 0xDA1C) {  /* _NewPtr $A51E call */
			    static int np_dbg = 0;
			    if (np_dbg++ < 3) {
			      uint32_t tz = m68ki_read_32(state, 0x0118);
			      printf("[DA08-NEWPTR] before: D0=$%08X A0=$%08X TheZone=$%08X\n",
			             REG_DA[0], REG_DA[8], tz);
			      printf("[DA08-NEWPTR] zone header at $%08X:\n", tz);
			      for (int k = 0; k < 56; k += 4)
			        printf("  +$%02X: $%08X\n", k, m68ki_read_32(state, tz + k));
			    }
			  }
			  if (rom_off == 0xDA1E) {  /* instruction after _NewPtr */
			    static int np2_dbg = 0;
			    if (np2_dbg++ < 3) {
			      printf("[DA08-NEWPTR] after:  D0=$%08X A0=$%08X (err=%s)\n",
			             REG_DA[0], REG_DA[8],
			             (REG_DA[0] & 0xFFFF) ? "YES" : "no");
			    }
			  }
			  /* Trace: is .Sony driver code ever reached? */
			  if (rom_off >= 0x346F2 && rom_off < 0x36C94) {
			    static int sony_pc = 0;
			    if (sony_pc++ < 5)
			      printf("[SONY-PC] PC=$%08X (driver+$%04X)\n",
			             REG_PC, rom_off - 0x34684);
			  }
			  /* .Sony driver dispatch trace */
			  if (rom_off == 0x4332) {
			    static int poll_dbg = 0;
			    if (poll_dbg++ < 3) {
			      uint32_t a0 = REG_DA[8];
			      int16_t ioResult = (int16_t)m68ki_read_16(state, a0 + 0x10);
			      uint32_t utbase = m68ki_read_32(state, 0x011C);
			      /* .Sony = unit -5, table entry at UTableBase + (-5 - (-1)) * 4
			       * Actually unit table is indexed by -(refnum+1)/4.
			       * .Sony refnum = -5. Index = (-(-5)-1)/4 = 4/4 = 1.
			       * But table[0] = refnum -1, table[1] = refnum -2, ...
			       * table entry = UTableBase + (-(refnum+1)) * 4 ?
			       * Actually: index = ~refnum >> 1 & 0xFF? Let me just dump first entries. */
			      printf("[POLL] ioResult=%d  D0=$%08X  D1=$%04X\n",
			             ioResult, REG_DA[0], REG_DA[1] & 0xFFFF);
			      printf("  UTableBase=$%08X  entries:\n", utbase);
			      for (int j = 0; j < 8; j++)
			        printf("    [%d] $%08X\n", j, m68ki_read_32(state, utbase + j * 4));
			      printf("  ioParam: name=$%08X refnum=$%04X\n",
			             m68ki_read_32(state, a0 + 0x12),
			             m68ki_read_16(state, a0 + 0x18) & 0xFFFF);
			    }
			  }
			  if (rom_off == 0xA41A) {
			    static int iz_dbg = 0;
			    if (iz_dbg++ < 5) {
			      uint32_t a0 = REG_DA[8];
			      printf("[INITZONE] A0=$%08X startPtr=$%08X limitPtr=$%08X\n",
			             a0, m68ki_read_32(state, a0),
			             m68ki_read_32(state, a0 + 4));
			    }
			  }
			}

			/* OVL off: when PC enters ROM range. The slow-path OVL check
			 * doesn't fire for fast-path ROM reads. On big-se this works
			 * because the SCSI exclusion zone forces slow-path reads, but
			 * on huge-se the sizing skip means ROM code reaches the stack
			 * before any exclusion-zone fetch.
			 * With PMMU active (huge-se), REG_PC is virtual ($800048),
			 * not physical ($40800048). Check both ranges. */
			if (state->ovl &&
			    ((REG_PC >= ovl_sysrom_pos && REG_PC < ovl_sysrom_pos + 0x80000) ||
			     (ovl_sysrom_pos >= 0x40000000 &&
			      REG_PC >= 0x800000 && REG_PC < 0x880000))) {
				ovl = 0;
				state->ovl = 0;
				printf("[MAC] OVL off (PC=$%08X in ROM range)\n", REG_PC);
				handle_ovl_mappings_mac68k(cfg);
			}

			/* Early boot trace — print last 50 instructions before sad mac */
			{
				static uint32_t boot_ring[64];
				static int boot_idx = 0;
				static int boot_done = 0;
				if (!boot_done) {
					boot_ring[boot_idx & 63] = REG_PC;
					boot_idx++;
					/* Detect entry to sad mac handler (ROM offset $2000-$2400).
					 * With PMMU (huge-se), PC is virtual ($8xxxxx), not physical. */
					uint32_t rom_off = REG_PC - ovl_sysrom_pos;
					if (ovl_sysrom_pos >= 0x40000000 && REG_PC >= 0x800000)
						rom_off = REG_PC - 0x800000;
					if (rom_off >= 0x2000 && rom_off < 0x2400 && boot_idx > 100) {
						printf("[BOOT] Sad mac entered at PC=$%08X after %d instructions\n",
						       REG_PC, boot_idx);
						printf("[BOOT] Last 50 PCs before sad mac:\n");
						for (int i = 50; i > 0; i--) {
							int idx = (boot_idx - i) & 63;
							printf("  $%08X\n", boot_ring[idx]);
						}
						printf("[BOOT] D0-D7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
						       REG_DA[0], REG_DA[1], REG_DA[2], REG_DA[3],
						       REG_DA[4], REG_DA[5], REG_DA[6], REG_DA[7]);
						printf("[BOOT] A0-A7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
						       REG_DA[8], REG_DA[9], REG_DA[10], REG_DA[11],
						       REG_DA[12], REG_DA[13], REG_DA[14], REG_DA[15]);
						boot_done = 1;
					}
				}
			}

			/* Big SE: redirect PC from old ROM space ($4xxxxx) to new ($8xxxxx).
			 * Code loaded from disk may reference $4xxxxx ROM addresses that
			 * the prescan didn't catch. Only redirect if the RAM at that
			 * address looks like ROM content (not like normal RAM data). */

			/* Big/Huge SE: force MemTop after memory sizing returns.
			 * The sizing routine ends with a JMP; the next instruction
			 * is at sysrom_pos+$48. A6 holds MemTop from sizing. */
			{
				extern uint32_t ovl_sysrom_pos;
				static int memtop_done = 0;
				uint32_t memtop_pc = ovl_sysrom_pos + 0x48;
				/* Huge SE: big-se ROM executes at virtual $800048 even
				 * though sysrom_pos is $40800000.  Check both. */
				uint32_t memtop_pc_virt = (ovl_sysrom_pos >= 0x40000000) ?
				    (0x800000 + 0x48) : 0;
				if (!memtop_done && ovl_sysrom_pos >= 0x800000 &&
				    (REG_PC == memtop_pc || REG_PC == memtop_pc_virt)) {
					uint32_t old = REG_DA[14];
					int32_t ram_idx = get_named_mapped_item(cfg, "sysram");
					uint32_t ram_size = (ram_idx >= 0) ? cfg->map_size[ram_idx] : 0x800000;
					int is_huge_se = (ovl_sysrom_pos >= 0x40000000);

					/* Huge SE: PMMU provides 24-bit virtual space over 32-bit
					 * physical.  Boot with 8MB visible — MODE32 will expand.
					 * Big SE: use actual RAM size. */
					uint32_t memtop = is_huge_se ? 0x800000 : ram_size;
					REG_DA[14] = memtop;
					printf("[%s] MemTop forced: A6=$%08X → $%08X\n",
					       is_huge_se ? "HUGE-SE" : "BIG-SE", old, memtop);

					/* Huge SE: set up Mac II-style PMMU for 24-bit boot.
					 *
					 * TC = $80F84500 (identical to Mac II ROM):
					 *   E=1, PS=15(32KB), IS=8, TIA=4, TIB=5
					 *   IS=8 strips the high byte of all virtual addresses,
					 *   giving 24-bit dirty pointer compatibility for free.
					 *
					 * Root table: 16 early-termination page descriptors (1MB each).
					 * Descriptor format from Mac II ROM at +$50:
					 *   RAM:  phys_base | $19  (DT=01, M, U)
					 *   I/O:  phys_base | $59  (DT=01, CI, M, U)
					 *
					 * Entry 7 maps $700000 → $01F00000 (not identity) so that
					 * video at virtual $7F0000 hits physical $01FF0000 in the
					 * WTC region at the top of 32MB RAM.
					 *
					 * CRP = ($7FFF0002, tbl) — Mac II format:
					 *   limit=$7FFF, DT=2 (valid 4-byte descriptors). */
					if (is_huge_se && cpu_type == M68K_CPU_TYPE_68030) {
						/* PMMU root table in RAM at $6FFF00 (in entry 6, identity-mapped).
						 * Written directly to RAM buffer. The PMMU walker reads via
						 * m68k_read_memory_32 (slow path, 24-bit masked). We add a
						 * check in the slow path so the walker can read from the
						 * RAM buffer instead of going to the SE bus. */
						uint32_t tbl = 0x01000040; /* 16MB into 32MB RAM buffer — above all
						                            * 24-bit handler ranges ($800000 ROM alias,
						                            * $880000 SCSI remap). Walker reads via
						                            * m68k_read_memory_32 → wtcram buffer. */
						printf("[HUGE-SE] PMMU root table at $%08X\n", tbl);

						/* Mac II-style root table: 16 × 4-byte page descriptors.
						 *  0-6: identity RAM    ($00x00019)
						 *    7: video remap      ($01F00019) — maps $7Fxxxx → WTC
						 *    8: ROM + SCSI remap ($40800019)
						 * 9-15: I/O              ($40x00059, CI) */
						const uint32_t pmmu_table[16] = {
							0x00000019, 0x00100019, 0x00200019, 0x00300019,  /* 0-3: RAM */
							0x00400019, 0x00500019, 0x00600019, 0x00700019,  /* 4-7: RAM (all identity) */
							0x40800019,                                       /* 8: ROM + SCSI remap */
							0x40900059, 0x40A00059, 0x40B00059,              /* 9-11: SCC (CI) */
							0x40C00059, 0x40D00059, 0x40E00059, 0x40F00059,  /* 12-15: I/O (CI) */
						};
						/* Write directly to RAM buffer */
						int32_t ri0 = get_named_mapped_item(cfg, "sysram");
						if (ri0 >= 0 && cfg->map_data[ri0]) {
							unsigned char *ram = cfg->map_data[ri0];
							for (int i = 0; i < 16; i++) {
								uint32_t d = pmmu_table[i];
								uint32_t off = tbl + i * 4;
								ram[off+0] = (d >> 24) & 0xFF;
								ram[off+1] = (d >> 16) & 0xFF;
								ram[off+2] = (d >> 8)  & 0xFF;
								ram[off+3] =  d        & 0xFF;
							}
						}

						printf("[HUGE-SE] PMMU table (Mac II style):\n");
						for (int i = 0; i < 16; i++) {
							printf("  [%2d] $%06X → $%08X %s\n", i,
							       i * 0x100000,
							       pmmu_table[i] & 0xFFF00000,
							       (pmmu_table[i] & 0x40) ? "CI" : "");
						}

						/* Low-memory MMU globals (24-bit mode for boot).
						 * Write directly to RAM buffer — m68k_write_memory
						 * goes to SE bus via slow path, not local buffer. */
						{
							int32_t ri = get_named_mapped_item(cfg, "sysram");
							if (ri >= 0 && cfg->map_data[ri]) {
								unsigned char *r = cfg->map_data[ri];
								r[0x0CB1] = 4;       /* MMUType = 68030 */
								r[0x0B73] = 0;       /* 24-bit mode */
								r[0x0CB4] = (tbl >> 24); r[0x0CB5] = (tbl >> 16);
								r[0x0CB6] = (tbl >> 8);  r[0x0CB7] = tbl;
								r[0x0CB8] = (tbl >> 24); r[0x0CB9] = (tbl >> 16);
								r[0x0CBA] = (tbl >> 8);  r[0x0CBB] = tbl;
							}
						}

						/* TC = $80F84500 (Mac II value):
						 *   E=1, PS=15, IS=8, TIA=4, TIB=5
						 *   Sum: 15+8+4+5+0+0 = 32, bit 23 = 1 (valid) */
						m68ki_cpu.mmu_crp_limit = 0x7FFF0002; /* DT=2: 4-byte descriptors */
						m68ki_cpu.mmu_crp_aptr = tbl;
						m68ki_cpu.mmu_srp_limit = 0x7FFF0002;
						m68ki_cpu.mmu_srp_aptr = tbl;
						m68ki_cpu.mmu_tt0 = 0;  /* disable transparent translation */
						m68ki_cpu.mmu_tt1 = 0;
						m68ki_cpu.mmu_tc = 0x80F84500;
						m68ki_cpu.pmmu_enabled = 1;
						/* Flush ATC and fast-path translation caches */
						for (int j = 0; j < MMU_ATC_ENTRIES; j++)
							m68ki_cpu.mmu_atc_tag[j] = 0;
						m68ki_cpu.mmu_atc_rr = 0;
						m68ki_cpu.fc_read_translation_cache.lower = 0;
						m68ki_cpu.fc_read_translation_cache.upper = 0;
						m68ki_cpu.fc_write_translation_cache.lower = 0;
						m68ki_cpu.fc_write_translation_cache.upper = 0;
						m68ki_cpu.code_translation_cache.lower = 0;
						m68ki_cpu.code_translation_cache.upper = 0;

						printf("[HUGE-SE] PMMU enabled: TC=$%08X CRP=($%08X,$%08X)\n",
						       m68ki_cpu.mmu_tc, m68ki_cpu.mmu_crp_limit,
						       m68ki_cpu.mmu_crp_aptr);
					}

					memtop_done = 1;
				}
			}

			/* Big SE: scan loaded code regions for $4xxxxx operands.
			 * Two triggers:
			 * 1. Device Manager dispatch at $81A424 = JMP (A0)
			 * 2. First entry into any 64K region $01xxxx-$07xxxx
			 * The dispatch hook catches Device Manager patches;
			 * the region-entry hook catches Sound Manager etc. */
			{
				extern uint32_t ovl_sysrom_pos;
				if (ovl_sysrom_pos >= 0x800000) {
					uint32_t pc24 = ADDRESS_68K(REG_PC);
					int do_scan = 0;
					uint32_t scan_lo = 0, scan_hi = 0;

					/* Trigger 1: Device Manager dispatch.
				 * With PMMU (huge-se), pc24 is virtual ($800000 base). */
					uint32_t dm_phys = ovl_sysrom_pos + 0x1A424;
					uint32_t dm_virt = (ovl_sysrom_pos >= 0x40000000) ?
					    (0x800000 + 0x1A424) : 0;
					if (pc24 == dm_phys || pc24 == dm_virt) {
						uint32_t a0 = ADDRESS_68K(REG_DA[8]);
						if (a0 >= 0x010000 && a0 < 0x080000) {
							scan_lo = a0 & 0xFF0000;
							scan_hi = scan_lo + 0x10000;
							do_scan = 1;
						}
					}

					/* Trigger 2: entry into loaded code region.
					 * Rescan periodically — code may be reloaded. */
					{
						static uint8_t rgn_scanned[8];
						static uint32_t rgn_scan_cycle = 0;
						unsigned rgn = pc24 >> 16;
						if (rgn >= 1 && rgn <= 7 && !rgn_scanned[rgn]) {
							rgn_scanned[rgn] = 1;
							scan_lo = rgn << 16;
							scan_hi = scan_lo + 0x10000;
							do_scan = 1;
						}
						/* Reset scan flags periodically so new code gets caught */
						if (++rgn_scan_cycle >= 10000000) {
							rgn_scan_cycle = 0;
							for (int i = 0; i < 8; i++) rgn_scanned[i] = 0;
						}
					}

					if (do_scan) {
						int patched = 0;
						static const uint16_t abs_ops[] = {
							0x4EF9, 0x4EB9, 0x4879,
							0x41F9, 0x43F9, 0x45F9, 0x47F9,
							0x49F9, 0x4BF9, 0x4DF9,
							0x21FC, 0x23FC, 0
						};
						for (uint32_t s = scan_lo; s < scan_hi - 5; s += 2) {
							uint16_t op = m68ki_read_16(state, s);
							int is_abs = 0;
							for (const uint16_t *p = abs_ops; *p; p++)
								if (op == *p) { is_abs = 1; break; }
							if ((op & 0xF1FF) == 0x203C || (op & 0xF1FF) == 0x207C)
								is_abs = 1;
							if (is_abs) {
								uint32_t val = m68ki_read_32(state, s + 2);
								if (val >= 0x00400000 && val < 0x00440000) {
									m68ki_write_32(state, s + 2, val + 0x400000);
									patched++;
								}
							}
						}
						if (patched)
							printf("[PRESCAN] %d patches in $%06X-$%06X (PC=$%06X)\n",
								patched, scan_lo, scan_hi, pc24);
					}
				}
			}

			/* Record previous program counter */
			REG_PPC = REG_PC;

			/* PC trace ring buffer */
			state->pc_trace[state->pc_trace_idx & 31] = REG_PC;
			state->pc_trace_idx = (state->pc_trace_idx + 1) & 31;

			/* Record previous D/A register state (in case of bus error) */
//#define M68K_BUSERR_THING
#ifdef M68K_BUSERR_THING
			for (int i = 15; i >= 0; i--){
				REG_DA_SAVE[i] = REG_DA[i];
			}
#endif

			/* Read an instruction and call its handler */
			REG_IR = m68ki_read_imm_16(state);
			m68ki_instruction_jump_table[REG_IR](state);
			USE_CYCLES(CYC_INSTRUCTION[REG_IR]);

			/* Trace m68k_exception, if necessary */
			m68ki_exception_if_trace(state); /* auto-disable (see m68kcpu.h) */
		} while(GET_CYCLES() > 0);

		/* set previous PC to current PC for the next entry into the loop */
		REG_PPC = REG_PC;
	}
	else
		SET_CYCLES(0);

	/* return how many clocks we used */
	return;
}

void *cpu_task() {
	m68ki_cpu_core *state = &m68ki_cpu;
  state->ovl = ovl;
  state->gpio = gpio;
	m68k_pulse_reset(state);

cpu_loop:
  if (mouse_hook_enabled) {
    get_mouse_status(&mouse_dx, &mouse_dy, &mouse_buttons, &mouse_extra);
  }

  /* VNC mouse button: inject events into Mac OS event queue (CPU thread) */
  if (vnc_cfg.mouse_pending) {
    uint8_t pend = vnc_cfg.mouse_pending;
    if (pend & 1) {  /* mouseDown first */
      if (vnc_inject_mouse_event(1))
        vnc_cfg.mouse_pending &= ~1;
    } else if (pend & 2) {  /* mouseUp only after mouseDown is done */
      if (vnc_inject_mouse_event(2))
        vnc_cfg.mouse_pending &= ~2;
    }
  }

  /* VNC keyboard: drain key event ring buffer into Mac OS event queue */
  while (vnc_cfg.key_tail != vnc_cfg.key_head) {
    struct vnc_key_event *ke = &vnc_cfg.key_queue[vnc_cfg.key_tail];
    if (!vnc_inject_key_event(ke->mac_keycode, ke->mac_char, ke->down, ke->modifiers))
      break;  /* queue locked or full — retry next iteration */
    vnc_cfg.key_tail = (vnc_cfg.key_tail + 1) % VNC_KEY_QUEUE_SIZE;
  }

  if (realtime_disassembly && (do_disasm || cpu_emulation_running)) {
    m68k_disassemble(disasm_buf, m68k_get_reg(NULL, M68K_REG_PC), cpu_type);
    printf("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3), \
            m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5), m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    printf("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3), \
            m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5), m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
    printf("%.8X (%.8X)]] %s\n", m68k_get_reg(NULL, M68K_REG_PC), (m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF), disasm_buf);
    if (do_disasm)
      do_disasm--;
	  m68k_execute_bef(state, 1);
  }
  else {
    if (cpu_emulation_running) {
		if (atomic_load(&irq))
			m68k_execute_bef(state, 5);
		else
			m68k_execute_bef(state, loop_cycles);
    }
  }

  /* SIGUSR1 trigger for MODE32 switch */
  if (mode32_trigger && !mode32_active) {
    mode32_trigger = 0;
    m68ki_cpu_core *st = &m68ki_cpu;
    st->dar[0] = 0x4D333200;
    printf("[MODE32-USR1] Triggering MODE32 switch\n");
    mode32_trap_handler(st);
  }

  /* PC logging disabled — Pi 4 bus verified solid */

#ifdef DEBUG_DIAG
  {
    // Diagnostic: dump Sound Driver state when stuck at $4031BE/$4031C2
    static int sound_diag_done = 0;
    if (!sound_diag_done) {
      uint32_t skip_pc = m68k_get_reg(NULL, M68K_REG_PC);
      if ((skip_pc == 0x4031BE || skip_pc == 0x4031C2) &&
          m68k_read_memory_32(0x16A) > 60) {
        uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
        printf("[SND-DIAG] Stuck at %08X, A0=%08X (ioResult=%d)\n",
               skip_pc, a0, (int16_t)m68k_read_memory_16(a0 + 0x10));
        // Dump the parameter block (first 0x32 bytes)
        printf("[SND-DIAG] PB: ");
        for (int i = 0; i < 0x32; i += 2)
          printf("%04X ", m68k_read_memory_16(a0 + i));
        printf("\n");
        // Find Sound Driver DCE: unit table at $11C, refNum=-5 => unit 4
        uint32_t utab = m68k_read_memory_32(0x11C);
        // Dump units 0-7 for reference
        printf("[SND-DIAG] UnitTable=%08X\n", utab);
        for (int u = 0; u < 8; u++) {
          uint32_t dce_u = m68k_read_memory_32(utab + u * 4);
          if (dce_u) {
            uint32_t drv_u = m68k_read_memory_32(dce_u);
            uint32_t drv_real = drv_u;
            if (drv_u & 0x80000000)
              drv_real = m68k_read_memory_32(drv_u & 0x00FFFFFF);
            // Read driver name (pascal string at header+18)
            char name[32] = {0};
            if (drv_real && !(drv_real & 0xFF000000)) {
              uint8_t nlen = m68k_read_memory_8(drv_real + 18);
              if (nlen > 0 && nlen < 30)
                for (int c = 0; c < nlen; c++)
                  name[c] = m68k_read_memory_8(drv_real + 19 + c);
            }
            printf("[SND-DIAG]   unit%d: DCE=%08X drv=%08X name='%s'\n",
                   u, dce_u, drv_real, name);
          }
        }
        // Dereference unit table handle to get real DCE
        for (int u = 1; u <= 4; u++) {
          uint32_t handle = m68k_read_memory_32(utab + u * 4);
          if (!handle) continue;
          uint32_t master = m68k_read_memory_32(handle);
          uint32_t dce_real = master & 0x00FFFFFF;
          if (!dce_real) continue;
          uint16_t flags = m68k_read_memory_16(dce_real + 4);
          uint8_t busy_byte = m68k_read_memory_8(dce_real + 5);
          uint32_t qhead = m68k_read_memory_32(dce_real + 8);
          uint32_t qtail = m68k_read_memory_32(dce_real + 12);
          printf("[DCE] unit%d: handle=%08X real=%08X flags=%04X busy_b=%02X qH=%08X qT=%08X\n",
                 u, handle, dce_real, flags, busy_byte, qhead, qtail);
        }
        // Key low-memory vectors
        printf("[DIAG] SonyVec($226)=%08X\n", direct_read_32(0x226));
        printf("[DIAG] jSonyPatch($B40)=%08X\n", direct_read_32(0xB40));
        printf("[DIAG] IWMBase($1E0)=%08X\n", direct_read_32(0x1E0));
        printf("[DIAG] VIABase($1D4)=%08X\n", direct_read_32(0x1D4));
        printf("[DIAG] SonyVars($134)=%08X\n", direct_read_32(0x134));
        printf("[DIAG] JIODone($8FC)=%08X\n", direct_read_32(0x8FC));
        printf("[DIAG] DskErr($142)=%04X\n", direct_read_16(0x142));
        // Dump stack (using direct reads — safe from callback context)
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
        printf("[DIAG] SP=%08X stack:", sp);
        for (int i = 0; i < 12; i++)
          printf(" %04X", direct_read_16(sp + i * 2));
        printf("\n");
        // SonyVars structure (first 32 bytes)
        uint32_t sv = direct_read_32(0x134);
        if (sv) {
          printf("[DIAG] SonyVars @%08X:", sv);
          for (int i = 0; i < 32; i += 2)
            printf(" %04X", direct_read_16(sv + i));
          printf("\n");
        }
        // Dump .Sony driver patch code in RAM (SonyPatch vector points here)
        {
          uint32_t spatch = direct_read_32(0xB40);
          printf("[DIAG] SonyPatch target=%08X\n", spatch);
          if (spatch && spatch < 0x400000) {
            // Dump 256 bytes of code starting 64 bytes before the patch target
            uint32_t dump_start = (spatch > 0x40) ? (spatch - 0x40) : 0;
            printf("[DIAG] RAM code dump %06X-%06X:\n", dump_start, dump_start + 0xFF);
            for (uint32_t a = dump_start; a < dump_start + 0x100; a += 16) {
              printf("[DIAG] %06X:", a);
              for (int j = 0; j < 16; j += 2)
                printf(" %04X", direct_read_16(a + j));
              printf("\n");
            }
          }
          // Also dump code around the PCs we saw in the stuck loop ($13100-$13200)
          printf("[DIAG] RAM code dump 013100-0131FF:\n");
          for (uint32_t a = 0x13100; a < 0x13200; a += 16) {
            printf("[DIAG] %06X:", a);
            for (int j = 0; j < 16; j += 2)
              printf(" %04X", direct_read_16(a + j));
            printf("\n");
          }
        }
        // Dump captured IWM reads (ring buffer — most recent IWM_LOG_SIZE entries)
        {
          unsigned int n = (iwm_log_head < IWM_LOG_SIZE) ? iwm_log_head : IWM_LOG_SIZE;
          unsigned int start = (iwm_log_head < IWM_LOG_SIZE) ? 0 : (iwm_log_head - IWM_LOG_SIZE);
          printf("[DIAG] IWM total=%u showing last %u\n", iwm_log_total, n);
          for (unsigned int i = 0; i < n; i++) {
            unsigned int idx = (start + i) % IWM_LOG_SIZE;
            uint32_t reg = (iwm_log_buf[idx].addr - 0xDFE1FF) >> 9;
            if (iwm_log_buf[idx].caller) {
              uint32_t rsp = iwm_log_buf[idx].caller & 0xFFFFFF;
              printf("[IWM] #%u reg%u addr=%06X val=%02X pc=%08X sp=%06X stk=%04X/%04X/%04X/%04X\n",
                     start + i, reg, iwm_log_buf[idx].addr, iwm_log_buf[idx].val,
                     iwm_log_buf[idx].pc, rsp,
                     direct_read_16(rsp), direct_read_16(rsp + 2),
                     direct_read_16(rsp + 4), direct_read_16(rsp + 6));
            } else
              printf("[IWM] #%u reg%u addr=%06X val=%02X pc=%08X\n",
                     start + i, reg, iwm_log_buf[idx].addr, iwm_log_buf[idx].val,
                     iwm_log_buf[idx].pc);
          }
        }
        sound_diag_done = 1;
      }
    }

    static unsigned long hb_cnt = 0;
    if ((hb_cnt++ & 0xFFFFF) == 0) {
      uint32_t hb_pc = m68k_get_reg(NULL, M68K_REG_PC);
      // Read Ticks from fast-path buffer (not GPIO slow path!)
      uint32_t ticks = 0;
      {
        uint32_t ta = 0x16A;
        for (int r = 0; r < m68ki_cpu.read_ranges; r++) {
          if (ta >= m68ki_cpu.read_addr[r] && ta + 4 <= m68ki_cpu.read_upper[r]) {
            unsigned char *p = m68ki_cpu.read_data[r] + (ta - m68ki_cpu.read_addr[r]);
            ticks = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
            break;
          }
        }
      }
      static uint32_t prev_ticks = 0;
      static struct timespec prev_ts = {0, 0};
      struct timespec now_ts;
      clock_gettime(CLOCK_MONOTONIC, &now_ts);
      double elapsed = (now_ts.tv_sec - prev_ts.tv_sec) + (now_ts.tv_nsec - prev_ts.tv_nsec) / 1e9;
      uint32_t delta_ticks = ticks - prev_ticks;
      double vbl_hz = (elapsed > 0.01 && prev_ts.tv_sec > 0) ? delta_ticks / elapsed : 0.0;
      printf("[HB] pc=%08X ticks=%08X (+%u in %.2fs = %.1f Hz) ack=%u ifrR=%u\n",
             hb_pc, ticks, delta_ticks, elapsed, vbl_hz, dbg_irq_ack_count, via_ifr_reads);
      prev_ticks = ticks;
      prev_ts = now_ts;
      static unsigned int prev_ca1 = 0;
      static unsigned int prev_slowio = 0;
      double ca1_hz = (elapsed > 0.01 && vbl_hz > 0) ? (via_ifr_bits[1] - prev_ca1) / elapsed : 0.0;
      printf("[IFR] CA2=%u CA1=%u(%.1fHz) SR=%u CB2=%u CB1=%u T2=%u T1=%u slowio=%u(+%u)\n",
             via_ifr_bits[0], via_ifr_bits[1], ca1_hz, via_ifr_bits[2],
             via_ifr_bits[3], via_ifr_bits[4], via_ifr_bits[5],
             via_ifr_bits[6], slowio_hit_count, slowio_hit_count - prev_slowio);
      prev_ca1 = via_ifr_bits[1];
      prev_slowio = slowio_hit_count;
#ifdef DEBUG_MAC_IO
      printf("[IER] last=%02X reads=%u writes=%u T1en=%u T2en=%u\n",
             via_ier_last, via_ier_reads, via_ier_writes,
             via_ier_t1_enabled, via_ier_t2_enabled);
      printf("[VIA-T1] CL_w=%u CH_w=%u\n", via_t1cl_writes, via_t1ch_writes);
      {
        static unsigned int prev_pacedio = 0;
        printf("[RD8] total=%u hi=%u via=%u iwm=%u scsiR=%u scsiW=%u pacedio=%u(+%u)\n",
               rd8_total, rd8_hi, rd8_via, rd8_iwm, rd8_scsi, wr8_scsi,
               pacedio_hit_count, pacedio_hit_count - prev_pacedio);
        prev_pacedio = pacedio_hit_count;
      }
      { unsigned int ss = (iwm_ph[3]<<3)|(iwm_ph[2]<<2)|(iwm_ph[1]<<1)|iwm_ph[0];
        printf("[IWM-MODE] data=%u status=%u hshk=%u write=%u Q6=%d Q7=%d ph=%d%d%d%d(%s) total=%u\n",
               iwm_mode_rd[0], iwm_mode_rd[1], iwm_mode_rd[2], iwm_mode_rd[3],
               iwm_q6, iwm_q7, iwm_ph[3], iwm_ph[2], iwm_ph[1], iwm_ph[0],
               iwm_sense_names[ss], iwm_log_total);
      }
      {
        uint32_t vbl_proc = m68k_read_memory_32(0x08EE);
        uint16_t ier_hw = ps_read_8(0xEFFDFE);
        uint16_t ifr_hw = ps_read_8(0xEFFBFE);
        printf("[SYS] VBLproc=$%08X IER_hw=$%02X IFR_hw=$%02X\n", vbl_proc, ier_hw, ifr_hw);
        // Read SCSI 5380 registers directly to see bus state
        uint8_t scsi_csbs = ps_read_8(0x5FF040);  // Current SCSI Bus Status
        uint8_t scsi_bsr  = ps_read_8(0x5FF050);  // Bus and Status Register
        uint8_t scsi_icr  = ps_read_8(0x5FF010);  // Initiator Command Register (reg 1)
        uint8_t scsi_mr   = ps_read_8(0x5FF020);  // Mode Register (reg 2)
        uint8_t scsi_tcr  = ps_read_8(0x5FF030);  // Target Command Register (reg 3)
        uint8_t scsi_csd  = ps_read_8(0x5FF000);  // Current SCSI Data
        printf("[SCSI-HW] CSBS=%02X BSR=%02X ICR=%02X MR=%02X TCR=%02X CSD=%02X last_csbs=%02X(x%u) trans=%u\n",
               scsi_csbs, scsi_bsr, scsi_icr, scsi_mr, scsi_tcr, scsi_csd,
               scsi_csbs_last, scsi_csbs_repeat, csbs_trans_count);
      }
#endif
      // Dump key low-memory globals after system has booted (wait for IRQ activity)
      {
        static int globals_dumped = 0;
        if (!globals_dumped && dbg_irq_ack_count > 500) {
          globals_dumped = 1;
          // Read from fast-path buffer (what CPU sees) AND GPIO (what hardware has)
          printf("[LOWMEM] Exception vectors — buffer vs GPIO:\n");
          for (int i = 0; i < 16; i++) {
            uint32_t buf_val = 0, gpio_val = 0;
            uint32_t addr = i * 4;
            // Buffer: scan read ranges for this address
            for (int r = 0; r < m68ki_cpu.read_ranges; r++) {
              if (addr >= m68ki_cpu.read_addr[r] && addr + 4 <= m68ki_cpu.read_upper[r]) {
                unsigned char *p = m68ki_cpu.read_data[r] + (addr - m68ki_cpu.read_addr[r]);
                buf_val = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
                break;
              }
            }
            // GPIO
            gpio_val = (ps_read_8(addr)<<24)|(ps_read_8(addr+1)<<16)|(ps_read_8(addr+2)<<8)|ps_read_8(addr+3);
            printf("  $%02X: buf=%08X gpio=%08X%s\n", addr, buf_val, gpio_val,
                   (buf_val != gpio_val) ? " MISMATCH" : "");
          }
          // Helper: read 32-bit big-endian from fast-path buffer
          #define BUF_READ32(a) ({ \
            uint32_t _v = 0; \
            for (int _r = 0; _r < m68ki_cpu.read_ranges; _r++) { \
              if ((a) >= m68ki_cpu.read_addr[_r] && (a) + 4 <= m68ki_cpu.read_upper[_r]) { \
                unsigned char *_p = m68ki_cpu.read_data[_r] + ((a) - m68ki_cpu.read_addr[_r]); \
                _v = (_p[0]<<24)|(_p[1]<<16)|(_p[2]<<8)|_p[3]; \
                break; \
              } \
            } _v; })
          #define BUF_READ16(a) ({ \
            uint16_t _v = 0; \
            for (int _r = 0; _r < m68ki_cpu.read_ranges; _r++) { \
              if ((a) >= m68ki_cpu.read_addr[_r] && (a) + 2 <= m68ki_cpu.read_upper[_r]) { \
                unsigned char *_p = m68ki_cpu.read_data[_r] + ((a) - m68ki_cpu.read_addr[_r]); \
                _v = (_p[0]<<8)|_p[1]; \
                break; \
              } \
            } _v; })

          // Key system globals (buf only)
          printf("[LOWMEM] Lvl1vec ($064): buf=%08X\n", BUF_READ32(0x64));
          printf("[LOWMEM] Lvl2vec ($068): buf=%08X\n", BUF_READ32(0x68));
          printf("[LOWMEM] Ticks   ($16A): buf=%08X\n", BUF_READ32(0x16A));
          printf("[LOWMEM] VBLQueue($160): buf=%08X\n", BUF_READ32(0x160));

          // Sony/IWM-related globals
          printf("[LOWMEM] SonyVars ($134): buf=%08X\n", BUF_READ32(0x134));
          printf("[LOWMEM] DskErr   ($142): buf=%04X\n", BUF_READ16(0x142));
          printf("[LOWMEM] IWM      ($1E0): buf=%08X\n", BUF_READ32(0x1E0));
          printf("[LOWMEM] JFetch   ($226): buf=%08X\n", BUF_READ32(0x226));
          printf("[LOWMEM] JIODone  ($22E): buf=%08X\n", BUF_READ32(0x22E));
          printf("[LOWMEM] DrvQHdr  ($308): buf=%04X %08X %08X\n",
                 BUF_READ16(0x308), BUF_READ32(0x30A), BUF_READ32(0x30E));
          printf("[LOWMEM] SdVolume ($260): buf=%02X\n", (uint8_t)(BUF_READ16(0x260) >> 8));
          printf("[LOWMEM] MemTop   ($108): buf=%08X\n", BUF_READ32(0x108));
          printf("[LOWMEM] BufPtr   ($10C): buf=%08X\n", BUF_READ32(0x10C));
          printf("[LOWMEM] SysZone  ($2A6): buf=%08X\n", BUF_READ32(0x2A6));
          printf("[LOWMEM] ApplZone ($2AA): buf=%08X\n", BUF_READ32(0x2AA));
          printf("[LOWMEM] HeapEnd  ($114): buf=%08X\n", BUF_READ32(0x114));
          printf("[LOWMEM] ScrnBase ($824): buf=%08X\n", BUF_READ32(0x824));
          printf("[LOWMEM] SCSIBase ($0C00):buf=%08X\n", BUF_READ32(0xC00));

          // Lvl1DT dispatch table
          printf("[LOWMEM] Lvl1DT ($192): buf=");
          for (int i = 0; i < 7; i++) {
            printf(" %08X", BUF_READ32(0x192 + i*4));
          }
          printf("\n");

          // Dump active fast-path ranges for sanity
          printf("[RANGES] read=%d write=%d\n", m68ki_cpu.read_ranges, m68ki_cpu.write_ranges);
          for (int i = 0; i < m68ki_cpu.read_ranges; i++) {
            printf("[RANGE-R%d] %08X-%08X data=%p\n", i,
                   m68ki_cpu.read_addr[i], m68ki_cpu.read_upper[i],
                   (void*)m68ki_cpu.read_data[i]);
          }
          for (int i = 0; i < m68ki_cpu.write_ranges; i++) {
            printf("[RANGE-W%d] %08X-%08X data=%p wtc=%d\n", i,
                   m68ki_cpu.write_addr[i], m68ki_cpu.write_upper[i],
                   (void*)m68ki_cpu.write_data[i],
                   m68ki_cpu.write_through[i]);
          }
          fflush(stdout);

          #undef BUF_READ32
          #undef BUF_READ16
        }
      }
    }
  }
#endif

  if (atomic_load(&irq)) {
    atomic_store(&irq, 0);
    // Read the actual IPL level from the CPLD status register.
    // Previously hardcoded to level 1, but Mac SE has multiple
    // interrupt sources at different levels (VIA=1, SCC=2/4).
    // Routing non-VIA interrupts to the VIA ISR causes the
    // dispatcher to fall off the Lvl1DT table into garbage.
    unsigned int status = ps_read_status_reg();
    unsigned int ipl = (status & 0xe000) >> 13;
    if (ipl > 0) {
      M68K_SET_IRQ(ipl);
      last_last_irq = ipl;
#ifdef DEBUG_DIAG
      dbg_cpu_irq++;
#endif
    }
  } else if (last_last_irq != 0) {
    // Deassertion: check GPIO pin directly (no bus operation)
    uint32_t gpio_val = *(gpio + 13);
    if (!(gpio_val & (1 << PIN_TXN_IN_PROGRESS)) &&
        (gpio_val & (1 << PIN_IPL_ZERO))) {
      // IPL pin is high (no interrupt) — clear CPU level
      M68K_SET_IRQ(0);
      last_last_irq = 0;
#ifdef DEBUG_DIAG
      dbg_cpu_deassert++;
#endif
    }
  }

  if (mouse_hook_enabled && (mouse_extra != 0x00)) {
    // dampen the scroll wheel until next while loop iteration
    mouse_extra = 0x00;
  }

  if (load_new_config) {
    printf("[CPU] Loading new config file.\n");
    goto stop_cpu_emulation;
  }

  if (end_signal)
	  goto stop_cpu_emulation;

  goto cpu_loop;

stop_cpu_emulation:
  printf("[CPU] End of CPU thread\n");
  return (void *)NULL;
}

void *keyboard_task() {
  struct pollfd kbdpoll[1];
  int kpollrc;
  char c = 0, c_code = 0, c_type = 0;
  char grab_message[] = "[KBD] Grabbing keyboard from input layer",
       ungrab_message[] = "[KBD] Ungrabbing keyboard";

  printf("[KBD] Keyboard thread started\n");

  // because we permit the keyboard to be grabbed on startup, quickly check if we need to grab it
  if (kb_hook_enabled && cfg->keyboard_grab) {
    puts(grab_message);
    grab_device(keyboard_fd);
  }

  kbdpoll[0].fd = keyboard_fd;
  kbdpoll[0].events = POLLIN;

key_loop:
  kpollrc = poll(kbdpoll, 1, KEY_POLL_INTERVAL_MSEC);
  if ((kpollrc > 0) && (kbdpoll[0].revents & POLLHUP)) {
    // in the event that a keyboard is unplugged, keyboard_task will whiz up to 100% utilisation
    // this is undesired, so if the keyboard HUPs, end the thread without ending the emulation
    printf("[KBD] Keyboard node returned HUP (unplugged?)\n");
    goto key_end;
  }

  // if kpollrc > 0 then it contains number of events to pull, also check if POLLIN is set in revents
  if ((kpollrc <= 0) || !(kbdpoll[0].revents & POLLIN)) {
    goto key_loop;
  }

  while (get_key_char(&c, &c_code, &c_type)) {
    if (c && c == cfg->keyboard_toggle_key && !kb_hook_enabled) {
      kb_hook_enabled = 1;
      printf("[KBD] Keyboard hook enabled.\n");
      if (cfg->keyboard_grab) {
        grab_device(keyboard_fd);
        puts(grab_message);
      }
    } else if (kb_hook_enabled) {
      if (c == 0x1B && c_type) {
        kb_hook_enabled = 0;
        printf("[KBD] Keyboard hook disabled.\n");
        if (cfg->keyboard_grab) {
          release_device(keyboard_fd);
          puts(ungrab_message);
        }
      } else {
        queue_keypress(c_code, c_type, cfg->platform->id);
      }
    }

    // pause pressed; trigger nmi (int level 7)
    if (c == 0x01 && c_type) {
      printf("[INT] Sending NMI\n");
      M68K_SET_IRQ(7);
    }

    if (!kb_hook_enabled && c_type) {
      if (c && c == cfg->mouse_toggle_key) {
        mouse_hook_enabled ^= 1;
        printf("Mouse hook %s.\n", mouse_hook_enabled ? "enabled" : "disabled");
        mouse_dx = mouse_dy = mouse_buttons = mouse_extra = 0;
      }
      if (c == 'r') {
        cpu_emulation_running ^= 1;
        printf("CPU emulation is now %s\n", cpu_emulation_running ? "running" : "stopped");
      }
      if (c == 'g') {
        realtime_graphics_debug ^= 1;
        printf("Real time graphics debug is now %s\n", realtime_graphics_debug ? "on" : "off");
      }
      if (c == 'R') {
        cpu_pulse_reset();
        //m68k_pulse_reset();
        printf("CPU emulation reset.\n");
      }
      if (c == 'q') {
        printf("Quitting and exiting emulator.\n");
	      end_signal = 1;
        goto key_end;
      }
      if (c == 'd') {
        realtime_disassembly ^= 1;
        do_disasm = 1;
        printf("Real time disassembly is now %s\n", realtime_disassembly ? "on" : "off");
      }
      if (c == 'D') {
        int r = get_mapped_item_by_address(cfg, 0x08000000);
        if (r != -1) {
          printf("Dumping first 16MB of mapped range %d.\n", r);
          FILE *dmp = fopen("./memdmp.bin", "wb+");
          fwrite(cfg->map_data[r], 16 * SIZE_MEGA, 1, dmp);
          fclose(dmp);
        }
      }
      if (c == 's' && realtime_disassembly) {
        do_disasm = 1;
      }
      if (c == 'S' && realtime_disassembly) {
        do_disasm = 128;
      }
    }
  }

  goto key_loop;

key_end:
  printf("[KBD] Keyboard thread ending\n");
  if (cfg->keyboard_grab) {
    puts(ungrab_message);
    release_device(keyboard_fd);
  }
  return (void*)NULL;
}

void stop_cpu_emulation(uint8_t disasm_cur) {
  M68K_END_TIMESLICE;
  if (disasm_cur) {
    m68k_disassemble(disasm_buf, m68k_get_reg(NULL, M68K_REG_PC), cpu_type);
    printf("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3), \
            m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5), m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    printf("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3), \
            m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5), m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
    printf("%.8X (%.8X)]] %s\n", m68k_get_reg(NULL, M68K_REG_PC), (m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF), disasm_buf);
    realtime_disassembly = 1;
  }

  cpu_emulation_running = 0;
  do_disasm = 0;
}

void sigusr1_handler(int sig_num) {
  (void)sig_num;
  mode32_trigger = 1;
}

void sigint_handler(int sig_num) {
  printf("Received sigint %d, exiting.\n", sig_num);

  /* Dump PC trace ring buffer — shows where CPU was when we hit Ctrl-C */
  {
    m68ki_cpu_core *state = &m68ki_cpu;
    printf("PC trace (oldest → newest):\n ");
    for (int i = 0; i < 32; i++) {
      int idx = (state->pc_trace_idx + i) & 31;
      printf(" %08X", state->pc_trace[idx]);
      if ((i & 7) == 7 && i < 31) printf("\n ");
    }
    printf("\n");
    printf("Current PC: %08X  SR: %04X\n",
           m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF,
           m68k_get_reg(NULL, M68K_REG_SR));
    printf("D0=%08X D1=%08X D2=%08X D3=%08X\n",
           m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
           m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3));
    printf("D4=%08X D5=%08X D6=%08X D7=%08X\n",
           m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
           m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
    printf("A0=%08X A1=%08X A2=%08X A3=%08X\n",
           m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
           m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3));
    printf("A4=%08X A5=%08X A6=%08X A7=%08X\n",
           m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
           m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
  }
  vnc_stop();

  if (mouse_fd != -1)
    close(mouse_fd);
  if (mem_fd)
    close(mem_fd);

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  while (!emulator_exiting) {
    emulator_exiting = 1;
    usleep(0);
  }

#ifdef DEBUG_DIAG
  printf("IPL assertions: %u\n", atomic_load(&dbg_ipl_assert));
  printf("CPU IRQ set: %u\n", dbg_cpu_irq);
  printf("CPU IRQ ack: %u\n", dbg_irq_ack_count);
  printf("CPU IRQ deassert: %u\n", dbg_cpu_deassert);
  printf("VIA IFR reads: %u\n", via_ifr_reads);
  printf("VIA IFR bits: CA2=%u CA1=%u SR=%u CB2=%u CB1=%u T2=%u T1=%u\n",
         via_ifr_bits[0], via_ifr_bits[1], via_ifr_bits[2],
         via_ifr_bits[3], via_ifr_bits[4], via_ifr_bits[5],
         via_ifr_bits[6]);
#endif

  pc_trace_flush();
  exit(0);
}

static char cfg_filename[256] = "";

int main(int argc, char *argv[]) {
  int g;

  ps_setup_protocol();

  //const struct sched_param priority = {99};

  // Some command line switch stuffles
  for (g = 1; g < argc; g++) {
    if (strcmp(argv[g], "--cpu_type") == 0 || strcmp(argv[g], "--cpu") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no CPU type specified.\n", argv[g]);
      } else {
        g++;
        cpu_type = get_m68k_cpu_type(argv[g]);
      }
    }
    else if (strcmp(argv[g], "--config-file") == 0 || strcmp(argv[g], "--config") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no config filename specified.\n", argv[g]);
      } else {
        g++;
        FILE *chk = fopen(argv[g], "rb");
        if (chk == NULL) {
          printf("Config file %s does not exist, please check that you've specified the path correctly.\n", argv[g]);
        } else {
          fclose(chk);
          load_new_config = 1;
          strncpy(cfg_filename, argv[g], sizeof(cfg_filename) - 1);
        }
      }
    }
    else if (strcmp(argv[g], "--keyboard-file") == 0 || strcmp(argv[g], "--kbfile") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no keyboard device path specified.\n", argv[g]);
      } else {
        g++;
        strcpy(keyboard_file, argv[g]);
      }
    }
  }

switch_config:
  srand(clock());

  ps_reset_state_machine();
  ps_pulse_reset();
  usleep(1500);

  if (load_new_config != 0) {
    load_new_config = 0;
    if (cfg) {
      free_config_file(cfg);
      free(cfg);
      cfg = NULL;
    }

    if (cfg_filename[0])
      cfg = load_config_file(cfg_filename);
    else
      cfg = load_config_file("default.cfg");
  }

  if (!cfg) {
    printf("No config file specified. Trying to load default.cfg...\n");
    cfg = load_config_file("default.cfg");
    if (!cfg) {
      printf("Couldn't load default.cfg, empty emulator config will be used.\n");
      cfg = (struct emulator_config *)calloc(1, sizeof(struct emulator_config));
      if (!cfg) {
        printf("Failed to allocate memory for emulator config!\n");
        return 1;
      }
      memset(cfg, 0x00, sizeof(struct emulator_config));
    }
  }

  if (cfg) {
    if (cfg->cpu_type) cpu_type = cfg->cpu_type;
    if (cfg->loop_cycles) loop_cycles = cfg->loop_cycles;

    if (!cfg->platform)
      cfg->platform = make_platform_config("none", "generic");
    cfg->platform->platform_initial_setup(cfg);

    // Scan config for slow IO regions
    slowio_count = 0;
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS && slowio_count < MAX_SLOWIO_REGIONS; i++) {
      if (cfg->map_type[i] == MAPTYPE_SLOWIO) {
        slowio[slowio_count].lo = cfg->map_offset[i];
        slowio[slowio_count].hi = cfg->map_high[i];
        slowio[slowio_count].delay = cfg->map_delay[i] ? (int)cfg->map_delay[i] : SLOWIO_DEFAULT_DELAY;
        printf("[SLOWIO] Region %d: %08X-%08X delay=%d\n", slowio_count,
               slowio[slowio_count].lo, slowio[slowio_count].hi,
               slowio[slowio_count].delay);
        slowio_count++;
      }
    }
    if (slowio_count)
      printf("[SLOWIO] %d slow IO region(s) active — bus cycle delay enabled\n", slowio_count);

    // Scan config for paced IO regions
    pacedio_count = 0;
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS && pacedio_count < MAX_PACEDIO_REGIONS; i++) {
      if (cfg->map_type[i] == MAPTYPE_PACEDIO) {
        pacedio[pacedio_count].lo = cfg->map_offset[i];
        pacedio[pacedio_count].hi = cfg->map_high[i];
        pacedio[pacedio_count].extra_cycles = cfg->map_delay[i];
        printf("[PACEDIO] Region %d: %08X-%08X extra_cycles=%d\n", pacedio_count,
               pacedio[pacedio_count].lo, pacedio[pacedio_count].hi,
               pacedio[pacedio_count].extra_cycles);
        pacedio_count++;
      }
    }
    if (pacedio_count)
      printf("[PACEDIO] %d paced IO region(s) active — stretched bus cycles\n", pacedio_count);
  }

  if (cfg->mouse_enabled) {
    mouse_fd = open(cfg->mouse_file, O_RDWR | O_NONBLOCK);
    if (mouse_fd == -1) {
      printf("Failed to open %s, can't enable mouse hook.\n", cfg->mouse_file);
      cfg->mouse_enabled = 0;
    } else {
      /**
       * *-*-*-* magic numbers! *-*-*-*
       * great, so waaaay back in the history of the pc, the ps/2 protocol set the standard for mice
       * and in the process, the mouse sample rate was defined as a way of putting mice into vendor-specific modes.
       * as the ancient gpm command explains, almost everything except incredibly old mice talk the IntelliMouse
       * protocol, which reports four bytes. by default, every mouse starts in 3-byte mode (don't report wheel or
       * additional buttons) until imps2 magic is sent. so, command $f3 is "set sample rate", followed by a byte.
       */
      uint8_t mouse_init[] = { 0xf4, 0xf3, 0x64 }; // enable, then set sample rate 100
      uint8_t imps2_init[] = { 0xf3, 0xc8, 0xf3, 0x64, 0xf3, 0x50 }; // magic sequence; set sample 200, 100, 80
      if (write(mouse_fd, mouse_init, sizeof(mouse_init)) != -1) {
        if (write(mouse_fd, imps2_init, sizeof(imps2_init)) == -1)
          printf("[MOUSE] Couldn't enable scroll wheel events; is this mouse from the 1980s?\n");
      } else
        printf("[MOUSE] Mouse didn't respond to normal PS/2 init; have you plugged a brick in by mistake?\n");
    }
  }

  if (cfg->keyboard_file)
    keyboard_fd = open(cfg->keyboard_file, O_RDONLY | O_NONBLOCK);
  else
    keyboard_fd = open(keyboard_file, O_RDONLY | O_NONBLOCK);

  if (keyboard_fd == -1) {
    printf("Failed to open keyboard event source.\n");
  }

  if (cfg->mouse_autoconnect)
    mouse_hook_enabled = 1;

  if (cfg->keyboard_autoconnect)
    kb_hook_enabled = 1;

  /* PC trace init: PC_TRACE=file.bin PC_TRACE_LIMIT=N (default 10M) */
  pc_trace_path = getenv("PC_TRACE");
  if (pc_trace_path) {
    const char *lim_str = getenv("PC_TRACE_LIMIT");
    pc_trace_limit = lim_str ? (uint32_t)atol(lim_str) : 10000000;
    pc_trace_buf = malloc(pc_trace_limit * sizeof(uint32_t));
    if (pc_trace_buf) {
      printf("[PC-TRACE] Recording up to %u instructions to %s\n",
             pc_trace_limit, pc_trace_path);
    } else {
      printf("[PC-TRACE] Failed to allocate %u MB buffer\n",
             (pc_trace_limit * 4) >> 20);
      pc_trace_limit = 0;
    }
  }

  signal(SIGINT, sigint_handler);
  signal(SIGUSR1, sigusr1_handler);

  ps_reset_state_machine();
  ps_pulse_reset();
  usleep(1500);

  m68k_init();
  printf("Setting CPU type to %d.\n", cpu_type);
	m68k_set_cpu_type(&m68ki_cpu, cpu_type);
  printf("[CPU] address_mask = %08X\n", m68ki_cpu.address_mask);
  cpu_pulse_reset();

  pthread_t ipl_tid = 0, cpu_tid, kbd_tid;
  int err;
  if (ipl_tid == 0) {
    err = pthread_create(&ipl_tid, NULL, &ipl_task, NULL);
    if (err != 0)
      printf("[ERROR] Cannot create IPL thread: [%s]", strerror(err));
    else {
      pthread_setname_np(ipl_tid, "pistorm: ipl");
      printf("IPL thread created successfully\n");
    }
  }

  // create keyboard task
  err = pthread_create(&kbd_tid, NULL, &keyboard_task, NULL);
  if (err != 0)
    printf("[ERROR] Cannot create keyboard thread: [%s]", strerror(err));
  else {
    pthread_setname_np(kbd_tid, "pistorm: kbd");
    printf("[MAIN] Keyboard thread created successfully\n");
  }

  // create cpu task
  err = pthread_create(&cpu_tid, NULL, &cpu_task, NULL);
  if (err != 0)
    printf("[ERROR] Cannot create CPU thread: [%s]", strerror(err));
  else {
    pthread_setname_np(cpu_tid, "pistorm: cpu");
    printf("[MAIN] CPU thread created successfully\n");
  }

  if (vnc_cfg.enabled) {
    int ram_idx = get_named_mapped_item(cfg, "sysram");
    if (ram_idx != -1) {
      vnc_cfg.ram_base = cfg->map_data[ram_idx];
      vnc_cfg.ram_size = cfg->map_size[ram_idx];
      vnc_start(&vnc_cfg);
    } else {
      printf("[VNC] No sysram mapped — VNC disabled\n");
    }
  }

  // wait for cpu task to end before closing up and finishing
  pthread_join(cpu_tid, NULL);

  while (!emulator_exiting) {
    emulator_exiting = 1;
    usleep(0);
  }

  if (load_new_config == 0)
    printf("[MAIN] All threads appear to have concluded; ending process\n");

  if (mouse_fd != -1)
    close(mouse_fd);
  if (mem_fd)
    close(mem_fd);

  if (load_new_config != 0)
    goto switch_config;

  vnc_stop();

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  return 0;
}

void cpu_pulse_reset(void) {
	m68ki_cpu_core *state = &m68ki_cpu;
  ps_pulse_reset();

  ovl = 1;
  m68ki_cpu.ovl = 1;
  for (int i = 0; i < 8; i++) {
    ipl_enabled[i] = 0;
  }

  if (cfg->platform->handle_reset)
    cfg->platform->handle_reset(cfg);

	m68k_pulse_reset(state);
}

unsigned int cpu_irq_ack(int level) {
#ifdef DEBUG_DIAG
  dbg_irq_ack_count++;
  if (dbg_irq_ack_count <= 3)
    printf("[IRQ-ACK] level=%d count=%u\n", level, dbg_irq_ack_count);
#endif

  // Clear the pending interrupt level after acknowledgment.
  // Without this, CPU_INT_LEVEL stays at 0x100 during the entire
  // m68k_execute() call. After RTE restores the SR (mask=0),
  // m68ki_check_interrupts() sees 0x100 > 0x000 and immediately
  // re-enters the ISR — trapping the CPU in an interrupt loop.
  // The IPL polling thread will re-assert if the VIA still has
  // active interrupt sources (level-triggered).
  CPU_INT_LEVEL = 0;
  last_last_irq = 0;

  return 24 + level;
}

static unsigned int target = 0;
static uint32_t platform_res;

unsigned int garbage = 0;

static inline int32_t platform_read_check(uint8_t type, uint32_t addr, uint32_t *res) {
  /* Custom I/O handlers use 24-bit SE bus addresses; mapped buffers use
   * full 32-bit so 32MB RAM doesn't shadow I/O (see huge-se VIA fix). */
  uint32_t bus24 = addr & 0x00FFFFFF;
  switch (cfg->platform->id) {
    case PLATFORM_MAC:
      /* Mac SE BBU clears OVL on first access to ROM/SCSI range */
      if (ovl && bus24 >= (ovl_sysrom_pos & 0x00FFFFFF) &&
          bus24 < (ovl_sysrom_pos & 0x00FFFFFF) + 0x100000) {
        ovl = 0;
        m68ki_cpu.ovl = 0;
        printf("[MAC] OVL off (read from ROM/SCSI range %08X).\n", bus24);
        handle_ovl_mappings_mac68k(cfg);
      }
      /* Custom read handler (Big SE SCSI remap, etc.) — uses 24-bit addr */
      if (cfg->platform->custom_read &&
          cfg->platform->custom_read(cfg, bus24, &target, type) != -1) {
        *res = target;
        return 1;
      }
      break;
    default:
      break;
  }

  if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high)) {
    if (handle_mapped_read(cfg, addr, &target, type) != -1) {
      *res = target;
      return 1;
    }
  }

  return 0;
}

unsigned int m68k_read_memory_8(unsigned int address) {
#ifdef DEBUG_DIAG
  rd8_total++;
#endif

  /* Use full 32-bit address for buffer/range checks so that 32MB RAM
   * ($00000000-$01FFFFFF) doesn't shadow I/O addresses like VIA ($EFxxxx).
   * With PMMU, physical VIA is $40EFxxxx which correctly misses the RAM range.
   * Mask to 24-bit only for the SE bus I/O path below. */
  uint32_t bus_addr = address & 0x00FFFFFF;

  if (platform_read_check(OP_TYPE_BYTE, address, &platform_res)) {
    return platform_res;
  }

#ifdef DEBUG_DIAG
  if (bus_addr >= 0x800000) rd8_hi++;
  if (bus_addr >= 0xDFE1FF && bus_addr <= 0xDFFFFF) rd8_iwm++;
  if (bus_addr >= 0xEFE1FE && bus_addr <= 0xEFFFFF) rd8_via++;
  if (bus_addr >= 0x580000 && bus_addr <= 0x5FFFFF)
    rd8_scsi++;
#endif

  // noscsi bypass: return 0 for all SCSI reads without hitting GPIO
  if (noscsi_enabled && bus_addr >= 0x580000 && bus_addr <= 0x5FFFFF) {
    return 0;
  }

  { int _sd = slowio_get_delay(bus_addr); if (_sd) slowio_delay(_sd); }

  unsigned int val;
  int paced = is_pacedio(bus_addr);
  if (paced) {
#ifdef DEBUG_DIAG
    pacedio_hit_count++;
#endif
    val = (unsigned int)ps_read_8_paced(bus_addr);
    for (int _i = 1; _i < paced; _i++)
      paced_dummy_cycle_1();
  } else {
    val = (unsigned int)ps_read_8(bus_addr);
  }

#ifdef DEBUG_DIAG
  if (address >= 0x580000 && address <= 0x5FFFFF) {
    // SCSI read detail printing disabled — too noisy during IWM debugging
    // Ring buffer: suppress repetitive CSBS polling (only log transitions)
    int do_log = 1;
    if (address == 0x5FF040) {
      if (csbs_tracking_active) {
        csbs_trans_polls++;
        if ((val & 0xFF) != scsi_csbs_last && csbs_trans_count < CSBS_TRANS_MAX) {
          // Read BSR and MR at transition time (single extra read each)
          uint8_t bsr_snap = ps_read_8(0x5FF050);
          uint8_t mr_snap = ps_read_8(0x5FF020);
          csbs_trans[csbs_trans_count].csbs = val & 0xFF;
          csbs_trans[csbs_trans_count].bsr = bsr_snap;
          csbs_trans[csbs_trans_count].mr = mr_snap;
          csbs_trans[csbs_trans_count].poll_count = csbs_trans_polls;
          csbs_trans_count++;
          csbs_trans_polls = 0;
          printf("[CSBS-TRANS] #%u csbs=%02X bsr=%02X mr=%02X after %u polls\n",
                 csbs_trans_count, val & 0xFF, bsr_snap, mr_snap,
                 csbs_trans[csbs_trans_count-1].poll_count);
          if ((val & 0xFF) == 0x00) {
            printf("[CSBS-TRANS] BUS FREE detected! Dumping all %u transitions:\n", csbs_trans_count);
            for (unsigned int t = 0; t < csbs_trans_count; t++) {
              printf("  T%u: csbs=%02X bsr=%02X mr=%02X after %u polls\n",
                     t, csbs_trans[t].csbs, csbs_trans[t].bsr, csbs_trans[t].mr, csbs_trans[t].poll_count);
            }
            csbs_tracking_active = 0;
          }
        }
      }
      if ((val & 0xFF) == scsi_csbs_last) {
        scsi_csbs_repeat++;
        do_log = 0;
      } else {
        scsi_csbs_last = val & 0xFF;
        scsi_csbs_repeat = 0;
      }
    }
    if (do_log) {
      unsigned int idx = scsi_log_head % SCSI_LOG_SIZE;
      scsi_log_buf[idx].addr = address;
      scsi_log_buf[idx].val = val & 0xFF;
      scsi_log_buf[idx].pc = m68k_get_reg(NULL, M68K_REG_PC);
      scsi_log_buf[idx].is_write = 0;
      scsi_log_head++;
      scsi_log_total++;
    }
  }
#endif

#ifdef DEBUG_DIAG
  // IWM access capture: $DFE1FF-$DFFFFF
  if (address >= 0xDFE1FF && address <= 0xDFFFFF) {
    iwm_log_access(address, val, 0);
  }
#endif

#ifdef DEBUG_DIAG
  // VIA IFR tracing: count which interrupt sources the ISR sees
  // VIA IFR is at base ($EFE1FE) + RS13*512 = $EFFBFE
  if (address == 0xEFFBFE) {
    via_ifr_reads++;
    for (int i = 0; i < 7; i++) {
      if (val & (1 << i))
        via_ifr_bits[i]++;
    }
  }
  // VIA IER tracing: capture which interrupts are enabled
  // VIA IER is at base + RS14*512 = $EFFDFE
  if (address == 0xEFFDFE) {
    via_ier_last = val;
    via_ier_reads++;
  }
  // VIA register reads that clear IFR sources (ACK tracking)
  // Reading IRA ($EFE3FE, RS=1) clears CA1 in IFR
  // Reading ORB ($EFE1FE, RS=0) clears CB1/CB2 in IFR
  // Reading T2C-L ($EFF1FE, RS=8) clears T2 in IFR
  // Reading SR ($EFF5FE, RS=10) clears SR in IFR
  if (address == 0xEFE3FE) via_ira_reads++;   // CA1 ACK
  if (address == 0xEFE1FE) via_orb_reads++;   // CB1/CB2 ACK
  if (address == 0xEFF1FE) via_t2cl_reads++;  // T2 ACK
  if (address == 0xEFF5FE) via_sr_reads++;    // SR ACK
#endif

  return val;
}

unsigned int m68k_read_memory_16(unsigned int address) {
  uint32_t bus_addr = address & 0x00FFFFFF;

  if (platform_read_check(OP_TYPE_WORD, address, &platform_res)) {
    return platform_res;
  }

  { int _sd = slowio_get_delay(bus_addr); if (_sd) slowio_delay(_sd); }

  uint32_t result16;
  if (bus_addr & 0x01) {
    result16 = ((ps_read_8(bus_addr) << 8) | ps_read_8(bus_addr + 1));
  } else {
    result16 = (unsigned int)ps_read_16(bus_addr);
  }

  return result16;
}

unsigned int m68k_read_memory_32(unsigned int address) {
  uint32_t bus_addr = address & 0x00FFFFFF;

  if (platform_read_check(OP_TYPE_LONGWORD, address, &platform_res)) {
    return platform_res;
  }

  /* Check fast-path RAM/ROM buffers before hitting SE bus.
   * Needed for PMMU table walks which call this slow-path function
   * but need to read from the local RAM buffer. */
  for (int i = 0; i < m68ki_cpu.read_ranges; i++) {
    if (address >= m68ki_cpu.read_addr[i] && address < m68ki_cpu.read_upper[i]) {
      unsigned char *p = m68ki_cpu.read_data[i] + (address - m68ki_cpu.read_addr[i]);
      return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    }
  }

  { int _sd = slowio_get_delay(bus_addr); if (_sd) slowio_delay(_sd); }

  uint32_t result32;
  if (bus_addr & 0x01) {
    uint32_t c = ps_read_8(bus_addr);
    c |= (be16toh(ps_read_16(bus_addr+1)) << 8);
    c |= (ps_read_8(bus_addr + 3) << 24);
    result32 = htobe32(c);
  } else {
    uint16_t a = ps_read_16(bus_addr);
    uint16_t b = ps_read_16(bus_addr + 2);
    result32 = (a << 16) | b;
  }

  return result32;
}

static inline int32_t platform_write_check(uint8_t type, uint32_t addr, uint32_t val) {
  uint32_t bus24 = addr & 0x00FFFFFF;
  switch (cfg->platform->id) {
    case PLATFORM_MAC: {
      /* Debug: log first writes to VIA range */
      static int via_dbg = 0;
      if (bus24 >= 0xE00000 && bus24 <= 0xEFFFFF && via_dbg < 20) {
        printf("[MAC-VIA] write addr=%08X val=%02X type=%d\n", bus24, val, type);
        via_dbg++;
      }
      if (ovl && bus24 >= (ovl_sysrom_pos & 0x00FFFFFF) &&
          bus24 < (ovl_sysrom_pos & 0x00FFFFFF) + 0x100000) {
        ovl = 0;
        m68ki_cpu.ovl = 0;
        printf("[MAC] OVL off (write to ROM/SCSI range %08X).\n", bus24);
        handle_ovl_mappings_mac68k(cfg);
      }
      /* Custom write handler — uses 24-bit addr for I/O range matching */
      if (cfg->platform->custom_write &&
          cfg->platform->custom_write(cfg, bus24, val, type) != -1) {
        return 1;
      }
      break;
    }
    default:
      break;
  }

  if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high)) {
    if (handle_mapped_write(cfg, addr, val, type) != -1) {
      return 1;
    }
  }

  return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  uint32_t bus_addr = address & 0x00FFFFFF;

  /* Trace VIA writes (skip first 20 = early init) */
  if (bus_addr >= 0xEFE000 && bus_addr <= 0xEFFFFF) {
    static int vw = 0;
    if (vw >= 20 && vw < 50) {
      int reg = (bus_addr - 0xEFE1FE) / 0x200;
      static const char *regnames[] = {
        "ORB","ORA","DDRB","DDRA","T1CL","T1CH","T1LL","T1LH",
        "T2CL","T2CH","SR","ACR","PCR","IFR","IER","ORA-NH"};
      printf("[VIA-WR] %s($%06X)=$%02X PC=$%08X\n",
             (reg >= 0 && reg < 16) ? regnames[reg] : "??",
             bus_addr, value & 0xFF,
             m68k_get_reg(NULL, M68K_REG_PC));
    }
    vw++;
  }

  if (address >= 0x51E0 && address <= 0x51F0)
    printf("[WR8-WATCH] $%06X ← $%02X  PC=$%08X\n",
           address, value & 0xFF, m68k_get_reg(NULL, M68K_REG_PC));

  if (address >= 0x580000 && address <= 0x5FFFFF) {
#ifdef DEBUG_DIAG
    wr8_scsi++;
    // SCSI write detail printing disabled — too noisy during IWM debugging
    unsigned int idx = scsi_log_head % SCSI_LOG_SIZE;
    scsi_log_buf[idx].addr = address;
    scsi_log_buf[idx].val = value & 0xFF;
    scsi_log_buf[idx].pc = m68k_get_reg(NULL, M68K_REG_PC);
    scsi_log_buf[idx].is_write = 1;
    scsi_log_head++;
    scsi_log_total++;
#endif
#ifdef DEBUG_MAC_IO
    // Dump registers on first ODR write during command phase
    {
      static int odr_cmd_dumped = 0;
      if (!odr_cmd_dumped && (address == 0x5FF001 || address == 0x5FF000) &&
          m68k_get_reg(NULL, M68K_REG_PC) == 0x0041A924) {
        odr_cmd_dumped = 1;
        printf("[SCSI-CMD] ODR write val=%02X\n", value & 0xFF);
      }
    }
    // Verify command bytes: when ICR=$11 (ACK+DATA asserted), read CSD to see
    // what the 5380 is putting on the SCSI bus (= what the target latches)
    {
      static int csd_verify_count = 0;
      if (csd_verify_count < 8 && (address & 0xFFFFF0) == 0x5FF010 && (value & 0xFF) == 0x11) {
        uint8_t csd = ps_read_8(0x5FF000);
        printf("[SCSI-CSD-VERIFY] #%d ACK+DATA asserted, CSD=%02X (bus data at target latch)\n",
               csd_verify_count, csd);
        csd_verify_count++;
      }
    }
#ifdef DEBUG_DIAG
    // Arm CSBS transition tracker when Start DMA Init Recv is written (reg 7)
    if ((address & 0xFFFFF0) == 0x5FF070 && !csbs_tracking_active) {
      csbs_tracking_active = 1;
      csbs_trans_count = 0;
      csbs_trans_polls = 0;
      printf("[SCSI-DMA-START] DMA armed, tracking CSBS transitions\n");
    }
#endif
#endif
  }

  if (platform_write_check(OP_TYPE_BYTE, address, value))
    return;

  { int _sd = slowio_get_delay(bus_addr); if (_sd) slowio_delay(_sd); }

  // noscsi bypass: swallow all SCSI writes without hitting GPIO
  if (noscsi_enabled && bus_addr >= 0x580000 && bus_addr <= 0x5FFFFF) {
    return;
  }

  // SCSI byte lane: the NCR 5380 is on D8-D15 (upper byte lane). ROM writes
  // to odd addresses, putting data on D0-D7. The BBU steers D0-D7→D8-D15
  // when the CPLD generates properly timed bus cycles. Byte replication in
  // ps_write_8 ensures D8-D15 also has the correct value.
  if (bus_addr >= 0x580000 && bus_addr <= 0x5FFFFF) {
    int paced = is_pacedio(bus_addr);
#ifdef DEBUG_DIAG
    if (paced) pacedio_hit_count++;
#endif
    if (paced) {
      ps_write_8_paced(bus_addr, value);
      for (int _i = 1; _i < paced; _i++)
        paced_dummy_cycle_1();
    } else {
      ps_write_8(bus_addr, value);
    }
    return;
  }

#ifdef DEBUG_MAC_IO
  if (address >= 0x142 && address <= 0x143) {
    printf("[DSKERR] W8 addr=%03X val=%02X pc=%08X a1=%08X sp=%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC),
           m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A7));
  }
  if (address == 0x1DB9) {
    printf("[DCE-BUSY] W8 val=%02X pc=%08X a1=%08X\n",
           value, m68k_get_reg(NULL, M68K_REG_PC),
           m68k_get_reg(NULL, M68K_REG_A1));
  }
  if (address >= 0x226 && address <= 0x229) {
    printf("[JFETCH] W8 addr=%03X val=%02X pc=%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC));
  }
  if (address >= 0x22E && address <= 0x231) {
    printf("[JIODONE-VEC] W8 addr=%03X val=%02X pc=%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC));
  }
  if (address >= 0x001FFC10 && address <= 0x001FFC11) {
    watch_jiodone++;
    printf("[IORESULT] W8 addr=%08X val=%02X pc=%08X count=%u\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC), watch_jiodone);
  }
  if (address == 0xEFFDFE) {
    printf("[VIA-IER] W %02X (set=%d) pc=%08X\n",
           value, (value >> 7) & 1, m68k_get_reg(NULL, M68K_REG_PC));
  }
  if (address == 0xEFE9FE || address == 0xEFEBFE) {
    printf("[VIA-T1] W %s=%02X pc=%08X\n",
           address == 0xEFE9FE ? "T1CL" : "T1CH", value,
           m68k_get_reg(NULL, M68K_REG_PC));
  }
#endif
#ifdef DEBUG_DIAG
  // Track VIA stats silently (needed for heartbeat summary)
  if (address == 0xEFFBFE) {
    via_ifr_writes++;
    if (value & 0x01) via_ifr_w_ca2++;
    if (value & 0x02) via_ifr_w_ca1++;
  }
  if (address == 0xEFFDFE) {
    via_ier_writes++;
    if (value & 0x80) {
      if (value & 0x40) via_ier_t1_enabled++;
      if (value & 0x20) via_ier_t2_enabled++;
    }
  }
  if (address == 0xEFE9FE) via_t1cl_writes++;
  if (address == 0xEFEBFE) via_t1ch_writes++;
  // IWM write capture
  if (address >= 0xDFE1FF && address <= 0xDFFFFF) {
    iwm_log_access(address, value, 1);
  }
#endif

  {
    int paced = is_pacedio(bus_addr);
    if (paced) {
#ifdef DEBUG_DIAG
      pacedio_hit_count++;
#endif
      ps_write_8_paced(bus_addr, value);
      for (int _i = 1; _i < paced; _i++)
        paced_dummy_cycle_1();
    } else {
      ps_write_8(bus_addr, value);
    }
  }
  return;
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
  uint32_t bus_addr = address & 0x00FFFFFF;

  /* Big SE: catch decompressor writing $0040 high word (potential $004xxxxx) */
  { extern uint32_t ovl_sysrom_pos;
    if (ovl_sysrom_pos >= 0x800000 && bus_addr < 0x400000 &&
        value == 0x0040) {
      static int decomp16_log = 0;
      if (decomp16_log++ < 10)
        printf("[DECOMP-WR16] $%06X ← $%04X  PC=$%06X\n",
               address, value, m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF);
    }
  }

  if (platform_write_check(OP_TYPE_WORD, address, value))
    return;

#ifdef DEBUG_MAC_IO
  if (bus_addr == 0x142) {
    printf("[DSKERR] W16 val=%04X pc=%08X d0=%08X a1=%08X sp=%08X\n",
           value, m68k_get_reg(NULL, M68K_REG_PC),
           m68k_get_reg(NULL, M68K_REG_D0),
           m68k_get_reg(NULL, M68K_REG_A1),
           m68k_get_reg(NULL, M68K_REG_A7));
  }
  if (address >= 0x226 && address <= 0x228) {
    printf("[JFETCH] W16 addr=%03X val=%04X pc=%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC));
  }
  if (address >= 0x22E && address <= 0x230) {
    printf("[JIODONE-VEC] W16 addr=%03X val=%04X pc=%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC));
  }
  if (address == 0x001FFC10) {
    watch_jiodone++;
    printf("[IORESULT] write val=%04X pc=%08X count=%u\n",
           value, m68k_get_reg(NULL, M68K_REG_PC), watch_jiodone);
  }
  {
    static int lo_w16 = 0;
    if (address < 0x80000 && lo_w16 < 40) {
      printf("[LO-W16] #%d addr=%08X val=%04X pc=%08X\n", lo_w16, address, value,
             m68k_get_reg(NULL, M68K_REG_PC));
      lo_w16++;
    }
  }
#endif

  { int _sd = slowio_get_delay(bus_addr); if (_sd) slowio_delay(_sd); }

  if (bus_addr & 0x01) {
    ps_write_8(bus_addr, value & 0xFF);
    ps_write_8(bus_addr + 1, (value >> 8) & 0xFF);
    return;
  }

  ps_write_16(bus_addr, value);
  return;
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
  uint32_t bus_addr = address & 0x00FFFFFF;

  /* Figment trap table: no longer intercepted here.
   * Trap addresses are baked into the ROM by patch-rom.py. */

  /* Debug: catch writes to UTableBase ($011C) */
  if (bus_addr == 0x011C) {
    static int utbl_log = 0;
    if (utbl_log++ < 10)
      printf("[UTBL-WR] $011C ← $%08X  PC=$%08X\n",
             value, m68k_get_reg(NULL, M68K_REG_PC));
  }

  /* Debug: catch writes near $51EC */
  if (address >= 0x51E0 && address <= 0x51F0) {
    printf("[WR32-WATCH] $%06X ← $%08X  PC=$%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC));
  }

  /* Big SE: catch decompressor writing $004xxxxx values to RAM */
  { extern uint32_t ovl_sysrom_pos;
    if (ovl_sysrom_pos >= 0x800000 && address < 0x400000 &&
        value >= 0x00400000 && value <= 0x004FFFFF) {
      static int decomp_log = 0;
      if (decomp_log++ < 30)
        printf("[DECOMP-WR32] $%06X ← $%08X  PC=$%06X\n",
               address, value, m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF);
    }
  }

  if (platform_write_check(OP_TYPE_LONGWORD, address, value))
    return;

#ifdef DEBUG_MAC_IO
  if (address >= 0x224 && address <= 0x226) {
    printf("[JFETCH] W32 addr=%03X val=%08X pc=%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC));
  }
  if (address >= 0x22C && address <= 0x22E) {
    printf("[JIODONE-VEC] W32 addr=%03X val=%08X pc=%08X\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC));
  }
  if (address >= 0x001FFC0E && address <= 0x001FFC10) {
    watch_jiodone++;
    printf("[IORESULT] W32 addr=%08X val=%08X pc=%08X count=%u\n",
           address, value, m68k_get_reg(NULL, M68K_REG_PC), watch_jiodone);
  }
#endif

  { int _sd = slowio_get_delay(bus_addr); if (_sd) slowio_delay(_sd); }

  if (bus_addr & 0x01) {
    ps_write_8(bus_addr, value & 0xFF);
    ps_write_16(bus_addr + 1, htobe16(((value >> 8) & 0xFFFF)));
    ps_write_8(bus_addr + 3, (value >> 24));
    return;
  }

  ps_write_16(bus_addr, value >> 16);
  ps_write_16(bus_addr + 2, value);
  return;
}
