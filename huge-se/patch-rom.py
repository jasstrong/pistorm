#!/usr/bin/env python3
"""
Huge SE: Patch SE ROM for 32-bit mode with 32MB RAM.

All hardware addresses get a $40000000 prefix (Mac II convention):
  ROM:         $400000 → $40800000
  SCSI/IWM:    $580000 → $40580000
  VIA:         $EFE000 → $40EFE000
  SCC:         $900000 → $40900000
  Video/sound: $3F0000 → $01FF0000 (top of 32MB RAM, no $40 prefix)
  MemTop:      $02000000 (32MB)

Strategy: find 68000 instruction patterns that contain absolute addresses
and relocate them.  The $40 prefix puts I/O above the 32MB RAM space.
The slow-path 24-bit mask in the emulator strips the $40 before hitting
the SE bus, so all hardware access works transparently.
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

    # === 1. Initial PC at file offset $4 ===
    print("=== Reset vector (initial PC) ===")
    patch32(4, 0x400000, 0x40800000)

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
            # ROM: $400000 → $40800000
            if patch32(off + 2, 0x400000, 0x40800000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # SCSI/IWM: $580000-$5FFFFF → $40580000-$405FFFFF
            if patch32(off + 2, 0x580000, 0x40580000, size=0x80000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # IWM VPA path: $DFE000-$DFFFFF → $40DFE000-$40DFFFFF
            if patch32(off + 2, 0xDFE000, 0x40DFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # VIA: $EFE000-$EFFFFF → $40EFE000-$40EFFFFF
            if patch32(off + 2, 0xEFE000, 0x40EFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # SCC: $900000-$BFFFFF → $40900000-$40BFFFFF
            if patch32(off + 2, 0x900000, 0x40900000, size=0x300000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            # Video/sound buffer: $3F0000-$3FFFFF → $01FF0000-$01FFFFFF
            if patch32(off + 2, 0x3F0000, 0x01FF0000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]

        elif opcode in moveq_imm:
            name = moveq_imm[opcode]
            if patch32(off + 2, 0x400000, 0x40800000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x580000, 0x40580000, size=0x80000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0xDFE000, 0x40DFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0xEFE000, 0x40EFE000, size=0x2000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x900000, 0x40900000, size=0x300000):
                log[-1] = f"  {name:10s} " + log[-1].split(': ', 1)[1]
            if patch32(off + 2, 0x3F0000, 0x01FF0000):
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
    print("\n=== Remaining $00400000 values (data constants) ===")
    for i in range(0, min(half, 0x1B000), 4):
        val = struct.unpack_from('>I', rom, i)[0]
        if val == 0x00400000:
            struct.pack_into('>I', rom, i, 0x40800000)
            patches += 1
            log.append(f"  ${i+0x40800000:08X} (+${i:05X}): $00400000 → $40800000 (data const)")
    for l in log:
        print(l)
    log.clear()

    # === 5. Skip RAM sizing, force MemTop = 32MB ===
    # The sizing routine at $1C00-$1CFC probes RAM and runs tests.
    # With 32MB WTC RAM, the probes find "RAM" at ROM addresses and
    # fail. Skip from $1C60 (after VIA/SCC init) to $1CFE (MemTop).
    print("\n=== Skip RAM sizing, force MemTop = 32MB ===")

    # At $1C60 (after VIA/SCC init): set MemTop, SP, jump to vector copy.
    # This replaces the entire sizing routine ($1C60-$1D03).
    #   MOVEA.L #$02000000, A6    ; MemTop = 32MB
    #   MOVEA.W #$0400, SP        ; stack pointer (same as original $1D00)
    #   BRA.W   $1D04             ; skip to exception vector copy
    # Replicate the state the sizing routine leaves after a successful run:
    #   D6 = 0 (no error)
    #   D7.W = 0 (test number cleared; high bits kept from VIA probe)
    #   A6 = MemTop (RAM size)
    #   SP = $400 (trap dispatch table base)
    skip_patch = bytes([
        0x7C, 0x00,                          # MOVEQ #0, D6
        0x42, 0x47,                          # CLR.W D7
        0x2C, 0x7C, 0x02, 0x00, 0x00, 0x00, # MOVEA.L #$2000000, A6
        0x3E, 0x7C, 0x04, 0x00,             # MOVEA.W #$400, SP
        0x60, 0x00, 0x00, 0x94,             # BRA.W $1D04 (offset = $1D04 - $1C70 = $94)
    ])
    off = 0x1C60
    old_bytes = bytes(rom[off:off+len(skip_patch)])
    rom[off:off+len(skip_patch)] = skip_patch
    patches += 1
    print(f"  ${off+0x40800000:08X} (+${off:05X}): {old_bytes.hex()} → {skip_patch.hex()}")
    print(f"    (MOVEA.L #$2000000,A6; MOVEA.W #$400,SP; BRA.W $1D04)")

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
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'se-rom-huge.bin'
    patch_rom(infile, outfile)
