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

# Late (post-boot32, OS-visible) MemTop.  boot32's RAM-sizing routine installs
# this as SP/MemTop after the early POST, and the born-32 PMMU maps the WTC
# video to its top-64KB.  Parsed from boot32.h so the ROM patches here, the
# boot32 C/asm, and the emulator's MemTop force all agree on ONE value — change
# BOOT32_MEMTOP in boot32.h (and the sysram/vidram sizes in huge-se.cfg) to
# reconfigure the born-32 RAM size.
def _parse_boot32_memtop():
    import os, re
    hdr = os.path.join(os.path.dirname(__file__), 'boot32', 'boot32.h')
    m = re.search(r'#define\s+BOOT32_MEMTOP\s+(0x[0-9A-Fa-f]+)', open(hdr).read())
    if not m:
        raise RuntimeError("could not parse BOOT32_MEMTOP from boot32.h")
    return int(m.group(1), 16)
BOOT32_MEMTOP   = _parse_boot32_memtop()      # e.g. $02000000 (32MB)
B32_VIDEO       = BOOT32_MEMTOP - 0x10000     # late WTC video base (top-64KB)
B32_RAMTEST_CAP = BOOT32_MEMTOP - 0x300       # RAM-test A1 cap = SoundBase (MemTop-$300):
# protects the sound buffer (+ the stack regs just under MemTop) from the destructive
# march sweep, which otherwise writes the pattern through the WTC-mirrored sound buffer
# and the SE plays it (a buzz, distinct from the chime).  Still keeps A0 natural so the
# screen buffer below SoundBase is swept as the intended visible boot test pattern.
# (bigSE doesn't buzz because its RAM test doesn't reach the mirrored sound buffer.)

# hugeSE video/sound buffer is just below the born-32 MemTop (16MB->$00FF0000),
# NOT bigSE's 8MB $7F0000.  Override VBUF_NEW (stale 8MB value) to the config-
# derived base so the absolute video/sound operands agree with ScrnBase/the WTC.
VBUF_NEW = B32_VIDEO

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
    # MPB offset within the ROZ zone. The old-MM value (168=$A8) is WRONG for
    # figment's 32-bit zone: at runtime the RM's _NewPtr MPB lands at zone+$1E0
    # (measured: trueROZ=$2338, MPB($1F40)=$2518). Using $A8 made RelHandles
    # $138 too low, dropping resource #8 (.Sony) onto ROMMapHndl's slot ($2420)
    # and clobbering the map handle -> RM walks driver code as a map -> hang.
    MP_BASE = 0x1E0   # figment ROZ: MPB at zone+$1E0 (DoRomEntry uses HandleZone+RelHandle)

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

    # TST.<sz> abs.L — the classic I/O-poll/strobe idiom (e.g. the ROM's
    # `tst.b $00DFF1FF` = IWM register 8, the drive-enable line).  Same operand
    # layout as JMP (abs.L at off+2).  Without relocation the bare 24-bit I/O
    # address hits 32MB RAM instead of the hardware — a no-op strobe (and a
    # stray [DIRTY-IO-RD]).  Handled SEPARATELY from abs_opcodes because TST
    # must NOT relocate the VBUF ($3F0000) range: the ROM reuses the top of it
    # ($3FFCxx) as the exception-time register-save area (MOVEM %d0-%sp,$3FFC80
    # etc., which stay un-relocated), and relocating a TST there would split it.
    tst_opcodes = {0x4A39: 'TST.B', 0x4A79: 'TST.W', 0x4AB9: 'TST.L'}

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
            if patch32(off + 2, 0x580000, 0x40580000, size=0x80000):
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

        elif opcode in tst_opcodes:
            # Hardware I/O ranges ONLY — deliberately no VBUF (see tst_opcodes
            # note above: $3FFCxx is the exception register-save area).
            name = tst_opcodes[opcode]
            if patch32(off + 2, 0x400000, 0x40800000, size=rom_size * 2):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x40580000, size=0x80000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0xDFE000, 0x40DFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0xEFE000, 0x40EFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x9F0000, 0x409F0000, size=0x10000) or \
                patch32(off + 2, 0xBF0000, 0x40BF0000, size=0x10000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]

        elif opcode in moveq_imm:
            name = moveq_imm[opcode]
            if patch32(off + 2, 0x400000, 0x40800000, size=rom_size * 2):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x40580000, size=0x80000):
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
    log.clear()

    # === 2c. Save-area consistency — relocate the DIRECT absolute accesses to
    # the exception/scratch save area up to VBUF_NEW, matching the LEA pointers
    # the abs_opcodes pass already relocated (and the born-32 layout: the save
    # area belongs just below the REAL 16MB MemTop, not the stock ROM's 4MB slot).
    #
    # The stock SE ROM's exception dispatcher saves/restores regs to a fixed
    # absolute area at $3FFC80.. ( = $400000-$380, the "just below MemTop" slot
    # for a 4MB machine).  LEA computations of pointers into it ($41F9 etc.) are
    # in abs_opcodes and were relocated to VBUF_NEW ($00FFFC80..); but the DIRECT
    # data accesses (MOVEM/MOVE/CLR/NOT/TST/ADDA abs.L) use opcodes not scanned
    # above, so they stayed at $3FFCxx.  That SPLIT makes the dispatcher save
    # regs at 4MB but reload (e.g.) a0 through a 16MB pointer -> garbage -> the
    # `jmp (a0)` bus error.  Relocate the direct accesses so the whole save area
    # is consistently at $00FFFCxx.  Gated on the operand landing in the tiny
    # save-area range, so only these slots are touched.
    print("\n=== Save-area direct accesses -> VBUF_NEW ($00FFFCxx) ===")
    SA_OLD, SA_NEW, SA_SIZE = 0x3FFC00, VBUF_NEW + 0xFC00, 0x200
    # opcode -> byte offset of its abs.L operand (MOVEM has a reg-mask word
    # before the operand; MOVE.L #imm,abs.L has the 4-byte immediate first).
    SA_OFF2 = {0x40F9, 0x33DF, 0x33D8, 0x23DF, 0x23C0, 0x23CB, 0x23CF,
               0x2F39, 0x3F39, 0x2039, 0x2239, 0x42B9, 0x4279, 0x4639,
               0x4A39, 0xDEF9}
    SA_OFF4 = {0x48F9, 0x4CF9}   # MOVEM.L reg,abs.L / abs.L,reg
    for off in range(0, half - 11, 2):
        opcode = struct.unpack_from('>H', rom, off)[0]
        if opcode in SA_OFF2:
            patch32(off + 2, SA_OLD, SA_NEW, size=SA_SIZE)
        elif opcode in SA_OFF4:
            patch32(off + 4, SA_OLD, SA_NEW, size=SA_SIZE)
        elif opcode == 0x23FC:   # MOVE.L #imm,abs.L — dest abs.L at off+6
            patch32(off + 6, SA_OLD, SA_NEW, size=SA_SIZE)
    for l in log:
        print(l)
    log.clear()

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

    # === StripAddress ($A055) -> no-op (born-32 is always 32-bit) ===
    # OS trap $55 (StripAddress) dispatches to $4080A7D0 = `and.l ($031A),d0; rts`
    # (an UNCONDITIONAL 24-bit strip; the stock 68000 SE ROM has no 32-bit mode).
    # In 32-bit mode StripAddress MUST be a no-op (as on a real 32-bit Mac / MODE32);
    # here it strips live $40808xxx ROM pointers down to $008xxxxx, which land in RAM.
    # STRIP-FIX in the emulator was a band-aid over exactly this; neuter the source
    # instead so the strip never happens and STRIP-FIX can be removed. Replace the
    # `and.l ($031A),d0` (c0b8 031a) with two NOPs, leaving the following RTS -> D0
    # is returned unchanged (identity), which is the correct 32-bit StripAddress.
    assert rom[0xA7D0:0xA7D6] == b'\xc0\xb8\x03\x1a\x4e\x75', "StripAddress site mismatch"
    rom[0xA7D0:0xA7D4] = bytes([0x4E, 0x71, 0x4E, 0x71])  # NOP NOP (keep RTS at $A7D4)
    patches += 1
    print(f"=== StripAddress ($A055 @ $A7D0) neutered to a 32-bit no-op ===")

    # === .DRVR open: keep the full 32-bit driver pointer (born-32) ===
    # The ROM's _Open for ROM-resident drivers (stock $403066-$403078) does
    #   MOVE.L (A0),D3 ; AND.L Lo3Bytes,D3 ; MOVE.L ROMBase,-(SP) ; CLR.B (SP) ;
    #   CMP.L (SP)+,D3 ; BCS ramdrvr ; MOVE.L D3,(A1)
    # i.e. it strips the DRVR master pointer to 24 bits and compares it with ROMBase
    # minus its high byte. On born-32 the .Sony DRVR lives in the ROM resources at
    # $40855406, which strips to $00855406 -- plain RAM -- so the DCE gets
    # dCtlDriver=$00855406, its flags and entry offsets are read from RAM, and the
    # Device Manager's JSR at $402F24 lands in never-written memory. (Found by running
    # this ROM in Snow; PiStorm's [DRV-FIX] in emulator.c papers over the same fault
    # at runtime by OR-ing $40000000 into A2 at $2F1E.) NOP the strip and the CLR.B so
    # the compare uses the real 32-bit pointer and ROMBase; figment master pointers
    # carry no flag bytes, so RAM-based drivers stay below ROMBase.
    assert rom[0x306A:0x306E] == b'\xc6\xb8\x03\x1a', ".DRVR open strip site mismatch"
    assert rom[0x3072:0x3074] == b'\x42\x17', ".DRVR open ROMBase CLR.B site mismatch"
    rom[0x306A:0x306E] = bytes([0x4E, 0x71, 0x4E, 0x71])  # NOP NOP (was AND.L Lo3Bytes,D3)
    rom[0x3072:0x3074] = bytes([0x4E, 0x71])              # NOP     (was CLR.B (SP))
    patches += 1
    print(f"=== .DRVR open ($306A/$3072): 32-bit driver pointer kept (no Lo3Bytes strip) ===")

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

    # === 5h. ROM-resident SCSI disk driver (".SCSIHD") — first-half code patches ===
    # Replace the on-disk Apple_Driver code with our own.  SCSIBoot's
    # driver-load SRead at $40DE is swapped for a shim that copies the
    # ROM-embedded driver image into the freshly _NewPtr'd sysheap buffer.
    # Everything downstream (CallDriver jsr (a3), DM dispatch) is stock —
    # our image simply IS "the loaded driver".  The driver does all I/O
    # via _SCSIDispatch, so no hardware addresses exist outside the
    # (already remapped) ROM SCSI Manager.  New-style 'PM' maps only.
    #
    # The driver image + copy-shim live in the MIRROR half and are embedded
    # after mirroring (below); here we only patch first-half SCSIBoot code,
    # which must happen before the checksum.  SCSIDRV_SHIM_OFF/_ROM_OFF are
    # mirror offsets (>= $40000), referenced as virtual $4080xxxx.
    SCSIDRV_ROM_OFF = 0x5E000
    SCSIDRV_SHIM_OFF = 0x5DF00
    scsidrv_path = os.path.join(os.path.dirname(__file__), 'scsidriver', 'SCSIDriver.bin')
    embed_scsidrv = os.path.exists(scsidrv_path)
    if embed_scsidrv:
        # Patch SCSIBoot driver-load: $40DE `bsr SRead` + $40E2 `bne` (6 bytes)
        # -> jsr shim.l (shim copies our image into a2's buffer, then rts).
        assert rom[0x40DE:0x40E0] == b'\x61\x00', "SCSIBoot load site mismatch"
        rom[0x40DE:0x40E4] = struct.pack('>HI', 0x4EB9, 0x40800000 + SCSIDRV_SHIM_OFF)

        # Checksum bypass: $416A `beq.s CkSumOK` -> `bra.s` (disk driver's
        # boot_cksum can't match our bytes; result intentionally ignored).
        assert rom[0x416A] == 0x67, "cksum branch site mismatch"
        rom[0x416A] = 0x60
        patches += 2

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

            # Build 68k installer: clear addr 0, set A-line vector, write trap table, RTS
            data_offset = 40
            lea_disp = data_offset - 16
            code = bytearray()
            # Clear address 0: the $1D10 vector-table copy leaves vector-0
            # (reset SSP = $2000) at address 0.  The File Manager FCB search
            # at $932E does MOVEA.L A3,A4 (A4=0); MOVE.L (A4),A4 — i.e. it
            # dereferences address 0 as a nil list head.  With $2000 there the
            # nil-terminated walk never ends and boot wedges.  Vector 0 is only
            # read at physical reset, so zeroing it at runtime is safe.
            code += struct.pack('>HH', 0x42B8, 0x0000)  # CLR.L $0000.W
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
            assert len(code) == 40
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

    # === Lock SERD before the ROM JSRs into it (first-half patch) ===
    # ROM $07B4 does MOVEA.L D0,A0; MOVEA.L (A0),A0; JSR (A0) to call the just-
    # GetResource'd 'SERD' serial driver with NO HLock.  The driver runs from a
    # movable handle; its init NewPtr compacts the system heap and slides the
    # unlocked driver block away → it calls into freed/reused heap → Line-F /
    # Sad Mac F/A.  Redirect through a stub that _HLocks the handle first.
    SERD_STUB_OFF = 0x46000  # free mirror space between installer and RESMGR
    serd_stub_vaddr = 0x40800000 + SERD_STUB_OFF
    assert rom[0x07B4:0x07BA] == b'\x20\x40\x20\x50\x4E\x90', "SERD call site mismatch"
    # SERD-HLock redirect DISABLED (2026-08-08).  Once the DoRomEntry ROM-resource fix
    # lets SERD resolve, it comes from the combo ROM (handle -> ROM data $4085xxxx), i.e.
    # a NON-movable ROM resource that needs no HLock.  The June HLock band-aid was for a
    # SERD loaded from DISK as a movable handle; here _HLock instead runs on the ROZ
    # pseudo-handle, driving figment _CheckHeap into the RM-built ROZ/map blocks it can't
    # validate -> RTS into RAM-fill -> Sad Mac.  Keep the original HLock-free call
    # (MOVEA.L D0,A0; MOVEA.L (A0),A0; JSR (A0)); the boot then clears the crash and
    # reaches the SCSI mount phase.  (Revisit only if SERD is ever served from disk.)
    # rom[0x07B4:0x07BA] = struct.pack('>HI', 0x4EB9, serd_stub_vaddr)  # JSR stub.l
    print(f"=== SERD HLock: disabled (SERD is a ROM resource; original HLock-free call at $07B4) ===")

    # === Boot-icon screen dest: use runtime ScrnBase, not the IS=8 video base ===
    # HAPPYMAC/boot-icon blits hardcode the screen dest as an absolute address
    # (e.g. $7FCB5E).  That's the original 4MB value $3FCB5E relocated by the
    # VBUF pass to the IS=8 8MB-top video base — but these icons are drawn
    # AFTER MODE32, when the framebuffer lives at the top of physical RAM
    # (ScrnBase = MemTop - $5900).  A static base can't serve both phases, so
    # replace each "moveal #addr,A2" with a JSR to a stub that loads the live
    # ScrnBase ($0824) and adds the (phase-independent) centering offset.
    # Boot-icon / Sad-Mac screen dests are hardcoded to the IS=8 8MB-top video
    # base ($7Fxxxx); but the icons draw AFTER MODE32, when the framebuffer is
    # at the 32MB top (ScrnBase $01FFA700 = MemTop $02000000 - $5900, confirmed
    # at runtime).  Repoint each absolute screen address in place, $7F -> $1FF
    # (new = (old & $FFFF) | $01FF0000).  Handles both "moveal #imm,A2" (247C)
    # and "lea imm.L,A2" (45F9).  Covers Happy Mac AND the Sad Mac screen-clear
    # + hex-code plotting in CRITERR ($10A0).
    _icon_sites = [
        (0x1188, 0x247C, 0x007FCB5E),  # HAPPYMAC icon
        (0x1196, 0x247C, 0x007FCCDF),  # HAPPYMAC symbol
        (0x0F4A, 0x247C, 0x007FCB5E),  # boot/Sad-Mac icon
        (0x0F5C, 0x247C, 0x007FCF1F),  # boot/Sad-Mac symbol
        (0x10A0, 0x45F9, 0x007FA700),  # CRITERR: screen-clear + hex-code base
    ]
    _icon_done = 0
    for _site, _op, _old in _icon_sites:
        _new = (_old & 0x0000FFFF) | B32_VIDEO
        if rom[_site:_site+6] == struct.pack('>HI', _op, _old):
            rom[_site:_site+6] = struct.pack('>HI', _op, _new)
            _icon_done += 1
            patches += 1
        else:
            print(f"  WARN: screen site ${_site:05X} mismatch (got {rom[_site:_site+6].hex()}), skipped")
    print(f"=== Boot/Sad-Mac screen dests → ${B32_VIDEO:07X}xxx: {_icon_done}/{len(_icon_sites)} sites ===")

    # === boot32: replace the SE ROM's 24-bit RAM test / sizing (first-half) ===
    # The power-on diagnostic dispatcher (entered via $40800044 -> $40801BDE)
    # calls leaf routines whose result D6 it checks with `tst.l d6; bne SadMac`
    # (0 = pass).  Three are fatal/wrong under born-32 (IS=0, flat 32MB):
    #   $26F0  destructive RAM test — sweeps a fill pattern across the tested
    #          range INCLUDING the low exception-vector table; a later Line-F
    #          then vectors through a clobbered $02C into the poisoned stack.
    #          -> ramtest (noErr, no writes).
    #   $2928  RAM aliasing TEST — must return 0=pass.  -> ramtest (noErr).
    #   $25FA  RAM SIZING — returns top-of-RAM in D6, installed as SP.  The SE
    #          probe caps well below 32MB.  -> memsize (reports $02000000).
    # boot32 jump table: blob+0 -> memsize ($02000000), blob+6 -> ramtest (0).
    boot32_path = os.path.join(os.path.dirname(__file__), 'boot32', 'boot32.bin')
    BOOT32_ROM_OFF = 0x70000        # mirror half -> virtual $40870000
    boot32 = b''
    if os.path.exists(boot32_path):
        boot32 = open(boot32_path, 'rb').read()
        boot32_vaddr = 0x40800000 + BOOT32_ROM_OFF
        assert rom[0x25FA:0x25FE] == b'\x41\xfa\x00\x7e', "sizing entry mismatch"
        assert rom[0x2928:0x292C] == b'\x20\x0f\x45\xf9', "aliasing-test entry mismatch"
        assert rom[0x26F0:0x26F4] == b'\x24\x48\x26\x09', "RAM-test entry mismatch"
        rom[0x25FA:0x2600] = struct.pack('>HI', 0x4EF9, boot32_vaddr + 0)  # sizing -> memsize
        rom[0x2928:0x292E] = struct.pack('>HI', 0x4EF9, boot32_vaddr + 6)  # aliasing -> ramtest
        # === REINSTATE the real RAM test ($26F0), 32-bit-patched (2026-07-09) ===
        # Instead of stubbing it to a no-op (which left RAM in the boot32 blanket
        # $6DB6DB6D fill — NOT bigSE's real 3-pattern march), run the ORIGINAL ROM
        # RAM test via a stub that CLAMPS the low bound above the live vectors +
        # low-mem (A0 = max(A0,$4000)) so it doesn't clobber $0-$3FF (the fatal
        # $02C Line-F vector) or the born-32 low structures.  The test's 32-bit
        # divide ($26FC/$270A via swap) already handles >4M ranges.  Stub lives in
        # free mirror space; jumps back into the real test at $26F2.
        RAMTEST_STUB_OFF = 0x6F000   # free mirror space (after combo $5D986, before boot32 $70000)
        ramtest_stub_vaddr = 0x40800000 + RAMTEST_STUB_OFF
        rom[0x26F0:0x26F6] = struct.pack('>HI', 0x4EF9, ramtest_stub_vaddr)  # RAM test -> clamp stub
        # The PMMU is enabled off the LAST RAM-test call via the $26F0 stub above
        # (A0==$00200000 → boot32 trampoline blob+12), NOT at a fixed seam — the
        # earlier $1F3C attempt was the mem-test-FAILURE path, never taken healthily.
        patches += 3
        print(f"=== boot32: $25FA->memsize(${boot32_vaddr:08X}), $2928->ramtest, "
              f"$26F0->REINSTATED-ramtest(clamp@${ramtest_stub_vaddr:08X}), "
              f"PMMU-enable off last RAM-test call ($200000) ===")
    else:
        # Hard error, not a warning.  Without boot32 the ROM still LOOKS fine and even
        # reaches "Starting up...", because the emulator supplies MemTop at the $48 seam
        # -- but $70000 keeps the raw mirror bytes instead of boot32's trampolines, so
        # memsize/ramtest/PMMU-setup are garbage and the boot hangs in the Device
        # Manager's ioResult wait.  Cost an hour on 2026-09-16 after a helper script
        # cleaned boot32.bin and left it unbuilt.
        raise SystemExit(f"ERROR: boot32.bin not found ({boot32_path}) -- "
                         f"run `gmake -C boot32` first (gmake, not make: see the Makefile notes)")

    # === Born-32 32-bit-clean: Lo3Bytes ($031A) = $FFFFFFFF ===
    # ROM $07CA does `move.l #$00FFFFFF, $031A` (Lo3Bytes, the 24-bit address
    # mask).  Throughout the ROM, `and.l Lo3Bytes,An` strips pointers to 24 bits
    # — e.g. the DCE install ($4080306A) turns the .Sony driver ptr $40855406
    # into $00855406 (RAM zeros) → wild jump.  In born-32 every pointer is a real
    # 32-bit address, so make the mask a no-op.  One byte: $00FFFFFF -> $FFFFFFFF.
    assert rom[0x07CA:0x07D2] == bytes.fromhex('21FC00FFFFFF031A'), "Lo3Bytes init site mismatch"
    rom[0x07CC] = 0xFF
    patches += 1
    print("=== Lo3Bytes init patched to $FFFFFFFF (32-bit clean) ===")

    # === FXM MapFBlock: add.w -> add.l (SuperMario 3/24/87 fix) ===
    # $4080783E in MapFBlock adds the intra-alloc-block physical-block offset
    # (d0) into the physical start block (d3) with a 16-bit `add.w d0,d3`
    # ($D640), losing carry into the high word — 24-bit-dirty.  SuperMario
    # OS/HFS/FXM.a fixed this to `ADD.L D0,D3` ($D680) on 3/24/87.  Harmless on a
    # 24-bit SE; under flat IS=0 the lost carry corrupts the physical block.
    assert rom[0x783E:0x7840] == b'\xD6\x40', "MapFBlock add.w site mismatch"
    rom[0x783E:0x7840] = b'\xD6\x80'
    patches += 1
    print("=== FXM MapFBlock add.w->add.l patched (32-bit clean) ===")

    # === CallWindow: decode windowDefProc instead of dereferencing it whole ===
    # windowDefProc (WindowRecord+$7E) is not a pointer, it is a packed field:
    # the window's VARIANT in the low 4 bits of the high byte, the WDEF handle in
    # the low 24.  System 7.5's GetWVariant patch reads it that way
    # (`moveq #$F,d0; and.b $7e(a1),d0`), so the packing is the System's contract
    # and cannot be moved elsewhere.  A 68000 never noticed, having no A24-A31, and
    # born-32 papered over it too: unknown high bytes alias to a dirty-alias L1
    # entry at physical $0, so the variant byte fell off on the way to RAM.
    #
    # At 32MB that stops being true for variant 1: L1 slot $01 is real memory now,
    # so $010B6CE4 no longer folds to $000B6CE4 and CallWindow dereferences 16MB
    # up, into RAM-test fill -> `jsr (a0)` into nowhere.  Every modal dialog
    # (dBoxProc = variant 1) takes the machine down.  $02-$FF still alias, so only
    # variant 1 is affected.
    #
    # The ROM already decodes the field correctly five instructions later, when it
    # hands the handle to _LoadResource (`move.l a0,-(a7); clr.b (a7)`); it just
    # forgot to do so for its own two loads.  Route both through a stub that strips
    # the variant byte the same way.  Fixing A0 at the load covers the whole
    # routine: the reload after _LoadResource, the `bset #7,(a0)` lock and the
    # `jsr (a0)` all use it.
    WDEF_STUB_OFF = 0x6F100      # free mirror space (RAM-test stub sits at $6F000)
    wdef_stub1 = 0x40800000 + WDEF_STUB_OFF
    wdef_stub2 = wdef_stub1 + 16
    assert rom[0xC0A2:0xC0A8] == bytes.fromhex('206b007e2010'), "CallWindow deref site mismatch"
    assert rom[0xC0C2:0xC0CA] == bytes.fromhex('206b007e08900007'), "CallWindow unlock site mismatch"
    rom[0xC0A2:0xC0A8] = struct.pack('>HI', 0x4EB9, wdef_stub1)          # jsr stub1.l
    rom[0xC0C2:0xC0CA] = struct.pack('>HIH', 0x4EB9, wdef_stub2, 0x4E71)  # jsr stub2.l ; nop
    patches += 2
    print(f"=== CallWindow windowDefProc decoded via stubs ${wdef_stub1:08X}/${wdef_stub2:08X} (32-bit clean) ===")

    # === CallControl: same field, same fix ===
    # contrlDefProc (ControlRecord+$18) packs the control's variant the same way, and
    # System 7.5's GetCVariant patch reads it the same way (`moveq #$F,d0;
    # and.b $18(a0),d0`).  CallControl at $4080D426 is CallWindow's twin, down to the
    # `move.l a0,-(a7); clr.b (a7)` before its own _LoadResource.  Speedometer launched
    # once the window side was fixed, then died here on the benchmark's controls.
    cdef_stub1 = wdef_stub1 + 32
    cdef_stub2 = wdef_stub1 + 48
    assert rom[0xD448:0xD44E] == bytes.fromhex('206800184a90'), "CallControl deref site mismatch"
    assert rom[0xD468:0xD470] == bytes.fromhex('2068001808900007'), "CallControl unlock site mismatch"
    rom[0xD448:0xD44E] = struct.pack('>HI', 0x4EB9, cdef_stub1)           # jsr stub3.l
    rom[0xD468:0xD470] = struct.pack('>HIH', 0x4EB9, cdef_stub2, 0x4E71)   # jsr stub4.l ; nop
    patches += 2
    print(f"=== CallControl contrlDefProc decoded via stubs ${cdef_stub1:08X}/${cdef_stub2:08X} (32-bit clean) ===")

    # === Route the ROM's system-heap-grow ($AE20) into figment ===
    # When the system heap fills, this ROM routine (entry: A0=new heap end,
    # A6=curHeap) splices a new free block at the old end into the zone using the
    # ROM Memory Manager's OWN 24-bit block/free-list format ($4080AAD2 /
    # $4080B03A) — which does NOT match figment's 32-bit zone, corrupting
    # figment's free list / sentinels.  It's a direct (non-trap) call so figment
    # never sees it.  Replace the routine body with a call to figment's
    # c_GrowSysZone(curHeap, newEnd) — it restores the system heap's spoof
    # back-link (which was never set, so KillBlock's coalescing back-walk would
    # otherwise fall off into block 0) and then ExtendHeapLimit's the zone in
    # figment's own format. Preserve all regs; RTS.
    import subprocess as _sp
    _nm = os.path.expanduser('~/Retro68-build/toolchain/bin/m68k-apple-macos-nm')
    _figelf = os.path.join(os.path.dirname(__file__), 'figment', 'figment.elf')
    FIG_GROWSYSZONE = None
    for _ln in _sp.check_output([_nm, _figelf], text=True).splitlines():
        _p = _ln.split()
        if len(_p) >= 3 and _p[2] == 'c_GrowSysZone':
            FIG_GROWSYSZONE = int(_p[0], 16)
    assert FIG_GROWSYSZONE, "c_GrowSysZone not found in figment.elf"
    assert rom[0x0AE20:0x0AE22] == b'\x48\xe7', "ROM heap-grow prologue mismatch"
    # C args pushed right->left: c_GrowSysZone(curHeap, newEnd) => push newEnd(A0), curHeap(A6)
    grow_stub  = struct.pack('>HH', 0x48E7, 0xFFFE)            # movem.l d0-d7/a0-a6,-(sp)
    grow_stub += struct.pack('>H', 0x2F08)                     # move.l a0,-(sp)  (newEnd, arg2)
    grow_stub += struct.pack('>H', 0x2F0E)                     # move.l a6,-(sp)  (curHeap, arg1)
    grow_stub += struct.pack('>HI', 0x4EB9, FIG_GROWSYSZONE)   # jsr c_GrowSysZone
    grow_stub += struct.pack('>H', 0x508F)                     # addq.l #8,sp
    grow_stub += struct.pack('>HH', 0x4CDF, 0x7FFF)            # movem.l (sp)+,d0-d7/a0-a6
    grow_stub += struct.pack('>H', 0x4E75)                     # rts
    assert len(grow_stub) <= 0x20, "grow stub too big"
    rom[0x0AE20:0x0AE20 + len(grow_stub)] = grow_stub
    patches += 1
    print(f"=== ROM system-heap-grow ($AE20) -> figment c_GrowSysZone (${FIG_GROWSYSZONE:08X}) ===")

    # Fix checksum (AFTER all first-half patches, before mirroring)
    checksum = 0
    # ROM version at $08 left as $0276 — System file needs it to find patches.

    for i in range(4, half, 2):
        checksum = (checksum + struct.unpack_from('>H', rom, i)[0]) & 0xFFFFFFFF
    struct.pack_into('>I', rom, 0, checksum)
    print(f"Checksum updated to ${checksum:08X}")

    # Write patched ROM (mirror to 512KB)
    out = bytearray(rom + rom)

    # SERD HLock stub (mirror half): MOVEA.L D0,A0; _HLock; MOVEA.L (A0),A0;
    # JSR (A0); RTS.  D0 = handle from GetResource; lock it, then call the
    # driver exactly as the original code did.
    serd_stub = struct.pack('>HHHHH', 0x2040, 0xA029, 0x2050, 0x4E90, 0x4E75)
    out[SERD_STUB_OFF:SERD_STUB_OFF + len(serd_stub)] = serd_stub
    print(f"=== SERD HLock stub embedded at ROM+${SERD_STUB_OFF:05X} ({len(serd_stub)} bytes) ===")

    # CallWindow windowDefProc stubs (mirror half; see the $C0A2/$C0C2 patches above).
    # `clr.b (a7)` on the pushed copy clears the high byte -- the ROM's own idiom for
    # this field.  stub1 also re-does the `move.l (a0),d0` it replaced, and the caller's
    # following `bne.b` reads the Z flag from it (jsr and rts leave the CCR alone).
    wdef_stub_code = struct.pack('>HHHHHHH',
        0x206B, 0x007E,   # movea.l $7e(a3),a0   -- variant byte still in the high byte
        0x2F08,           # move.l  a0,-(a7)
        0x4217,           # clr.b   (a7)         -- strip it
        0x205F,           # movea.l (a7)+,a0     -- A0 = the real 32-bit handle
        0x2010,           # move.l  (a0),d0      -- the load the patch displaced
        0x4E75)           # rts
    assert len(wdef_stub_code) == 14
    wdef_stub2_code = struct.pack('>HHHHHHHH',
        0x206B, 0x007E,   # movea.l $7e(a3),a0
        0x2F08,           # move.l  a0,-(a7)
        0x4217,           # clr.b   (a7)
        0x205F,           # movea.l (a7)+,a0
        0x0890, 0x0007,   # bclr.b  #7,(a0)      -- unlock, now at the right address
        0x4E75)           # rts
    assert len(wdef_stub2_code) == 16
    out[WDEF_STUB_OFF:WDEF_STUB_OFF + 14] = wdef_stub_code
    out[WDEF_STUB_OFF + 16:WDEF_STUB_OFF + 32] = wdef_stub2_code
    print(f"=== CallWindow windowDefProc stubs embedded at ROM+${WDEF_STUB_OFF:05X} (14+16 bytes) ===")

    # CallControl stubs.  Here the ControlRecord pointer is already in A0 and the load
    # overwrites it, so the stub reloads from $18(a0) exactly as the original did.
    cdef_stub_code = struct.pack('>HHHHHHH',
        0x2068, 0x0018,   # movea.l $18(a0),a0
        0x2F08,           # move.l  a0,-(a7)
        0x4217,           # clr.b   (a7)        -- strip the variant
        0x205F,           # movea.l (a7)+,a0
        0x4A90,           # tst.l   (a0)        -- the test the patch displaced
        0x4E75)           # rts                 -- the caller's bne.b reads its Z flag
    assert len(cdef_stub_code) == 14
    cdef_stub2_code = struct.pack('>HHHHHHHH',
        0x2068, 0x0018,   # movea.l $18(a0),a0
        0x2F08,           # move.l  a0,-(a7)
        0x4217,           # clr.b   (a7)
        0x205F,           # movea.l (a7)+,a0
        0x0890, 0x0007,   # bclr.b  #7,(a0)     -- unlock at the right address
        0x4E75)           # rts
    assert len(cdef_stub2_code) == 16
    out[WDEF_STUB_OFF + 32:WDEF_STUB_OFF + 46] = cdef_stub_code
    out[WDEF_STUB_OFF + 48:WDEF_STUB_OFF + 64] = cdef_stub2_code
    print(f"=== CallControl contrlDefProc stubs embedded at ROM+${WDEF_STUB_OFF + 32:05X} (14+16 bytes) ===")

    # Reinstated RAM-test clamp stub (mirror half; see the $26F0 redirect above).
    # The $26F0 redirect is a 6-byte jmp that overwrites THREE original instructions:
    #   $26F0 2448 movea.l a0,a2 ; $26F2 2609 move.l a1,d3 ; $26F4 9688 sub.l a0,d3
    # (they compute the sweep length d3 = a1-a0). The stub MUST replicate all three
    # and jump back to $26F6 (first intact instruction, move.w d3,d5) — NOT $26F2,
    # which now lands inside the jmp operand and corrupts the registers.
    if boot32:
        # Use the DISPATCHER'S natural A0/A1 range (do NOT clamp): the real ROM
        # sweeps up to MemTop including the screen buffer, which is why the RAM-test
        # pattern is visible on a real SE. The earlier clamp was a workaround for
        # "A0/A1 garbage above 32MB" — but that garbage was the stub's own jmp-back
        # landing mid-operand (fixed below), not the dispatcher. So just replicate
        # the 3 overwritten instructions and continue with the natural range.
        # The RAM test runs across 4 calls (probed order): #1 $0-$400, #2 screen
        # $1FFA700-$1FFFF00, #3 $40000-$200000, #4 $200000-$1FFA700 (LAST). We:
        #  (a) CAP A1 at $01FFFF00 always — keeps A0 natural so the test still writes
        #      the screen buffer ($1FFA700-$1FFFC80 → visible pattern), but stops below
        #      the caller's stack-saved regs (~$1FFFFC0, under MemTop=SP=$2000000) that
        #      it would otherwise overwrite → caller's `movem (sp)+; rts` wild-jumps.
        #  (b) on the LAST call (A0==$00200000), save the diagnostic return in A3 and
        #      point A6 at the boot32 PMMU-setup trampoline (blob+12): the test's
        #      `jmp (a6)` lands there AFTER the final sweep, so it builds the page
        #      tables + MMU globals + enables translation with nothing left to clobber
        #      them, then restores A6 from A3 and returns to the POST — MMU now on.
        _rts = (struct.pack('>HI', 0xB3FC, B32_RAMTEST_CAP)  # cmpa.l #cap,a1
              + struct.pack('>H', 0x6306)                 # bls.s +6 (a1<=cap: keep)
              + struct.pack('>HI', 0x227C, B32_RAMTEST_CAP)  # movea.l #cap,a1 (protect stack regs)
              + struct.pack('>HI', 0xB1FC, 0x00200000)    # cmpa.l #$200000,a0  (LAST RAM-test call?)
              + struct.pack('>H', 0x6608)                 # bne.s +8 (not last: skip A3/A6 redirect)
              + struct.pack('>H', 0x264E)                 # movea.l a6,a3  (save diag return)
              + struct.pack('>HI', 0x2C7C, boot32_vaddr + 12)  # movea.l #pmmu_trampoline,a6
              + struct.pack('>H', 0x2448)                 # movea.l a0,a2  (orig $26F0)
              + struct.pack('>H', 0x2609)                 # move.l a1,d3   (orig $26F2)
              + struct.pack('>H', 0x9688)                 # sub.l a0,d3    (orig $26F4)
              + struct.pack('>HI', 0x4EF9, 0x408026F6))   # jmp $408026F6  (continue at move.w d3,d5)
        assert RAMTEST_STUB_OFF + len(_rts) <= len(out)
        out[RAMTEST_STUB_OFF:RAMTEST_STUB_OFF + len(_rts)] = _rts
        print(f"=== RAM-test clamp stub embedded at ROM+${RAMTEST_STUB_OFF:05X} ({len(_rts)} bytes) ===")

    # Embed boot32 blob in mirror half ($40870000)
    if boot32:
        assert BOOT32_ROM_OFF + len(boot32) <= len(out), "boot32 overflows ROM"
        out[BOOT32_ROM_OFF:BOOT32_ROM_OFF + len(boot32)] = boot32
        print(f"=== boot32 embedded at ROM+${BOOT32_ROM_OFF:05X} ({len(boot32)} bytes) ===")

    # Embed Figment binary in mirror half
    if os.path.exists(figment_path):
        if FIGMENT_ROM_OFF + len(figment) <= len(out):
            out[FIGMENT_ROM_OFF:FIGMENT_ROM_OFF + len(figment)] = figment
            print(f"\n=== Figment MM embedded at ROM+${FIGMENT_ROM_OFF:05X} ({len(figment)} bytes) ===")
        else:
            print(f"\n=== WARNING: Figment doesn't fit ===")

    # Embed figext (figment/figext.bin): born-32's _HeapDispatch ($A0A4) + Apple's
    # ProcessMgrHeap.c, linked at $40874000 against figment.elf because figment's own slot
    # is full.  Its trap entry comes from figment_offsets.h.  The slot is the unused upper
    # mirror of the 256KB base ROM; refuse to overwrite anything already patched there.
    figext_path = os.path.join(os.path.dirname(__file__), 'figment', 'figext.bin')
    FIGEXT_ROM_OFF = 0x74000
    if os.path.exists(figext_path):
        figext = open(figext_path, 'rb').read()
        _mirror = FIGEXT_ROM_OFF - 0x40000
        assert out[FIGEXT_ROM_OFF:FIGEXT_ROM_OFF + len(figext)] == out[_mirror:_mirror + len(figext)], \
            "figext slot at ROM+$74000 is already in use"
        out[FIGEXT_ROM_OFF:FIGEXT_ROM_OFF + len(figext)] = figext
        print(f"=== figext (_HeapDispatch) embedded at ROM+${FIGEXT_ROM_OFF:05X} ({len(figext)} bytes) ===")

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

    # Embed .SCSIHD driver image + copy-shim in mirror half (after combo).
    # SCSIDRV_SHIM_OFF/_ROM_OFF set in section 5h; the first-half SCSIBoot
    # patch jumps to virtual $40800000+SHIM_OFF.  Verify no overlap with
    # the combo blob, then write image then shim.
    if embed_scsidrv:
        scsidrv = open(scsidrv_path, 'rb').read()
        if len(scsidrv) & 1:
            scsidrv += b'\0'
        combo_end = COMBO_ROM_OFF + (len(combo_blob) if combo_blob else 0)
        assert SCSIDRV_SHIM_OFF >= combo_end, "shim overlaps combo blob"
        assert SCSIDRV_ROM_OFF + len(scsidrv) <= len(out), "driver overflows ROM"

        out[SCSIDRV_ROM_OFF:SCSIDRV_ROM_OFF + len(scsidrv)] = scsidrv

        drv_vaddr = 0x40800000 + SCSIDRV_ROM_OFF
        # DBG string lives just before the shim; A0 points at it for $A0FE.
        dbg_str = b"SCSIHD shim ran\x00"
        dbg_off = SCSIDRV_SHIM_OFF - 0x20
        out[dbg_off:dbg_off + len(dbg_str)] = dbg_str
        dbg_vaddr = 0x40800000 + dbg_off
        # Copy-shim — replaces SCSIBoot's `bsr SRead;bne` with a2 = dest
        # buffer (preserved): [dbg: lea str,a0; $A0FE] movem.l d0/a0-a1,-(sp);
        # movea.l a2,a1; lea drv,a0; move.w #words-1,d0; (a0)+→(a1)+; dbra;
        # restore; rts.
        shim = struct.pack('>HI', 0x41F9, dbg_vaddr) # lea dbg.l,a0
        shim += struct.pack('>H', 0xA0FE)            # debug trap (A0=str)
        shim += struct.pack('>HH', 0x48E7, 0x80C0)   # movem.l d0/a0-a1,-(sp)
        shim += struct.pack('>H', 0x224A)            # movea.l a2,a1
        shim += struct.pack('>HI', 0x41F9, drv_vaddr)# lea drv.l,a0
        shim += struct.pack('>HH', 0x303C, len(scsidrv) // 2 - 1)  # move.w #n,d0
        shim += struct.pack('>H', 0x32D8)            # move.w (a0)+,(a1)+
        shim += struct.pack('>HH', 0x51C8, 0xFFFC)   # dbra d0,-4
        shim += struct.pack('>HH', 0x4CDF, 0x0301)   # movem.l (sp)+,d0/a0-a1
        shim += struct.pack('>H', 0x4E75)            # rts
        out[SCSIDRV_SHIM_OFF:SCSIDRV_SHIM_OFF + len(shim)] = shim
        print(f"=== .SCSIHD driver embedded at ROM+${SCSIDRV_ROM_OFF:05X} "
              f"({len(scsidrv)} bytes), shim at +${SCSIDRV_SHIM_OFF:05X} ===")

    # === 6. 32-bit-clean the ROM's BARE 24-bit VIA I/O accesses ===
    # The stock SE sound volume/enable routine (3 copies) uses `move.b`/`bclr`
    # with a HARDCODED bare 24-bit VIA absolute address ($EFFFFE = Port A / sound
    # volume, $EFE1FE = Port B / sndEnb).  The general opcode scan (§2) only
    # remaps JMP/JSR/LEA/PEA/MOVE.L#imm operands, so these byte/bit accesses slip
    # through.  Clean in 24-bit (bigSE masks the top byte); in flat-32 born-32 the
    # bare $00EFxxxx hits the 32MB RAM instead of the VIA -> sound (and, since VIA
    # Port A also carries PA5=floppy head-select, potentially the IWM) misbehave.
    # Remap each operand's high byte $00 -> $40 (into the iomap window).  Offsets
    # are in the FINAL assembled `out` (first-half copy is checksummed + mirrored;
    # the live copy is in the embed-overwritten second half).  Recompute the
    # first-half self-checksum afterward.
    _bare_via_ops = [0x036D30, 0x036D44, 0x036D70,   # copy 1 (first half, checksummed)
                     0x057AD6, 0x057AEA, 0x057B16,   # copy 2 (second half, LIVE)
                     0x076D30, 0x076D44, 0x076D70]   # copy 3 (second half, mirror)
    _fixed = 0
    for _o in _bare_via_ops:
        _v = struct.unpack_from('>I', out, _o)[0]
        if (_v >> 24) == 0x00 and 0xEFE000 <= (_v & 0xFFFFFF) < 0xF00000:
            struct.pack_into('>I', out, _o, 0x40000000 | _v)
            _fixed += 1
        else:
            print(f"  WARNING: bare-VIA operand at +${_o:05X} = ${_v:08X} (unexpected; skipped)")
    # Recompute the first-half self-checksum (sum of words from +4), since some
    # patched operands ($036Dxx) live in the checksummed first half.
    _ck = 0
    for i in range(4, half, 2):
        _ck = (_ck + struct.unpack_from('>H', out, i)[0]) & 0xFFFFFFFF
    struct.pack_into('>I', out, 0, _ck)
    print(f"=== 32-bit-clean: remapped {_fixed} bare-VIA I/O operands -> $40EFxxxx; checksum ${_ck:08X} ===")

    open(outfile, 'wb').write(out)
    print(f"Written to {outfile}")

if __name__ == '__main__':
    infile = sys.argv[1] if len(sys.argv) > 1 else 'se-rom.bin'
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'se-rom-huge.bin'
    patch_rom(infile, outfile)
