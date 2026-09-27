#!/usr/bin/env python3
"""List what of the code segment the analysis has not reached.

    gaps.py HINTS [--all]

Each gap is shown with its first instructions (linear decoding), to decide
whether it is code only reached through a pointer or data."""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm, capstone

ap = argparse.ArgumentParser()
ap.add_argument('hints')
ap.add_argument('-n', type=int, default=4, help='instructions shown per gap')
ap.add_argument('--seg', default='CODE')
a = ap.parse_args()
an, em = disasm.generate(a.hints)
S = an.byname[a.seg]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
g = disasm.gaps(an, a.seg)
tot = sum(e - s for s, e in g)
print(f'{len(g)} gaps, {tot} of {S.size} bytes not reached as code')
for s, e in g:
    b = bytes(an.p.img[S.base + s:S.base + e])
    lab = an.labels.get((a.seg, s), '')
    zero = ' (zeros)' if not any(b) else ''
    print(f'{s:04X}-{e:04X} {e - s:5d} {lab}{zero}')
    if zero:
        continue
    for i, ins in enumerate(md.disasm(b, s)):
        if i >= a.n:
            break
        print(f'        {ins.address:04X} {ins.bytes.hex():14s} {ins.mnemonic} {ins.op_str}')

# where each gap's start appears as a word: immediates of reached
# instructions, and data
print('\nwhere gap starts appear as words:')
imm_at = {}
for (s, o), ins in an.insns.items():
    ci = ins.ci
    if ci.imm_size == 2:
        v = int.from_bytes(ci.bytes[ci.imm_offset:ci.imm_offset + 2], 'little')
        imm_at.setdefault(v, []).append(f'{s}:{o:04X} {ci.mnemonic} {ci.op_str}')
    if ci.disp_size == 2:
        pass
img = an.p.img
for s, e in g:
    b = bytes(img[S.base + s:S.base + e])
    if not any(b):
        continue
    for k in range(s, e):
        if k != s and (a.seg, k) not in an.labels:
            continue
        hits = list(imm_at.get(k, []))
        w = k.to_bytes(2, 'little')
        p = img.find(w)
        while p >= 0 and len(hits) < 8:
            T = an.seg_at(p)
            if T and T.cls != 'CODE':
                hits.append(f'{T.name}:{p - T.base:04X} (data)')
            p = img.find(w, p + 1)
        if hits:
            print(f'  {k:04X}: ' + '; '.join(hits[:8]))
