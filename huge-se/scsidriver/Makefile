GCC = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-gcc
LD = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-ld
OBJCOPY = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-objcopy

# Position-independent image: linked at 0, runs wherever SCSIBoot's
# sysheap buffer lands.  drv_install must stay first in link order.
SCSIDriver.bin: SCSIDriver.elf
	$(OBJCOPY) -O binary $< $@
	@echo "=== .SCSIHD driver image: $$(wc -c < $@) bytes ==="

SCSIDriver.elf: SCSIDriver.o
	$(LD) -Ttext=0 -o $@ $< -nostdlib

SCSIDriver.o: SCSIDriver.S
	$(GCC) -m68030 -c $< -o $@

clean:
	rm -f *.o *.elf *.bin
