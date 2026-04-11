#!/usr/bin/env python3
"""Generate figment_offsets.h from figment.elf symbol table."""
import subprocess, sys

nm = sys.argv[1] if len(sys.argv) > 1 else "m68k-apple-macos-nm"
out = subprocess.check_output([nm, "figment.elf"], text=True)

# Map glue name → OS trap base number (low byte of trap word)
# Verified against Multiverse.h enum values
trap_map = {
    "fig_InitZone":      0x19,  # _InitZone = $A019
    "fig_GetZone":       0x1A,  # _GetZone = $A11A (base $1A)
    "fig_SetZone":       0x1B,  # _SetZone = $A01B
    "fig_FreeMem":       0x1C,  # _FreeMem = $A01C
    "fig_MaxMem":        0x1D,  # _MaxMem = $A11D (base $1D)
    "fig_NewPtr":        0x1E,  # _NewPtr = $A11E (base $1E)
    "fig_DisposePtr":    0x1F,  # _DisposePtr = $A01F
    "fig_SetPtrSize":    0x20,  # _SetPtrSize = $A020
    "fig_GetPtrSize":    0x21,  # _GetPtrSize = $A021
    "fig_NewHandle":     0x22,  # _NewHandle = $A122 (base $22)
    "fig_DisposeHandle": 0x23,  # _DisposeHandle = $A023
    "fig_SetHandleSize": 0x24,  # _SetHandleSize = $A024
    "fig_GetHandleSize": 0x25,  # _GetHandleSize = $A025
    "fig_HandleZone":    0x26,  # _HandleZone = $A126 (base $26)
    "fig_ReallocHandle": 0x27,  # _ReallocateHandle = $A027
    "fig_RecoverHandle": 0x28,  # _RecoverHandle = $A128 (base $28)
    "fig_HLock":         0x29,  # _HLock = $A029
    "fig_HUnlock":       0x2A,  # _HUnlock = $A02A
    "fig_EmptyHandle":   0x2B,  # _EmptyHandle = $A02B
    "fig_InitApplZone":  0x2C,  # _InitApplZone = $A02C
    "fig_SetApplLimit":  0x2D,  # _SetApplLimit = $A02D
    # NOTE: $2E = _BlockMove, NOT _NewEmptyHandle!
    "fig_MoveHHi":       0x2F,  # _MoveHHi = $A064? NO...
    "fig_MoreMasters":   0x36,  # _MoreMasters = $A036
    "fig_ReserveMem":    0x40,  # _ReserveMem = $A040
    "fig_PtrZone":       0x48,  # _PtrZone = $A148 (base $48)
    "fig_HPurge":        0x49,  # _HPurge = $A049
    "fig_HNoPurge":      0x4A,  # _HNoPurge = $A04A
    "fig_SetGrowZone":   0x4B,  # _SetGrowZone = $A04B
    "fig_CompactMem":    0x4C,  # _CompactMem = $A04C (WAS $16!)
    "fig_PurgeMem":      0x4D,  # _PurgeMem = $A04D (WAS $65!)
    "fig_StackSpace":    0x65,  # _StackSpace = $A065
    "fig_MaxBlock":      0x61,  # _MaxBlock = $A061
    "fig_PurgeSpace":    0x62,  # _PurgeSpace = $A062
    "fig_MaxApplZone":   0x63,  # _MaxApplZone = $A063
    "fig_MoveHHi":       0x64,  # _MoveHHi = $A064 (WAS $2F!)
    "fig_HSetRBit":      0x67,  # _HSetRBit = $A067 (WAS $6F!)
    "fig_HClrRBit":      0x68,  # _HClrRBit = $A068
    "fig_HGetState":     0x69,  # _HGetState = $A069
    "fig_HSetState":     0x6A,  # _HSetState = $A06A
}
# NOTE: fig_MoveHLow ($A09D) = base $1D? No, $9D. Need to check.
# NOTE: fig_NewEmptyHandle ($A166) is a TOOLBOX trap, not OS trap.

entries = []
for line in out.strip().split('\n'):
    parts = line.split()
    if len(parts) >= 3 and parts[1] == 'T' and parts[2] in trap_map:
        addr = int(parts[0], 16)
        name = parts[2]
        trap_num = trap_map[name]
        entries.append((trap_num, addr, name))

entries.sort()

print("/* Auto-generated — do not edit */")
print("#define FIGMENT_TRAP_COUNT %d" % len(entries))
print("static const struct { uint8_t trap_num; uint32_t offset; } figment_trap_table[] = {")
for trap_num, addr, name in entries:
    print("    { 0x%02X, 0x%08X }, /* %s */" % (trap_num, addr, name))
print("    { 0, 0 }  /* sentinel */")
print("};")
