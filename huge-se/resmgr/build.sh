#!/bin/bash
# Build the embedded SuperMario ResourceMgr for huge-se.
# Reconstructed 2026-06-19 (validated: reproduces the prior ResourceMgr.bin byte-for-byte).
#
# The .S uses // comments (needs cpp -> .S extension) + MPW trap macros (traps.inc)
# + equates (resmgr_equ*.inc), and 020+ instructions (tst.l An) -> -m68030.
# Linked at $40848000 (the combo-ROM mirror slot patch-rom embeds it into).
set -e
cd "$(dirname "$0")"
TOOLCHAIN=~/Retro68-build/toolchain/bin
GCC="$TOOLCHAIN/m68k-apple-macos-gcc"
LD="$TOOLCHAIN/m68k-apple-macos-ld"
OBJCOPY="$TOOLCHAIN/m68k-apple-macos-objcopy"
NM="$TOOLCHAIN/m68k-apple-macos-nm"

# cpp requires a .S (uppercase) extension; concatenate macros+equates+source.
cat traps.inc resmgr_equ.inc resmgr_equ2.inc ResourceMgr.S > /tmp/rm_combined.S
"$GCC" -m68030 -c /tmp/rm_combined.S -o /tmp/rm.o
"$LD" -Ttext=0x40848000 -o ResourceMgr.elf /tmp/rm.o 2>/dev/null || \
  "$LD" -Ttext=0x40848000 -o ResourceMgr.elf /tmp/rm.o   # ignore missing _start warning
"$OBJCOPY" -O binary ResourceMgr.elf ResourceMgr.bin
python3 gen_rm_offsets.py "$NM" > resmgr_offsets.h
echo "Built ResourceMgr.bin ($(stat -f%z ResourceMgr.bin 2>/dev/null || stat -c%s ResourceMgr.bin) bytes) + resmgr_offsets.h"
