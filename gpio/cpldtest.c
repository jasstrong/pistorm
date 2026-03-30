// SPDX-License-Identifier: MIT
/* cpldtest — test CPLD communication using pigpio for all GPIO access.
 * Usage: sudo ./cpldtest
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pigpio.h>

/* PiStorm pin assignments */
#define PIN_TXN  0
#define PIN_IPL  1
#define PIN_A0   2
#define PIN_A1   3
#define PIN_CLK  4
#define PIN_RST  5
#define PIN_RD   6
#define PIN_WR   7
/* D0-D15 = GPIO 8-23 */

/* CPLD register addresses */
#define REG_DATA    0
#define REG_ADDR_LO 1
#define REG_ADDR_HI 2
#define REG_STATUS  3

static void set_data_pins_output(void) {
    for (int i = 8; i <= 23; i++)
        gpioSetMode(i, PI_OUTPUT);
}

static void set_data_pins_input(void) {
    for (int i = 8; i <= 23; i++) {
        gpioSetMode(i, PI_INPUT);
        gpioSetPullUpDown(i, PI_PUD_UP);
    }
}

static void write_data(uint16_t val) {
    for (int i = 0; i < 16; i++)
        gpioWrite(8 + i, (val >> i) & 1);
}

static void pulse_clock(int n) {
    for (int i = 0; i < n; i++) {
        gpioWrite(PIN_CLK, 1);
        gpioDelay(1);  /* 1 microsecond */
        gpioWrite(PIN_CLK, 0);
        gpioDelay(1);
    }
}

static void writereg(int reg, uint16_t data) {
    /* Put data and register address on pins */
    write_data(data);
    gpioWrite(PIN_A0, reg & 1);
    gpioWrite(PIN_A1, (reg >> 1) & 1);
    pulse_clock(5);

    /* Raise WR */
    gpioWrite(PIN_WR, 1);
    pulse_clock(10);

    /* Lower WR */
    gpioWrite(PIN_WR, 0);
    pulse_clock(5);

    /* Clear data */
    write_data(0);
    gpioWrite(PIN_A0, 0);
    gpioWrite(PIN_A1, 0);
}

int main(void) {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio init failed\n");
        return 1;
    }
    printf("pigpio initialized (hardware revision: %u)\n", gpioHardwareRevision());

    /* Configure pins */
    gpioSetMode(PIN_TXN, PI_INPUT);
    gpioSetPullUpDown(PIN_TXN, PI_PUD_UP);
    gpioSetMode(PIN_IPL, PI_INPUT);
    gpioSetPullUpDown(PIN_IPL, PI_PUD_UP);
    gpioSetMode(PIN_A0, PI_OUTPUT);
    gpioSetMode(PIN_A1, PI_OUTPUT);
    gpioSetMode(PIN_CLK, PI_OUTPUT);
    gpioSetMode(PIN_RST, PI_INPUT);
    gpioSetPullUpDown(PIN_RST, PI_PUD_UP);
    gpioSetMode(PIN_RD, PI_OUTPUT);
    gpioSetMode(PIN_WR, PI_OUTPUT);
    set_data_pins_input();

    /* Clear all outputs */
    gpioWrite(PIN_A0, 0);
    gpioWrite(PIN_A1, 0);
    gpioWrite(PIN_CLK, 0);
    gpioWrite(PIN_RD, 0);
    gpioWrite(PIN_WR, 0);

    printf("TXN=%d IPL=%d RST=%d\n",
           gpioRead(PIN_TXN), gpioRead(PIN_IPL), gpioRead(PIN_RST));

    /* Test 1: 100 idle clocks */
    printf("\nTest 1: 100 idle clocks...\n");
    pulse_clock(100);
    printf("TXN=%d (expect 0 if CPLD is responding to clock)\n", gpioRead(PIN_TXN));

    /* Test 2: Write ADDR_LO register */
    printf("\nTest 2: Write ADDR_LO...\n");
    set_data_pins_output();
    writereg(REG_ADDR_LO, 0x0000);
    int txn = gpioRead(PIN_TXN);
    printf("TXN=%d (expect 1 — CPLD sets TXN high on ADDR_LO write)\n", txn);

    /* Test 3: Write ADDR_HI to start a bus cycle, wait for completion */
    printf("\nTest 3: Write ADDR_HI, clock until TXN clears...\n");
    writereg(REG_ADDR_HI, 0x0200);  /* read, word */
    set_data_pins_input();

    int cleared = 0;
    for (int i = 0; i < 100000; i++) {
        gpioWrite(PIN_CLK, 1);
        gpioDelay(1);
        gpioWrite(PIN_CLK, 0);
        gpioDelay(1);
        if (gpioRead(PIN_TXN) == 0) {
            printf("TXN cleared after %d clocks\n", i);
            cleared = 1;
            break;
        }
    }
    if (!cleared)
        printf("TXN never cleared after 100000 clocks\n");

    /* Test 4: Read status register */
    printf("\nTest 4: Read STATUS register...\n");
    gpioWrite(PIN_A0, REG_STATUS & 1);
    gpioWrite(PIN_A1, (REG_STATUS >> 1) & 1);
    gpioWrite(PIN_RD, 1);
    pulse_clock(10);
    /* Read data pins */
    uint16_t status = 0;
    for (int i = 0; i < 16; i++)
        status |= (gpioRead(8 + i) << i);
    gpioWrite(PIN_RD, 0);
    printf("STATUS = 0x%04X\n", status);

    printf("\nDone.\n");
    gpioTerminate();
    return 0;
}
