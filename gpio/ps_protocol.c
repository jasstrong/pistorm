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

static void setup_io() {
  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) {
    printf("Unable to open /dev/mem. Run as root using sudo?\n");
    exit(-1);
  }

  void *gpio_map = mmap(
      NULL,                    // Any adddress in our space will do
      BCM2708_PERI_SIZE,       // Map length
      PROT_READ | PROT_WRITE,  // Enable reading & writting to mapped memory
      MAP_SHARED,              // Shared with other processes
      fd,                      // File to map
      BCM2708_PERI_BASE        // Offset to GPIO peripheral
  );

  close(fd);

  if (gpio_map == MAP_FAILED) {
    printf("mmap failed, errno = %d\n", errno);
    exit(-1);
  }

  gpio = ((volatile unsigned *)gpio_map) + GPIO_ADDR / 4;
  gpclk = ((volatile unsigned *)gpio_map) + GPCLK_ADDR / 4;
}

static void setup_gpclk() {
  // Enable 200MHz CLK output on GPIO4, adjust divider and pll source depending
  // on pi model
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);
  usleep(10);
  while ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7))
    ;
  usleep(100);
  *(gpclk + (CLK_GP0_DIV / 4)) =
      CLK_PASSWD | (6 << 12);  // divider , 6=200MHz on pi3
  usleep(10);
  *(gpclk + (CLK_GP0_CTL / 4)) =
      CLK_PASSWD | 5 | (1 << 4);  // pll? 6=plld, 5=pllc
  usleep(10);
  while (((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0)
    ;
  usleep(100);

  SET_GPIO_ALT(PIN_CLK, 0);  // gpclk0
}

void ps_setup_protocol() {
  setup_io();
  setup_gpclk();

  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}

void ps_write_16(unsigned int address, unsigned int data) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0000 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
}

void ps_write_8(unsigned int address, unsigned int data) {
  data &= 0xff;
  data |= (data << 8);  // 68000 replicates byte on both halves of data bus

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

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

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  return (value >> 8) & 0xffff;
}

unsigned int ps_read_8(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  value = (value >> 8) & 0xffff;

  if ((address & 1) == 0)
    return (value >> 8) & 0xff;  // EVEN, A0=0,UDS
  else
    return value & 0xff;  // ODD , A0=1,LDS
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

  for (int i = 0; i < 3; i++) {
    uint32_t addr = 0x400000 + (i * 2);  // ROM range — BBU responds immediately

    *(gpio + 7) = ((addr & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
    *(gpio + 7) = 1 << PIN_WR;
    *(gpio + 10) = 1 << PIN_WR;
    *(gpio + 10) = 0xffffec;

    *(gpio + 7) = ((0x0200 | (addr >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
    *(gpio + 7) = 1 << PIN_WR;
    *(gpio + 10) = 1 << PIN_WR;
    *(gpio + 10) = 0xffffec;

    *(gpio + 0) = GPFSEL0_INPUT;
    *(gpio + 1) = GPFSEL1_INPUT;
    *(gpio + 2) = GPFSEL2_INPUT;

    *(gpio + 7) = (REG_DATA << PIN_A0);
    *(gpio + 7) = 1 << PIN_RD;

    while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
    *(gpio + 10) = 0xffffec;

    *(gpio + 0) = GPFSEL0_OUTPUT;
    *(gpio + 1) = GPFSEL1_OUTPUT;
    *(gpio + 2) = GPFSEL2_OUTPUT;
  }

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}

// Single dummy read cycle — minimum bus activity to let the BBU advance.
void paced_dummy_cycle_1(void) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((0x400000 & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0200 | (0x400000 >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  *(gpio + 10) = 0xffffec;
}

#define PACED_DUMMY_CYCLES() paced_dummy_cycle_1()

unsigned int ps_read_8_paced(unsigned int address) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}
  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  PACED_DUMMY_CYCLES();

  value = (value >> 8) & 0xffff;

  if ((address & 1) == 0)
    return (value >> 8) & 0xff;  // EVEN, A0=0,UDS
  else
    return value & 0xff;  // ODD , A0=1,LDS
}

void ps_write_8_paced(unsigned int address, unsigned int data) {
  data &= 0xff;
  data |= (data << 8);  // 68000 replicates byte on both halves of data bus

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}

  PACED_DUMMY_CYCLES();
}

void ps_write_status_reg(unsigned int value) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((value & 0xffff) << 8) | (REG_STATUS << PIN_A0);

  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 7) = 1 << PIN_WR; // delay
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_WR; // delay 210810
#endif
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}

unsigned int ps_read_status_reg() {
  *(gpio + 7) = (REG_STATUS << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_RD; // delay 210810
  *(gpio + 7) = 1 << PIN_RD; // delay 210810
#endif

  unsigned int value = *(gpio + 13);
  while ((value=*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {}
  
  *(gpio + 10) = 0xffffec;

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
