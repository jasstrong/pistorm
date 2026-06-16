GCC = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-gcc
NM = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-nm
OBJDUMP = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-objdump
OBJCOPY = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-objcopy
LD = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-ld

R68INC = $(HOME)/Retro68-build/toolchain/m68k-apple-macos/include

CFLAGS = -m68030 -O2 -fno-builtin -I. -I"$(R68INC)"

# Link at the virtual address where boot32 lives in the ROM mirror.
# ROM base = $40800000, boot32 at ROM+$70000 = $40870000.
BOOT32_VADDR = 0x40870000

# boot32_glue.o MUST be first so boot32_table is at BOOT32_VADDR+0.
OBJS = boot32_glue.o boot32.o

all: boot32.bin

%.o: %.c boot32.h
	$(GCC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(GCC) -m68030 -c $< -o $@

boot32.elf: $(OBJS)
	$(LD) -Ttext=$(BOOT32_VADDR) -o $@ $(OBJS) -nostdlib

boot32.bin: boot32.elf
	$(OBJCOPY) -O binary $< $@
	@echo "=== boot32 binary: $$(wc -c < $@) bytes ==="
	@echo "=== entry symbols ==="
	@$(NM) boot32.elf | grep -E "boot32_(table|memsize_rom|ramtest_rom)" | sort

dis: boot32.elf
	$(OBJDUMP) -d boot32.elf | head -40

clean:
	rm -f *.o *.elf *.bin
