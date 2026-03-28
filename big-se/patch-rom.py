#!/usr/bin/env python3
"""
Patch SE ROM to relocate from $400000 to $800000.
ROM base: $400000 → $800000
SCSI base: $580000 → $880000

Strategy: find 68000 instruction patterns that contain absolute addresses
in the $4xxxxx or $58xxxx ranges, and relocate them. This avoids blindly
patching data that happens to look like addresses.
"""
import struct
import sys

def patch_rom(infile, outfile):
    rom = bytearray(open(infile, 'rb').read())
    half = len(rom) // 2  # 256KB, rest is mirror
    rom = rom[:half]

    patches = 0
    log = []

    def patch32(off, old_base, new_base):
        """Patch a 32-bit value at offset if it's in the target range."""
        nonlocal patches
        val = struct.unpack_from('>I', rom, off)[0]
        if old_base <= val < old_base + 0x100000:
            new_val = val - old_base + new_base
            struct.pack_into('>I', rom, off, new_val)
            log.append(f"  ${off+0x800000:06X} (+${off:05X}): ${val:08X} → ${new_val:08X}")
            patches += 1
            return True
        return False

    # === 1. Initial PC at file offset $4 ===
    print("=== Reset vector (initial PC) ===")
    patch32(4, 0x400000, 0x800000)

    # === 2. Scan for instruction patterns with absolute addresses ===
    # These instructions have a 4-byte absolute address after the opcode word:
    #   JMP xxx.L    = $4EF9 + addr32
    #   JSR xxx.L    = $4EB9 + addr32
    #   LEA xxx.L,An = $41F9/$43F9/$45F9/$47F9/$49F9/$4BF9/$4DF9 + addr32
    #   PEA xxx.L    = $4879 + addr32
    #   MOVE.L #imm  = various (harder to detect)

    abs_opcodes = {
        0x4EF9: 'JMP',
        0x4EB9: 'JSR',
        0x41F9: 'LEA.A0', 0x43F9: 'LEA.A1', 0x45F9: 'LEA.A2',
        0x47F9: 'LEA.A3', 0x49F9: 'LEA.A4', 0x4BF9: 'LEA.A5',
        0x4DF9: 'LEA.A6',
        0x4879: 'PEA',
    }

    # Also MOVE.L #imm,xxx patterns where the immediate is a ROM/SCSI address:
    #   MOVE.L #imm,abs.W  = $21FC + imm32 + abs16
    #   MOVE.L #imm,abs.L  = $23FC + imm32 + abs32
    #   MOVE.L #imm,Dn     = $20BC-$2EBC (step 0x200) NO — that's MOVE.L abs,Dn
    # Actually: MOVE.L #imm,Dn = $203C/$223C/$243C/$263C/$283C/$2A3C/$2C3C/$2E3C
    #          MOVEA.L #imm,An = $207C/$227C/$247C/$267C/$287C/$2A7C/$2C7C/$2E7C

    moveq_imm = {}
    for dn in range(8):
        moveq_imm[0x203C + dn * 0x200] = f'MOVE.L#,D{dn}'
        moveq_imm[0x207C + dn * 0x200] = f'MOVEA.L#,A{dn}'

    # MOVE.L #imm to memory
    moveq_imm[0x21FC] = 'MOVE.L#,abs.W'
    moveq_imm[0x23FC] = 'MOVE.L#,abs.L'

    # CMPI.L #imm patterns (less likely to be addresses, but scan anyway)

    print("\n=== Absolute address instructions (ROM $4xxxxx → $8xxxxx, $3Fxxxx → $7Fxxxx) ===")
    for off in range(0, half - 5, 2):
        opcode = struct.unpack_from('>H', rom, off)[0]

        if opcode in abs_opcodes:
            # Next 4 bytes are the absolute address
            if patch32(off + 2, 0x400000, 0x800000):
                log[-1] = f"  {abs_opcodes[opcode]:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x880000):
                log[-1] = f"  {abs_opcodes[opcode]:10s} " + log[-1].split(': ', 1)[1]
            # Video/sound buffer: $3F0000-$3FFFFF → $7F0000-$7FFFFF
            if patch32(off + 2, 0x3F0000, 0x7F0000):
                log[-1] = f"  {abs_opcodes[opcode]:10s} " + log[-1].split(': ', 1)[1]

        elif opcode in moveq_imm:
            # Next 4 bytes are the immediate value
            if patch32(off + 2, 0x400000, 0x800000):
                log[-1] = f"  {moveq_imm[opcode]:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x880000):
                log[-1] = f"  {moveq_imm[opcode]:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x3F0000, 0x7F0000):
                log[-1] = f"  {moveq_imm[opcode]:10s} " + log[-1].split(': ', 1)[1]

    for l in log:
        print(l)

    # === 3. Exception vector data table at $19E8 (64 longwords) ===
    print("\n=== Exception vector table at $19E8 ===")
    for i in range(64):
        off = 0x19E8 + i * 4
        if patch32(off, 0x400000, 0x800000):
            pass  # logged by patch32
    for l in log[len(log)-63:]:
        print(l)
    log.clear()

    # === 4. Brute-force: patch ALL remaining $00400000 in main code section ===
    # The ROM stores ROMBase ($400000) as data constants in various places.
    # These aren't caught by instruction-pattern matching.
    print("\n=== Remaining $00400000 values (data constants) ===")
    for i in range(0, min(half, 0x1B000), 4):  # main code section only
        val = struct.unpack_from('>I', rom, i)[0]
        if val == 0x00400000:
            struct.pack_into('>I', rom, i, 0x00800000)
            patches += 1
            log.append(f"  ${i+0x800000:06X} (+${i:05X}): $00400000 → $00800000 (data const)")
    for l in log:
        print(l)
    log.clear()

    # === 5. Force MemTop = 8MB at end of sizing routine ===
    # At $401CFE, MOVEA.L SP,A6 copies the sizing result (4MB) to A6 (MemTop).
    # Replace with MOVEA.L #$800000,A6 — all BBU/VIA config runs normally
    # (so the BBU knows the physical 4MB layout), but the system sees 8MB.
    # The MOVEA.W #$400,SP that followed is sacrificed, but the vector copy
    # at $401D04 doesn't use the stack, and $800048 sets up its own SP.
    print("\n=== Memory sizing: force MemTop = 8MB ===")
    sizing_patch = bytes([
        0x2C, 0x7C, 0x00, 0x80, 0x00, 0x00,  # MOVEA.L #$800000, A6
    ])
    off = 0x1CFE
    old_bytes = bytes(rom[off:off+len(sizing_patch)])
    rom[off:off+len(sizing_patch)] = sizing_patch
    patches += 1
    print(f"  ${off+0x800000:06X} (+${off:05X}): {old_bytes.hex()} → {sizing_patch.hex()}")

    print(f"\n=== Total patches: {patches} ===")

    # Fix checksum: ROM uses 32-bit sum of all 16-bit words from offset 4 to end,
    # stored at offset 0.  XOR of stored value with computed sum must be 0.
    checksum = 0
    for i in range(4, half, 2):
        checksum = (checksum + struct.unpack_from('>H', rom, i)[0]) & 0xFFFFFFFF
    struct.pack_into('>I', rom, 0, checksum)
    print(f"Checksum updated to ${checksum:08X}")

    # Write patched ROM (mirror it back to 512KB)
    out = rom + rom
    open(outfile, 'wb').write(out)
    print(f"Written to {outfile}")

if __name__ == '__main__':
    infile = sys.argv[1] if len(sys.argv) > 1 else 'se-rom.bin'
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'se-rom-big.bin'
    patch_rom(infile, outfile)
