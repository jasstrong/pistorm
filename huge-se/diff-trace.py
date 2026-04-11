#!/usr/bin/env python3
"""Compare two binary PC traces and find divergence points.

Usage: diff-trace.py <big-se.trace> <huge-se.trace> [rom_base_a] [rom_base_b]

Each trace file is a sequence of raw 32-bit little-endian PC values.
Normalizes ROM PCs to offsets, then finds all divergence points,
re-syncing after insertions/deletions.
"""
import struct
import sys

def load_trace(path):
    data = open(path, 'rb').read()
    n = len(data) // 4
    return struct.unpack(f'<{n}I', data[:n*4])

def normalize(pc, base):
    """Convert ROM PC to offset, leave non-ROM PCs as-is."""
    off = pc - base
    if 0 <= off < 0x80000:
        return ('ROM', off)
    return ('ABS', pc)

def fmt_pc(pc, base):
    off = pc - base
    if 0 <= off < 0x80000:
        return f"ROM+${off:05X}"
    return f"${pc:08X}"

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    path_a, path_b = sys.argv[1], sys.argv[2]
    base_a = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x800000
    base_b = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x40800000

    print(f"Loading {path_a}...")
    raw_a = load_trace(path_a)
    print(f"  {len(raw_a)} instructions")

    print(f"Loading {path_b}...")
    raw_b = load_trace(path_b)
    print(f"  {len(raw_b)} instructions")

    # Normalize both traces
    norm_a = [normalize(pc, base_a) for pc in raw_a]
    norm_b = [normalize(pc, base_b) for pc in raw_b]

    # Walk both traces, re-syncing after divergences
    ia, ib = 0, 0
    divergences = []
    LOOKAHEAD = 200  # how far to search for re-sync

    while ia < len(norm_a) and ib < len(norm_b):
        if norm_a[ia] == norm_b[ib]:
            ia += 1
            ib += 1
            continue

        # Divergence found — try to re-sync
        div_ia, div_ib = ia, ib
        synced = False

        # Try: B has extra instructions (search ahead in B for norm_a[ia])
        for skip in range(1, LOOKAHEAD):
            if ib + skip < len(norm_b) and norm_a[ia] == norm_b[ib + skip]:
                divergences.append({
                    'type': 'B_extra',
                    'pos_a': ia, 'pos_b': ib,
                    'extra_b': list(range(ib, ib + skip)),
                })
                ib += skip
                synced = True
                break

        if not synced:
            # Try: A has extra instructions
            for skip in range(1, LOOKAHEAD):
                if ia + skip < len(norm_a) and norm_a[ia + skip] == norm_b[ib]:
                    divergences.append({
                        'type': 'A_extra',
                        'pos_a': ia, 'pos_b': ib,
                        'extra_a': list(range(ia, ia + skip)),
                    })
                    ia += skip
                    synced = True
                    break

        if not synced:
            # True divergence — paths split permanently (or need larger lookahead)
            # Try larger lookahead with both sides moving
            found = False
            for window in range(1, 2000):
                for da in range(min(window + 1, len(norm_a) - ia)):
                    db = window - da
                    if db < 0 or ib + db >= len(norm_b):
                        continue
                    if ia + da < len(norm_a) and norm_a[ia + da] == norm_b[ib + db]:
                        if da > 0:
                            divergences.append({
                                'type': 'A_extra',
                                'pos_a': ia, 'pos_b': ib,
                                'extra_a': list(range(ia, ia + da)),
                            })
                        if db > 0:
                            divergences.append({
                                'type': 'B_extra',
                                'pos_a': ia + da, 'pos_b': ib,
                                'extra_b': list(range(ib, ib + db)),
                            })
                        ia += da
                        ib += db
                        found = True
                        break
                if found:
                    break

            if not found:
                divergences.append({
                    'type': 'permanent',
                    'pos_a': ia, 'pos_b': ib,
                })
                break

    # Report
    print(f"\n=== Found {len(divergences)} divergence(s) ===\n")

    for idx, d in enumerate(divergences):
        if d['type'] == 'B_extra':
            extras = d['extra_b']
            print(f"--- Divergence #{idx+1}: huge-se has {len(extras)} extra instructions at big-se #{d['pos_a']}, huge-se #{d['pos_b']} ---")
            for i in extras[:30]:
                print(f"  huge-se #{i}: {fmt_pc(raw_b[i], base_b)}")
            if len(extras) > 30:
                print(f"  ... and {len(extras)-30} more")

        elif d['type'] == 'A_extra':
            extras = d['extra_a']
            print(f"--- Divergence #{idx+1}: big-se has {len(extras)} extra instructions at big-se #{d['pos_a']}, huge-se #{d['pos_b']} ---")
            for i in extras[:30]:
                print(f"  big-se #{i}: {fmt_pc(raw_a[i], base_a)}")
            if len(extras) > 30:
                print(f"  ... and {len(extras)-30} more")

        elif d['type'] == 'permanent':
            print(f"--- Divergence #{idx+1}: PERMANENT split at big-se #{d['pos_a']}, huge-se #{d['pos_b']} ---")
            print(f"  Context before (big-se):")
            for i in range(max(0, d['pos_a']-5), d['pos_a']):
                print(f"    #{i}: {fmt_pc(raw_a[i], base_a)}")
            print(f"  big-se continues:")
            seen = set()
            for i in range(d['pos_a'], min(len(raw_a), d['pos_a']+200)):
                npc = normalize(raw_a[i], base_a)
                if npc not in seen:
                    seen.add(npc)
                    print(f"    #{i}: {fmt_pc(raw_a[i], base_a)}")
                    if len(seen) >= 40: break
            print(f"  huge-se continues:")
            seen = set()
            for i in range(d['pos_b'], min(len(raw_b), d['pos_b']+200)):
                npc = normalize(raw_b[i], base_b)
                if npc not in seen:
                    seen.add(npc)
                    print(f"    #{i}: {fmt_pc(raw_b[i], base_b)}")
                    if len(seen) >= 40: break

        print()

    if divergences and divergences[-1]['type'] != 'permanent':
        print(f"Traces re-synced after all divergences.")
        print(f"  big-se: consumed {ia}/{len(raw_a)} instructions")
        print(f"  huge-se: consumed {ib}/{len(raw_b)} instructions")

if __name__ == '__main__':
    main()
