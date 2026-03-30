// SPDX-License-Identifier: MIT

/*
  Original Copyright 2020 Claude Schwarz
  Code reorganized and rewritten by
  Niklas Ekström 2021 (https://github.com/niklasekstrom)
*/

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ps_protocol.h"
#include "m68k.h"

volatile unsigned int *gpio;
volatile unsigned int *gpclk;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;

unsigned int ps_peri_base = PERI_BASE_PI3;  /* default, overridden by detection */
static int pi_model = 3;  /* 3 = Pi 3, 4 = Pi 4 */

void ps_detect_pi_model(void) {
  FILE *f = fopen("/proc/device-tree/model", "r");
  if (f) {
    char model[128] = {0};
    fread(model, 1, sizeof(model)-1, f);
    fclose(f);
    if (strstr(model, "Pi 4") || strstr(model, "Pi 5")) {
      ps_peri_base = PERI_BASE_PI4;
      pi_model = 4;
      printf("[GPIO] Detected Pi 4/5: peripheral base $%08X\n", ps_peri_base);
    } else if (strstr(model, "Pi 3") || strstr(model, "Pi 2")) {
      ps_peri_base = PERI_BASE_PI3;
      pi_model = 3;
      printf("[GPIO] Detected Pi 2/3: peripheral base $%08X\n", ps_peri_base);
    } else if (strstr(model, "Pi Zero") || strstr(model, "Model B")) {
      ps_peri_base = PERI_BASE_PI0_1;
      pi_model = 1;
      printf("[GPIO] Detected Pi 0/1: peripheral base $%08X\n", ps_peri_base);
    } else {
      printf("[GPIO] Unknown model '%s', assuming Pi 3\n", model);
    }
  } else {
    printf("[GPIO] Cannot read /proc/device-tree/model, assuming Pi 3\n");
  }
}

static void setup_io() {
  {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
      printf("Unable to open /dev/mem. Run as root using sudo?\n");
      exit(-1);
    }
    void *gpio_map = mmap(NULL, BCM2708_PERI_SIZE,
                          PROT_READ | PROT_WRITE, MAP_SHARED,
                          fd, BCM2708_PERI_BASE);
    close(fd);
    if (gpio_map == MAP_FAILED) {
      printf("mmap failed, errno = %d\n", errno);
      exit(-1);
    }
    gpio = ((volatile unsigned *)gpio_map) + GPIO_ADDR / 4;
    gpclk = ((volatile unsigned *)gpio_map) + GPCLK_ADDR / 4;
    printf("[GPIO] Using /dev/mem, base=$%08X\n", ps_peri_base);
  }
}

static void setup_gpclk() {
  /* Enable 200MHz CLK output on GPIO4.
   * Pi 3: PLLC=1200MHz / 6 = 200MHz, source 5
   * Pi 4: PLLC=1000MHz / 5 = 200MHz, source 5
   *        (PLLD=750MHz, OSC=54MHz on Pi 4) */

  int clk_div, clk_src;
  if (pi_model >= 4) {
    clk_div = 3;   // PLLC_PER 600MHz / 3 = 200MHz (core_freq=600)
    clk_src = 5;   // pllc
    printf("[GPIO] Pi 4 GPCLK: PLLC/%d = 200MHz\n", clk_div);
  } else {
    clk_div = 6;   // PLLC 1200MHz / 6 = 200MHz
    clk_src = 5;   // pllc
  }

  /* Step 1: Kill the clock */
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);  /* KILL */
  usleep(10);
  while ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7))       /* wait not BUSY */
    ;
  usleep(100);

  /* Step 2: Set divider (integer only, no MASH) */
  *(gpclk + (CLK_GP0_DIV / 4)) = CLK_PASSWD | (clk_div << 12);
  usleep(10);

  /* Step 3: Set source, no enable yet */
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | clk_src;
  usleep(10);

  /* Step 4: Enable */
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | clk_src | (1 << 4);
  usleep(10);

  /* Wait for BUSY (clock running) */
  int timeout = 1000;
  while (((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0) {
    usleep(100);
    if (--timeout == 0) {
      printf("[GPIO] GPCLK never went BUSY!\n");
      break;
    }
  }
  usleep(100);

  /* Set GPIO4 to ALT0 (GPCLK0) */
  SET_GPIO_ALT(PIN_CLK, 0);

  printf("[GPIO] GPCLK CTL=0x%08X DIV=0x%08X\n",
         *(gpclk + (CLK_GP0_CTL / 4)),
         *(gpclk + (CLK_GP0_DIV / 4)));
}

static void setup_pullups() {
  if (pi_model >= 4) {
    /* BCM2711: GPIO_PUP_PDN_CNTRL_REG0-1 at offset 0xE4-0xE8
     * 2 bits per GPIO: 00=none, 01=up, 10=down
     * Pull-up on: TXN(0), IPL(1), RST(5), D0-D15(8-23)
     * No pull on: A0(2), A1(3), CLK(4), RD(6), WR(7) */
    volatile unsigned int *pup0 = gpio + (0xE4 / 4);  /* GPIO 0-15 */
    volatile unsigned int *pup1 = gpio + (0xE8 / 4);  /* GPIO 16-31 */
    unsigned int val0 = *pup0;
    unsigned int val1 = *pup1;
    /* GPIO 0-15 */
    for (int i = 0; i < 16; i++) {
      val0 &= ~(0x3 << (i * 2));
      if (i == 0 || i == 1 || i == 5 || (i >= 8 && i <= 15))
        val0 |= (0x1 << (i * 2));  /* pull-up */
    }
    *pup0 = val0;
    /* GPIO 16-23 */
    for (int i = 0; i < 8; i++) {
      val1 &= ~(0x3 << (i * 2));
      val1 |= (0x1 << (i * 2));  /* pull-up on D8-D15 */
    }
    *pup1 = val1;
    printf("[GPIO] Pi 4: configured pull-ups on GPIO 0-1, 5, 8-23\n");
  } else {
    /* BCM2835: legacy pull-up mechanism */
    GPIO_PULL = 2;  /* pull-up */
    usleep(10);
    GPIO_PULLCLK0 = (1 << PIN_TXN_IN_PROGRESS) | (1 << PIN_IPL_ZERO);
    usleep(10);
    GPIO_PULL = 0;
    GPIO_PULLCLK0 = 0;
  }
}

void ps_setup_protocol() {
  ps_detect_pi_model();
  setup_io();
  setup_pullups();

  /* Ensure GPIO4 is ALT0 (GPCLK0) FIRST — a previous run may have
   * left it as output, killing the CPLD clock. Restore it and wait
   * for any stuck bus cycle to complete before touching anything else. */
  INP_GPIO(PIN_CLK);
  SET_GPIO_ALT(PIN_CLK, 0);

  if (pi_model >= 4) {
    printf("[GPIO] Pi 4: restored GPIO4=ALT0, waiting for CPLD...\n");
    usleep(500000);  /* 500ms for stuck bus cycle to complete */
    printf("[GPIO] TXN=%d\n", (*(gpio + 13) >> PIN_TXN_IN_PROGRESS) & 1);
  }

  if (pi_model < 4)
    setup_gpclk();
  else
    printf("[GPIO] Pi 4: skipping GPCLK setup (use gpclk.ko module)\n");

  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  /* Release Mac from reset FIRST — the CPLD holds RESET_n low at power-on
   * (status[1]=0). Without releasing reset, the SE bus won't assert DTACK
   * and any stuck bus cycle can never complete. REG_STATUS writes work
   * even when TXN is stuck (they don't use the bus cycle state machine). */
  /* Full CPLD init: reset state machine, then release Mac from reset */
  printf("[GPIO] CPLD init: STATUS=INIT...\n");
  ps_write_status_reg(STATUS_BIT_INIT);
  usleep(1500);
  ps_write_status_reg(0);
  usleep(100);
  printf("[GPIO] CPLD init: STATUS=RESET (releasing Mac)...\n");
  ps_write_status_reg(0);
  usleep(100000);
  ps_write_status_reg(STATUS_BIT_RESET);
  usleep(100000);
  uint32_t status_readback = ps_read_status_reg();
  printf("[GPIO] Status readback=0x%04X, TXN=%d, RESET pin=%d\n",
         status_readback,
         (*(gpio + 13) >> PIN_TXN_IN_PROGRESS) & 1,
         (*(gpio + 13) >> PIN_RESET) & 1);

  /* If TXN is still stuck, the SE bus should now be able to respond */
  if ((*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {
    printf("[GPIO] TXN still stuck — writing ADDR_HI to complete stale cycle\n");
    GPFSEL_OUTPUT;
    GPIO_WRITEREG(REG_ADDR_HI, 0x0200);
    GPFSEL_INPUT;
    usleep(500000);
    printf("[GPIO] TXN=%d\n", (*(gpio + 13) >> PIN_TXN_IN_PROGRESS) & 1);
  }

  uint32_t lev = *(gpio + 13);
  printf("[GPIO] TXN=%d IPL=%d (GPLEV0=0x%08X)\n",
         (lev >> 0) & 1, (lev >> 1) & 1, lev);

  /* Step-by-step single read diagnostic */
  {
    printf("[GPIO] Step-by-step read from $400000...\n");
    GPFSEL_OUTPUT;
    printf("  1. GPFSEL=OUT:     GPLEV0=0x%08X\n", *(gpio+13));
    GPIO_WRITEREG(REG_ADDR_LO, 0x0000);
    printf("  2. After ADDR_LO:  GPLEV0=0x%08X TXN=%d\n", *(gpio+13), *(gpio+13)&1);
    GPIO_WRITEREG(REG_ADDR_HI, 0x0240);
    printf("  3. After ADDR_HI:  GPLEV0=0x%08X TXN=%d\n", *(gpio+13), *(gpio+13)&1);
    GPFSEL_INPUT;
    printf("  4. GPFSEL=IN:      GPLEV0=0x%08X\n", *(gpio+13));
    *(gpio + 7) = (REG_DATA << PIN_A0);
    GPIO_WAIT;
    printf("  5. A=DATA:         GPLEV0=0x%08X A0=%d A1=%d\n",
           *(gpio+13), (*(gpio+13)>>2)&1, (*(gpio+13)>>3)&1);
    *(gpio + 7) = 1 << PIN_RD;
    GPIO_WAIT;
    printf("  6. RD high:        GPLEV0=0x%08X RD=%d\n",
           *(gpio+13), (*(gpio+13)>>6)&1);
    int timeout = 1000000;
    while ((*(gpio+13) & 1) && --timeout > 0) {}
    printf("  7. TXN clear (%s): GPLEV0=0x%08X\n",
           timeout==0?"TIMEOUT":"ok", *(gpio+13));
    unsigned int raw = *(gpio+13);
    printf("  8. Data read:      GPLEV0=0x%08X → 0x%04X (expect 0xB2E3)\n",
           raw, (raw>>8)&0xFFFF);
    *(gpio + 10) = 0xFFFFEC;
    GPIO_WAIT;
  }

  /* ROM read-back test: read first 256 bytes from SE bus at $400000
   * and compare against our ROM file. */
  {
    printf("[GPIO] ROM readback test ($400000, 256 bytes)...\n");
    FILE *romf = fopen("big-se/se-rom.bin", "rb");
    if (romf) {
      uint8_t expected[256];
      fread(expected, 1, 256, romf);
      fclose(romf);
      int errors = 0;
      for (int i = 0; i < 256; i += 2) {
        uint32_t addr = 0x400000 + i;
        unsigned int got = ps_read_16(addr);
        uint16_t exp = (expected[i] << 8) | expected[i+1];
        if (got != exp) {
          if (errors < 20)
            printf("[ROM]  +$%02X: got=$%04X exp=$%04X\n", i, got, exp);
          errors++;
        }
      }
      printf("[ROM] %d/128 words correct, %d errors\n", 128 - errors, errors);
    } else {
      printf("[ROM] Cannot open se-rom.bin for comparison\n");
    }
  }

  /* Bus transaction stress test */
  printf("[GPIO] Stress test (1000 transactions)...\n");
  int pass = 0, fail = 0;
  for (int t = 0; t < 1000; t++) {
    uint32_t txn_before = *(gpio + 13) & 1;
    GPFSEL_OUTPUT;
    GPIO_WRITEREG(REG_ADDR_LO, 0x0000);
    uint32_t txn_after_lo = *(gpio + 13) & 1;
    GPIO_WRITEREG(REG_ADDR_HI, 0x0240);
    uint32_t txn_after_hi = *(gpio + 13) & 1;
    GPFSEL_INPUT;
    GPIO_PIN_RD;
    int timeout = 1000000;
    while ((*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) && --timeout > 0) {}
    unsigned int value = *(gpio + 13);
    *(gpio + 10) = 0xFFFFEC;
    GPIO_FLUSH; GPIO_SYNC;
    if (timeout == 0) {
      printf("[GPIO]   FAIL at %d: before=%d after_LO=%d after_HI=%d GPLEV0=0x%08X\n",
             t, txn_before, txn_after_lo, txn_after_hi, value);
      fail++;
      break;
    }
    pass++;
  }
  printf("[GPIO] Result: %d pass, %d fail\n", pass, fail);
}

void ps_write_16(unsigned int address, unsigned int data) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0000 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
}

void ps_write_8(unsigned int address, unsigned int data) {
  data &= 0xff;
  data |= (data << 8);  // 68000 replicates byte on both halves of data bus

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
}

void ps_write_32(unsigned int address, unsigned int value) {
  ps_write_16(address, value >> 16);
  ps_write_16(address + 2, value);
}

#define NOP asm("nop"); asm("nop");

unsigned int ps_read_16(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  return (value >> 8) & 0xffff;
}

unsigned int ps_read_8(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  unsigned int raw16 = (value >> 8) & 0xffff;
  if ((address & 1) == 0)
    return (raw16 >> 8) & 0xff;  // EVEN, A0=0,UDS
  else
    return raw16 & 0xff;  // ODD , A0=1,LDS
}

unsigned int ps_read_32(unsigned int address) {
  return (ps_read_16(address) << 16) | ps_read_16(address + 2);
}

// Paced IO: after each pseudo-DMA bus cycle, generate 3 dummy read cycles
// to a harmless RAM address. On a real 68000, the btst/dbra loop between
// pseudo-DMA reads generates ~3 instruction fetch bus cycles. The BBU expects
// to see this bus activity — it uses those cycles for DMA slots, state machine
// advancement, and /DACK deassertion to the NCR 5380. Without them, the BBU
// never lets DRQ cycle and pseudo-DMA reads return stale data (0x00).
// Use the raw GPIO bus cycle functions directly — ps_read_16() would hit
// the WTC RAM fast path in emulator.c and never generate a real bus cycle.
// Reading from ROM ($400000-$400004) matches what a real 68000 does —
// instruction fetches between SCSI accesses come from ROM, and the BBU
// decodes that region differently (immediate DTACK, no DMA contention).
void paced_dummy_cycles(void) {
  // Three word reads, simulating instruction fetches
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  for (int i = 0; i < 3; i++) {
    uint32_t addr = 0x400000 + (i * 2);  // ROM range — BBU responds immediately

    *(gpio + 7) = ((addr & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
    *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
    *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
    *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

    *(gpio + 7) = ((0x0200 | (addr >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
    *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
    *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
    *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

    *(gpio + 0) = GPFSEL0_INPUT;
    *(gpio + 1) = GPFSEL1_INPUT;
    *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

    *(gpio + 7) = (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
    *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;

    while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
    *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

    *(gpio + 0) = GPFSEL0_OUTPUT;
    *(gpio + 1) = GPFSEL1_OUTPUT;
    *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;
  }

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;
}

// Single dummy read cycle — minimum bus activity to let the BBU advance.
void paced_dummy_cycle_1(void) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x400000 & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0200 | (0x400000 >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;
}

#define PACED_DUMMY_CYCLES() paced_dummy_cycle_1()

unsigned int ps_read_8_paced(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  unsigned int raw16 = (value >> 8) & 0xffff;

  PACED_DUMMY_CYCLES();

  if ((address & 1) == 0)
    return (raw16 >> 8) & 0xff;  // EVEN, A0=0,UDS
  else
    return raw16 & 0xff;  // ODD , A0=1,LDS
}

// Like ps_read_8_paced but always returns upper byte (D8-D15).
// For devices whose data bus is wired to D8-D15 where the BBU doesn't
// always steer to D0-D7 (e.g. IWM during SET register accesses).
unsigned int ps_read_8_paced_hi(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  unsigned int raw16 = (value >> 8) & 0xffff;

  PACED_DUMMY_CYCLES();

  return (raw16 >> 8) & 0xff;  // Always D8-D15
}

void ps_write_8_paced(unsigned int address, unsigned int data) {
  data &= 0xff;
  data |= (data << 8);  // 68000 replicates byte on both halves of data bus

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}

  PACED_DUMMY_CYCLES();
}

void ps_write_status_reg(unsigned int value) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = ((value & 0xffff) << 8) | (REG_STATUS << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC; // delay
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC; // delay 210810
#endif
  *(gpio + 10) = 1 << PIN_WR;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
  GPIO_FLUSH; GPIO_SYNC;
}

unsigned int ps_read_status_reg() {
  *(gpio + 7) = (REG_STATUS << PIN_A0);
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC;
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC; // delay 210810
  *(gpio + 7) = 1 << PIN_RD;
  GPIO_FLUSH; GPIO_SYNC; // delay 210810
#endif

  /* Status read is combinational in the CPLD — no TXN involvement.
   * Just read the data pins directly, no need to wait. */
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;
  GPIO_FLUSH; GPIO_SYNC;

  return (value >> 8) & 0xffff;
}

void ps_reset_state_machine() {
  ps_write_status_reg(STATUS_BIT_INIT);
  usleep(1500);
  ps_write_status_reg(0);
  usleep(100);
}

void ps_pulse_reset() {
  ps_write_status_reg(0);
  usleep(100000);
  ps_write_status_reg(STATUS_BIT_RESET);
}

unsigned int ps_get_ipl_zero() {
  unsigned int value = *(gpio + 13);
  while ((value=*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {}
  return value & (1 << PIN_IPL_ZERO);
}

#define INT2_ENABLED 1

void ps_update_irq() {
  unsigned int ipl = 0;

  if (!ps_get_ipl_zero()) {
    unsigned int status = ps_read_status_reg();
    ipl = (status & 0xe000) >> 13;
  }

  /*if (ipl < 2 && INT2_ENABLED && emu_int2_req()) {
    ipl = 2;
  }*/

  m68k_set_irq(ipl);
}
