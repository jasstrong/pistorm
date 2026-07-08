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

/* Statistical PC profiler: ring buffer of recent PCs, one store per instruction.
 * Dump a histogram of the hottest PCs on SIGUSR2 (no need to stop the run). */
#define PCRING_SIZE 16384
static uint32_t pc_ring[PCRING_SIZE];
static uint32_t pc_ring_pos = 0;
static uint32_t g_d3_before = 0;  /* [D3-FROM-708C] d3 captured before jsr $708C */
uint32_t g_last_scrn_pc = 0;      /* [SCRN-WR] log writer only when it changes */

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
    else if (is_040) result = 15;       // hugeSE-040: UNALLOCATED machineType (Apple gap) — same reason
                                        // as the 030 case below: reporting a real machine (Quadra700) made
                                        // 7.5.5's ROM patcher patch the wrong ROM.
    else if (is_030) result = 16;       // hugeSE: UNALLOCATED machineType (12/14/15/16 are Apple gaps).
                                        // NOT a real SE/30 — reporting gestaltMacSE30(9) made System 7.5.5's
                                        // ROM patcher patch the (non-SE/30) hugeSE ROM -> resmgr map corruption.
                                        // May require adding machineType 16 to the 'gusd' resource.
    else return 0;
  } else {
    return 0;  // not handled — let the trap dispatcher run
  }
  m68k_set_reg(NULL, M68K_REG_D0, 0);       // noErr
  m68k_set_reg(NULL, M68K_REG_A0, result);
  if (selector == 0x6D616368) { static int gm=0; if(gm++<6) printf("[MACH-INTERCEPT] Gestalt('mach') -> machineType=%u\n", result); }
  return 1;
}

int mode32_active = 0;
int mode32_enabled = 0;  /* set by config: setvar mode32 1 */
volatile int mode32_trigger = 0;  /* set by SIGUSR1 */
int figment_enabled = 0;  /* set when Figment is embedded in ROM */
int figment_verbose = 0;  /* set from HUGESE_VERBOSE env; gates debug printfs only */
int dbg_codewin = 1;            /* log writes into the $17600-$17E00 crash window */
unsigned int dbg_codewin_n = 0; /* shared cap counter for CODEWR logs */
uint32_t g_last_getres_type = 0; uint32_t g_last_getres_id = 0; uint32_t g_last_getres_pc = 0;
int suppress_rom_patches = 0;   /* HUGESE_NOPATCH: force GetResource('ptch'/'PTCH') -> nil
                                 * so the OS doesn't patch our already-modified ROM */
int gusd_arm = 0; uint32_t gusd_ret_pc = 0; uint32_t gusd_res_sp = 0; /* 'gusd' dump-on-return */
int gest_arm = 0; uint32_t gest_ret_pc = 0; uint32_t gest_sel = 0; /* Gestalt mach/addr capture */
int mdb_arm = 0; int mdb_done = 0; uint32_t mdb_buf = 0; /* HFS MDB read: dump on-disk $4244 block when it fills */
int mdb_settle = 0; uint32_t mdb_pend = 0; /* once $4244 seen, settle N instrs so the byte-pump finishes before dumping */
int node_settle = 0; uint32_t node_buf = 0; uint32_t node_pos = 0; /* dump+validate the B-tree node a high read fills, after the byte-pump settles */
uint32_t g_mnext_addr = 0, g_mnext_val = 0; /* dynamic watch: a resmgr map's mNext field addr + the valid value written, to catch the stray write that corrupts it (figment-heap nondeterministic so the addr varies per run) */

/* Figment trap table: maps OS trap number → binary offset of fig_XXX glue.
 * Auto-generated from figment.elf symbols. */
#define FIGMENT_ROM_OFF 0x40000  /* in the mirror half of 512K ROM */
#include "huge-se/figment/figment_offsets.h"

int r0_dirty = 1;  /* bigSE: region-0 code window written since last scan (see R0_MARK_DIRTY) */

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

			/* [REMOVED 2026-06-19] The stripped-ROM-pointer redirect that bounced
			 * $00800000-$0087FFFF execution to ROM was WRONG: the System loads real
			 * code into that RAM region (8MB mark, NOT a ROM mirror), so it misfired
			 * on legitimate loaded-System execution -> instant boot death (<10s) vs
			 * yesterday's full minutes-long System load. Reverted to no-redirect. */

			/* Call external hook to peek at CPU */
			m68ki_instr_hook(REG_PC); /* auto-disable (see m68kcpu.h) */

			/* PC trace for big-se vs huge-se comparison */
			if (pc_trace_buf && pc_trace_count < pc_trace_limit)
				pc_trace_buf[pc_trace_count++] = REG_PC;

			/* Statistical profiler ring — dump hottest PCs on SIGUSR2 */
			pc_ring[pc_ring_pos++ & (PCRING_SIZE - 1)] = REG_PC;
			{ /* wild-jump detector: catch valid->wild PC transition (bad JMP/RTS target) */
			  static uint32_t prev_valid = 0;
			  int valid = (REG_PC < 0x02000000) || (REG_PC >= 0x40800000 && REG_PC < 0x40880000);
			  if (valid) prev_valid = REG_PC;
			  else { static int wj = 0; if (wj++ < 4)
			    printf("[WILD-JMP] PC=$%08X  from=$%08X  A0=$%08X A1=$%08X A6=$%08X SP=$%08X *(SP)=$%08X  D0=$%08X D2=$%08X\n",
			           REG_PC, prev_valid, REG_DA[8], REG_DA[9], REG_DA[14], REG_DA[15],
			           m68ki_read_32(state, REG_DA[15]), REG_DA[0], REG_DA[2]); }
			}

			/* Debug: trace T2 load and disk subroutines */
			{
			  uint32_t rom_off = REG_PC - ovl_sysrom_pos;
			  /* MODE32: the SE ROM's InitMemMgr ($07CA) never clears SystemInfo ($0B73)
			   * bits 0/1 (Systemis24bit/Sysheapis24bit), so 7.5.5 stays 24-bit and packs
			   * the top byte of pointers. SuperMario's Fig_InitMemMgr does this BCLR; the
			   * SE ROM doesn't. Clear them right after InitMemMgr returns (InitRsrcMgr
			   * entry $07E8), before the System launches. */
			  /* SCOPE (2026-07-03): force is hugeSE-only (flat 32-bit). Ordinary SE and
			   * bigSE are 24-bit and MUST keep $0B73 bits 0/1 set — clearing them stops
			   * the System masking pointer high bytes, so junk-topped pointers deref into
			   * random RAM (nondeterministic heap corruption). Gate by ROM position, the
			   * only reliable distinguisher of the three configs: SE $00400000 / bigSE
			   * $00800000 (skip) vs hugeSE $40800000 (apply). $xx07E8 is shared low ROM. */
			  if (ovl_sysrom_pos >= 0x40000000)
			  { static int go32=0;
			    if (!go32 && rom_off == 0x07E8) { go32=1;
			      uint8_t ov=m68ki_read_8(state,0x0B73); m68ki_write_8(state,0x0B73, ov & 0xFC);
			      printf("[GO32] $0B73 $%02X -> $%02X (clear Systemis24bit) at InitRsrcMgr\n", ov, ov & 0xFC); }
			    /* clamp it clear: the loaded System MM init may re-derive 24-bit */
			    if (go32) { static uint32_t pc=0; if ((pc++ & 0x3F)==0) { uint8_t v=m68ki_read_8(state,0x0B73);
			      if (v & 3) { m68ki_write_8(state,0x0B73, v & 0xFC); static int rl=0; if (rl++<12) printf("[GO32-RE] $0B73 re-set to $%02X by something, re-cleared\n", v); } } } }
			  /* ADB deferred-queue integrity at $3A32 (after $3A16 reads the 14-byte
			   * ADBCmdQEntry): is a4 (queue ptr) in-bounds, or is ABusVars/the queue
			   * corrupt? Filter to the garbage transaction (fQComp top byte set). */
			  if (rom_off == 0x3A32) { uint32_t a1=REG_DA[9];
			    if (a1 >> 24) { static int q=0; if (q++<4) { uint32_t a3=REG_DA[11], a4=REG_DA[12];
			      printf("[ADBQ] ABusVars(a3)=$%08X qptr(a4)=$%08X cmd=$%02X\n", a3, a4, REG_DA[0]&0xFF);
			      printf("[ADBQ]   bounds: start(+316)=$%08X end(+320)=$%08X (+328)=$%08X rd(+324)=$%08X\n",
			        m68ki_read_32(state,a3+316), m68ki_read_32(state,a3+320), m68ki_read_32(state,a3+328), m68ki_read_32(state,a3+324));
			      printf("[ADBQ]   fQBuff=$%08X fQComp=$%08X fQData=$%08X  raw14:", REG_DA[8], a1, REG_DA[10]);
			      for(int k=0;k<14;k++) printf(" %02X", m68ki_read_8(state, a4+k));
			      printf("\n"); } } }
			  if (rom_off == 0x3884) { static int mq=0; if (mq++<2) printf("[MMU32@ADB] $0B73=$%02X(24bit=%d) MMU32bit($0CB2)=$%02X MMUType($0CB1)=$%02X PC=$%08X\n", m68ki_read_8(state,0x0B73), m68ki_read_8(state,0x0B73)&1, m68ki_read_8(state,0x0CB2), m68ki_read_8(state,0x0CB1), REG_PC);
  { uint32_t a0b=REG_DA[8], svc=m68ki_read_32(state, a0b+4);
			    if ((svc >> 24) != 0) { static int b=0; if (b++ < 4)
			      printf("[ADB-BLOCK] a0(block)=$%08X +0=$%08X +4(svc)=$%08X +8=$%08X PPC=$%08X\n",
			             a0b, m68ki_read_32(state,a0b), svc, m68ki_read_32(state,a0b+8), REG_PPC); } } }
			  if ((rom_off == 0x3880 || rom_off == 0x3890) && (REG_DA[9] >> 24) != 0) {
			    static int ae=0; if (ae++ < 3) { uint32_t sp=REG_DA[15];
			      printf("[ADBOP-ENTRY] @%05X a1=$%08X PPC=$%08X SP=$%08X stk:", rom_off, REG_DA[9], REG_PPC, sp);
			      for (int k=0;k<8;k++) printf(" $%08X", m68ki_read_32(state, sp+k*4));
			      printf("\n"); } }
			  /* ADBOP completion-routine store ($38E8: movel a1,a3@(308)) — catch the
			   * caller passing a 32-bit-DIRTY completion pointer (top byte != 0). */
			  if (rom_off == 0x38E8 && (REG_DA[9] >> 24) != 0) {
			    static int ad=0; if (ad++ < 4) {
			      uint32_t sp=REG_DA[15];
			      printf("[ADB-DIRTY] a1(compl)=$%08X a3(ABusVars)=$%08X SP=$%08X chain: $%08X $%08X $%08X $%08X\n",
			             REG_DA[9], REG_DA[11], sp, m68ki_read_32(state,sp), m68ki_read_32(state,sp+4),
			             m68ki_read_32(state,sp+8), m68ki_read_32(state,sp+12)); } }
			  if (rom_off == 0x5CA) { static int s=0; if(s++<2) printf("[SCRNBASE-SET] $0824 <- *(0x10C)=$%08X PC=$%08X\n", m68ki_read_32(state,0x10C), REG_PC); }
			  if (rom_off == 0xF4A) { static int f=0; if(f==0) branch_ring_dump("floppy-X bail (1st give-up)"); if(f++<3) printf("[ICON-F4A] ScrnBase($0824)=$%08X PC=$%08X\n", m68ki_read_32(state,0x0824), REG_PC); }
			  if (rom_off == 0x1176) { static int h=0; if(h++<3) printf("[HAPPYMAC] ScrnBase($0824)=$%08X PC=$%08X\n", m68ki_read_32(state,0x0824), REG_PC); }
			  /* [ROVR-GET] InitResources' GetResource('ROvr',0) returns a bogus non-nil handle
			   * whose master ptr = a resource map base -> ROM executes the map (Sad Mac F/3).
			   * $48234 = insn right after _GetResource pops the handle into D0. Log the handle,
			   * its deref, and the map-chain heads to see WHICH handle came back. */
			  if (rom_off == 0x48236) { static int n=0; if (n++<4) { uint32_t h=REG_DA[0]&0x1FFFFFF; uint32_t tmh=m68ki_read_32(state,0xA50), smh=m68ki_read_32(state,0xA54);
			      printf("[ROVR-GET] D0(handle)=$%08X *h=$%08X | TopMapHndl=$%08X *TMH=$%08X SysMapHndl=$%08X *SMH=$%08X ResErr($A60)=$%04X CurMap($A5A)=$%04X\n",
			             REG_DA[0], h?m68ki_read_32(state,h):0xDEAD, tmh, m68ki_read_32(state,tmh&0x1FFFFFF), smh, m68ki_read_32(state,smh&0x1FFFFFF), m68ki_read_16(state,0xA60), m68ki_read_16(state,0xA5A)); } }
			  /* [ROVR-JSR] about to execute the 'ROvr' resource: A0 = its (deref'd) code ptr.
			   * Also dump the figment block header (16 bytes below data; flags@+4 bit layout)
			   * to see whether the block is LOCKED — the doc requires locked+nonpurgeable. */
			  if (rom_off == 0x4823E) { static int n=0; if (n++<4) { uint32_t a0=REG_DA[8]&0x1FFFFFF, hdr=a0-16;
			      printf("[ROVR-JSR] A0=$%08X first-words: %04X %04X %04X %04X | blkhdr@$%06X: %08X %08X %08X %08X\n", REG_DA[8],
			             m68ki_read_16(state,a0), m68ki_read_16(state,a0+2), m68ki_read_16(state,a0+4), m68ki_read_16(state,a0+6),
			             hdr, m68ki_read_32(state,hdr), m68ki_read_32(state,hdr+4), m68ki_read_32(state,hdr+8), m68ki_read_32(state,hdr+12));
			      printf("[ROVR-DUMP] full resource ($E4 bytes @$%06X):\n", a0);
			      for (uint32_t off=0; off<0xE8; off+=16) { printf("  +%03X:", off);
			        for (int w=0; w<16; w+=2) printf(" %04X", m68ki_read_16(state,a0+off+w)); printf("\n"); } } }
			  /* [ROVRMP-WR] TEMP: watch the 'ROvr' master pointer slot ($22B8) — catch the
			   * relocation of the executing block (who moves it, from which trap). */
			  { static uint32_t p=0xDEADBEEF; uint32_t v=m68ki_read_32(state,0x22B8); if(v!=p){ static int n=0; if(p!=0xDEADBEEF && n++<14) { uint32_t ob=p&0x1FFFFFF, nb=v&0x1FFFFFF;
			      printf("[ROVRMP-WR] $22B8(MP): $%08X -> $%08X @PPC=$%08X PC=$%08X SP=$%08X | old tags=$%02X first: %04X %04X %04X %04X | new tags=$%02X\n",
			             p, v, REG_PPC, REG_PC, REG_DA[15],
			             (ob>0x2000&&ob<0x1F00000)?m68ki_read_8(state,ob-16+4):0xEE,
			             (ob>0x2000&&ob<0x1F00000)?m68ki_read_16(state,ob):0xEEEE, (ob>0x2000&&ob<0x1F00000)?m68ki_read_16(state,ob+2):0xEEEE,
			             (ob>0x2000&&ob<0x1F00000)?m68ki_read_16(state,ob+4):0xEEEE, (ob>0x2000&&ob<0x1F00000)?m68ki_read_16(state,ob+6):0xEEEE,
			             (nb>0x2000&&nb<0x1F00000)?m68ki_read_8(state,nb-16+4):0xEE); } p=v; } }
			  /* [GUSD-RAM] hugeSE 'gusd' override (the volume-independent sibling of bigSE's
			   * fake_gusd stream revert): the boot volume's stock gusd carries the 24-bit
			   * methods -> the System runs 24-bit-dirty pointers on flat-32 hugeSE (jas's
			   * known non-patched-gusd hang). Arm on GetResource/Get1Resource entry
			   * ($490DE/$490D6) when the stacked type is 'gusd'; at StdExitOut's RTS
			   * ($49554) read the returned handle from the Pascal result slot and rewrite
			   * machine-5 (+$0F) and machine-9 (+$1F) method low-bytes to $05 (32-bit
			   * IIci method) in RAM. Idempotent; disk untouched. setvar patch_gusd 1. */
			  { extern int patch_gusd_enabled; static int gr_armed=0;
			    if (patch_gusd_enabled) {
			      if (rom_off == 0x490DE || rom_off == 0x490D6) {
			        uint32_t sp=REG_DA[15]; uint32_t rtype=m68ki_read_32(state,sp+6);
			        if (rtype==0x67757364u) { gr_armed=1; printf("[GUSD-RAM] GetResource('gusd') id=%d — armed\n",(int16_t)m68ki_read_16(state,sp+4)); } }
			      else if (rom_off == 0x49554) {
			        /* Content-keyed: at EVERY RM trap exit, if the Pascal result slot holds a
			         * handle whose data starts with the gusd anchor, patch the methods. Catches
			         * the fetch regardless of which trap loaded it (Get/Get1/Named/RGetResource).
			         * The gusd streams COMPRESSED off disk (stream watcher blind) and only
			         * exists in this form after decompression. */
			        uint32_t sp=REG_DA[15]; uint32_t hv=m68ki_read_32(state,sp+4); uint32_t h=hv&0x1FFFFFF; gr_armed=0;
			        uint32_t p = (h && h>0x1000 && h<0x1F00000) ? (m68ki_read_32(state,h)&0x1FFFFFF) : 0;
			        if (p && p>0x1000 && p<0x1F00000 &&
			            m68ki_read_32(state,p)==0x0001AE5Bu && m68ki_read_32(state,p+4)==0x5E75006Du) {
			          uint8_t m5=m68ki_read_8(state,p+0x0F), m9=m68ki_read_8(state,p+0x1F);
			          printf("[GUSD-RAM] found via RM exit: handle=$%08X data=$%06X m5-method=$%02X m9-method=$%02X",
			                 hv, p, m5, m9);
			          if (m5!=0x05) { m68ki_write_8(state,p+0x0F,0x05); }
			          if (m9!=0x05) { m68ki_write_8(state,p+0x1F,0x05); }
			          printf(" -> now $05/$05\n"); }
			        { static int once=0; if(!once){ once=1;
			            printf("[GUSD-RAM] toolbox tbl: $A80C(rGetResource)->$%08X $A9A0(GetResource)->$%08X\n",
			                   m68ki_read_32(state,0xE00+0x0C*4), m68ki_read_32(state,0xE00+0x1A0*4)); } } }
			    } }
			  /* [WARM-RESET] catch re-entry at the ROM reset PC ($4080002A) after cold boot —
			   * dump the branch ring to show who jumped back to ROM start (pass-1 silent reboot). */
			  if (rom_off == 0x2A) { static int r=0; r++; if (r >= 2 && r <= 4) { printf("[WARM-RESET] ROM reset entry #%d PPC=$%08X SP=$%08X\n", r, REG_PPC, REG_DA[15]); branch_ring_dump("warm reset"); } }
			  /* [BOOTCODE-WR] TEMP: pass-2 System startup code runs at $205xx (sys heap); its block
			   * turned into "SICN" resource data mid-trap -> wild RTS -> Sad Mac F/3. Watch $20600
			   * to catch the overwriter (figment move/purge of an executing block?). REMOVE AFTER USE. */
			  { static uint32_t p=0xDEADBEEF; uint32_t v=m68ki_read_32(state,0x20600); if(v!=p){ static int n=0; if(p!=0xDEADBEEF && n++<10) printf("[BOOTCODE-WR] $20600: $%08X -> $%08X @PPC=$%08X PC=$%08X A0=$%08X A1=$%08X SP=$%08X\n", p, v, REG_PPC, REG_PC, REG_DA[8], REG_DA[9], REG_DA[15]); p=v; } }
			  /* [CHKHEAP] figment _CheckHeap walk advance ($40846610: adda.l $8(a2),a2 =
			   * workBlock += workBlock->size@+8). If size==0 (or a2 doesn't advance) the walk
			   * loops forever — that malformed block is the heap-corruption root the $A02F
			   * "hang" really is. a2=workBlock, flags@+4, size@+8, prevBlock(back)@+0 (figment). */
			  /* [GROWSYS] c_GrowSysZone(curHeap,newEnd) @$40843AEE — born-32 system-heap-grow. */
			  if (rom_off == 0x43AEE) {
			    static int g=0; if (g++ < 8) { uint32_t sp=REG_DA[15];
			      uint32_t ch=m68ki_read_32(state,sp+4), ne=m68ki_read_32(state,sp+8);
			      printf("[GROWSYS] curHeap=$%08X newEnd=$%08X curBackLimit=$%08X PC=$%08X\n",
			             ch, ne, m68ki_read_32(state,ch+0x40), REG_PC); }
			  }
			  /* [EXTHEAP] ExtendHeapLimit(newTrailBlock,curHeap) @$408439A8 — sets oldTrailBlock
			   * (=curHeap->backLimit@+$40)->size = newTrailBlock-oldTrailBlock, then KillBlocks it.
			   * If oldTrailBlock==$25604 we expect its size set to ~$19E4 — but it's 0, so watch. */
			  if (rom_off == 0x439A8) {
			    static int e=0; if (e++ < 8) { uint32_t sp=REG_DA[15];
			      uint32_t ntb=m68ki_read_32(state,sp+4), ch=m68ki_read_32(state,sp+8);
			      uint32_t otb=m68ki_read_32(state,ch+0x40);
			      printf("[EXTHEAP] newTrailBlock=$%08X curHeap=$%08X oldTrailBlock=$%08X (oldTrail->size will be $%X) PC=$%08X\n",
			             ntb, ch, otb, ntb-otb, REG_PC); }
			  }
			  if (rom_off == 0x46610) {
			    static uint32_t prev=0; static int n=0, stuck=0;
			    uint32_t wb = REG_DA[10];                       /* a2 = workBlock */
			    uint32_t size = m68ki_read_32(state, wb + 8);
			    uint8_t  flags = m68ki_read_8(state, wb + 4);
			    if ((size == 0 || wb == prev) && stuck++ < 3) {
			      uint32_t ch = REG_DA[11];   /* a3 = curHeap (zone header) */
			      printf("[CHKHEAP-STUCK] workBlock=$%08X size=$%08X flags=$%02X back=$%08X prev=$%08X heapStart=$%08X backLimit(d7)=$%08X\n",
			             wb, size, flags, m68ki_read_32(state, wb), prev, m68ki_read_32(state, ch+0x18), REG_DA[7]);
			      printf("  curHeap(a3)=$%08X  ch+$A4(stat)=$%08X  ch+$40(backLimit)=$%08X  (is $2400C == ch+$A4? %d)\n",
			             ch, ch+0xA4, m68ki_read_32(state, ch+0x40), (ch+0xA4)==0x2400C);
			      printf("  prevBlk $%06X: back=$%08X flags/size=$%08X size=$%08X\n", prev,
			             m68ki_read_32(state, prev), m68ki_read_32(state, prev+4), m68ki_read_32(state, prev+8));
			      printf("  raw $24000: %08X %08X %08X %08X  $25600: %08X %08X %08X %08X\n",
			             m68ki_read_32(state,0x24000), m68ki_read_32(state,0x24004), m68ki_read_32(state,0x24008), m68ki_read_32(state,0x2400C),
			             m68ki_read_32(state,0x25600), m68ki_read_32(state,0x25604), m68ki_read_32(state,0x25608), m68ki_read_32(state,0x2560C));
			      printf("  ZONES: SysZone($2A6)=%08X ApplZone($2AA)=%08X ApplLimit($130)=%08X HeapEnd($114)=%08X MemTop($108)=%08X BufPtr($10C)=%08X\n",
			             m68ki_read_32(state,0x2A6), m68ki_read_32(state,0x2AA), m68ki_read_32(state,0x130),
			             m68ki_read_32(state,0x114), m68ki_read_32(state,0x108), m68ki_read_32(state,0x10C));
			      printf("  zone@$23F68: heapStart(+$18)=%08X backLimit(+$40)=%08X  | does $23F68 overlap SysZone $2000-$26FE8? curHeap=%08X\n",
			             m68ki_read_32(state,0x23F68+0x18), m68ki_read_32(state,0x23F68+0x40), ch);
			    }
			    else if (n++ < 24)
			      printf("[CHKHEAP] workBlock=$%08X size=$%08X flags=$%02X\n", wb, size, flags);
			    prev = wb;
			  }
			  /* [CHILD-RAW] $809392 read child node# as a 32-bit value: move.l $18(a1,d1.w),d0.
			   * At $9396 d0 holds it. When d0 has a dirty high word ($0035...), dump the buffer
			   * a1, offset d1, and raw bytes around a1+d1+$14..$20 -- shows whether $0035 is IN the
			   * buffer (cascade/on-disk) or whether the read offset is wrong (child# is 16-bit at +$1A). */
			  /* [D3-FROM-708C] d3 (child node#) before vs after the $6FC-vectored search
			   * (jsr $708C @ $809380). If before already has $0035 high word -> $708C only
			   * sets the low 16 bits (stale-high-word bug). If before is clean -> $708C
			   * computes the full dirty value. d5 = the position it read (d2<<9). */
			  if (rom_off == 0x9380) { g_d3_before = REG_DA[3]; }
			  /* [CHAIN] resmgr map-chain walk ($40849442: d0 = map+16 next-link). Dump
			   * each map (a0=master ptr) and its next-link; flag a ROM/wild handle. [CHAIN-WR]
			   * = the link write ($40849470: movel a3,a0@(16)). Shows which map has the bad
			   * $40801370 link and whether it was written or is uninitialized garbage. */
			  /* TEST: at every resmgr map+16 read site, if the next-link is a wild/
			   * uninitialized handle, ZERO THE FIELD in memory (a0/a3 = mapbase, +16 =
			   * link) AND the loaded reg → terminates the chain for all future walks.
			   * $49284: moveal a3@(16),a4 ; $49442/$49490: movel a0@(16),d0. */
			  /* NEUTRALIZED 2026-06-23: the field-write band-aid caused a wild-write cascade
			   * (mapbase was already garbage $1AE61AC). Log-only now. Real fix = figment heap
			   * stability for resmgr map handles (mNext corrupted in heap, not by the walk). */
			  if (rom_off == 0x49284) { uint32_t mp=REG_DA[11]&0x1FFFFFF, a4=REG_DA[12]; if(a4 && ((a4&0xFF000000u)||a4<0x100u)) { static int w=0; if(w++<8) printf("[CHAIN-BAD] $49284 map=$%06X bad mNext=$%08X\n", mp, a4); } }
			  /* [TMH-ONEONE] catch TopMapHndl ($A50) clobbered to $00010001 (OneOne) — the derail
			   * head seen earlier. PPC = the writing instruction. */
			  /* DISABLED 2026-07-02: bug fixed (frame-layout + $A822 stub); per-instruction read = throttle.
			  { static uint32_t p=0; uint32_t v=m68ki_read_32(state,0xA50); if(v!=p){ if(v==0x00010001u){ static int n=0; if(n++<4) printf("[TMH-ONEONE] TopMapHndl <- $00010001 @PPC=$%08X prev=$%08X D0=$%08X D1=$%08X A0=$%08X A1=$%08X\n", REG_PPC, p, REG_DA[0], REG_DA[1], REG_DA[8], REG_DA[9]); } p=v; } } */
			  /* [FB62] watch the frame-local slot ($00FFFB62 = A6-8 from SWAP-A3) for the
			   * $00010001 (OneOne) write, capturing the culprit instruction (PPC). */
			  /* DISABLED 2026-07-02: bug fixed; per-instruction read = throttle.
			  { static uint32_t p=0xDEADBEEF; uint32_t v=m68ki_read_32(state,0x00FFFB62); if(v!=p){ if(v==0x00010001u){ static int n=0; if(n++<8) printf("[FB62] $FFFB62 <- $00010001 @PPC=$%08X (PC=$%08X) prev=$%08X D0=$%08X D1=$%08X A0=$%08X A1=$%08X A6=$%08X\n", REG_PPC, REG_PC, p, REG_DA[0], REG_DA[1], REG_DA[8], REG_DA[9], REG_DA[14]); } p=v; } } */
			  /* [SWAP-A3] confirm A6 at the corrupting write (is the frame at $FFFB6A?). */
			  if (rom_off == 0x494B0 && REG_DA[11]==0x00010001u) { static int n=0; if(n++<2) printf("[SWAP-A3] A6=$%08X *(A6-8)=$%08X\n", REG_DA[14], m68ki_read_32(state,(REG_DA[14]&0x1FFFFFF)-8)); }
			  /* [MNEXT-WR] resmgr writes to mNext(map+16). Compare vs [CHAIN-BAD] reads: if a
			   * garbage-read map WAS written correctly here -> it got corrupted/relocated after
			   * (heap instability); if never written -> uninitialized. $489AC=insert(TopMapHndl). */
			  /* [BADMP-ZONE] at CheckZoneForBadFreeMPs ($466CE, where firstFreeMP is loaded):
			   * which zone has the garbage firstFreeMP(+8)? low SysZone (~$2000) or the high
			   * ROM-placed ApplZone (~$100000+, in the filled region)? Decides the fix site. */
			  if (rom_off == 0x466EE) { uint32_t a1=REG_DA[9]&0x1FFFFFF, z=REG_DA[11]; uint32_t val=m68ki_read_32(state,a1), nxt=val&~1u, hs=m68ki_read_32(state,z+0x18), bl=m68ki_read_32(state,z+0x40); if(nxt && (nxt<hs||nxt>=bl)){ static int n=0; if(n++<10) printf("[DIRTY-SLOT] slot=$%06X *slot=$%08X (next=$%08X OUT-OF-ZONE) head=$%08X zone=$%08X\n", a1, val, nxt, m68ki_read_32(state,z+8), z); } }
			  /* [2254-WRITE] catch the store that stamps a dirty high word into MP slot $2254
			   * (valid MP content is an in-zone addr, high word $0000/$0010; $22B4 = the bug). */
			  /* DISABLED 2026-07-02: bug fixed; per-instruction read = throttle.
			  { static uint32_t prev=0xFFFFFFFF; uint32_t v=m68ki_read_32(state,0x2254); if(v!=prev){ if((v>>16) >= 0x100u){ static int n=0; if(n++<8) printf("[2254-WRITE] $2254: $%08X -> $%08X @PPC=$%08X (PC=$%08X) D0=$%08X D1=$%08X A0=$%08X A1=$%08X SP=$%08X\n", prev, v, REG_PPC, REG_PC, REG_DA[0], REG_DA[1], REG_DA[8], REG_DA[9], REG_DA[15]); } prev=v; } } */
			  if (rom_off == 0x489AC) { static int w=0; if(w++<14) printf("[MNEXT-WR] $489AC map(a1)=$%06X <- TopMapHndl=$%08X\n", REG_DA[9]&0x1FFFFFF, m68ki_read_32(state,0xA50)); }
			  if (rom_off == 0x49456) { static int w=0; if(w++<14) printf("[MNEXT-WR] $49456 map(a0)=$%06X <- a2=$%08X\n", REG_DA[8]&0x1FFFFFF, REG_DA[10]); }
			  if (rom_off == 0x49470) { uint32_t mp=REG_DA[8]&0x1FFFFFF, val=REG_DA[11]; static int w=0; if(w++<14) printf("[MNEXT-WR] $49470 map(a0)=$%06X <- a3=$%08X\n", mp, val); extern uint32_t g_mnext_addr, g_mnext_val; if(val>=0x1000u && val<0x40000u && mp<0x2000000u){ g_mnext_addr=mp+16; g_mnext_val=val; } }
			  if (rom_off == 0x48EEC) { static int w=0; if(w++<14) printf("[MNEXT-WR] $48EEC map(a1)=$%06X <- a0=$%08X\n", REG_DA[9]&0x1FFFFFF, REG_DA[8]); }
			  if (rom_off == 0x481F0) { static int w=0; if(w++<14) printf("[MNEXT-WR] $481F0 map(a1)=$%06X <- *($A54)=$%08X\n", REG_DA[9]&0x1FFFFFF, m68ki_read_32(state,0xA54)); }
			  /* [PCHIST] where do the (scarce) instructions go? coarse PC buckets. */
			  { static unsigned long long tot=0, b_chk=0, b_fig=0, b_rm=0, b_scsi=0, b_drv=0, b_rom=0, b_oth=0;
			    uint32_t pc=REG_PC; tot++;
			    if (pc>=0x40846580 && pc<0x40846b00) b_chk++;
			    else if (pc>=0x40840000 && pc<0x40848000) b_fig++;
			    else if (pc>=0x40848000 && pc<0x4084b000) b_rm++;
			    else if (pc>=0x4081a000 && pc<0x4081b000) b_scsi++;
			    else if (pc>=0x00013000 && pc<0x00016000) b_drv++;
			    else if (pc>=0x40800000 && pc<0x40880000) b_rom++;
			    else b_oth++;
			    if ((tot % 20000000ULL)==0) printf("[PCHIST] %lluM instr: chkheap=%.0f%% fig=%.0f%% resmgr=%.0f%% scsiMgr=%.0f%% driver=%.0f%% otherROM=%.0f%% other=%.0f%%\n", tot/1000000ULL, 100.0*b_chk/tot, 100.0*b_fig/tot, 100.0*b_rm/tot, 100.0*b_scsi/tot, 100.0*b_drv/tot, 100.0*b_rom/tot, 100.0*b_oth/tot); }
			  /* [253C-CLOBBER] removed 2026-06-27: debunked (was reading stale D0); also it
			   * did an m68ki_read_32 EVERY instruction — a major throttle. */
			  if (rom_off == 0x9384) {
			    static int n=0; if ((REG_DA[3] & 0xFFFF0000) && n++<5)
			      printf("[D3-FROM-708C] before=$%08X after(d3)=$%08X d5(pos)=$%08X d2=$%08X vec($6FC)=$%08X a4=$%06X a3=$%06X\n",
			             g_d3_before, REG_DA[3], REG_DA[5], REG_DA[2], m68ki_read_32(state,0x6FC), REG_DA[12]&0xFFFFFF, REG_DA[11]&0xFFFFFF);
				    /* born-32 FIX: the SE ROM FM catalog B-tree search ($40807092) returns the
				     * node# in d3 with a stale high word (its divu.w/mulu.w position math is
				     * 24-bit-dirty). Harmless on a real 24-bit SE — node#<<9 truncates the junk
				     * off the top of the bus — but fatal under flat IS=0. Node#s are 16-bit here,
				     * so clear the high word to mimic the 24-bit machine. */
				    /* DISABLED 2026-06-22: $0035 is NOT dirt — it's the high byte of a LEGIT
				     * large-volume physical block ($00353486) on the real ~1.8GB 'MacPack'
				     * volume. Masking truncated the B-tree node read to block $3486 (wrong
				     * sector) -> catalog garbage -> floppy-X. Keep the log; do NOT mask. */
				    if (REG_DA[3] & 0xFFFF0000u) { static int fx=0; if (fx++<5) printf("[D3-FIX-DISABLED] keeping legit high block $%08X (would have masked to $%08X)\n", REG_DA[3], REG_DA[3]&0xFFFF); }
			  }
			  if (rom_off == 0x70A4) { static int p=0; if ((REG_DA[3] & 0xFFFF0000u) && p++<8) { uint32_t a1=REG_DA[9], d1=REG_DA[1]&0xFFFF, src=(a1+d1+6)&0xFFFFFF; printf("[70A4-PRE] d3=$%08X (high=$%04X DIRTY) a1=$%08X d1=$%X src[a1+d1+6]=$%04X PC=$%08X\n", REG_DA[3], (REG_DA[3]>>16)&0xFFFF, a1, d1, m68ki_read_16(state, src), REG_PC); } }
				  if (rom_off == 0x7096) { static int q=0; if ((REG_DA[3] & 0xFFFF0000u) && q++<8) printf("[6964-RET] d3=$%08X (high=$%04X) PC=$%08X\n", REG_DA[3], (REG_DA[3]>>16)&0xFFFF, REG_PC); }
				  /* [D3-DIRTY-AT] disabled 2026-06-22 (jas): ran logic every instruction = major emulator slowdown; $0035 is legit, not dirt */
				  /* catch the instant any data reg's high word becomes $0035 (the node# poison) */
				  /* [D-0035-AT] disabled 2026-06-22 (jas): looped all 8 regs every instruction = major slowdown; $0035 is legit high byte, not poison */
				  /* [SCRN-CHG] disabled 2026-06-22 (jas): read $0824 every instruction = slowdown; ScrnBase churn is benign (see notes) */
				  if (rom_off == 0x939C) { /* DISABLED 2026-06-22: storing the LEGIT physblk $353486 into BTCB+$12; masking it to $3486 truncated the node read to the wrong sector. Keep log, do NOT mask. */ if (REG_DA[2] & 0xFFFF0000u) { static int f2=0; if (f2++<6) printf("[D2-FIX-DISABLED] keeping BTCB+$12=$%08X (would have masked to $%08X)\n", REG_DA[2], REG_DA[2]&0xFFFF); } }
				  if (rom_off == 0x783C) { uint32_t _d3=REG_DA[3]&0xFFFFu, _d6=REG_DA[6]&0xFFFFu, _p=_d3*_d6; if (((_p>>16)&0xFFFFu)==0x0035u && (_p&0xFFu)!=0u) { static int mb=0; if (mb++<6) { uint32_t a2=REG_DA[10]&0xFFFFFFu, a1=REG_DA[9]&0xFFFFFFu; uint8_t nlen=m68ki_read_8(state,a2+0x2C); char vn[28]; int vi=0; for(;vi<nlen&&vi<27;vi++) vn[vi]=(char)m68ki_read_8(state,a2+0x2D+vi); vn[vi]=0; printf("[MAPFB] xdrStABN=$%04X * AlBlkSiz/$200=$%04X = $%08X | VCB '%s': SigWord(+$08)=$%04X NmAlBlks(+$1A)=$%04X AlBlkSiz(+$1C)=$%08X AlBlSt(+$24)=$%04X | a2(VCB)=$%06X a1(extent)=$%06X\n", _d3,_d6,_p, vn, m68ki_read_16(state,a2+0x08), m68ki_read_16(state,a2+0x1A), m68ki_read_32(state,a2+0x1c), m68ki_read_16(state,a2+0x24), a2, a1); } } }
				  /* [MAPFB-EXIT] MapFBlock rts ($4080785E): capture its result for the
				   * high-block (System-file) case. d0 = error code (expect 0/noErr), d3 =
				   * the 32-bit physical device block (~$353486), sp@ = CALLER return addr,
				   * d5 = file position. The caller is who reads the block — disassemble it
				   * to find where born-32 truncates d3<<9 or reads via a non-$A002 path. */
				  if (rom_off == 0x785E) { uint32_t d3=REG_DA[3]; if (d3 > 0x300000u) { static int me=0; if (me++<8) { uint32_t sp=REG_DA[15]; printf("[MAPFB-EXIT] d0(err)=%d physblk(d3)=$%08X pos(d3<<9)=$%08X caller(ret)=$%08X d5(filepos)=$%08X d4=$%08X\n", (int16_t)(REG_DA[0]&0xFFFF), d3, d3<<9, m68ki_read_32(state,sp), REG_DA[5], REG_DA[4]); } } }
				  /* [BTREAD] B-tree node-read path for the high-block case. $9702 = read
				   * routine entry (reached only if the cache-check at $9396 didn't skip it);
				   * $9764 = ioPosOffset just built (d0 = physblk<<9), plus *($720) = where the
				   * rts at $9768 jumps (should be $4080976E to reach the _Read at $977C);
				   * $976E = the read-issue code actually reached. Filter to high physblk. */
				  if (rom_off == 0x9702) { uint32_t pb=m68ki_read_32(state,(REG_DA[12]&0xFFFFFF)+0x12); if (pb>0x300000u){ static int b=0; if(b++<6) printf("[BTREAD-ENTRY] a4(BTCB)=$%06X physblk(+$12)=$%08X\n", REG_DA[12]&0xFFFFFF, pb); } }
				  if (rom_off == 0x9764) { uint32_t pos=REG_DA[0]; if (pos>0x10000000u){ static int b=0; if(b++<6) printf("[BTREAD-POS] ioPosOffset(d0)=$%08X a0(PB)=$%08X *($720)=$%08X (->$976E reaches _Read)\n", pos, REG_DA[8], m68ki_read_32(state,0x720)); } }
				  if (rom_off == 0x976E) { uint32_t p=m68ki_read_32(state,0x3A4+0x2E); if (p>0x10000000u){ static int b=0; if(b++<6) printf("[BTREAD-ISSUE] reached read-issue; ioPos@$3D2=$%08X d1=$%X $342=$%02X (next: $A002/$A003)\n", p, REG_DA[1]&0xFF, m68ki_read_8(state,0x342)); } }
				  /* [GN-*] pinpoint why GetNode bails before reading the high node. d2 =
				   * physblk ($353486) filters to our case. $9392: the skip test value
				   * (a1@($18+d1)==0 -> $93C0). $9404: cache-buffer-walk found nothing
				   * (a0/a1/a5 candidates, a3=head, *(a3)=first buffer). $940A: err3 (no-alloc
				   * flag d3 bit1). $9422: err1 (no buffer). $9490: the read was actually reached. */
				  if (rom_off == 0x9392 && REG_DA[2] > 0x300000u) { static int s=0; if(s++<4){ uint32_t a1=REG_DA[9]; int16_t d1=(int16_t)REG_DA[1]; uint32_t v=m68ki_read_32(state,(a1+0x18+d1)&0xFFFFFF); printf("[GN-SKIP] a1(*$34E)=$%06X d1=$%04X a1@($18+d1)=$%08X (0=>skip direct read, go $93C0) d2(physblk)=$%08X\n", a1&0xFFFFFF,(uint16_t)d1,v,REG_DA[2]); } }
				  if (rom_off == 0x9404 && REG_DA[2] > 0x300000u) { static int s=0; if(s++<4){ uint32_t a3=REG_DA[11]&0xFFFFFF; printf("[GN-NOBUF] a3(head)=$%06X *(a3)=$%08X cand a0=$%08X a1=$%08X a5=$%08X d3=$%02X d2=$%08X\n", a3, m68ki_read_32(state,a3), REG_DA[8], REG_DA[9], REG_DA[13], REG_DA[3]&0xFF, REG_DA[2]); } }
				  if (rom_off == 0x940A && REG_DA[2] > 0x300000u) { static int s=0; if(s++<3) printf("[GN-ERR3] d3-bit1 set -> GetNode err 3, NO read. d2=$%08X\n", REG_DA[2]); }
				  if (rom_off == 0x9422 && REG_DA[2] > 0x300000u) { static int s=0; if(s++<3) printf("[GN-ERR1] no cache buffer -> GetNode err 1, NO read. d2=$%08X\n", REG_DA[2]); }
				  if (rom_off == 0x9490 && REG_DA[2] > 0x300000u) { static int s=0; if(s++<3) printf("[GN-READ] node read REACHED for physblk d2=$%08X\n", REG_DA[2]); }
				  if (rom_off == 0x9396) {
			    static int cd=0; uint32_t d0=REG_DA[0];
			    if (((d0>>16)&0xFFFFu)==0x0035u && cd++<6) {
			      uint32_t a1=REG_DA[9], d1=REG_DA[1]&0xFFFF, base=a1+d1;
			      printf("[CHILD-RAW] d0=$%08X a1=$%08X d1=$%X", d0, a1, d1);
			      if ((a1 & 0xFF000000)==0 && (a1 & 0xFFFFFF) < 0x02000000) {
			        printf(" buf[+$10..+$20]:");
			        for (uint32_t off=0x10; off<=0x20; off+=2) printf(" %04X", m68ki_read_16(state, (base&0xFFFFFF)+off));
			      } else printf(" (a1 out of RAM range)");
			      printf(" (read@+$18) PC=$%08X\n", REG_PC);
			    }
			  }
				  /* born-32 skips the SCSI-timing calibration big-se runs, leaving
				   * TimeSCSIDB ($D04)=$FFFF. SCSI Mgr derives its timing from it at init,
				   * so set it BEFORE any SCSI activity: once TimeSCCDB ($D02) is
				   * calibrated, mirror it into $D04 (big-se ends up with both equal). */
				  { static int tscsi_done = 0;
				    if (!tscsi_done && rom_off == 0x4A4) {   /* just after the TimeSCCDB store at $4A0 — fire for BOTH bigSE & hugeSE to compare */
				      {
				        /* DO NOT force bigSE's hardcoded timing. born-32's OWN calibration is
				         * valid — TimeDBRA calibrates to ~$0C01, squarely in bigSE's measured
				         * $0C77-$0FD5 range — but the old force jammed in $3A2E (≈3x too big),
				         * making every DBRA-paced delay (SCSI handshake, VIA) mistimed and the
				         * System-load fragile. Let the real calibration stand; the skipped
				         * TimeSCSIDB ($FFFF) is filled from the REAL TimeSCCDB by [TIMESCSI-FIX]
				         * at $1A410 below. */
				        uint32_t dbra_scc = m68ki_read_32(state, 0x0D00);
				        uint32_t scsidb   = m68ki_read_32(state, 0x0D04);
				        tscsi_done = 1;
				        printf("[TIMING-REAL] calibrated TimeDBRA=$%04X TimeSCCDB=$%04X TimeSCSIDB=$%04X (force disabled) PC=$%08X\n",
				               (dbra_scc>>16)&0xFFFF, dbra_scc&0xFFFF, (scsidb>>16)&0xFFFF, REG_PC);
				      } } }
					  /* born-32: repair a stripped ROM driver pointer at the .DRVR dispatcher.
					   * ROM-based drivers (.Sony etc.) live in the ROM mirror at $408xxxxx, but
					   * the DCE dCtlDriver gets stored 24-bit-masked ($008xxxxx), so the dispatch
					   * jumps into low RAM (zeros) and wild-jumps.  Restore the $40 ROM high byte
					   * before the jump-table read at $40802F1E. */
					  if (ovl_sysrom_pos >= 0x40000000 && rom_off == 0x2F1E && (REG_DA[10] & 0x00F80000) == 0x00800000) {
					    REG_DA[10] |= 0x40000000;
					  }
					  /* born-32: repair the SE IWM base for the .Sony/disk driver.
					   * The driver loads A3 from low-mem $0C00 ($4081A40C) as a 24-bit
					   * $005FFxxx value, which lands in flat-mapped RAM instead of the
					   * real IWM on the SE bus -> the Q6L poll at $1A54A spins forever.
					   * OR in the $40 prefix so $405FFxxx hits the CI I/O page (L2
					   * entry 5) and routes via custom_read to the real 5380, exactly
					   * like VIA ($40EFxxxx) and IWM ($40DFxxxx). */
					  /* PROBE: TimeDBRA calibration — answer: is VIA mapped right? is PMMU on yet?
				   * does the VIA access actually land? a1 should be the VIA base. */
				  if (rom_off == 0x44C) { static int c=0; if (c++<2)
				    printf("[CAL-IN] a1(VIAbase)=$%08X pmmu_en=%d tc=$%08X (VIA T2CL via ps=$%02X) PC=$%08X\n",
				      REG_DA[9], m68ki_cpu.pmmu_enabled, m68ki_cpu.mmu_tc, ps_read_8(0xEFE800), REG_PC); }
				  if (rom_off == 0x466) { static int c=0; if (c++<2)
				    printf("[CAL-OUT] a1=$%08X TimeDBRA(d0)=$%04X pmmu_en=%d\n",
				      REG_DA[9], REG_DA[0] & 0xFFFF, m68ki_cpu.pmmu_enabled); }
				  if (ovl_sysrom_pos >= 0x40000000 && rom_off == 0x1A410 && (REG_DA[11] & 0x00F80000) == 0x00580000) {
					    REG_DA[11] |= 0x40000000;
					  }
					  /* TEST: TimeSCSIDB ($D04) is uncalibrated ($FFFF) in born-32 — the SCSI
					   * Mgr's selection/handshake delays come out wrong. Force it to the SCC
					   * calibration ($D02, same paced custom path) before the disk driver runs. */
					  if (ovl_sysrom_pos >= 0x40000000 && rom_off == 0x1A410) {
					    uint32_t d04 = (m68ki_read_32(state, 0x0D04) >> 16) & 0xFFFF;
					    if (d04 == 0xFFFF || d04 == 0) {
					      uint32_t scc = m68ki_read_32(state, 0x0D00) & 0xFFFF; /* TimeSCCDB @ $D02 */
					      if (!scc) scc = 0x017B;
					      uint32_t cur = m68ki_read_32(state, 0x0D04);
					      m68ki_write_32(state, 0x0D04, (scc << 16) | (cur & 0xFFFF));
					      static int tf = 0;
					      if (tf++ < 2) printf("[TIMESCSI-FIX] $D04 $FFFF -> $%04X (from TimeSCCDB)\n", scc);
					    }
					  }
					  /* [DACK-DECISION] hugeSE enters the pseudo-DMA/DACK transfer setup
					   * ($1A774 Mode=DMA); bigSE never gets here (it does PIO). Dump the call
					   * chain (branch ring + stack ret-addrs) + regs to find WHERE the SCSI
					   * Mgr chose DACK over PIO and what condition it keyed on. */
					  if (ovl_sysrom_pos >= 0x40000000 && rom_off == 0x1A774) {
					    static int dack_once = 0;
					    if (!dack_once) { dack_once = 1; extern void branch_ring_dump(const char *why);
					      uint32_t sp = REG_DA[15];
					      printf("[DACK-DECISION] entered DACK-DMA @$1A774  D0-D7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
					        REG_DA[0],REG_DA[1],REG_DA[2],REG_DA[3],REG_DA[4],REG_DA[5],REG_DA[6],REG_DA[7]);
					      printf("  A0-A7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
					        REG_DA[8],REG_DA[9],REG_DA[10],REG_DA[11],REG_DA[12],REG_DA[13],REG_DA[14],REG_DA[15]);
					      printf("  stack ret-chain: %08X %08X %08X %08X %08X %08X %08X %08X\n",
					        m68ki_read_32(state,sp), m68ki_read_32(state,sp+4), m68ki_read_32(state,sp+8), m68ki_read_32(state,sp+12),
					        m68ki_read_32(state,sp+16), m68ki_read_32(state,sp+20), m68ki_read_32(state,sp+24), m68ki_read_32(state,sp+28));
					      branch_ring_dump("at DACK-DMA path entry (SCSI Mgr chose blind-DMA)");
					    }
					  }
					  /* [SCSI-DISPATCH] the SCSI Mgr transfer jump-table dispatch
					   * ($1A21A: movea.l (A4,D0.w),A0; jmp (A0)). Log the table base A4,
					   * index D0, and selected routine — to see if hugeSE gets a different
					   * TABLE (mis-detected machine) or a different INDEX (blind vs polled)
					   * than bigSE, landing it on the DACK ($1A2F6) vs PIO ($1A2F4) entry. */
					  if (ovl_sysrom_pos >= 0x800000 && rom_off == 0x1A21A) {
					    static int sd_n = 0;
					    if (sd_n++ < 30) {
					      uint32_t a4 = REG_DA[12]; int16_t d0 = (int16_t)(REG_DA[0] & 0xFFFF);
					      uint32_t ent = m68ki_read_32(state, ADDRESS_68K(a4 + d0));
					      printf("[SCSI-DISPATCH] A4(tbl)=$%08X D0(idx)=$%04X -> routine=$%08X (24=$%06X) D1(op)=$%04X\n",
					             a4, (uint16_t)d0, ent, ent & 0xFFFFFF, REG_DA[1] & 0xFFFF);
					    }
					    /* one-shot: when the BLIND read (op5, idx=$14) is chosen, dump the
					     * call chain — the SCSI-Mgr code that pushed op5 is the blind-decision. */
					    if (ovl_sysrom_pos >= 0x40000000 && (int16_t)(REG_DA[0] & 0xFFFF) == 0x14) {
					      static int o5 = 0;
					      if (!o5) { o5 = 1; extern void branch_ring_dump(const char *why);
					        uint32_t sp = REG_DA[15];
					        printf("[OP5-CALLER] stack: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
					          m68ki_read_32(state,sp),m68ki_read_32(state,sp+4),m68ki_read_32(state,sp+8),m68ki_read_32(state,sp+12),
					          m68ki_read_32(state,sp+16),m68ki_read_32(state,sp+20),m68ki_read_32(state,sp+24),m68ki_read_32(state,sp+28),
					          m68ki_read_32(state,sp+32),m68ki_read_32(state,sp+36));
					        branch_ring_dump("at op5(blind) dispatch — who chose blind?");
					      }
					    }
					    /* [FORCE-POLL] (A): redirect hugeSE's blind read (op5 -> $1A2F6) to
					     * the POLLED read routine op4 ($1A388, read-with-Ticks-timeout) that
					     * bigSE uses. Patch the op5 dispatch-table entry. */
					    if (0 && ovl_sysrom_pos >= 0x40000000) {
					      uint32_t a4 = REG_DA[12]; int16_t d0 = (int16_t)(REG_DA[0] & 0xFFFF);
					      if (d0 == 0x14) {
					        uint32_t ea = ADDRESS_68K(a4 + d0), ent = m68ki_read_32(state, ea);
					        if ((ent & 0x00FFFFFF) == 0x0081A2F6) {
					          m68ki_write_32(state, ea, (ent & 0xFF000000) | 0x0081A388);
					          static int fp = 0; if (fp++ < 3) printf("[FORCE-POLL] op5 entry @$%06X $%08X -> op4-polled $1A388\n", ea, ent);
					        }
					      }
					    }
					  }
					  /* [BLIND-FLAGS] the SCSI Mgr blind-vs-polled BUILD decision at $40815D40
					   * (D0 = (-$136,A6) & (-$138,A6); bpl skips). Compare hugeSE vs bigSE to
					   * find the differing capability bit that makes hugeSE emit blind ops. */
					  if (ovl_sysrom_pos >= 0x800000 && rom_off == 0x15D40) {
					    static int bf = 0;
					    if (bf++ < 12) {
					      uint32_t a6 = REG_DA[14];
					      uint16_t f136 = m68ki_read_16(state, ADDRESS_68K(a6 - 0x136));
					      uint16_t f138 = m68ki_read_16(state, ADDRESS_68K(a6 - 0x138));
					      uint16_t sz   = m68ki_read_16(state, ADDRESS_68K(a6 + 0x8));
					      printf("[BLIND-FLAGS] (-$136)=$%04X & (-$138)=$%04X = $%04X (bit15=%d) size($8,A6)=$%04X A6=$%08X\n",
					             f136, f138, (uint16_t)(f136 & f138), ((f136 & f138) >> 15) & 1, sz, a6);
					    }
					  }
					  /* PROBE: RM master-pointer store $40849642 (move.l a1,(a0)).
					   * Dump the slot (a0), value (a1), the (a1-4) master-ptr-offset
					   * source, and the bases/Lo3Bytes used to compute the slot. */
					  if (rom_off == 0x49642) {
					    static int rc = 0;
					    if (rc < 12) {
					      uint32_t a1 = REG_DA[9];
					      uint32_t relh = m68ki_read_32(state, a1 - 4) & 0x00FFFFFF;
					      uint32_t hz = REG_DA[8] - relh;  /* a0 = HandleZone+relh, so HandleZone = a0-relh */
					      uint32_t truezone = m68ki_read_32(state, 0x1F44);  /* RM-saved ROZ zone ptr */
					      uint32_t mpb = m68ki_read_32(state, 0x1F40);       /* RM-saved MPB base */
					      printf("[RM-MPW] slot=$%08X relh=$%04X HandleZone=$%08X | trueROZ($1F44)=$%08X MPB($1F40)=$%08X ROMMapHndl=$%08X  -> %s\n",
					             REG_DA[8], relh, hz, truezone, mpb, m68ki_read_32(state, 0x0B5E),
					             (hz==truezone) ? "HandleZone-OK(rootB:figment)" : "HandleZone-WRONG(rootA:_HandleZone)");
					      rc++;
					    }
					  }
					  /* PROBE: RM map-base/type-list derivation at $40849290.
					   * Dump map handle, *(handle)=map base, the map header (first 16B),
					   * the type-list offset (map+$18), and #types — to see which is bad. */
					  if (rom_off == 0x4929A) {
					    uint32_t a4 = REG_DA[12];
					    uint32_t mb = m68ki_read_32(state, a4);
					    static int mp = 0;
					    if ((mp < 6 || mb >= 0x40000000) && mp < 40) {
					      printf("[RM-MAP] a4(hdl)=$%08X *a4(mapbase)=$%08X hdr=$%08X $%08X $%08X $%08X tloff(+18)=$%04X a3(typelist)=$%08X d5(#types-1)=$%04X\n",
					             a4, mb, m68ki_read_32(state, mb), m68ki_read_32(state, mb+4),
					             m68ki_read_32(state, mb+8), m68ki_read_32(state, mb+12),
					             (m68ki_read_32(state, mb+24) >> 16) & 0xFFFF, REG_DA[11], REG_DA[5] & 0xFFFF);
					      mp++;
					    }
					  }
					  /* PROBE: ResourceMgr search loop $40849568 — cmpa.l (a2)+,a1.
					   * Dump the search key (a1) vs the table entries (a2) to test
					   * whether one side is 24-bit-stripped (compare never matches). */
					  if (rom_off == 0x4956C) {
					    static int rs = 0;
					    if (rs < 40) {
					      printf("[RM-SEARCH] a1(key)=$%08X a2=$%08X (a2)=$%08X a3=$%08X d4=$%04X d5=$%04X\n",
					             REG_DA[9], REG_DA[10], m68ki_read_32(state, REG_DA[10]),
					             REG_DA[11], REG_DA[4] & 0xFFFF, REG_DA[5] & 0xFFFF);
					      rs++;
					    }
					  }
			  /* catch OccupyFreeSpace stamping a block in the zone header (the corruption) */
			  if ((rom_off==0x433CA||rom_off==0x433EC||rom_off==0x4345C) && (REG_DA[8]>=0x2000 && REG_DA[8]<0x2080)) {
			    static int st=0;
			    if (st<12){ uint32_t a0=REG_DA[8];
			      printf("[STAMP-HDR] A0(blk)=$%08X back(+0)=$%08X size(+8)=$%08X  rangeStart=$%08X rangeEnd=$%08X PC=$%08X\n",
			             a0, m68ki_read_32(state,a0), m68ki_read_32(state,a0+8), REG_DA[12], REG_DA[10], rom_off); st++; }
			  }
			  if (rom_off==0x43440 && (REG_DA[11]>=0x2000 && REG_DA[11]<0x2080)) {
			    static int s3=0;
			    if (s3<12){ uint32_t a3=REG_DA[11];
			      printf("[STAMP-HDR3] A3(blk)=$%08X back(+0)=$%08X size(+8)=$%08X rangeStart=$%08X rangeEnd=$%08X\n",
			             a3, m68ki_read_32(state,a3), m68ki_read_32(state,a3+8), REG_DA[12], REG_DA[10]); s3++; }
			  }
			  /* one-shot: classify the OS trap table MM entries (figment vs old ROM MM) */
			  if (rom_off == 0x41AA0) {
			    static int done = 0;
			    if (!done) { done = 1;
			      static const int mmtraps[] = {0x19,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,
			        0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x36,0x40,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,-1};
			      printf("[TRAPTBL] OS MM trap vectors (fig=$40840000-$408445D7):\n");
			      for (int i=0; mmtraps[i]>=0; i++) {
			        uint32_t t=mmtraps[i], a=m68ki_read_32(state, 0x0400 + t*4);
			        const char* w = (a>=0x40840000 && a<=0x408445D7) ? "figment" :
			                        (a>=0x40800000 && a<0x40840000) ? "** ROM-MM **" :
			                        (a>=0x408445D8 && a<0x40880000) ? "mirror/other" : "?";
			        printf("   $A0%02X -> $%08X  %s\n", t, a, w);
			      }
			    }
			  }
			  if (rom_off == 0xAE20) {  /* system-heap-grow stub entry: who called it? */
			    static int gc = 0;
			    if (gc < 8) { uint32_t ret = m68ki_read_32(state, REG_DA[15]);
			      printf("[GROW-CALLER] grow called, return=$%08X %s  newEnd(A0)=$%08X curHeap(A6)=$%08X\n",
			             ret, (ret>=0x40840000&&ret<=0x408445D7)?"(figment re-entrant!)":(ret>=0x40800000&&ret<0x40840000)?"(ROM)":"(other)",
			             REG_DA[8], REG_DA[14]); gc++; }
			  }
			  /* fig_InitZone entry ($46D2C): a0 = InitZoneParamBlock. Log every zone
			   * figment creates, so we see whether the $2000 SysZone goes through it. */
			  if (rom_off == 0x46D2C) {
			    uint32_t pb = REG_DA[8];   /* a0 */
			    printf("[FIG-INITZONE] paramBlk=$%08X start=$%08X limit=$%08X moreMast=$%04X PC=$%08X\n",
			           pb, m68ki_read_32(state, pb), m68ki_read_32(state, pb + 4),
			           m68ki_read_16(state, pb + 8), REG_PC);
			  }
			  /* KillBlock runaway downward-scan trace (MemMgrInternal.c:2714).
			   * Reset per-call at entry ($433CA); count loop iters at $43570
			   * (a4=workBlock, a2=curHeap). Dump the chain once it runs away. */
			  {
			    static uint32_t kb_count = 0;
			    static uint32_t kb_seq[64];
			    static int kb_dumped = 0;
			    static uint32_t kb_blockHeader = 0;
			    if (rom_off == 0x433CA) { kb_count = 0; kb_blockHeader = m68ki_read_32(state, REG_DA[15] + 4); }
			    if (rom_off == 0x43570) {
			      uint32_t wb = REG_DA[12];
			      if (kb_count < 64) kb_seq[kb_count] = wb;
			      kb_count++;
			      if (kb_count == 2000 && !kb_dumped) {
			        kb_dumped = 1;
			        uint32_t ch = REG_DA[10];
			        printf("[KILLBLOCK] *** runaway downward-scan (>2000 iters) *** curHeap=$%08X\n", ch);
			        printf("[KILLBLOCK] curHeap fields +0..+0x64:\n");
			        for (int o = 0; o <= 0x64; o += 4)
			          printf("    +$%02X = $%08X\n", o, m68ki_read_32(state, ch + o));
			        { uint32_t dummy    = ch + 0x48;   /* DummyFree = &favoredFree (+$48) */
			          uint32_t firstFree = m68ki_read_32(state, ch + 0x54);  /* firstFree at +$54 */
			          printf("[KILLBLOCK] blockHeader(freed)=$%08X   firstFree=$%08X (blockHeader < firstFree? %s)\n",
			                 kb_blockHeader, firstFree, (kb_blockHeader < firstFree) ? "YES->should use dummy, NO scan" : "no->scan");
			          printf("[KILLBLOCK] firstFree block: back@+0=$%08X  tagByte@+4=$%02X (0=free)\n",
			                 m68ki_read_32(state, firstFree), m68ki_read_8(state, firstFree + 4));
			          printf("[KILLBLOCK] dummyFree(&favoredFree)=$%08X back@+0=$%08X tagByte@+4=$%02X (0=free)\n",
			                 dummy, m68ki_read_32(state, dummy), m68ki_read_8(state, dummy + 4)); }
			        printf("[KILLBLOCK] first 64 workBlocks (wb / back@+0 / word@+4):\n");
			        for (int i = 0; i < 64; i++) {
			          uint32_t b = kb_seq[i];
			          printf("  [%2d] wb=$%08X  back@+0=$%08X  +4=$%08X\n",
			                 i, b, m68ki_read_32(state, b), m68ki_read_32(state, b + 4));
			        }
			        fflush(stdout);
			      }
			    }
			  }
			  if (rom_off == 0x4229A) {  /* KillBlock physical back-walk: dump the chain */
			    static int kw = 0;
			    if (kw < 24) { uint32_t a1 = REG_DA[9];
			      printf("[KILL-BACK] blk=$%08X back(+0)=$%08X tag(+4)=$%02X size(+8)=$%08X\n",
			             a1, m68ki_read_32(state,a1), m68ki_read_8(state,a1+4), m68ki_read_32(state,a1+8)); kw++; }
			  }
			  if (rom_off == 0x43254) {  /* JumpRelocateRange: A4=rangeStart A2=rangeEnd A6=curHeap */
			    static int jr = 0;
			    if (jr < 14) {
			      uint32_t rs=REG_DA[12], re=REG_DA[10], ch=REG_DA[14];
			      printf("[JRR] rangeStart=$%08X rangeEnd=$%08X curHeap=$%08X bkLim=$%08X  %s\n",
			             rs, re, ch, m68ki_read_32(state,ch),
			             (rs < ch + 0x80) ? "<<< rangeStart INSIDE HEADER" : "");
			      jr++;
			    }
			  }
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
			      uint8_t r0=ps_read_8(0x5FF000), r1=ps_read_8(0x5FF010), r2=ps_read_8(0x5FF020), r5=ps_read_8(0x5FF050);
			      printf("[SEL-STATE] #%d | reg0(Data)=$%02X reg1(ICR)=$%02X reg2(Mode)=$%02X reg4(BusStat)=$%02X reg5(B&S)=$%02X  (Data should be $C0=ID6|ID7)\n",
			             iwm_stuck, r0, r1, r2, iwm_q6l, r5);
			      (void)ticks;
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
			  { extern int gest_arm; extern uint32_t gest_ret_pc, gest_sel;
			    if (gest_arm && REG_PC == gest_ret_pc) {
			      gest_arm = 0;
			      printf("[GESTALT] '%c%c%c%c' -> A0=$%08X D0err=$%08X PC=$%08X\n",
			        (gest_sel>>24)&0xFF,(gest_sel>>16)&0xFF,(gest_sel>>8)&0xFF,gest_sel&0xFF,
			        REG_DA[8], REG_DA[0], REG_PC);
			    } }
			  { extern int gusd_arm; extern uint32_t gusd_ret_pc, gusd_res_sp;
			    if (gusd_arm && REG_PC == gusd_ret_pc) {
			      gusd_arm = 0;
			      uint32_t h = m68ki_read_32(state, gusd_res_sp);
			      printf("[GUSD] handle=$%08X ResErr=%d\n", h, (int16_t)m68ki_read_16(state, 0x0A60));
			      if (h) { uint32_t m = m68ki_read_32(state, h);
			        printf("[GUSD] master=$%08X data (words at offsets 0..):\n", m);
			        for (int k=0;k<96;k+=16) {
			          printf("  +$%02X:", k);
			          for (int j=0;j<16;j+=2) printf(" %04X", m68ki_read_16(state, m+k+j));
			          printf("\n"); }
			      }
			    } }
			  /* [MDB-READ] poll the most-recently-armed disk read buffer for the HFS
			   * MDB signature ($4244 'BD' at +0; or at +$200/+$400 if boot blocks were
			   * read with it). Once it fills, dump the ON-DISK geometry — drNmAlBlks,
			   * drAlBlkSiz, drAlBlSt — the ground truth to compare against the garbage
			   * in-memory VCB (AlBlkSiz=$6E00) that MapFBlock later trips on. On-disk
			   * sane + VCB garbage -> VCB-build corrupts it; on-disk ALSO garbage ->
			   * the read position itself is dirty (the $0035 bug, one layer up). */
			  { extern int mdb_arm, mdb_done, mdb_settle; extern uint32_t mdb_buf, mdb_pend;
			    if (!mdb_done) {
			      if (mdb_settle > 0) {
			        /* a $4244 block was spotted; let the sequential byte-pump finish
			         * filling it (the SCSI driver fills one byte per cycle) before we
			         * sample, otherwise we read a half-pumped block (just the sig word). */
			        if (--mdb_settle == 0) {
			          static int md = 0;
			          uint32_t base = mdb_pend;
			          printf("[MDB-READ] base=$%06X drNmAlBlks(+$12)=$%04X drAlBlkSiz(+$14)=$%08X drAlBlSt(+$1C)=$%04X drVBMSt(+$0E)=$%04X drCrDate(+$02)=$%08X PC=$%08X\n",
			                 base, m68ki_read_16(state, base + 0x12), m68ki_read_32(state, base + 0x14),
			                 m68ki_read_16(state, base + 0x1C), m68ki_read_16(state, base + 0x0E),
			                 m68ki_read_32(state, base + 0x02), REG_PC);
			          printf("  raw[+0..+$26]:");
			          for (int k = 0; k < 0x28; k += 2) printf(" %04X", m68ki_read_16(state, base + k));
			          printf("\n");
			          if (++md >= 4) mdb_done = 1;
			        }
			      } else if (mdb_arm) {
			        uint32_t base = 0; int found = 0;
			        for (uint32_t o = 0; o <= 0x400 && !found; o += 0x200) {
			          if ((m68ki_read_16(state, mdb_buf + o) & 0xFFFF) == 0x4244) { base = mdb_buf + o; found = 1; }
			        }
			        if (found) { mdb_pend = base; mdb_settle = 20000; mdb_arm = 0; }
			      }
			    } }
			  /* [NODE-DATA] after a high read's byte-pump settles, dump+validate the
			   * filled buffer as an HFS B-tree node: ndFLink(+0)/ndBLink(+4)/ndType(+8,
			   * $00=idx $01=hdr $FE=map $FF=leaf)/ndNHeight(+9)/ndNRecs(+$A). Valid node =
			   * the high-sector read returned good data (as bigSE gets); garbage = the read
			   * itself is wrong on hugeSE. */
			  { extern int node_settle; extern uint32_t node_buf, node_pos;
			    if (node_settle && (REG_PC - ovl_sysrom_pos) == 0x977E) {  /* dump the instant the read returns, before buffer reuse */
			      static int nd = 0;
			      uint32_t b = node_buf;
			      uint8_t ndType = m68ki_read_8(state, b + 8);
			      const char *tn = ndType==0x00?"index":ndType==0x01?"header":ndType==0xFE?"map":ndType==0xFF?"leaf":"??BAD??";
			      printf("[NODE-DATA] pos=$%08X buf=$%06X ndFLink=$%08X ndBLink=$%08X ndType=$%02X(%s) ndNHeight=$%02X ndNRecs=$%04X\n",
			             node_pos, b, m68ki_read_32(state,b+0), m68ki_read_32(state,b+4),
			             ndType, tn, m68ki_read_8(state,b+9), m68ki_read_16(state,b+0x0A));
			      printf("  raw[+0..+$1E]:");
			      for (int k=0;k<0x20;k+=2) printf(" %04X", m68ki_read_16(state, b+k));
			      printf("\n");
			      node_settle = 0; (void)nd;  /* disarm; the next high read re-arms */
			    } }
			  uint32_t rom_off2 = REG_PC - ovl_sysrom_pos;
			  if (rom_off2 == 0x2D5C) {  /* SetTrapAddress store: movel a0,a1@(0,d0:w) */
			    uint32_t a0h = REG_DA[8], a1t = REG_DA[9], d0o = REG_DA[0] & 0xFFFF;
			    uint32_t slot = a1t + d0o;
			    uint32_t base = (a1t == 0x0E00) ? 0x0E00 : 0x0400;
			    uint32_t trap = 0xA000 | ((slot - base) >> 2);
			    printf("[SETTRAP] slot=$%04X (trap $%04X) <- handler=$%08X  (tbl=$%04X) PPC=$%08X\n",
			           slot, trap, a0h, a1t, REG_PPC);
			  }
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

			/* [CRASH822] bigSE one-shot: at the ACTUAL crash site $800822
			 * (jsr (2,A6)) — this only executes on the CRASHING pass, when
			 * (6,A6) matched $44/$C0 (i.e. A6 points at fill, not a real driver
			 * struct). Dump full regs, what A6 points at, the trigger byte, the
			 * stack chain, and the JSR/RTS call chain — to find which driver-init
			 * loop entry landed A6 on $400000-pointing-at-fill, and where. */
			{
				extern uint32_t ovl_sysrom_pos;
				static int crash_once = 0;
				if (!crash_once && ovl_sysrom_pos == 0x800000
				    && ADDRESS_68K(REG_PC) == 0x800822) {
					crash_once = 1;
					uint32_t a6 = ADDRESS_68K(REG_DA[14]);
					uint32_t sp = ADDRESS_68K(REG_DA[15]);
					uint16_t bbid = m68ki_read_16(state, a6);
					printf("[BOOTBLK] bbEntry jsr(2,A6): A6=$%08X bbID=$%04X -> %s  (6,A6)=$%02X\n",
					       a6, bbid, bbid == 0x4C4B ? "VALID boot blocks, jsr proceeds" : "FILL/garbage -> wild-exec",
					       m68ki_read_8(state, a6 + 6));
					printf("[CRASH822] D0-D7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
					       REG_DA[0], REG_DA[1], REG_DA[2], REG_DA[3],
					       REG_DA[4], REG_DA[5], REG_DA[6], REG_DA[7]);
					printf("[CRASH822] A0-A6: %08X %08X %08X %08X %08X %08X %08X\n",
					       ADDRESS_68K(REG_DA[8]), ADDRESS_68K(REG_DA[9]), ADDRESS_68K(REG_DA[10]),
					       ADDRESS_68K(REG_DA[11]), ADDRESS_68K(REG_DA[12]), ADDRESS_68K(REG_DA[13]),
					       a6);
					printf("[CRASH822] what A6 points at ($%08X): ", a6);
					for (int i = 0; i < 8; i++) printf("%04X ", m68ki_read_16(state, a6 + i * 2));
					printf("\n[CRASH822] SP=$%06X stack chain:\n", sp);
					for (int i = 0; i < 14; i++)
						printf("   [SP+%02X] $%08X\n", i * 4, m68ki_read_32(state, sp + i * 4));
					{ FILE *f = fopen("/home/jas/pistorm/ram-dump.bin", "wb");
					  if (f) { for (uint32_t a = 0x2000; a < 0x4200; a++) fputc(m68ki_read_8(state, a), f); fclose(f);
						printf("[CRASH822] dumped loaded RAM $2000-$4200 to ram-dump.bin (dasmtool base $2000)\n"); } }
					{ extern void branch_ring_dump(const char *); branch_ring_dump("at CRASH822 $800822"); }
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

					/* Huge SE: born-32-bit. Full 32MB visible, IS=0 PMMU.
					 * Big SE: use actual RAM size. */
					uint32_t memtop = is_huge_se ? 0x02000000 : ram_size;
					REG_DA[14] = memtop;
					printf("[%s] MemTop forced: A6=$%08X → $%08X\n",
					       is_huge_se ? "HUGE-SE" : "BIG-SE", old, memtop);

					/* Huge SE: born-32-bit (IS=0) PMMU set up at the $48 seam.
					 * No 24-bit window — the SE ROM runs full 32-bit from here on,
					 * so high I/O ($40xxxxxx) and RAM coexist without the high-byte
					 * strip that collapsed them in IS=8.  Ported from
					 * mode32_trap_handler (minus the WTC relocation, which a born-32
					 * boot doesn't need).
					 *
					 *   TC = $80F08450: E=1, PS=15(32KB), IS=0, TIA=8, TIB=4, TIC=5
					 *   L1 (256 × 16MB): [$00]=RAM0-16M [$01]=RAM16-32M
					 *                    [$40]→L2  [else]=dirty alias to $0
					 *   L2 for $40 (16 × 1MB): [0-7]=strip $40 → RAM
					 *                          [8]=ROM+SCSI ($40800000, no CI)
					 *                          [9-F]=I/O ($409-$40F, CI) */
					if (is_huge_se && cpu_type == M68K_CPU_TYPE_68030) {
						uint32_t level1_addr = 0x01010000;  /* 16MB+64K into RAM */
						uint32_t level2_addr = 0x01010400;
						uint32_t level1[256], level2[16];
						for (int i = 0; i < 256; i++) level1[i] = 0x00000019;
						level1[0x00] = 0x00000019;   /* RAM 0-16MB identity */
						level1[0x01] = 0x01000019;   /* RAM 16-32MB identity */
						level1[0x40] = (level2_addr & 0xFFFFFFFC) | 0x02;  /* → L2 */
						for (int i = 0; i < 8; i++)
							level2[i] = ((uint32_t)i << 20) | 0x19;        /* strip $40 → RAM */
						/* Treat SCSI like VIA/IWM: $405xxxxx is a CI I/O page that routes
						 * to the SE bus (custom_read masks to $5FF040 -> real 5380),
						 * instead of aliasing to flat RAM. The 5380 lives at $5FF000. */
						level2[5] = (0x40000000 | (5u << 20)) | 0x59;       /* SCSI I/O, CI */
						level2[8] = 0x40800019;                             /* ROM + SCSI, no CI */
						for (int i = 9; i <= 15; i++)
							level2[i] = (0x40000000 | ((uint32_t)i << 20)) | 0x59;  /* I/O, CI */

						int32_t ri0 = get_named_mapped_item(cfg, "sysram");
						if (ri0 >= 0 && cfg->map_data[ri0]) {
							unsigned char *ram = cfg->map_data[ri0];
							for (int i = 0; i < 256; i++) {
								uint32_t off = level1_addr + i * 4, d = level1[i];
								ram[off]=(d>>24); ram[off+1]=(d>>16); ram[off+2]=(d>>8); ram[off+3]=d;
							}
							for (int i = 0; i < 16; i++) {
								uint32_t off = level2_addr + i * 4, d = level2[i];
								ram[off]=(d>>24); ram[off+1]=(d>>16); ram[off+2]=(d>>8); ram[off+3]=d;
							}
							/* low-memory MMU globals */
							ram[0x0CB1] = 4;       /* MMUType = 68030 */
							ram[0x0B73] = 0;       /* (mode flag; MODE32 leaves 0) */
							ram[0x0CB4]=(level1_addr>>24); ram[0x0CB5]=(level1_addr>>16);
							ram[0x0CB6]=(level1_addr>>8);  ram[0x0CB7]=level1_addr;
							uint32_t tbl_size = 256*4 + 16*4;
							ram[0x0CB8]=(tbl_size>>24); ram[0x0CB9]=(tbl_size>>16);
							ram[0x0CBA]=(tbl_size>>8);  ram[0x0CBB]=tbl_size;
						}
						printf("[HUGE-SE] IS=0 PMMU: L1@$%08X L2@$%08X TC=$80F08450 MemTop=$02000000\n",
						       level1_addr, level2_addr);

						m68ki_cpu.mmu_crp_limit = 0x7FFF0002;
						m68ki_cpu.mmu_crp_aptr = level1_addr;
						m68ki_cpu.mmu_srp_limit = 0x7FFF0002;
						m68ki_cpu.mmu_srp_aptr = level1_addr;
						m68ki_cpu.mmu_tt0 = 0;
						m68ki_cpu.mmu_tt1 = 0;
						m68ki_cpu.mmu_tc = 0x80F08450;   /* IS=0 — full 32-bit */
						m68ki_cpu.pmmu_enabled = 1;
						for (int j = 0; j < MMU_ATC_ENTRIES; j++)
							m68ki_cpu.mmu_atc_tag[j] = 0;
						m68ki_cpu.mmu_atc_rr = 0;
						m68ki_cpu.fc_read_translation_cache.lower = 0;
						m68ki_cpu.fc_read_translation_cache.upper = 0;
						m68ki_cpu.fc_write_translation_cache.lower = 0;
						m68ki_cpu.fc_write_translation_cache.upper = 0;
						m68ki_cpu.code_translation_cache.lower = 0;
						m68ki_cpu.code_translation_cache.upper = 0;

						printf("[HUGE-SE] IS=0 PMMU enabled: TC=$%08X CRP=($%08X,$%08X)\n",
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
					 * Rescan periodically — code may be reloaded.
					 * Region 0 IS scanned (from $8000, skipping the lowmem
					 * globals/vectors/trap-tables below): loaded System drivers/
					 * patches live in low RAM < $10000 and reference stock ROM
					 * (e.g. the $FC9A `JMP $41A2A0.L` = post-Welcome dirty jump). */
					{
						static uint8_t rgn_scanned[8];
						static uint32_t rgn_scan_cycle = 0;
						unsigned rgn = pc24 >> 16;
						if (rgn >= 1 && rgn <= 7 && !rgn_scanned[rgn]) {
							rgn_scanned[rgn] = 1;
							scan_lo = rgn << 16;
							scan_hi = (rgn << 16) + 0x10000;
							do_scan = 1;
						}
						/* Reset scan flags periodically so new code gets caught */
						if (++rgn_scan_cycle >= 10000000) {
							rgn_scan_cycle = 0;
							for (int i = 0; i < 8; i++) rgn_scanned[i] = 0;
						}
					}

					{ extern int r0_dirty; if (ovl_sysrom_pos == 0x800000 && pc24 >= 0x8000 && pc24 < 0x10000 && r0_dirty) { r0_dirty = 0; scan_lo = 0x8000; scan_hi = 0x10000; do_scan = 1; } }
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
    static struct timespec hb_last = {0, 0};
    int hb_fire = 0;
    if ((hb_cnt++ & 0xFFF) == 0) {
      struct timespec hb_now; clock_gettime(CLOCK_MONOTONIC, &hb_now);
      double hb_el = (hb_now.tv_sec - hb_last.tv_sec) + (hb_now.tv_nsec - hb_last.tv_nsec) / 1e9;
      if (hb_last.tv_sec == 0 || hb_el >= 2.0) { hb_fire = 1; hb_last = hb_now; }
    }
    if (hb_fire) {
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

/* SIGUSR2: dump a histogram of the hottest PCs in the ring buffer.
 * Tells us exactly which loop the CPU is spinning in, without stopping. */
void sigusr2_handler(int sig_num) {
  (void)sig_num;
  static uint32_t upc[1024];
  static uint64_t ucnt[1024];
  int nu = 0;
  for (int i = 0; i < PCRING_SIZE; i++) {
    uint32_t pc = pc_ring[i];
    int j;
    for (j = 0; j < nu; j++) if (upc[j] == pc) { ucnt[j]++; break; }
    if (j == nu && nu < 1024) { upc[nu] = pc; ucnt[nu] = 1; nu++; }
  }
  printf("\n=== PC PROFILE (last %d instrs, %d distinct PCs) ===\n", PCRING_SIZE, nu);
  for (int k = 0; k < 30; k++) {
    int best = -1; uint64_t bc = 0;
    for (int j = 0; j < nu; j++) if (ucnt[j] > bc) { bc = ucnt[j]; best = j; }
    if (best < 0 || bc == 0) break;
    uint32_t pc = upc[best];
    printf("  #%2d  PC=$%08X  rom_off=$%06X  count=%5llu (%4.1f%%)\n",
           k + 1, pc, pc - ovl_sysrom_pos, (unsigned long long)bc,
           100.0 * (double)bc / (double)PCRING_SIZE);
    ucnt[best] = 0;
  }
  printf("=== END PROFILE ===\n\n");
  fflush(stdout);
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
    aline_ring_dump();  /* last toolbox A-traps before the crash/stop */
    branch_ring_dump("at SIGINT");  /* last absolute jumps — find stripped targets */
    printf("Current PC: %08X  SR: %04X\n",
           m68k_get_reg(NULL, M68K_REG_PC),  /* full 32-bit — do NOT mask: the old
             * & 0xFFFFFF made correct $40xxxxxx PCs look 24-bit-"stripped" and sent
             * us chasing a phantom stripped-pointer bug for multiple sessions. */
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

  /* Line-buffer stdout so diagnostics (e.g. [LINE-F] at a crash) flush to a
   * redirected log immediately instead of sitting in the block buffer while
   * the CPU spins in the Sad Mac loop. */
  setvbuf(stdout, NULL, _IOLBF, 0);

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

  /* Figment debug print gate: HUGESE_VERBOSE=1 enables the $A0FE paravirt
   * debug trap output (figment's internal _CheckHeap / DbgMessage). */
  {
    const char *fv = getenv("HUGESE_VERBOSE");
    if (fv && fv[0] && fv[0] != '0') {
      figment_verbose = 1;
      printf("[FIGMENT] verbose debug output enabled (HUGESE_VERBOSE)\n");
    }
  }

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
  signal(SIGUSR2, sigusr2_handler);
  branch_ring_arm(1);  /* live-flag 24-bit-stripped jump targets ([STRIP-JUMP]) */

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

  // optional GDB remote stub — set GDB_PORT env to enable (e.g. 2159)
  {
    const char *gp = getenv("GDB_PORT");
    if (gp && *gp) {
      extern int gdbstub_init(int port);
      gdbstub_init(atoi(gp));
    }
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
  switch (cfg->platform->id) {
    case PLATFORM_MAC:
      /* Mac SE BBU clears OVL on first access to the ROM range. With I/O now
       * carried at full $40-prefixed physical (no 24-bit mask), the ROM lives
       * at ovl_sysrom_pos ($40800000); accept the 24-bit form too so OVL can't
       * get stuck during the transition. */
      if (ovl) {
        uint32_t a24 = addr & 0x00FFFFFF, rom24 = ovl_sysrom_pos & 0x00FFFFFF;
        if ((addr  >= ovl_sysrom_pos && addr  < ovl_sysrom_pos + 0x100000) ||
            (a24   >= rom24          && a24   < rom24 + 0x100000)) {
          ovl = 0;
          m68ki_cpu.ovl = 0;
          printf("[MAC] OVL off (read from ROM range %08X).\n", addr);
          handle_ovl_mappings_mac68k(cfg);
        }
      }
      /* Custom I/O handler — full $40xxxxxx physical; remaps to the SE bus via
       * the configured iomap windows (custom_read_mac68k → se_bus_addr). */
      if (cfg->platform->custom_read &&
          cfg->platform->custom_read(cfg, addr, &target, type) != -1) {
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

  if ((address >= 0x02000000u && address < 0x40000000u) || address >= 0x41000000u) {
    static int wr=0; if (wr++<24) printf("[WILD-RD8] addr=$%08X PC=$%08X\n", address, m68k_get_reg(NULL, M68K_REG_PC)); }
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

  if ((address >= 0x02000000u && address < 0x40000000u) || address >= 0x41000000u) {
    static int wr=0; if (wr++<24) printf("[WILD-RD16] addr=$%08X PC=$%08X\n", address, m68k_get_reg(NULL, M68K_REG_PC)); }
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

  if ((address >= 0x02000000u && address < 0x40000000u) || address >= 0x41000000u) {
    static int wr=0; if (wr++<24) printf("[WILD-RD32] addr=$%08X PC=$%08X\n", address, m68k_get_reg(NULL, M68K_REG_PC)); }
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
  switch (cfg->platform->id) {
    case PLATFORM_MAC: {
      /* OVL clear — same as the read path: ROM at full ovl_sysrom_pos, 24-bit
       * form accepted too so OVL can't get stuck during the transition. */
      if (ovl) {
        uint32_t a24 = addr & 0x00FFFFFF, rom24 = ovl_sysrom_pos & 0x00FFFFFF;
        if ((addr >= ovl_sysrom_pos && addr < ovl_sysrom_pos + 0x100000) ||
            (a24  >= rom24          && a24  < rom24 + 0x100000)) {
          ovl = 0;
          m68ki_cpu.ovl = 0;
          printf("[MAC] OVL off (write to ROM range %08X).\n", addr);
          handle_ovl_mappings_mac68k(cfg);
        }
      }
      /* Custom I/O write handler — full $40xxxxxx physical; remaps to the SE bus
       * via the configured iomap windows (custom_write_mac68k → se_bus_addr). */
      if (cfg->platform->custom_write &&
          cfg->platform->custom_write(cfg, addr, val, type) != -1) {
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
  /* [WILD-WR] a register holding a 24-bit-dirty value used as a RAM address:
   * outside 32MB RAM ($0-$01FFFFFF) and the $40xxxxxx ROM/IO window. This is
   * where the junk gets written into structures under flat IS=0 (a real SE's
   * 24-bit bus would have masked it back into RAM). */
  if ((address >= 0x02000000u && address < 0x40000000u) || address >= 0x41000000u) {
    static int ww=0; if (ww++<24) printf("[WILD-WR8] addr=$%08X <- $%02X PC=$%08X\n", address, value & 0xFF, m68k_get_reg(NULL, M68K_REG_PC)); }
  if (address >= 0x822u && address <= 0x827u) { static int sb=0; if (sb++<20) printf("[SCRNBASE-WR8] $%08X <- $%02X PC=$%08X\n", address, value & 0xFF, m68k_get_reg(NULL, M68K_REG_PC)); }
  { uint32_t pc=m68k_get_reg(NULL,M68K_REG_PC); if (address>=0x01FFA700u && address<0x01FFFC80u && pc!=g_last_scrn_pc) { g_last_scrn_pc=pc; static int s=0; if(s++<80) printf("[SCRN-WR8] $%08X <- $%02X PC=$%08X\n", address, value&0xFF, pc); } }
  if (bus_addr >= 0x2420 && bus_addr <= 0x2423)
    printf("[MP2420-WR8] addr=$%06X <- $%02X  PC=$%08X\n", bus_addr, value & 0xFF, m68k_get_reg(NULL, M68K_REG_PC));

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
  if ((address >= 0x02000000u && address < 0x40000000u) || address >= 0x41000000u) {
    static int ww=0; if (ww++<24) printf("[WILD-WR16] addr=$%08X <- $%04X PC=$%08X\n", address, value & 0xFFFF, m68k_get_reg(NULL, M68K_REG_PC)); }
  if (address >= 0x822u && address <= 0x827u) { static int sb=0; if (sb++<20) printf("[SCRNBASE-WR16] $%08X <- $%04X PC=$%08X\n", address, value & 0xFFFF, m68k_get_reg(NULL, M68K_REG_PC)); }
  { uint32_t pc=m68k_get_reg(NULL,M68K_REG_PC); if (address>=0x01FFA700u && address<0x01FFFC80u && pc!=g_last_scrn_pc) { g_last_scrn_pc=pc; static int s=0; if(s++<80) printf("[SCRN-WR16] $%08X <- $%04X PC=$%08X\n", address, value&0xFFFF, pc); } }
  if (bus_addr >= 0x2420 && bus_addr <= 0x2423)
    printf("[MP2420-WR16] addr=$%06X <- $%04X  PC=$%08X\n", bus_addr, value & 0xFFFF, m68k_get_reg(NULL, M68K_REG_PC));

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
  if ((address >= 0x02000000u && address < 0x40000000u) || address >= 0x41000000u) {
    static int ww=0; if (ww++<24) printf("[WILD-WR32] addr=$%08X <- $%08X PC=$%08X\n", address, value, m68k_get_reg(NULL, M68K_REG_PC)); }
  if (address >= 0x824u && address <= 0x827u) { static int sb=0; if (sb++<16) printf("[SCRNBASE-WR] $%08X <- $%08X PC=$%08X\n", address, value, m68k_get_reg(NULL, M68K_REG_PC)); }
  { uint32_t pc=m68k_get_reg(NULL,M68K_REG_PC); if (address>=0x01FFA700u && address<0x01FFFC80u && pc!=g_last_scrn_pc) { g_last_scrn_pc=pc; static int s=0; if(s++<80) printf("[SCRN-WR32] $%08X <- $%08X PC=$%08X\n", address, value, pc); } }
  if (bus_addr >= 0x2420 && bus_addr <= 0x2423)
    printf("[MP2420-WR32] addr=$%06X <- $%08X  PC=$%08X\n", bus_addr, value, m68k_get_reg(NULL, M68K_REG_PC));
  /* RM map-chain low-mem globals: TopMapHndl/SysMapHndl/CurMap/SuperMario-terminator */
  if (bus_addr==0x0A50||bus_addr==0x0A54||bus_addr==0x0A5A||bus_addr==0x0B84)
    printf("[RMGLOB-WR] $%04X <- $%08X  PC=$%08X\n", bus_addr, value, m68k_get_reg(NULL, M68K_REG_PC));

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

  /* PROBE: catch the .Sony driver ptr ($40855406/$00855406) being written into
   * a master-pointer slot, clobbering the System resource-map handle ($2420). */
  if ((value == 0x40855406 || value == 0x00855406) && bus_addr < 0x00010000) {
    static int sc = 0;
    if (sc++ < 20)
      printf("[SONY-CLOBBER] addr=$%06X ← $%08X  PC=$%08X\n",
             bus_addr, value, m68k_get_reg(NULL, M68K_REG_PC));
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

  /* born-32: repair the SE 5380 SCSI base stored in low-mem $0C00.
   * The ROM disk driver (and its RAM working copy at $434xxx) loads its
   * SCSI register base from $0C00 as a 24-bit $005FFxxx value, which the
   * flat 32MB RAM mapping shadows -> the bus-status poll ($1A54A / $4346xx)
   * spins. Relocate to the $40880000 remap region so $408FFxxx routes via
   * custom_read to the real SCSI controller. Fixing the global covers every
   * reader (both code copies), unlike a per-PC A3 repair. */
  { extern uint32_t ovl_sysrom_pos;
    if (ovl_sysrom_pos >= 0x40000000 && bus_addr == 0x0C00 &&
        (value & 0x00F80000) == 0x00580000) {
      uint32_t hi = value | 0x40000000;
      printf("[SCSIBASE-FIX] $0C00 $%08X -> $%08X  PC=$%08X\n",
             value, hi, m68k_get_reg(NULL, M68K_REG_PC));
      value = hi;
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
