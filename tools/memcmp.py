#!/usr/bin/env python3
"""Compare the memory of the C implementation (port/) with the original's
in tools/run, both stopped at the same point, by the names of the hints.

    memcmp.py HINTS RAM_A RAM_B [--vram VRAM_A VRAM_B] [--load SEG] [--max N]

RAM_A/RAM_B: memory 0-A0000h (pddrun -ram FILE, the port's PD_RAM).
The program's segments are compared (CODE's frame at --load, default 0077h,
where both put PD.EXE and PD2.EXE); each run of differing bytes is printed
with the name at or before it ("hit_rects+4").  VRAM_A/VRAM_B: the 256 KB
of video memory (byte 4 * offset + plane), compared by region.
"""
import argparse, bisect, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from disasm import Hints

VRAM_REGIONS = [(0x0000, 0x0780, 'display (split screen)'),
                (0x0780, 0xA780, 'table picture'),
                (0xA780, 0xBB10, 'flipper shapes, backgrounds'),
                (0xBB10, 0x10000, 'draw_sprites area buffer, rest')]


def runs(a, b, base, length):
    """(start, end) of the runs of differing bytes in a[base:base+length]"""
    out, start = [], None
    for i in range(length):
        d = a[base + i] != b[base + i]
        if d and start is None:
            start = i
        elif not d and start is not None:
            out.append((start, i))
            start = None
    if start is not None:
        out.append((start, length))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hints')
    ap.add_argument('ram_a')
    ap.add_argument('ram_b')
    ap.add_argument('--vram', nargs=2)
    ap.add_argument('--load', default='0077')
    ap.add_argument('--max', type=int, default=60)
    args = ap.parse_args()
    h = Hints(args.hints)
    a, b = open(args.ram_a, 'rb').read(), open(args.ram_b, 'rb').read()
    load = int(args.load, 16)
    segs = h.segs
    total = 0
    for k, s in enumerate(segs):
        if s.size is not None:
            size = s.size
        elif k + 1 < len(segs):
            size = (segs[k + 1].frame - s.frame) * 16
        else:
            size = 0x10000
        names = sorted((off, n) for (sg, off), n in h.names.items() if sg == s.name)
        offs = [o for o, _ in names]
        base = (load + s.frame) * 16
        rs = runs(a, b, base, min(size, len(a) - base))
        n = sum(e - st for st, e in rs)
        total += n
        print(f'{s.name:6} {n:6} bytes differ in {len(rs)} runs')
        for st, e in rs[:args.max]:
            i = bisect.bisect_right(offs, st) - 1
            where = f'{names[i][1]}+{st - offs[i]:X}' if i >= 0 else '-'
            sa = a[base + st:base + min(e, st + 8)].hex(' ')
            sb = b[base + st:base + min(e, st + 8)].hex(' ')
            print(f'   {s.name}:{st:04X}..{e - 1:04X} {where:28} {sa:24} | {sb}')
        if len(rs) > args.max:
            print(f'   ... {len(rs) - args.max} more')
    if args.vram:
        va, vb = (open(p, 'rb').read() for p in args.vram)
        for lo, hi, what in VRAM_REGIONS:
            n = sum(1 for o in range(lo * 4, hi * 4) if va[o] != vb[o])
            total += n
            print(f'vram {lo:04X}..{hi - 1:04X} {what:30} {n:6} bytes differ')
    sys.exit(1 if total else 0)


if __name__ == '__main__':
    main()
