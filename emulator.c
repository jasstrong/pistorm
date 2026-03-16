// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "emulator.h"
#include "platforms/platforms.h"
#include "input/input.h"
#include "m68kcpu.h"

#include "platforms/amiga/Gayle.h"
#include "platforms/amiga/amiga-registers.h"
#include "platforms/amiga/amiga-interrupts.h"
#include "platforms/amiga/rtg/rtg.h"
#include "platforms/amiga/hunk-reloc.h"
#include "platforms/amiga/piscsi/piscsi.h"
#include "platforms/amiga/piscsi/piscsi-enums.h"
#include "platforms/amiga/net/pi-net.h"
#include "platforms/amiga/net/pi-net-enums.h"
#include "platforms/amiga/ahi/pi_ahi.h"
#include "platforms/amiga/ahi/pi-ahi-enums.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
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
/* #define DEBUG_DIAG */  /* Uncomment (or -DDEBUG_DIAG) for diagnostic counters & ring buffers */
#define KEY_POLL_INTERVAL_MSEC 5000

unsigned int ovl;

int kb_hook_enabled = 0;
int mouse_hook_enabled = 0;
int cpu_emulation_running = 1;
int swap_df0_with_dfx = 0;
int spoof_df0_id = 0;
int move_slow_to_chip = 0;
int force_move_slow_to_chip = 0;

uint8_t mouse_dx = 0, mouse_dy = 0;
uint8_t mouse_buttons = 0;
uint8_t mouse_extra = 0;

extern uint8_t gayle_int;
extern uint8_t gayle_ide_enabled;
extern uint8_t gayle_emulation_enabled;
extern uint8_t gayle_a4k_int;
extern volatile unsigned int *gpio;
extern volatile uint16_t srdata;
extern uint8_t realtime_graphics_debug, emulator_exiting;
extern uint8_t rtg_on;
extern uint32_t scsi_rom_low, scsi_rom_high;
extern uint8_t noscsi_enabled;
extern struct vnc_config vnc_cfg;
uint8_t realtime_disassembly, int2_enabled = 0;
uint32_t do_disasm = 0, old_level;
uint32_t last_irq = 0, last_last_irq = 0;

uint8_t ipl_enabled[8];

uint8_t end_signal = 0, load_new_config = 0;

char disasm_buf[4096];

#define KICKBASE 0xF80000
#define KICKSIZE 0x7FFFF

int mem_fd, mouse_fd = -1, keyboard_fd = -1;
int mem_fd_gpclk;
atomic_int irq = 0;
int gayleirq;

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
// IWM read log — ring buffer captures most recent N reads
#define IWM_LOG_SIZE 128
static struct { uint32_t addr; uint8_t val; uint32_t pc; } iwm_log_buf[IWM_LOG_SIZE];
static unsigned int iwm_log_head = 0;   // next write position (wraps)
static unsigned int iwm_log_total = 0;  // total IWM reads logged

// IWM mode tracking — persistent Q6/Q7 state + per-mode counters
// IWM register mapping: reg12=Q6off, reg13=Q6on, reg14=Q7off, reg15=Q7on
// Modes: Q6=0,Q7=0 → data; Q6=1,Q7=0 → status; Q6=0,Q7=1 → handshake; Q6=1,Q7=1 → write
static int iwm_q6 = 0, iwm_q7 = 0;
static unsigned int iwm_mode_rd[4] = {0};  // [q7*2+q6] read counts
static unsigned int iwm_hshk_ready = 0;    // handshake reads with bit7=1 (byte ready)
static unsigned int iwm_hshk_notready = 0; // handshake reads with bit7=0
static unsigned int iwm_data_zero = 0;     // data reg reads returning $00
static unsigned int iwm_data_nonzero = 0;  // data reg reads returning non-$00
static unsigned int iwm_data_ff = 0;       // data reg reads returning $FF (no data)
static unsigned int iwm_data_hi = 0;       // data reg reads with bit7=1 (valid GCR byte)
static unsigned int iwm_data_lo = 0;       // data reg reads with bit7=0 (no valid byte yet)
#endif

// Slow IO regions — addresses that need bus cycle delays to match real 68000 timing.
// Populated from MAPTYPE_SLOWIO entries in the config file.
#define MAX_SLOWIO_REGIONS 8
static struct { uint32_t lo; uint32_t hi; } slowio[MAX_SLOWIO_REGIONS];
static int slowio_count = 0;

static inline int is_slowio(uint32_t addr) {
  for (int i = 0; i < slowio_count; i++) {
    if (addr >= slowio[i].lo && addr < slowio[i].hi)
      return 1;
  }
  return 0;
}

// Populated from MAPTYPE_PACEDIO entries in the config file.
// Paced IO stretches the bus cycle itself (not inter-cycle delay like slowio).
#define MAX_PACEDIO_REGIONS 8
static struct { uint32_t lo; uint32_t hi; } pacedio[MAX_PACEDIO_REGIONS];
static int pacedio_count = 0;

static inline int is_pacedio(uint32_t addr) {
  for (int i = 0; i < pacedio_count; i++) {
    if (addr >= pacedio[i].lo && addr < pacedio[i].hi)
      return 1;
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
static inline void slowio_delay(void) {
  slowio_hit_count++;
  for (volatile int dly = 0; dly < 150; dly++) ;
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

uint16_t irq_delay = 0;
unsigned int amiga_reset=0, amiga_reset_last=0;
unsigned int do_reset=0;

void *ipl_task(void *args) {
  printf("IPL thread running\n");
  uint32_t value;
  while (1) {
    value = *(gpio + 13);
    if (value & (1 << PIN_TXN_IN_PROGRESS))
      goto noppers;

    if (!(value & (1 << PIN_IPL_ZERO)) || ipl_enabled[amiga_emulated_ipl()]) {
      if (!atomic_load(&irq)) {
        M68K_END_TIMESLICE;
        atomic_store(&irq, 1);
#ifdef DEBUG_DIAG
        atomic_fetch_add(&dbg_ipl_assert, 1);
#endif
      }
    }
    if(do_reset==0)
    {
      amiga_reset=(value & (1 << PIN_RESET));
      if(amiga_reset!=amiga_reset_last)
      {
        amiga_reset_last=amiga_reset;
        if(amiga_reset==0)
        {
          printf("Amiga Reset is down...\n");
          do_reset=1;
          M68K_END_TIMESLICE;
        }
        else
        {
          printf("Amiga Reset is up...\n");
        }
      }
    }

    /*if (gayle_ide_enabled) {
      if (((gayle_int & 0x80) || gayle_a4k_int) && (get_ide(0)->drive[0].intrq || get_ide(0)->drive[1].intrq)) {
        //get_ide(0)->drive[0].intrq = 0;
        gayleirq = 1;
        M68K_END_TIMESLICE;
      }
      else
        gayleirq = 0;
    }*/
    //usleep(0);
    //NOP NOP
noppers:
    NOP NOP NOP NOP NOP NOP NOP NOP
    //NOP NOP NOP NOP NOP NOP NOP NOP
    //NOP NOP NOP NOP NOP NOP NOP NOP
    /*NOP NOP NOP NOP NOP NOP NOP NOP
    NOP NOP NOP NOP NOP NOP NOP NOP
    NOP NOP NOP NOP NOP NOP NOP NOP*/
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

			/* Mac SE ROM SCSI delay loop at $41A816: moveq #$30,d0; dbra d0,*
			 * On real 68000 @ 8MHz this burns ~60us. On PiStorm it's instant.
			 * Without this delay, SCSI phase match checks run before the
			 * target has time to transition phases. */
			if (REG_PC == 0x0041A816 || REG_PC == 0x0041A8BC) {
				usleep(60);
			}

			/* Mac SE ROM SCSI byte-by-byte pseudo-DMA loops.
			 * These use move.b from $5FF260 (pseudo-DMA address) with btst/dbra.
			 * On a real 68000, each iteration takes ~5us. On PiStorm the loop
			 * runs so fast that the BBU can't complete its /DTACK+/DACK
			 * handshake for the pseudo-DMA read/write between iterations.
			 *   $41A5A6: byte-by-byte READ  (btst d3,$0050(a3))
			 *   $41A582: byte-by-byte VERIFY (btst d3,$0050(a3))
			 *   $41A5C4: byte-by-byte WRITE  (btst d3,$0050(a3))
			 */
			if (REG_PC == 0x0041A5A6 || REG_PC == 0x0041A582 || REG_PC == 0x0041A5C4) {
				usleep(5);
			}

			/* Record previous program counter */
			REG_PPC = REG_PC;

			/* PC trace ring buffer for crash diagnostics */
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
        printf("[DIAG] SonyVec($226)=%08X\n", m68k_read_memory_32(0x226));
        printf("[DIAG] jSonyPatch($B40)=%08X\n", m68k_read_memory_32(0xB40));
        printf("[DIAG] IWMBase($1E0)=%08X\n", m68k_read_memory_32(0x1E0));
        printf("[DIAG] VIABase($1D4)=%08X\n", m68k_read_memory_32(0x1D4));
        printf("[DIAG] SonyVars($134)=%08X\n", m68k_read_memory_32(0x134));
        printf("[DIAG] JIODone($8FC)=%08X\n", m68k_read_memory_32(0x8FC));
        printf("[DIAG] DskErr($142)=%04X\n", m68k_read_memory_16(0x142));
        // Dump stack
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
        printf("[DIAG] SP=%08X stack:", sp);
        for (int i = 0; i < 12; i++)
          printf(" %04X", m68k_read_memory_16(sp + i * 2));
        printf("\n");
        // SonyVars structure (first 32 bytes)
        uint32_t sv = m68k_read_memory_32(0x134);
        if (sv) {
          printf("[DIAG] SonyVars:");
          for (int i = 0; i < 32; i += 2)
            printf(" %04X", m68k_read_memory_16(sv + i));
          printf("\n");
        }
        // Dump captured IWM reads (ring buffer — most recent IWM_LOG_SIZE entries)
        {
          unsigned int n = (iwm_log_head < IWM_LOG_SIZE) ? iwm_log_head : IWM_LOG_SIZE;
          unsigned int start = (iwm_log_head < IWM_LOG_SIZE) ? 0 : (iwm_log_head - IWM_LOG_SIZE);
          printf("[DIAG] IWM total=%u showing last %u\n", iwm_log_total, n);
          for (unsigned int i = 0; i < n; i++) {
            unsigned int idx = (start + i) % IWM_LOG_SIZE;
            uint32_t reg = (iwm_log_buf[idx].addr - 0xDFE1FF) >> 9;
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
      printf("[IWM-MODE] data=%u status=%u hshk=%u write=%u Q6=%d Q7=%d\n",
             iwm_mode_rd[0], iwm_mode_rd[1], iwm_mode_rd[2], iwm_mode_rd[3],
             iwm_q6, iwm_q7);
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

  if (do_reset) {
    cpu_pulse_reset();
    do_reset=0;
    usleep(1000000); // 1sec
    rtg_on=0;
//    while(amiga_reset==0);
//    printf("CPU emulation reset.\n");
  }

  if (mouse_hook_enabled && (mouse_extra != 0x00)) {
    // mouse wheel events have occurred; unlike l/m/r buttons, these are queued as keypresses, so add to end of buffer
    switch (mouse_extra) {
      case 0xff:
        // wheel up
        queue_keypress(0xfe, KEYPRESS_PRESS, PLATFORM_AMIGA);
        break;
      case 0x01:
        // wheel down
        queue_keypress(0xff, KEYPRESS_PRESS, PLATFORM_AMIGA);
        break;
    }

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
    if (cfg->platform->id == PLATFORM_AMIGA && last_irq != 2 && get_num_kb_queued()) {
      amiga_emulate_irq(PORTS);
    }
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
        if (queue_keypress(c_code, c_type, cfg->platform->id)) {
          if (cfg->platform->id == PLATFORM_AMIGA && last_irq != 2) {
            amiga_emulate_irq(PORTS);
          }
        }
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

void sigint_handler(int sig_num) {
  //if (sig_num) { }
  //cpu_emulation_running = 0;

  //return;
  printf("Received sigint %d, exiting.\n", sig_num);
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

  exit(0);
}

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
          set_pistorm_devcfg_filename(argv[g]);
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
    uint8_t config_action = load_new_config - 1;
    load_new_config = 0;
    if (cfg) {
      free_config_file(cfg);
      free(cfg);
      cfg = NULL;
    }

    switch(config_action) {
      case PICFG_LOAD:
      case PICFG_RELOAD:
        cfg = load_config_file(get_pistorm_devcfg_filename());
        break;
      case PICFG_DEFAULT:
        cfg = load_config_file("default.cfg");
        break;
    }
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
        printf("[SLOWIO] Region %d: %08X-%08X\n", slowio_count,
               slowio[slowio_count].lo, slowio[slowio_count].hi);
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
        printf("[PACEDIO] Region %d: %08X-%08X\n", pacedio_count,
               pacedio[pacedio_count].lo, pacedio[pacedio_count].hi);
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

  InitGayle();

  signal(SIGINT, sigint_handler);

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
static uint32_t platform_res, rres;

uint8_t cdtv_dmac_reg_idx_read();
void cdtv_dmac_reg_idx_write(uint8_t value);
uint32_t cdtv_dmac_read(uint32_t address, uint8_t type);
void cdtv_dmac_write(uint32_t address, uint32_t value, uint8_t type);

unsigned int garbage = 0;

static inline uint32_t ps_read(uint8_t type, uint32_t addr) {
  switch (type) {
    case OP_TYPE_BYTE:
      return ps_read_8(addr);
    case OP_TYPE_WORD:
      return ps_read_16(addr);
    case OP_TYPE_LONGWORD:
      return ps_read_32(addr);
  }
  // This shouldn't actually happen.
  return 0;
}

static inline void ps_write(uint8_t type, uint32_t addr, uint32_t val) {
  switch (type) {
    case OP_TYPE_BYTE:
      ps_write_8(addr, val);
      return;
    case OP_TYPE_WORD:
      ps_write_16(addr, val);
      return;
    case OP_TYPE_LONGWORD:
      ps_write_32(addr, val);
      return;
  }
  // This shouldn't actually happen.
  return;
}

static inline int32_t platform_read_check(uint8_t type, uint32_t addr, uint32_t *res) {
  switch (cfg->platform->id) {
    case PLATFORM_AMIGA:
      switch (addr) {
        case INTREQR:
          return amiga_handle_intrqr_read(res);
          break;
        case CIAAPRA:
          if (mouse_hook_enabled && (mouse_buttons & 0x01)) {
            rres = (uint32_t)ps_read(type, addr);
            *res = (rres ^ 0x40);
            return 1;
          }
          if (swap_df0_with_dfx && spoof_df0_id) {
            // DF0 doesn't emit a drive type ID on RDY pin
            // If swapping DF0 with DF1-3 we need to provide this ID so that DF0 continues to function.
            rres = (uint32_t)ps_read(type, addr);
            *res = (rres & 0xDF); // Spoof drive id for swapped DF0 by setting RDY low
            return 1;
          }
          return 0;
          break;
        case CIAAICR:
          if (kb_hook_enabled && get_num_kb_queued() && amiga_emulating_irq(PORTS)) {
            *res = 0x88;
            return 1;
          }
          return 0;
          break;
        case CIAADAT:
          if (kb_hook_enabled && amiga_emulating_irq(PORTS)) {
            uint8_t c = 0, t = 0;
            pop_queued_key(&c, &t);
            t ^= 0x01;
            rres = ((c << 1) | t) ^ 0xFF;
            *res = rres;
            return 1;
          }
          return 0;
          break;
        case JOY0DAT:
          if (mouse_hook_enabled) {
            unsigned short result = (mouse_dy << 8) | (mouse_dx);
            *res = (unsigned int)result;
            return 1;
          }
          return 0;
          break;
        case INTENAR: {
          // This code is kind of strange and should probably be reworked/revoked.
          uint8_t enable = 1;
          rres = (uint16_t)ps_read(type, addr);
          uint16_t val = rres;
          if (val & 0x0007) {
            ipl_enabled[1] = enable;
          }
          if (val & 0x0008) {
            ipl_enabled[2] = enable;
          }
          if (val & 0x0070) {
            ipl_enabled[3] = enable;
          }
          if (val & 0x0780) {
            ipl_enabled[4] = enable;
          }
          if (val & 0x1800) {
            ipl_enabled[5] = enable;
          }
          if (val & 0x2000) {
            ipl_enabled[6] = enable;
          }
          if (val & 0x4000) {
            ipl_enabled[7] = enable;
          }
          //printf("Interrupts enabled: M:%d 0-6:%d%d%d%d%d%d\n", ipl_enabled[7], ipl_enabled[6], ipl_enabled[5], ipl_enabled[4], ipl_enabled[3], ipl_enabled[2], ipl_enabled[1]);
          *res = rres;
          return 1;
          break;
        }
        case POTGOR:
          if (mouse_hook_enabled) {
            unsigned short result = (unsigned short)ps_read(type, addr);
            // bit 1 rmb, bit 2 mmb
            if (mouse_buttons & 0x06) {
              *res = (unsigned int)((result ^ ((mouse_buttons & 0x02) << 9))   // move rmb to bit 10
                                  & (result ^ ((mouse_buttons & 0x04) << 6))); // move mmb to bit 8
              return 1;
            }
            *res = (unsigned int)(result & 0xfffd);
            return 1;
          }
          return 0;
          break;
        case CIABPRB:
          if (swap_df0_with_dfx) {
            uint32_t result = (uint32_t)ps_read(type, addr);
            // SEL0 = 0x80, SEL1 = 0x10, SEL2 = 0x20, SEL3 = 0x40
            if (((result >> SEL0_BITNUM) & 1) != ((result >> (SEL0_BITNUM + swap_df0_with_dfx)) & 1)) { // If the value for SEL0/SELx differ
              result ^= ((1 << SEL0_BITNUM) | (1 << (SEL0_BITNUM + swap_df0_with_dfx)));                // Invert both bits to swap them around
            }
            *res = result;
            return 1;
          }
          return 0;
          break;
        default:
          break;
      }

      if (move_slow_to_chip && addr >= 0x080000 && addr <= 0x0FFFFF) {
        // A500 JP2 connects Agnus' A19 input to A23 instead of A19 by default, and decodes trapdoor memory at 0xC00000 instead of 0x080000.
        // We can move the trapdoor to chipram simply by rewriting the address.
        addr += 0xB80000;
        *res = ps_read(type, addr);
        return 1;
      }

      if (move_slow_to_chip && addr >= 0xC00000 && addr <= 0xC7FFFF) {
        // Block accesses through to trapdoor at slow ram address, otherwise it will be detected at 0x080000 and 0xC00000.
        *res = 0;
        return 1;
      }

      if (addr >= cfg->custom_low && addr < cfg->custom_high) {
        if (addr >= PISCSI_OFFSET && addr < PISCSI_UPPER) {
          *res = handle_piscsi_read(addr, type);
          return 1;
        }
        if (addr >= PINET_OFFSET && addr < PINET_UPPER) {
          *res = handle_pinet_read(addr, type);
          return 1;
        }
        if (addr >= PIGFX_RTG_BASE && addr < PIGFX_UPPER) {
          *res = rtg_read((addr & 0x0FFFFFFF), type);
          return 1;
        }
        if (addr >= PI_AHI_OFFSET && addr < PI_AHI_UPPER) {
          *res = handle_pi_ahi_read(addr, type);
          return 1;
        }
        if (custom_read_amiga(cfg, addr, &target, type) != -1) {
          *res = target;
          return 1;
        }
      }

      break;
    case PLATFORM_MAC:
      /* Mac SE BBU clears OVL on first access to ROM/SCSI range */
      if (ovl && addr >= 0x400000 && addr < 0x600000) {
        ovl = 0;
        m68ki_cpu.ovl = 0;
        printf("[MAC] OVL off (read from ROM/SCSI range %08X).\n", addr);
        handle_ovl_mappings_mac68k(cfg);
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

  // 68000 has 24-bit address bus — mask upper 8 bits
  address &= 0x00FFFFFF;

  if (platform_read_check(OP_TYPE_BYTE, address, &platform_res)) {
    return platform_res;
  }

#ifdef DEBUG_DIAG
  if (address >= 0x800000) rd8_hi++;
  if (address >= 0xDFE1FF && address <= 0xDFFFFF) rd8_iwm++;
  if (address >= 0xEFE1FE && address <= 0xEFFFFF) rd8_via++;
  if (address >= 0x580000 && address <= 0x5FFFFF)
    rd8_scsi++;
#endif

  // noscsi bypass: return 0 for all SCSI reads without hitting GPIO
  if (noscsi_enabled && address >= 0x580000 && address <= 0x5FFFFF) {
    return 0;
  }

  if (is_slowio(address))
    slowio_delay();

  unsigned int val;
  if (is_pacedio(address)) {
#ifdef DEBUG_DIAG
    pacedio_hit_count++;
#endif
    val = (unsigned int)ps_read_8_paced((uint32_t)address);
  } else {
    val = (unsigned int)ps_read_8((uint32_t)address);
  }

#ifdef DEBUG_DIAG
  if (address >= 0x580000 && address <= 0x5FFFFF) {
    // Print first 64 SCSI reads in detail
    if (scsi_log_total < 64) {
      printf("[SCSI-RD] #%u addr=%06X val=%02X pc=%08X\n",
             scsi_log_total, address, val & 0xFF, m68k_get_reg(NULL, M68K_REG_PC));
    }
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
  // IWM access capture: $DFE1FF-$DFFFFF (ring buffer + mode tracking)
  if (address >= 0xDFE1FF && address <= 0xDFFFFF) {
    uint32_t iwm_pc = m68k_get_reg(NULL, M68K_REG_PC);
    unsigned int iwm_reg = (address - 0xDFE1FF) >> 9;
    static const char *iwm_reg_names[] = {
      "ph0L","ph0H","ph1L","ph1H","ph2L","ph2H","ph3L","ph3H",
      "mtrOff","mtrOn","intDrv","extDrv","Q6L","Q6H","Q7L","Q7H"
    };
    // Print first 64 IWM accesses in detail
    if (iwm_log_total < 64) {
      printf("[IWM-RD] #%u reg%u(%s) addr=%06X val=%02X pc=%08X Q6=%d Q7=%d\n",
             iwm_log_total, iwm_reg,
             iwm_reg < 16 ? iwm_reg_names[iwm_reg] : "?",
             address, val & 0xFF, iwm_pc, iwm_q6, iwm_q7);
    }
    unsigned int idx = iwm_log_head % IWM_LOG_SIZE;
    iwm_log_buf[idx].addr = address;
    iwm_log_buf[idx].val = val;
    iwm_log_buf[idx].pc = iwm_pc;
    iwm_log_head++;
    iwm_log_total++;

    // Track Q6/Q7 state changes (reg12=Q6off, reg13=Q6on, reg14=Q7off, reg15=Q7on)
    unsigned int reg = (address - 0xDFE1FF) >> 9;
    if (reg == 12) iwm_q6 = 0;
    else if (reg == 13) iwm_q6 = 1;
    else if (reg == 14) iwm_q7 = 0;
    else if (reg == 15) iwm_q7 = 1;

    // Count reads by current mode and track key values
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
  // 68000 has 24-bit address bus — mask upper 8 bits
  address &= 0x00FFFFFF;

  if (platform_read_check(OP_TYPE_WORD, address, &platform_res)) {
    return platform_res;
  }

  if (is_slowio(address))
    slowio_delay();

  uint32_t result16;
  if (address & 0x01) {
    result16 = ((ps_read_8(address) << 8) | ps_read_8(address + 1));
  } else {
    result16 = (unsigned int)ps_read_16((uint32_t)address);
  }

  return result16;
}

unsigned int m68k_read_memory_32(unsigned int address) {
  // 68000 has 24-bit address bus — mask upper 8 bits
  address &= 0x00FFFFFF;

  if (platform_read_check(OP_TYPE_LONGWORD, address, &platform_res)) {
    return platform_res;
  }

  if (is_slowio(address))
    slowio_delay();

  uint32_t result32;
  if (address & 0x01) {
    uint32_t c = ps_read_8(address);
    c |= (be16toh(ps_read_16(address+1)) << 8);
    c |= (ps_read_8(address + 3) << 24);
    result32 = htobe32(c);
  } else {
    uint16_t a = ps_read_16(address);
    uint16_t b = ps_read_16(address + 2);
    result32 = (a << 16) | b;
  }

  return result32;
}

static inline int32_t platform_write_check(uint8_t type, uint32_t addr, uint32_t val) {
  switch (cfg->platform->id) {
    case PLATFORM_MAC: {
      /* Debug: log first writes to VIA range */
      static int via_dbg = 0;
      if (addr >= 0xE00000 && addr <= 0xEFFFFF && via_dbg < 20) {
        printf("[MAC-VIA] write addr=%08X val=%02X type=%d\n", addr, val, type);
        via_dbg++;
      }
      /* Mac SE BBU clears OVL on the first access to the ROM/SCSI
       * address range ($400000-$5FFFFF).  "Designing Cards and Drivers
       * for Macintosh II and Macintosh SE" (1987), p12-6:
       * "Following the first normal ROM access or SCSI access
       *  ($40 0000 through $5F FFFF), RAM appears at $00 0000."
       * OVL is only re-asserted by a hardware reset.
       */
      if (ovl && addr >= 0x400000 && addr < 0x600000) {
        ovl = 0;
        m68ki_cpu.ovl = 0;
        printf("[MAC] OVL off (write to ROM/SCSI range %08X).\n", addr);
        handle_ovl_mappings_mac68k(cfg);
      }
      break;
    }
    case PLATFORM_AMIGA:
      switch (addr) {
        case INTREQ:
          return amiga_handle_intrq_write(val);
          break;
        case CIAAPRA:
          if (ovl != (val & (1 << 0))) {
            ovl = (val & (1 << 0));
            m68ki_cpu.ovl = ovl;
            printf("OVL:%x\n", ovl);
          }
          return 0;
          break;
        case SERDAT: {
          char *serdat = (char *)&val;
          // SERDAT word. see amiga dev docs appendix a; upper byte is control codes, and bit 0 is always 1.
          // ignore this upper byte as it's not viewable data, only display lower byte.
          printf("%c", serdat[0]);
          return 0;
          break;
        }
        case INTENA: {
          // This code is kind of strange and should probably be reworked/revoked.
          uint8_t enable = 1;
          if (!(val & 0x8000))
            enable = 0;
          if (val & 0x0007) {
            ipl_enabled[1] = enable;
          }
          if (val & 0x0008) {
            ipl_enabled[2] = enable;
          }
          if (val & 0x0070) {
            ipl_enabled[3] = 1;
          }
          if (val & 0x0780) {
            ipl_enabled[4] = enable;
          }
          if (val & 0x1800) {
            ipl_enabled[5] = enable;
          }
          if (val & 0x2000) {
            ipl_enabled[6] = enable;
          }
          if (val & 0x4000 && enable) {
            ipl_enabled[7] = 1;
          }
          //printf("Interrupts enabled: M:%d 0-6:%d%d%d%d%d%d\n", ipl_enabled[7], ipl_enabled[6], ipl_enabled[5], ipl_enabled[4], ipl_enabled[3], ipl_enabled[2], ipl_enabled[1]);
          return 0;
          break;
        }
        case CIABPRB:
          if (swap_df0_with_dfx) {
            if ((val & ((1 << (SEL0_BITNUM + swap_df0_with_dfx)) | 0x80)) == 0x80) {
              // If drive selected but motor off, Amiga is reading drive ID.
              spoof_df0_id = 1;
            } else {
              spoof_df0_id = 0;
            }

            if (((val >> SEL0_BITNUM) & 1) != ((val >> (SEL0_BITNUM + swap_df0_with_dfx)) & 1)) { // If the value for SEL0/SELx differ
              val ^= ((1 << SEL0_BITNUM) | (1 << (SEL0_BITNUM + swap_df0_with_dfx)));             // Invert both bits to swap them around
            }
            ps_write(type,addr,val);
            return 1;
          }
          return 0;
          break;
        default:
          break;
      }

      if (move_slow_to_chip && addr >= 0x080000 && addr <= 0x0FFFFF) {
        // A500 JP2 connects Agnus' A19 input to A23 instead of A19 by default, and decodes trapdoor memory at 0xC00000 instead of 0x080000.
        // We can move the trapdoor to chipram simply by rewriting the address.
        addr += 0xB80000;
        ps_write(type,addr,val);
        return 1;
      }

      if (move_slow_to_chip && addr >= 0xC00000 && addr <= 0xC7FFFF) {
        // Block accesses through to trapdoor at slow ram address, otherwise it will be detected at 0x080000 and 0xC00000.
        return 1;
      }

      if (addr >= cfg->custom_low && addr < cfg->custom_high) {
        if (addr >= PISCSI_OFFSET && addr < PISCSI_UPPER) {
          handle_piscsi_write(addr, val, type);
          return 1;
        }
        if (addr >= PINET_OFFSET && addr < PINET_UPPER) {
          handle_pinet_write(addr, val, type);
          return 1;
        }
        if (addr >= PIGFX_RTG_BASE && addr < PIGFX_UPPER) {
          rtg_write((addr & 0x0FFFFFFF), val, type);
          return 1;
        }
        if (addr >= PI_AHI_OFFSET && addr < PI_AHI_UPPER) {
          handle_pi_ahi_write(addr, val, type);
          return 1;
        }
        if (custom_write_amiga(cfg, addr, val, type) != -1) {
          return 1;
        }
      }

      break;
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
  // 68000 has 24-bit address bus — mask upper 8 bits
  address &= 0x00FFFFFF;

  if (address >= 0x580000 && address <= 0x5FFFFF) {
#ifdef DEBUG_DIAG
    wr8_scsi++;
    // Print first 64 SCSI writes in detail
    if (wr8_scsi <= 64) {
      printf("[SCSI-WR] #%u addr=%06X val=%02X pc=%08X\n",
             wr8_scsi, address, value & 0xFF, m68k_get_reg(NULL, M68K_REG_PC));
    }
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

  if (is_slowio(address))
    slowio_delay();

  // noscsi bypass: swallow all SCSI writes without hitting GPIO
  if (noscsi_enabled && address >= 0x580000 && address <= 0x5FFFFF) {
    return;
  }

  // SCSI byte lane: the NCR 5380 is on D8-D15 (upper byte lane). ROM writes
  // to odd addresses, putting data on D0-D7. The BBU steers D0-D7→D8-D15
  // when the CPLD generates properly timed bus cycles. Byte replication in
  // ps_write_8 ensures D8-D15 also has the correct value.
  if (address >= 0x580000 && address <= 0x5FFFFF) {
    int paced = is_pacedio(address);
#ifdef DEBUG_DIAG
    if (paced) pacedio_hit_count++;
#endif
    if (paced)
      ps_write_8_paced((uint32_t)address, value);
    else
      ps_write_8((uint32_t)address, value);
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
#endif

  if (is_pacedio(address)) {
#ifdef DEBUG_DIAG
    pacedio_hit_count++;
#endif
    ps_write_8_paced((uint32_t)address, value);
  } else {
    ps_write_8((uint32_t)address, value);
  }
  return;
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
  // 68000 has 24-bit address bus — mask upper 8 bits
  address &= 0x00FFFFFF;

  if (platform_write_check(OP_TYPE_WORD, address, value))
    return;

#ifdef DEBUG_MAC_IO
  if (address == 0x142) {
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

  if (is_slowio(address))
    slowio_delay();

  if (address & 0x01) {
    ps_write_8((uint32_t)address, value & 0xFF);
    ps_write_8((uint32_t)address + 1, (value >> 8) & 0xFF);
    return;
  }

  ps_write_16((uint32_t)address, value);
  return;
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
  // 68000 has 24-bit address bus — mask upper 8 bits
  address &= 0x00FFFFFF;

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

  if (is_slowio(address))
    slowio_delay();

  if (address & 0x01) {
    ps_write_8((uint32_t)address, value & 0xFF);
    ps_write_16((uint32_t)address + 1, htobe16(((value >> 8) & 0xFFFF)));
    ps_write_8((uint32_t)address + 3, (value >> 24));
    return;
  }

  ps_write_16((uint32_t)address, value >> 16);
  ps_write_16((uint32_t)address + 2, value);
  return;
}
