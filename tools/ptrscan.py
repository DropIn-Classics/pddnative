#!/usr/bin/env python3
"""Find immediates that look like addresses but are written as numbers.

    ptrscan.py HINTS [--hints]

A MOV r16,imm whose register then serves as a base or index register (or
is compared, or stored), before anything else is loaded into it, most
likely holds an offset.  The segment is the one the memory operand goes
through (DS/ES as the analysis tracks them).  With --hints the result is
printed as ptr/dptr lines to paste into the hints file after a look."""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm
from capstone import x86

ap = argparse.ArgumentParser()
ap.add_argument('hints')
ap.add_argument('--hints', dest='as_hints', action='store_true')
a = ap.parse_args()
an, em = disasm.generate(a.hints)
keys = sorted(an.insns)
index = {k: i for i, k in enumerate(keys)}


def regs_written(ci):
    try:
        return {ci.reg_name(r).upper() for r in ci.regs_access()[1]}
    except Exception:
        return set()


found = []
for i, k in enumerate(keys):
    ins = an.insns[k]
    ci = ins.ci
    if ci.mnemonic != 'mov' or 'imm' in ins.refs or ci.imm_size != 2:
        continue
    d, s = ci.operands
    if d.type != x86.X86_OP_REG or s.type != x86.X86_OP_IMM:
        continue
    r = ci.reg_name(d.reg).upper()
    if r in ('AX', 'CX', 'DX') and False:
        continue
    v = s.imm & 0xFFFF
    if v < 0x100:
        continue
    # follow the straight line of instructions after it
    for j in range(i + 1, min(i + 12, len(keys))):
        k2 = keys[j]
        if k2[0] != k[0]:
            break
        c2 = an.insns[k2].ci
        used = None
        for op in c2.operands:
            if op.type == x86.X86_OP_MEM:
                regs = {c2.reg_name(x).upper() for x in (op.mem.base, op.mem.index) if x}
                if r in regs:
                    used = an.mem_seg(an.insns[k2], op)
        if used:
            S = an.byname.get(used)
            if S and v < S.size:
                found.append((k, r, v, used, f'{k2[1]:04X} {c2.mnemonic} {c2.op_str}'))
            break
        if r in regs_written(c2) or c2.mnemonic in ('ret', 'jmp', 'call', 'retf', 'iret'):
            break

for (s, o), r, v, seg, why in found:
    if a.as_hints:
        kind = 'ptr' if seg == 'CODE' and (seg, v) in an.insns else 'dptr'
        print(f'{kind} {s}:{o:04X} {seg}\t; {r} = {v:04X}, used at {why}')
    else:
        print(f'{s}:{o:04X} MOV {r},{v:04X} -> {seg}   (used at {why})')
print(f'; {len(found)} candidates', file=sys.stderr)
