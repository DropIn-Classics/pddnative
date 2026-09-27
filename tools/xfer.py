#!/usr/bin/env python3
"""Carry hints from one program to a sibling built from the same source.

    xfer.py FROM.hints TO.hints [--segments TO-SEGMENT-LINES] [-o OUT]

PD.EXE and PD2.EXE share most of their code at other addresses.  This
aligns the two instruction streams (the analysed one of FROM, a linear
decoding of TO's code segment), matching instructions by their shape
(mnemonic, registers, kinds of operands).  Matched pairs give the address
map for code; the memory operands and immediates of matched pairs vote
for the map of every other segment.  Each hint of FROM is then written
for TO with its addresses mapped, or commented out with the reason.

TO.hints must exist with at least exe and segment lines; its other lines
are kept and the carried ones appended after a marker (replacing any
earlier carried block)."""
import argparse, difflib, os, re, struct, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm
import capstone
from capstone import x86

MARK = '; ==== carried over from '


def shape(ci):
    """what must be equal for two instructions to match"""
    parts = [ci.mnemonic]
    for op in ci.operands:
        if op.type == x86.X86_OP_REG:
            parts.append(ci.reg_name(op.reg))
        elif op.type == x86.X86_OP_IMM:
            v = op.imm & 0xFFFF
            parts.append('i' + (str(v) if v < 0x100 else 'W'))
        elif op.type == x86.X86_OP_MEM:
            m = op.mem
            parts.append('m%d%s%s%s%s' % (op.size, ci.reg_name(m.base) if m.base else '',
                                          ci.reg_name(m.index) if m.index else '',
                                          ci.reg_name(m.segment) if m.segment else '',
                                          str(m.disp) if ci.disp_size == 1 else ('W' if ci.disp_size else '')))
    return ' '.join(parts)


def linear(an, seg='CODE'):
    """(offset, capstone insn) for the whole segment, decoded in a line;
    instructions the analysis knows are taken from it"""
    S = an.byname[seg]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.detail = True
    out = []
    o = 0
    img = an.p.img
    while o < S.size:
        ins = an.insns.get((seg, o))
        if ins:
            out.append((o, ins.ci))
            o += ins.size
            continue
        ci = next(md.disasm(bytes(img[S.base + o:S.base + o + 16]), o), None)
        if ci is None:
            o += 1
            continue
        out.append((o, ci))
        o += ci.size
    return out


def build_maps(a, b):
    la, lb = linear(a), linear(b)
    ta, tb = [shape(c) for _, c in la], [shape(c) for _, c in lb]
    sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
    code = {}
    votes = defaultdict(Counter)       # (seg, off) in A -> Counter((seg, off) in B)
    matched = 0
    for i, j, n in sm.get_matching_blocks():
        if n < 3:
            continue
        for k in range(n):
            oa, ca = la[i + k]
            ob, cb = lb[j + k]
            code[oa] = ob
            matched += 1
            # operands with 16-bit values: displacements and immediates
            for x, y, what in ((ca, cb, 'disp'), (ca, cb, 'imm')):
                if what == 'disp' and x.disp_size == 2 and y.disp_size == 2:
                    va = int.from_bytes(x.bytes[x.disp_offset:x.disp_offset + 2], 'little')
                    vb = int.from_bytes(y.bytes[y.disp_offset:y.disp_offset + 2], 'little')
                elif what == 'imm' and x.imm_size == 2 and y.imm_size == 2:
                    va = int.from_bytes(x.bytes[x.imm_offset:x.imm_offset + 2], 'little')
                    vb = int.from_bytes(y.bytes[y.imm_offset:y.imm_offset + 2], 'little')
                else:
                    continue
                insa = a.insns.get(('CODE', oa))
                if insa is None or what not in insa.refs:
                    continue
                ref = insa.refs[what]
                if isinstance(ref, str) or ref[0] == 'CODE':
                    continue
                votes[ref][(ref[0], (ref[1] + (vb - va)) & 0xFFFF)] += 1
    data = {}
    for ref, c in votes.items():
        (t, v), n = c.most_common(1)[0]
        data[ref] = (t, v)
    return code, data, matched, len(la), len(lb)


def map_data(ref, data, near=0x40):
    """a data address: exact vote, else shifted like the nearest voted one
    below it (within `near` bytes)"""
    if ref in data:
        return data[ref]
    s, o = ref
    best = None
    for (s2, o2), (t, v) in data.items():
        if s2 == s and 0 <= o - o2 <= near and (best is None or o2 > best[0]):
            best = (o2, v)
    if best:
        return s, (best[1] + o - best[0]) & 0xFFFF
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    args = ap.parse_args()
    a, _ = disasm.generate(args.src)
    # the target, analysed with its own lines only (none carried yet)
    text = open(args.dst, encoding='utf-8').read()
    own = text.split(MARK)[0].rstrip('\n') + '\n'
    tmp = args.dst + '.tmp'
    open(tmp, 'w', encoding='utf-8').write(own)
    try:
        b, _ = disasm.generate(tmp)
    finally:
        os.remove(tmp)
    code, data, matched, na, nb = build_maps(a, b)
    print(f'{matched} of {na} / {nb} instructions matched, {len(data)} data addresses mapped',
          file=sys.stderr)
    # a code address carries over only if the instructions from there on
    # look the same; else it goes where that look is found once
    la, lb = linear(a), linear(b)
    ia = {o: i for i, (o, _) in enumerate(la)}
    ib = {o: i for i, (o, _) in enumerate(lb)}
    sa = [shape(c) for _, c in la]
    sb = [shape(c) for _, c in lb]
    K = 10

    def run_a(o):
        i = ia.get(o)
        return sa[i:i + K] if i is not None else None

    def check_code(o):
        want = run_a(o)
        if want is None:
            return None
        t = code.get(o)
        if t is not None and ib.get(t) is not None and sb[ib[t]:ib[t] + K] == want:
            return t
        hits = [lb[j][0] for j in range(len(sb) - K) if sb[j:j + K] == want]
        return hits[0] if len(hits) == 1 else None

    cache = {}

    def code_at(o):
        if o not in cache:
            cache[o] = check_code(o)
        return cache[o]

    def conv(tok):
        s, o = tok.split(':')
        if '-' in o:
            x, y = (int(v, 16) for v in o.split('-'))
            if s == 'CODE' and code_at(x) is not None and y in code:
                return f'{s}:{code_at(x):04X}-{code[y]:04X}'
            return None
        o = int(o, 16)
        if s == 'CODE':
            t = code_at(o)
            if t is None and o in code and o not in ia:
                t = code[o]           # inside an instruction: the plain map
            return f'CODE:{t:04X}' if t is not None else None
        r = map_data((s, o), data)
        return f'{r[0]}:{r[1]:04X}' if r else None

    out = [MARK + os.path.basename(args.src) + ' by tools/xfer.py; check, then keep or edit']
    skip = ('exe', 'segment', 'relocorder')
    n_ok = n_bad = 0
    for line in open(args.src, encoding='utf-8'):
        line = line.rstrip('\n')
        f = line.split()
        if not f or f[0].startswith(';') or f[0] in skip:
            if f and f[0].startswith(';'):
                out.append(line)
            continue
        new = []
        ok = True
        for i, t in enumerate(f):
            if i > 0 and re.fullmatch(r'[A-Z]+:[0-9A-F]+(-[0-9A-F]+)?', t) and t.split(':')[0] in a.byname:
                c = conv(t)
                if c is None:
                    ok = False
                    break
                new.append(c)
            else:
                new.append(t)
        if f[0] == 'words' and ok:
            # the table's contents must be code there as well; the count stays
            pass
        if ok:
            out.append(' '.join(new[:1]) + ' ' + ' '.join(new[1:]) if len(new) > 1 else new[0])
            n_ok += 1
        else:
            out.append('; (not mapped) ' + line)
            n_bad += 1
    open(args.dst, 'w', encoding='utf-8').write(own + '\n' + '\n'.join(out) + '\n')
    print(f'{n_ok} hints carried, {n_bad} not mapped -> {args.dst}', file=sys.stderr)


if __name__ == '__main__':
    main()
