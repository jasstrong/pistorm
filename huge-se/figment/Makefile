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

OBJS = MemMgr.o MemMgrInternal.o MemMgrHeap.o MemMgrUtils.o MemMgrDebug.o figment_stubs.o figment_glue.o

all: figment.bin figext.bin

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

# figext: born-32's _HeapDispatch ($A0A4) + Apple's ProcessMgrHeap.c.  Figment's ROM slot
# is full, so it links on its own at ROM+$74000 against figment.elf's symbols;
# figext_glue.o goes first so b32_HeapDispatch sits at FIGEXT_VADDR.
FIGEXT_VADDR = 0x40874000
FIGEXT_OBJS = figext_glue.o ProcessMgrHeap.o

figext.elf: $(FIGEXT_OBJS) figment.elf
	$(LD) -Ttext=$(FIGEXT_VADDR) -e b32_HeapDispatch --just-symbols=figment.elf -nostdlib -o $@ $(FIGEXT_OBJS)
	@undef=$$($(NM) -u $@); [ -z "$$undef" ] || { echo "figext: undefined symbols: $$undef"; rm -f $@; exit 1; }

figext.bin: figext.elf
	$(OBJCOPY) -O binary $< $@
	@echo "=== figext binary: $$(wc -c < $@) bytes ==="

check: $(OBJS)
	@echo "=== Unresolved symbols ==="
	@$(NM) -u $(OBJS) | sort -u
	@echo "=== Code sizes ==="
	@$(SIZE) $(OBJS)

clean:
	rm -f *.o *.elf *.bin
