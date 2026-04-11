#!/usr/bin/env python3
"""
Huge SE boot ROM: big-se-style addresses + RAM sizing skip.

Uses big-se address relocations (24-bit range, PMMU handles the rest):
  ROM:         $400000 → $800000
  SCSI/IWM:    $580000 → $880000
  Video/sound: $3F0000 → $7F0000

Skips RAM sizing (32MB WTC RAM makes probes find "RAM" at ROM addresses).
Forces MemTop = $800000 (8MB visible during 24-bit boot).
The emulator's PMMU with IS=8 handles the virtual→physical translation.
"""
import struct
import sys

def patch_rom(infile, outfile):
    rom = bytearray(open(infile, 'rb').read())
    half = len(rom) // 2  # 256KB, rest is mirror
    rom = rom[:half]

    patches = 0
    log = []

    def patch32(off, old_base, new_base, size=0x100000):
        nonlocal patches
        val = struct.unpack_from('>I', rom, off)[0]
        if old_base <= val < old_base + size:
            new_val = val - old_base + new_base
            struct.pack_into('>I', rom, off, new_val)
            log.append(f"  ${off+0x800000:06X} (+${off:05X}): ${val:08X} → ${new_val:08X}")
            patches += 1
            return True
        return False

    # === 1. Initial PC ===
    print("=== Reset vector (initial PC) ===")
    patch32(4, 0x400000, 0x800000)

    # === 2. Instruction patterns ===
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
            if patch32(off + 2, 0x400000, 0x800000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x880000, size=0x80000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x3F0000, 0x7F0000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]

        elif opcode in moveq_imm:
            name = moveq_imm[opcode]
            if patch32(off + 2, 0x400000, 0x800000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x880000, size=0x80000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x3F0000, 0x7F0000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]

    for l in log:
        print(l)

    # === 3. Exception vector table at $19E8 ===
    print("\n=== Exception vector table at $19E8 ===")
    for i in range(64):
        off = 0x19E8 + i * 4
        patch32(off, 0x400000, 0x800000)
    for l in log[len(log)-63:]:
        print(l)
    log.clear()

    # === 4. Brute-force remaining $00400000 data constants ===
    print("\n=== Remaining $00400000 values (data constants) ===")
    for i in range(0, min(half, 0x1B000), 4):
        val = struct.unpack_from('>I', rom, i)[0]
        if val == 0x00400000:
            struct.pack_into('>I', rom, i, 0x00800000)
            patches += 1
            log.append(f"  ${i+0x800000:06X} (+${i:05X}): $00400000 → $00800000 (data const)")
    for l in log:
        print(l)
    log.clear()

    # === 5. Skip RAM sizing, force MemTop = 8MB ===
    # Same skip as huge-se but MemTop = $800000 (8MB visible in 24-bit boot).
    # The emulator's PMMU hook will override if needed.
    print("\n=== Skip RAM sizing, force MemTop = 8MB ===")
    skip_patch = bytes([
        0x7C, 0x00,                          # MOVEQ #0, D6
        0x42, 0x47,                          # CLR.W D7
        0x2C, 0x7C, 0x00, 0x80, 0x00, 0x00, # MOVEA.L #$800000, A6
        0x3E, 0x7C, 0x04, 0x00,             # MOVEA.W #$400, SP
        0x60, 0x00, 0x00, 0x94,             # BRA.W $1D04 (offset = $1D04 - $1C70 = $94)
    ])
    off = 0x1C60
    old_bytes = bytes(rom[off:off+len(skip_patch)])
    rom[off:off+len(skip_patch)] = skip_patch
    patches += 1
    print(f"  ${off+0x800000:06X} (+${off:05X}): {old_bytes.hex()} → {skip_patch.hex()}")

    print(f"\n=== Total patches: {patches} ===")

    # Fix checksum
    checksum = 0
    for i in range(4, half, 2):
        checksum = (checksum + struct.unpack_from('>H', rom, i)[0]) & 0xFFFFFFFF
    struct.pack_into('>I', rom, 0, checksum)
    print(f"Checksum updated to ${checksum:08X}")

    # Write patched ROM (mirror to 512KB)
    out = rom + rom
    open(outfile, 'wb').write(out)
    print(f"Written to {outfile}")

if __name__ == '__main__':
    infile = sys.argv[1] if len(sys.argv) > 1 else 'se-rom.bin'
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'se-rom-huge-boot.bin'
    patch_rom(infile, outfile)
