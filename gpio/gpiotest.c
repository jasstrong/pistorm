/* Minimal GPIO test — same sequence as the Python test that worked */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }

    volatile uint32_t *gpio = (volatile uint32_t *)mmap(NULL, 0x1000,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0xFE200000);
    close(fd);
    if (gpio == MAP_FAILED) { perror("mmap"); return 1; }

    /* GPFSEL0: set GPIO7 to output, GPIO4 to ALT0, rest as needed */
    gpio[0] = 0x00244240;
    printf("GPFSEL0 = 0x%08X\n", gpio[0]);
    printf("GPLEV0  = 0x%08X\n", gpio[13]);

    /* Set GPIO7 (WR) high via GPSET0 */
    gpio[7] = 1 << 7;
    asm volatile("dsb st" ::: "memory");
    (void)gpio[13]; /* flush */
    printf("After SET WR:  GPLEV0=0x%08X  WR=%d\n", gpio[13], (gpio[13]>>7)&1);

    /* Clear GPIO7 via GPCLR0 */
    gpio[10] = 1 << 7;
    asm volatile("dsb st" ::: "memory");
    (void)gpio[13]; /* flush */
    printf("After CLR WR:  GPLEV0=0x%08X  WR=%d\n", gpio[13], (gpio[13]>>7)&1);

    /* Now do what GPIO_WRITEREG does for ADDR_LO:
     * 1. Set data + A0/A1 via GPSET0
     * 2. Set WR high via GPSET0
     * 3. Clear WR via GPCLR0
     * 4. Clear all via GPCLR0
     */
    printf("\nSimulating GPIO_WRITEREG(REG_ADDR_LO, 0)...\n");
    printf("  TXN before = %d\n", gpio[13] & 1);

    /* Step 1: data=0, A=REG_ADDR_LO(1) → A0=1, A1=0 → bit2 */
    gpio[7] = (0 << 8) | (1 << 2);
    asm volatile("dsb st" ::: "memory"); (void)gpio[13];
    printf("  After data+A: GPLEV0=0x%08X\n", gpio[13]);

    /* Step 2: WR high */
    gpio[7] = 1 << 7;
    asm volatile("dsb st" ::: "memory"); (void)gpio[13];
    printf("  After WR high: GPLEV0=0x%08X WR=%d\n", gpio[13], (gpio[13]>>7)&1);

    /* Step 3: WR low */
    gpio[10] = 1 << 7;
    asm volatile("dsb st" ::: "memory"); (void)gpio[13];
    printf("  After WR low: GPLEV0=0x%08X WR=%d\n", gpio[13], (gpio[13]>>7)&1);

    /* Step 4: clear all */
    gpio[10] = 0xFFFFEC;
    asm volatile("dsb st" ::: "memory"); (void)gpio[13];
    printf("  After clear: GPLEV0=0x%08X\n", gpio[13]);

    printf("  TXN after = %d (expect 1 if CPLD saw the write)\n", gpio[13] & 1);

    /* Restore GPIO7 to input */
    gpio[0] = 0x00004000; /* only GPIO4 ALT0 */

    munmap((void*)gpio, 0x1000);
    return 0;
}
