GCC = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-gcc
NM = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-nm
SIZE = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-size
OBJCOPY = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-objcopy
LD = $(HOME)/Retro68-build/toolchain/bin/m68k-apple-macos-ld

R68INC = $(HOME)/Retro68-build/toolchain/m68k-apple-macos/include
SMINT = /Users/jas/Mac Documentation/supermario/base/SuperMarioProj.1994-02-09/Internal/C

CFLAGS = -m68030 -O2 -fno-builtin -DpatchedIn \
         -include figment_shim.h \
         -I. -I"$(R68INC)" -I"$(SMINT)"

OBJS = MemMgr.o MemMgrInternal.o MemMgrHeap.o MemMgrUtils.o figment_stubs.o figment_glue.o

all: figment.bin

%.o: %.c figment_shim.h MemMgr.h
	$(GCC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(GCC) -m68030 -c $< -o $@

# Link at the virtual address where Figment lives in the ROM image.
# ROM base = $40800000, Figment at ROM+$40000 = $40840000.
FIGMENT_VADDR = 0x40840000

figment.elf: $(OBJS)
	$(LD) -Ttext=$(FIGMENT_VADDR) -o $@ $(OBJS) -nostdlib

figment.bin: figment.elf
	$(OBJCOPY) -O binary $< $@
	@echo "=== Figment binary: $$(wc -c < $@) bytes ==="

check: $(OBJS)
	@echo "=== Unresolved symbols ==="
	@$(NM) -u $(OBJS) | sort -u
	@echo "=== Code sizes ==="
	@$(SIZE) $(OBJS)

clean:
	rm -f *.o *.elf *.bin
