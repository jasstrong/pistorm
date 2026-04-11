#!/usr/bin/env python3
"""Generate a minimal Mac INIT resource file that triggers the MODE32 trap.

68k code:
    MOVE.L  #$4D333200, D0  ; 'M32\0' selector
    DC.W    $A0FF           ; pseudovirt trap
    RTS

Output: MODE32.rsrc (raw resource fork)
"""
import struct, sys

# 68k machine code:
#   MOVE.L  #'M32\0', D0
#   DC.W    $A0FF           ; pseudovirt trap (switches PMMU, relocates WTC, expands RAM)
#   TST.L   D0              ; check result
#   BNE.S   done            ; failed, skip heap expansion
#   _MaxApplZone            ; expand application heap to new MemTop
# done:
#   RTS
code = bytes([
    0x20, 0x3C, 0x4D, 0x33,  # MOVE.L #$4D333200, D0
    0x32, 0x00,
    0xA0, 0xFF,              # DC.W $A0FF (pseudovirt trap)
    0x4A, 0x80,              # TST.L D0
    0x66, 0x02,              # BNE.S +2 (skip _MaxApplZone)
    0xA0, 0x63,              # _MaxApplZone
    0x4E, 0x75,              # RTS
])

def build_resource_file(resources):
    """Build a Mac resource file from a list of (type, id, name, data) tuples."""
    res_data = b''
    data_offsets = []
    for _, _, _, data in resources:
        data_offsets.append(len(res_data))
        res_data += struct.pack('>I', len(data)) + data

    num_types = {}
    for typ, rid, name, data in resources:
        num_types.setdefault(typ, []).append((rid, name, data))

    type_list_off = 28
    type_list_size = 2 + 8 * len(num_types)
    ref_lists = b''
    type_entries = b''
    name_list = b''
    ref_offset = type_list_size

    for typ, items in num_types.items():
        type_entries += struct.pack('>4sHH', typ, len(items) - 1, ref_offset)
        for rid, name, data in items:
            name_off = 0xFFFF
            if name:
                name_off = len(name_list)
                name_list += struct.pack('B', len(name)) + name.encode('mac_roman')

            idx = next(i for i, (t, r, n, d) in enumerate(resources)
                       if t == typ and r == rid)
            doff = data_offsets[idx]
            attr_and_offset = (0 << 24) | (doff & 0xFFFFFF)
            ref_lists += struct.pack('>HH', rid, name_off)
            ref_lists += struct.pack('>I', attr_and_offset)
            ref_lists += struct.pack('>I', 0)
        ref_offset += len(items) * 12

    type_list = struct.pack('>H', len(num_types) - 1) + type_entries
    name_list_off = type_list_off + len(type_list) + len(ref_lists)

    map_header = struct.pack('>IHH HH', 0, 0, 0, type_list_off, name_list_off)
    res_map = (b'\x00' * 16) + map_header + type_list + ref_lists + name_list

    data_offset = 256
    map_offset = data_offset + len(res_data)
    header = struct.pack('>IIII', data_offset, map_offset, len(res_data), len(res_map))
    header += b'\x00' * (256 - len(header))
    res_map = header[:16] + res_map[16:]

    return header + res_data + res_map

resources = [(b'INIT', 31, 'MODE32 Huge SE', code)]
data = build_resource_file(resources)

outfile = sys.argv[1] if len(sys.argv) > 1 else 'huge-se/MODE32.rsrc'
with open(outfile, 'wb') as f:
    f.write(data)
print(f"Written {len(data)} bytes to {outfile}")
