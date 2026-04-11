#!/usr/bin/env python3
"""Generate resmgr_offsets.h from ResourceMgr.elf symbol table."""
import subprocess, sys

nm = sys.argv[1] if len(sys.argv) > 1 else "m68k-apple-macos-nm"
out = subprocess.check_output([nm, "ResourceMgr.elf"], text=True)

# Map function name → Toolbox trap number
# Toolbox traps use stack-based calling convention (params on stack, result on stack)
# The RM functions are already in Toolbox convention (Pascal calling convention)
trap_map = {
    "InitResources":    0x0995,   # _InitResources
    "RsrcZoneInit":     0x0996,   # _RsrcZoneInit
    "OpenResFile":      0x0997,   # _OpenResFile
    "UseResFile":       0x0998,   # _UseResFile
    "UpdateResFile":    0x0999,   # _UpdateResFile
    "CloseResFile":     0x099A,   # _CloseResFile
    "SetResLoad":       0x099B,   # _SetResLoad
    "CountResources":   0x099C,   # _CountResources
    "GetIndResource":   0x099D,   # _GetIndResource
    "GetResource":      0x09A0,   # _GetResource
    "GetNamedResource": 0x09A1,   # _GetNamedResource
    "LoadResource":     0x09A2,   # _LoadResource
    "ReleaseResource":  0x09A3,   # _ReleaseResource
    "HomeResFile":      0x09A4,   # _HomeResFile
    "SizeResource":     0x09A5,   # _SizeResource
    "GetResAttrs":      0x09A6,   # _GetResAttrs
    "SetResAttrs":      0x09A7,   # _SetResAttrs
    "GetResInfo":       0x09A8,   # _GetResInfo
    "SetResInfo":       0x09A9,   # _SetResInfo
    "ChangedResource":  0x09AA,   # _ChangedResource
    "AddResource":      0x09AB,   # _AddResource
    "AddReference":     0x09AC,   # _AddReference
    "RmveResource":     0x09AD,   # _RmveResource
    "RmveReference":    0x09AE,   # _RmveReference
    "ResError":         0x09AF,   # _ResError
    "WriteResource":    0x09B0,   # _WriteResource
    "CreateResFile":    0x09B1,   # _CreateResFile
    "DetachResource":   0x0992,   # _DetachResource
    "SetResPurge":      0x0993,   # _SetResPurge
    "CurResFile":       0x0994,   # _CurResFile
    "OpenRFPerm":       0x09C4,   # _OpenRFPerm
    "UniqueID":         0x09C1,   # _UniqueID
    "GetResFileAttrs":  0x09F6,   # _GetResFileAttrs
    "SetResFileAttrs":  0x09F7,   # _SetResFileAttrs
    "MaxSizeRsrc":      0x0821,   # _MaxSizeRsrc
    "Count1Resources":  0x080D,   # _Count1Resources
    "Get1IndResource":  0x080E,   # _Get1IxResource
    "Count1Types":      0x081C,   # _Count1Types
    "Get1Resource":     0x081F,   # _Get1Resource
    "Get1NamedResource": 0x0820,  # _Get1NamedResource
    "Get1IndType":      0x080F,   # _Get1IxType
    "Unique1ID":        0x0810,   # _Unique1ID
    "HCreateResFile":   0x081B,   # _HCreateResFile
    "CountTypes":       0x099E,   # _CountTypes (if exists)
    "GetIndType":       0x099F,   # _GetIndType (if exists)
}

entries = []
for line in out.strip().split('\n'):
    parts = line.split()
    if len(parts) >= 3 and parts[1] == 'T' and parts[2] in trap_map:
        addr = int(parts[0], 16)
        name = parts[2]
        trap_num = trap_map[name]
        entries.append((trap_num, addr, name))

entries.sort(key=lambda x: x[0])

print("/* Auto-generated — do not edit */")
print(f"#define RESMGR_TRAP_COUNT {len(entries)}")
print("static const struct { uint16_t trap_num; uint32_t offset; } resmgr_trap_table[] = {")
for trap_num, addr, name in entries:
    print(f"    {{ 0x{trap_num:04X}, 0x{addr:08X} }}, /* {name} */")
print("};")
