#!/usr/bin/env python3
"""
Huge SE: Patch SE ROM for 32-bit mode with configurable RAM.

All addresses use $40xxxxxx prefix (Mac II convention) so the same ROM
code works in both 24-bit (IS=8 strips the $40) and 32-bit (identity)
PMMU modes.  MODE32 just swaps tables, no ROM patching needed.

  ROM:         $400000 → $40800000  (entry 8)
  SCSI/IWM:    $580000 → $40880000  (entry 8, big-se style remap)
  VIA:         $EFE000 → $40EFE000  (entry 14)
  SCC:         $900000 → $40900000  (entry 9)
  IWM VPA:     $DFE000 → $40DFE000  (entry 13)
  Video/sound: $3F0000 → MemTop-$10000 (WTC at top of visible RAM)

The video/sound/timing area is placed just below MemTop so the BBU
screen and sound buffers sit at the top of visible RAM, matching the
SE's hardware convention (ScrnBase = MemTop - $5900).

IS=8 stripping:  $40880000 → $880000 → entry 8 → phys $40880000  ✓
IS=0 identity:   $40880000 → phys $40880000                       ✓
Slow-path mask:  $40880000 → $880000 → custom_read → SE bus $580000
"""
import struct
import sys

# --- Configurable parameters ---
RAM_SIZE = 32 * 1024 * 1024       # Total physical RAM (32MB)
MEMTOP   = 0x800000               # Visible MemTop during 24-bit boot (8MB)
# Video/sound/timing: top 64KB of visible RAM, matching SE convention.
# During IS=8 boot, MemTop=$800000, video at $7F0000.  After MODE32
# switches to IS=0, the PMMU table remaps $7Fxxxx → physical WTC at
# top of 32MB.  The ROM always uses 24-bit addresses.
VBUF_OLD  = 0x3F0000              # Original SE video/sound base
VBUF_NEW  = MEMTOP - 0x10000     # $7F0000 (just below 8MB MemTop)

def build_combo_resources(rom, combo_rom_off):
    """Convert old-format ROM resources into SuperMario HiRAM combo format.

    The SE ROM (1987) stores resources as: count(2) + mapsize(2) + standard_map + data.
    The SuperMario RM expects a structure table + linked list of entries with inline data.

    combo_rom_off: offset within the ROM image where the combo blob will be placed.
    Returns the combo blob (bytearray).
    """
    # --- Parse old-format resource map ---
    rsrc_off = struct.unpack('>I', rom[0x1A:0x1E])[0]
    map_start = rsrc_off + 4  # skip count(2) + mapsize(2)

    typelist_off = struct.unpack('>H', rom[map_start + 24:map_start + 26])[0]
    namelist_off = struct.unpack('>H', rom[map_start + 26:map_start + 28])[0]
    num_types = struct.unpack('>H', rom[map_start + 28:map_start + 30])[0] + 1
    tl_base = map_start + typelist_off

    resources = []
    for t in range(num_types):
        te = tl_base + 2 + t * 8
        rtype = bytes(rom[te:te + 4])
        rcount = struct.unpack('>H', rom[te + 4:te + 6])[0] + 1
        reflist_off = struct.unpack('>H', rom[te + 6:te + 8])[0]
        rl_base = map_start + typelist_off + reflist_off
        for r in range(rcount):
            re = rl_base + r * 12
            rid = struct.unpack('>h', rom[re:re + 2])[0]
            name_off_raw = struct.unpack('>H', rom[re + 2:re + 4])[0]
            attrs = rom[re + 4]
            data_off_3b = (rom[re + 5] << 16) | (rom[re + 6] << 8) | rom[re + 7]
            name = b''
            if name_off_raw != 0xFFFF:
                nb = map_start + namelist_off + name_off_raw
                nl = rom[nb]
                name = bytes(rom[nb + 1:nb + 1 + nl])
            resources.append(dict(type=rtype, id=rid, name=name, attrs=attrs,
                                  rom_offset=data_off_3b))

    # Sort by ROM offset for size computation
    resources.sort(key=lambda r: r['rom_offset'])

    # --- Compute actual resource sizes ---
    half = len(rom) // 2 if len(rom) > 0x40000 else len(rom)
    for i, r in enumerate(resources):
        start = r['rom_offset']
        limit = resources[i + 1]['rom_offset'] if i < len(resources) - 1 else half

        # Special cases: bbmc has non-resource code in gap, FONT 521 trails to ROM end
        rtype = r['type']
        if rtype == b'bbmc':
            r['size'] = 24  # 6 longs of bus config
        elif rtype == b'FONT' and i == len(resources) - 1:
            # Compute from FONT header
            fh = struct.unpack('>H', rom[start + 14:start + 16])[0]  # fRectHeight
            ow = struct.unpack('>h', rom[start + 16:start + 18])[0]  # owTLoc
            fc = struct.unpack('>H', rom[start + 2:start + 4])[0]    # firstChar
            lc = struct.unpack('>H', rom[start + 4:start + 6])[0]    # lastChar
            nc = lc - fc + 2  # chars + missing glyph sentinel
            r['size'] = 16 + ow * 2 + (nc + 1) * 2
        elif rtype == b'CURS':
            r['size'] = 68  # fixed: 32 data + 32 mask + 2 hotY + 2 hotX
        else:
            # Use gap (resources are densely packed except the special cases above)
            r['size'] = limit - start

        r['data'] = bytes(rom[start:start + r['size']])

    # --- Build combo format ---
    COMBO_BF_SZ = 1   # 1 byte of combo bits per entry
    MAX_COM_IND = 1   # combo indices 0-1 valid (d3 forced to 1 in RM)
    VERSION = 1
    MEM_HEAD_SZ = 8   # 8-byte memory block header (old MM format)
    MP_BASE = 168     # zone_header(120) + MoreMasters_block(32) + ptrBlock_header(16)

    # Structure table: 10 bytes
    #   [0-3]: offset from RomBase to first entry
    #   [4]:   MaxComInd
    #   [5]:   ComBFSz
    #   [6-7]: Version
    #   [8-9]: MemHeadSz
    struct_table = bytearray(10)
    struct_table[4] = MAX_COM_IND
    struct_table[5] = COMBO_BF_SZ
    struct.pack_into('>H', struct_table, 6, VERSION)
    struct.pack_into('>H', struct_table, 8, MEM_HEAD_SZ)

    # Build each entry
    entries = []
    for i, r in enumerate(resources):
        e = bytearray()
        e.append(0xFF)                           # combo bits: all configs
        e += b'\x00\x00\x00\x00'                 # link (fill later)
        e += b'\x00\x00\x00\x00'                 # data_offset (fill later)
        e += r['type']                            # resource type (4)
        e += struct.pack('>h', r['id'])           # resource ID (2)
        e.append(r['attrs'])                      # attributes (1)
        if r['name']:
            e.append(len(r['name']))              # name length (1)
            e += r['name']                        # name data
        else:
            e.append(0)                           # zero-length name
        while len(e) % 4:
            e.append(0)                           # align to 4
        # Memory block header (8 bytes before data)
        rel_handle = MP_BASE + 8 * i
        e += struct.pack('>I', r['size'])         # size at data-8
        e += struct.pack('>I', rel_handle)        # relHandle at data-4
        data_rel = len(e)                         # data starts here
        e += r['data']                            # resource data
        while len(e) % 4:
            e.append(0)                           # align to 4
        entries.append(dict(bytes=e, data_rel=data_rel))

    # Compute absolute offsets and fill in link + data_offset fields
    pos = 10  # after structure table
    for e in entries:
        e['abs'] = combo_rom_off + pos
        pos += len(e['bytes'])

    for i, e in enumerate(entries):
        b = e['bytes']
        # Link at offset 1: ROM offset to next entry (0 = last)
        link = entries[i + 1]['abs'] if i < len(entries) - 1 else 0
        struct.pack_into('>I', b, 1, link)
        # Data offset at offset 5: ROM offset to data (past memory header)
        struct.pack_into('>I', b, 5, e['abs'] + e['data_rel'])

    # First entry offset in structure table
    struct.pack_into('>I', struct_table, 0, entries[0]['abs'])

    # Assemble
    blob = bytearray(struct_table)
    for e in entries:
        blob += e['bytes']

    total_data = sum(r['size'] for r in resources)
    print(f"\n=== ROM resources: {len(resources)} resources, "
          f"{total_data} bytes data, {len(blob)} bytes combo format ===")
    for r in resources:
        t = r['type'].decode('ascii', errors='replace')
        n = r['name'].decode('ascii', errors='replace') if r['name'] else ''
        print(f"    {t:>6s} {r['id']:>6d}  {r['size']:>6d} bytes  {n}")
    return blob


def patch_rom(infile, outfile):
    rom = bytearray(open(infile, 'rb').read())
    half = len(rom) // 2  # 256KB, rest is mirror
    rom = rom[:half]

    patches = 0
    log = []

    def patch32(off, old_base, new_base, size=0x100000):
        """Patch a 32-bit value at offset if it's in the target range."""
        nonlocal patches
        val = struct.unpack_from('>I', rom, off)[0]
        if old_base <= val < old_base + size:
            new_val = val - old_base + new_base
            struct.pack_into('>I', rom, off, new_val)
            log.append(f"  ${off+0x40800000:08X} (+${off:05X}): ${val:08X} → ${new_val:08X}")
            patches += 1
            return True
        return False

    # ROM is 256KB, mirrored once to 512KB. Valid addresses: $400000-$47FFFF.
    # Anything above $480000 in the $4xxxxx range is NOT a ROM address.
    rom_size = half  # 256KB ($40000)

    # === 1. Initial PC at file offset $4 ===
    print("=== Reset vector (initial PC) ===")
    patch32(4, 0x400000, 0x40800000, size=rom_size * 2)  # include mirror

    # === 2. Scan for instruction patterns with absolute addresses ===
    abs_opcodes = {
        0x4EF9: 'JMP',
        0x4EB9: 'JSR',
        0x41F9: 'LEA.A0', 0x43F9: 'LEA.A1', 0x45F9: 'LEA.A2',
        0x47F9: 'LEA.A3', 0x49F9: 'LEA.A4', 0x4BF9: 'LEA.A5',
        0x4DF9: 'LEA.A6',
        0x4879: 'PEA',
    }

    moveq_imm = {}
    for dn in range(8):
        moveq_imm[0x203C + dn * 0x200] = f'MOVE.L#,D{dn}'
        moveq_imm[0x207C + dn * 0x200] = f'MOVEA.L#,A{dn}'
    moveq_imm[0x21FC] = 'MOVE.L#,abs.W'
    moveq_imm[0x23FC] = 'MOVE.L#,abs.L'

    print("\n=== Absolute address instructions ===")
    for off in range(0, half - 5, 2):
        opcode = struct.unpack_from('>H', rom, off)[0]

        if opcode in abs_opcodes:
            name = abs_opcodes[opcode]
            # ROM: $400000 → $40800000 (only within ROM+mirror, not beyond)
            if patch32(off + 2, 0x400000, 0x40800000, size=rom_size * 2):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # SCSI/IWM: $580000-$5FFFFF → $40880000-$408FFFFF (big-se remap + $40 prefix)
            if patch32(off + 2, 0x580000, 0x40880000, size=0x80000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # IWM VPA path: $DFE000-$DFFFFF → $40DFE000-$40DFFFFF
            if patch32(off + 2, 0xDFE000, 0x40DFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # VIA: $EFE000-$EFFFFF → $40EFE000-$40EFFFFF
            if patch32(off + 2, 0xEFE000, 0x40EFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # SCC: $900000-$BFFFFF → $40900000-$40BFFFFF
            if patch32(off + 2, 0x9F0000, 0x409F0000, size=0x10000) or \
                patch32(off + 2, 0xBF0000, 0x40BF0000, size=0x10000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # Video/sound buffer: $3F0000 → VBUF_NEW (parametric, below MemTop)
            if patch32(off + 2, VBUF_OLD, VBUF_NEW, size=0x10000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]

        elif opcode in moveq_imm:
            name = moveq_imm[opcode]
            if patch32(off + 2, 0x400000, 0x40800000, size=rom_size * 2):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x40880000, size=0x80000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0xDFE000, 0x40DFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0xEFE000, 0x40EFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x9F0000, 0x409F0000, size=0x10000) or \
                patch32(off + 2, 0xBF0000, 0x40BF0000, size=0x10000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, VBUF_OLD, VBUF_NEW, size=0x10000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]

    for l in log:
        print(l)

    # === 3. Exception vector data table at $19E8 (64 longwords) ===
    print("\n=== Exception vector table at $19E8 ===")
    for i in range(64):
        off = 0x19E8 + i * 4
        if patch32(off, 0x400000, 0x40800000):
            pass
    for l in log[len(log)-63:]:
        print(l)
    log.clear()

    # === 4. Brute-force: patch ALL remaining $00400000 in main code section ===
    # Use $00800000 (not $40800000) — data constants may be packed with
    # 16-bit fields that interpret the high word.  $00800000 keeps the high
    # byte zero (safe for moreMast etc.) and works via PMMU IS=8.
    print("\n=== Remaining $00400000 values (data constants) ===")
    for i in range(0, min(half, 0x1B000), 4):
        val = struct.unpack_from('>I', rom, i)[0]
        if val == 0x00400000:
            struct.pack_into('>I', rom, i, 0x00800000)
            patches += 1
            log.append(f"  ${i+0x40800000:08X} (+${i:05X}): $00400000 → $00800000 (data const, 24-bit safe)")
    for l in log:
        print(l)
    log.clear()

    # === 5. Let all tests run, override MemTop ===
    # All tests including Test 4 (sizing) run — the BBU needs the probe
    # bus cycles to detect SIMM configuration (VIA ORB bits 3-5).
    memtop_val = MEMTOP
    print(f"\n=== Force MemTop = ${memtop_val:X} ({memtop_val // (1024*1024)}MB) ===")
    sizing_patch = struct.pack('>HI', 0x2C7C, memtop_val)  # MOVEA.L #MemTop, A6
    off = 0x1CFE
    old_bytes = bytes(rom[off:off+len(sizing_patch)])
    rom[off:off+len(sizing_patch)] = sizing_patch
    patches += 1
    print(f"  +${off:05X}: {old_bytes.hex()} → {sizing_patch.hex()}")

    # === 5b. Move system zone above Toolbox trap table for Figment ===
    # The Toolbox trap table occupies $0E00-$1E00 (1024 entries × 4 bytes).
    # The original system zone starts at $1600 — right in the trap table!
    # The old 52-byte MM header coexisted with the trap table, but Figment's
    # 120-byte header creates blocks at $1678 which get overwritten by trap
    # addresses. Move the zone start past the trap table end.
    syszone_start_off = 0x01CC      # startPtr in the param block
    syszone_limit_off = 0x01CC + 4  # limitPtr in the param block
    old_start = struct.unpack_from('>I', rom, syszone_start_off)[0]
    old_limit = struct.unpack_from('>I', rom, syszone_limit_off)[0]
    new_start = 0x00002000  # above trap table end ($1E00) with margin
    new_limit = 0x00020000  # 128KB syszone (sweet spot: got happy mac at this value)
    struct.pack_into('>I', rom, syszone_start_off, new_start)
    struct.pack_into('>I', rom, syszone_limit_off, new_limit)
    patches += 2
    print(f"\n=== System zone moved: ${old_start:08X}-${old_limit:08X} → ${new_start:08X}-${new_limit:08X} ===")

    # === 5c. Enlarge resource map allocation for Figment zone overhead ===
    # The ROM at $DA12 has MOVEQ #$1C,D0 which feeds into the resource map
    # size computation: size = ($1C + word1) * 4 + word2.  Figment's zone
    # header is 68 bytes larger than the old MM's, so the embedded zone that
    # the ROM creates in this allocation needs more space.  Change $1C→$9C
    # to add 512 bytes ((156-28)*4) to the allocation.
    # === 5c. Enlarge resource map allocation for Figment zone overhead ===
    # ROM at $DA13: MOVEQ #$1C,D0 feeds the resource map size computation.
    # Figment's 120-byte zone header needs more space than old MM's 52-byte.
    # Change $1C (28) → $7C (124) to add 384 bytes to the allocation.
    # NOTE: MOVEQ is sign-extended, max positive = $7F.
    rom[0xDA13] = 0x7C
    patches += 1
    print(f"\n=== Resource map alloc enlarged: MOVEQ #$1C → #$7C at +$DA13 ===")

    # === 5d. NOP the RM back-pointer write that corrupts the system zone ===
    # At $E736: MOVE.L A1,(A0) writes a ROM resource pointer into a computed
    # address.  With Figment, the offset computation goes wrong (reads -4
    # from ROM resource data, not a block header), and A0 ends up pointing
    # at the system zone's free block tag field, corrupting it.
    # The write is guarded by TST.B $0A5E / BEQ.S — meant to skip for ROM
    # resources, but the flag is wrong under Figment.  NOP the write.
    # NOP the old RM's back-pointer write — still runs before SuperMario RM
    # takes over (InitRSRCMgr installs RM traps later in boot).
    rom[0xE736:0xE738] = bytes([0x4E, 0x71])  # NOP
    patches += 1
    print(f"=== NOP'd old RM back-pointer write at +$E736 ===")

    # Clear address 0 so nil-terminated linked list walks work correctly.
    # The ROM's File Manager search at $932E does MOVEA.L A3,A4; MOVEA.L (A4),A4
    # with A3=0 (nil list head). *(0) is the reset SSP vector (non-zero),
    # so the loop never terminates. We clear address 0 via an extra byte in
    # the trap installer code. (The RM's InitRSRCMgr also clears it.)

    # A-line dispatcher: no trampoline needed.  The SE ROM's dispatcher
    # at $2CB6 handles the 68030 format word correctly (it uses RTS, not
    # RTE, so the format word is irrelevant).
    aline_trampoline = None
    tramp_rom_off = 0
    # We'll embed this and patch $2CB6 after we know the mirror offset.

    # === 5g. Replace ROM's InitRSRCMgr with JMP to SuperMario version ===
    # ROM's InitRSRCMgr at $07E8 (14 bytes): ST SysMap, CLR TopMapHndl,
    # CLR.W -(SP), _InitResources, ADDQ #2,SP, RTS.
    # Replace with JMP to SuperMario InitRSRCMgr which does the same plus
    # ExpandMem bootstrap and RM trap installation.
    import os
    _resmgr_path = os.path.join(os.path.dirname(__file__), 'resmgr', 'ResourceMgr.bin')
    _resmgr = open(_resmgr_path, 'rb').read() if os.path.exists(_resmgr_path) else b''
    if _resmgr:
        # SuperMario InitRSRCMgr is at a known address (from nm)
        rm_initrsmgr = 0x40848022  # InitRSRCMgr in RM binary
        jmp = struct.pack('>HI', 0x4EF9, rm_initrsmgr)
        rom[0x07E8:0x07E8+6] = jmp
        rom[0x07EE:0x07F8] = bytes([0x4E, 0x71] * 5)
        patches += 1
        print(f"=== InitRSRCMgr: JMP ${rm_initrsmgr:08X} at +$07E8 ===")

    # Resource map: no padding/relocation needed — SuperMario RM handles
    # Figment zone headers natively.  MOVEQ enlargement above gives
    # enough space for the Figment zone header + RM allocations.
    resmap_relocated = None

    print(f"\n=== Total patches: {patches} ===")

    # === 6. Patch ROM $0762 with JMP to Figment trap installer ===
    # The ROM at $0762 does MOVE.L A0,$0028 + RTS (6 bytes).
    # Replace with JMP to installer in mirror half.  The installer does
    # the original A-line vector write, then writes Figment trap addresses
    # into the OS trap table.  Must patch BEFORE checksum.
    import os
    figment_path = os.path.join(os.path.dirname(__file__), 'figment', 'figment.bin')
    offsets_path = os.path.join(os.path.dirname(__file__), 'figment', 'figment_offsets.h')
    FIGMENT_ROM_OFF = 0x40000  # start of mirror half

    installer_code = None
    installer_rom_off = 0
    resmgr_path = os.path.join(os.path.dirname(__file__), 'resmgr', 'ResourceMgr.bin')
    rm_offsets_path = os.path.join(os.path.dirname(__file__), 'resmgr', 'resmgr_offsets.h')
    if os.path.exists(figment_path) and os.path.exists(offsets_path):
        figment = open(figment_path, 'rb').read()
        resmgr = open(resmgr_path, 'rb').read() if os.path.exists(resmgr_path) else b''
        import re
        trap_entries = []
        # Figment MM traps (OS trap table at $0400)
        with open(offsets_path) as f:
            for m in re.finditer(r'\{\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*\}', f.read()):
                trap_num = int(m.group(1), 16)
                trap_addr = int(m.group(2), 16)
                trap_entries.append((trap_num * 4, trap_addr))  # offset from $0400
        figment_count = len(trap_entries)
        # Resource Manager: install ONLY _InitResources at boot time.
        # The ROM's InitRSRCMgr calls _InitResources as a trap ($A995).
        # SuperMario's InitResources handles the RM setup correctly, then
        # InitRSRCMgr installs the remaining RM traps.
        rm_count = 0
        if os.path.exists(rm_offsets_path):
            with open(rm_offsets_path) as f:
                for m in re.finditer(r'\{\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*\}', f.read()):
                    trap_num = int(m.group(1), 16)
                    trap_addr = int(m.group(2), 16)
                    if False:  # RM traps installed by InitRSRCMgr, not at boot
                        tb_off = 0x0A00 + (trap_num & 0x3FF) * 4
                        trap_entries.append((tb_off, trap_addr))
                        rm_count += 1

        if trap_entries:
            installer_rom_off = FIGMENT_ROM_OFF + len(figment)
            if installer_rom_off & 1:
                installer_rom_off += 1
            installer_vaddr = 0x40800000 + installer_rom_off

            # Build 68k installer: set A-line vector, write trap table, RTS
            data_offset = 36
            lea_disp = data_offset - 12
            code = bytearray()
            code += struct.pack('>HH', 0x21C8, 0x0028)  # MOVE.L A0,$0028
            code += struct.pack('>H', 0x2F08)            # MOVE.L A0,-(SP)
            code += struct.pack('>H', 0x2F09)            # MOVE.L A1,-(SP)
            code += struct.pack('>H', 0x2F00)            # MOVE.L D0,-(SP)
            code += struct.pack('>HH', 0x41FA, lea_disp) # LEA trap_data(PC),A0
            code += struct.pack('>H', 0x3018)            # MOVE.W (A0)+,D0
            code += struct.pack('>H', 0x6B0A)            # BMI.S .done
            code += struct.pack('>HH', 0x43F8, 0x0400)  # LEA $0400.W,A1
            code += struct.pack('>H', 0xD2C0)            # ADDA.W D0,A1
            code += struct.pack('>H', 0x2298)            # MOVE.L (A0)+,(A1)
            code += struct.pack('>H', 0x60F2)            # BRA.S .loop
            code += struct.pack('>H', 0x201F)            # MOVE.L (SP)+,D0
            code += struct.pack('>H', 0x225F)            # MOVEA.L (SP)+,A1
            code += struct.pack('>H', 0x205F)            # MOVEA.L (SP)+,A0
            code += struct.pack('>H', 0x4E75)            # RTS
            assert len(code) == 36
            for tb_off, ta in trap_entries:
                code += struct.pack('>H', tb_off)
                code += struct.pack('>I', ta)
            code += struct.pack('>H', 0xFFFF)
            installer_code = code

            # Patch JMP into first half (BEFORE checksum)
            jmp_patch = struct.pack('>HI', 0x4EF9, installer_vaddr)
            rom[0x0762:0x0762 + 6] = jmp_patch
            patches += 1
            print(f"\n=== Trap installer: JMP ${installer_vaddr:08X} at $0762 ===")
            print(f"    {figment_count} Figment MM + {rm_count} RM traps, {len(code)} bytes")

            # A-line dispatcher: no patch needed

    # ROM+$1A: DON'T patch — it overlaps ROM header bytes at $18-$1D which
    # are used during early boot (OVL exception vectors / jump table).
    # Instead, the RM code hardcodes the combo format offset.
    COMBO_ROM_OFF = 0x50000  # in mirror half, after Figment + RM + installer
    combo_blob = build_combo_resources(rom, COMBO_ROM_OFF)
    # ROM+$1A left as original $1AF1C — RM ignores it
    print(f"  RomRsrcStart ($1A) unchanged (RM hardcodes ${COMBO_ROM_OFF:06X})")

    # Fix checksum (AFTER all first-half patches, before mirroring)
    checksum = 0
    # ROM version at $08 left as $0276 — System file needs it to find patches.

    for i in range(4, half, 2):
        checksum = (checksum + struct.unpack_from('>H', rom, i)[0]) & 0xFFFFFFFF
    struct.pack_into('>I', rom, 0, checksum)
    print(f"Checksum updated to ${checksum:08X}")

    # Write patched ROM (mirror to 512KB)
    out = bytearray(rom + rom)

    # Embed Figment binary in mirror half
    if os.path.exists(figment_path):
        if FIGMENT_ROM_OFF + len(figment) <= len(out):
            out[FIGMENT_ROM_OFF:FIGMENT_ROM_OFF + len(figment)] = figment
            print(f"\n=== Figment MM embedded at ROM+${FIGMENT_ROM_OFF:05X} ({len(figment)} bytes) ===")
        else:
            print(f"\n=== WARNING: Figment doesn't fit ===")

    # Embed Resource Manager binary in mirror half
    RESMGR_ROM_OFF = 0x48000  # linked at $40848000
    if resmgr and RESMGR_ROM_OFF + len(resmgr) <= len(out):
        out[RESMGR_ROM_OFF:RESMGR_ROM_OFF + len(resmgr)] = resmgr
        print(f"=== Resource Manager embedded at ROM+${RESMGR_ROM_OFF:05X} ({len(resmgr)} bytes) ===")

    # Embed trap installer in mirror half (after Figment)
    if installer_code and installer_rom_off + len(installer_code) <= len(out):
        out[installer_rom_off:installer_rom_off + len(installer_code)] = installer_code
        print(f"    Installer at ROM+${installer_rom_off:05X}")

    # Embed combo format ROM resources in mirror half
    if combo_blob and COMBO_ROM_OFF + len(combo_blob) <= len(out):
        out[COMBO_ROM_OFF:COMBO_ROM_OFF + len(combo_blob)] = combo_blob
        print(f"=== Combo resources at ROM+${COMBO_ROM_OFF:05X} "
              f"({len(combo_blob)} bytes, ends ${COMBO_ROM_OFF + len(combo_blob):05X}) ===")
    else:
        print(f"=== WARNING: Combo resources don't fit at ${COMBO_ROM_OFF:05X} ===")

    open(outfile, 'wb').write(out)
    print(f"Written to {outfile}")

if __name__ == '__main__':
    infile = sys.argv[1] if len(sys.argv) > 1 else 'se-rom.bin'
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'se-rom-huge.bin'
    patch_rom(infile, outfile)
