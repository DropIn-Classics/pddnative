#!/usr/bin/env python3
"""Turn a shipped program back into assembly source that tasm.py rebuilds
to the same bytes.

    disasm.py HINTS [-o OUT.ASM]

HINTS names the program (relative to game/) and says what the analysis
cannot find out by itself: the segments, entry points the code reaches
only through pointers, tables, names and comments.  It holds no bytes of
the game, so the source is made from the player's own copy each time.

The analysis:
  * code: recursive descent from the entry points, following jumps and
    calls.  Along each path it tracks what DS and ES hold (a segment of
    the program or an unknown value), so that a direct address can be
    written as a label of the segment it really reads;
  * relocations: every segment value in the program is a relocation site,
    written as the segment's name (MOV AX,DATA; DD far pointers);
  * data: the bytes nothing decoded as code, as DB lines (text as
    strings), cut at every label.

Hints syntax (one per line, ';' starts a comment, numbers are hex):
    exe        DREAMS1/PD.EXE
    segment    NAME FRAME CLASS [stack] [size=N]   in image order
    code       SEG:OFF [NAME]          an entry point
    coderange  SEG:OFF-END             code throughout, a routine after every
                                       RET/JMP (handlers reached by pointers)
    words      SEG:OFF COUNT TARGETSEG a table of near pointers into TARGETSEG
                                       (TARGETSEG CODE also seeds code)
    name       SEG:OFF NAME            a label's name
    ptr        SEG:OFF TARGETSEG       the immediate of the instruction at
                                       SEG:OFF is an offset in TARGETSEG
    dptr       SEG:OFF TARGETSEG       the same for an offset of data (no code
                                       is looked for there)
    var        SEG:OFF SEG|num         what a word variable holds (offsets in
                                       SEG, or numbers); the analysis finds
                                       most pointer variables by itself
    num        SEG:OFF                 the instruction's address operand
                                       stays a number
    ds         SEG:OFF-END DSSEG       what DS holds in that code range
    comment    SEG:OFF TEXT            a comment line before the address
    raw        SEG:OFF                 write the instruction as DB (the
                                       assembler would pick other bytes)
    keeptail                           the bytes after the program image
                                       (debug information) are copied from
                                       the original, not made
    relocorder SEG SEG...              the order of the relocation table:
                                       by the segment holding the site
"""
import argparse, os, re, struct, sys
import capstone
from capstone import x86

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))


def game_dir():
    """Where the unpacked CD is: $PDD_GAME, else game/ of this checkout,
    else game/ of the main checkout (a git worktree has none of its own)."""
    env = os.environ.get('PDD_GAME')
    if env:
        return env
    own = os.path.join(ROOT, 'game')
    if os.path.isdir(own):
        return own
    try:
        import subprocess
        common = subprocess.run(['git', 'rev-parse', '--git-common-dir'], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        main = os.path.normpath(os.path.join(ROOT, common, '..', 'game'))
        if os.path.isdir(main):
            return main
    except OSError:
        pass
    return own

REG = {}


def hexnum(n):
    if n < 10:
        return str(n)
    h = f'{n:X}H'
    return '0' + h if h[0] in 'ABCDEF' else h


# ---------------------------------------------------------------- the program

class Seg:
    def __init__(self, name, frame, cls, stack=False, size=None):
        self.name, self.frame, self.cls, self.stack = name, frame, cls, stack
        self.base = frame * 16
        self.size = size          # set from the next segment when not given
        self.prefix = name[0]


class Program:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.file = d
        (sig, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs,
         lfarlc, ovno) = struct.unpack_from('<2s13H', d)
        assert sig == b'MZ'
        size = (cp - 1) * 512 + (cblp or 512)
        self.hdr = d[:cparhdr * 16]
        self.img = d[cparhdr * 16:size]
        self.minalloc, self.maxalloc = minalloc, maxalloc
        self.ss, self.sp, self.cs, self.ip = ss, sp, cs, ip
        self.relocs = [struct.unpack_from('<HH', d, lfarlc + 4 * i) for i in range(crlc)]
        self.tail = d[size:]                   # after the image (debug information)
        self.relsites = {}                     # image offset -> segment value
        for off, seg in self.relocs:
            a = seg * 16 + off
            self.relsites[a] = struct.unpack_from('<H', self.img, a)[0]


# ---------------------------------------------------------------- hints

class Hints:
    def __init__(self, path):
        self.path = path
        self.exe = None
        self.segs = []
        self.code = []            # (seg, off, name)
        self.coderanges = []      # (seg, start, end)
        self.words = []           # (seg, off, count, target)
        self.names = {}           # (seg, off) -> name
        self.ptr = {}             # (seg, off) -> target seg
        self.dptr = set()         # ptr hints to data
        self.vars = {}            # (seg, off) -> what a word variable holds
        self.num = set()
        self.ds = []              # (seg, start, end, dsseg)
        self.comments = {}        # (seg, off) -> [text]
        self.raw = set()
        self.relocorder = []
        self.keeptail = False
        for n, line in enumerate(open(path, encoding='utf-8'), 1):
            line = line.split(';', 1)[0].strip() if not line.lstrip().startswith('comment') else line.strip()
            if not line:
                continue
            f = line.split()
            k = f[0]
            try:
                if k == 'exe':
                    self.exe = f[1]
                elif k == 'segment':
                    opts = f[4:]
                    size = next((int(o[5:], 16) for o in opts if o.startswith('size=')), None)
                    self.segs.append(Seg(f[1], int(f[2], 16), f[3], 'stack' in opts, size))
                elif k == 'code':
                    s, o = self.addr(f[1])
                    self.code.append((s, o, f[2] if len(f) > 2 else None))
                elif k == 'words':
                    s, o = self.addr(f[1])
                    self.words.append((s, o, int(f[2], 16), f[3]))
                elif k == 'name':
                    self.names[self.addr(f[1])] = f[2]
                elif k == 'ptr':
                    self.ptr[self.addr(f[1])] = f[2]
                elif k == 'dptr':
                    self.ptr[self.addr(f[1])] = f[2]
                    self.dptr.add(self.addr(f[1]))
                elif k == 'var':
                    self.vars[self.addr(f[1])] = f[2]
                elif k == 'num':
                    self.num.add(self.addr(f[1]))
                elif k == 'coderange':
                    sg, rng = f[1].split(':')
                    a_, b_ = rng.split('-')
                    self.coderanges.append((sg, int(a_, 16), int(b_, 16)))
                elif k == 'ds':
                    s, rng = f[1].split(':')
                    a, b = rng.split('-')
                    self.ds.append((s, int(a, 16), int(b, 16), f[2]))
                elif k == 'comment':
                    text = line.split(None, 2)[2] if len(f) > 2 else ''
                    self.comments.setdefault(self.addr(f[1]), []).append(text)
                elif k == 'keeptail':
                    self.keeptail = True
                elif k == 'relocorder':
                    self.relocorder = f[1:]
                elif k == 'raw':
                    self.raw.add(self.addr(f[1]))
                else:
                    raise ValueError(f'unknown hint {k}')
            except (ValueError, IndexError) as e:
                raise SystemExit(f'{path}:{n}: {e}')

    @staticmethod
    def addr(t):
        s, o = t.split(':')
        return s, int(o, 16)


# ---------------------------------------------------------------- analysis

JUMPS = {'jmp', 'je', 'jne', 'jb', 'jae', 'jbe', 'ja', 'jl', 'jge', 'jle', 'jg', 'js', 'jns',
         'jo', 'jno', 'jp', 'jnp', 'jcxz', 'loop', 'loope', 'loopne'}
STOP = {'jmp', 'ret', 'retf', 'iret', 'ljmp'}

SEGREGS = {x86.X86_REG_ES: 'ES', x86.X86_REG_CS: 'CS', x86.X86_REG_SS: 'SS', x86.X86_REG_DS: 'DS'}


class Insn:
    __slots__ = ('seg', 'off', 'size', 'ci', 'ds', 'es', 'refs', 'text', 'raw')

    def __init__(self, seg, off, ci, ds, es):
        self.seg, self.off, self.size, self.ci = seg, off, ci.size, ci
        self.ds, self.es = ds, es
        self.refs = {}            # 'disp'/'imm'/'target' -> (segname, offset) or 'SEG:name'
        self.text = None
        self.raw = False


class Analysis:
    def __init__(self, prog, hints):
        self.p, self.h = prog, hints
        self.segs = hints.segs
        for i, s in enumerate(self.segs):
            if s.size is None:
                nxt = self.segs[i + 1].base if i + 1 < len(self.segs) else len(prog.img)
                s.size = nxt - s.base
        self.byname = {s.name: s for s in self.segs}
        self.byframe = {s.frame: s for s in self.segs}
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
        self.md.detail = True
        self.insns = {}           # (seg, off) -> Insn
        self.labels = {}          # (seg, off) -> name
        self.farptrs = {}         # image offset of offset word -> (seg, off)
        self.warnings = []
        self.dsmap = [(s, a, b, d) for s, a, b, d in hints.ds]

    # ---- helpers
    def seg_at(self, a):
        for s in self.segs:
            if s.base <= a < s.base + s.size:
                return s
        return None

    def byte(self, a):
        return self.p.img[a] if a < len(self.p.img) else 0

    def label(self, seg, off, kind=None):
        key = (seg, off)
        if key not in self.labels:
            name = self.h.names.get(key)
            if name is None:
                s = self.byname[seg]
                code = kind == 'code'
                name = ('L' if code else s.prefix) + f'{off:04X}'
                if code and s.cls != 'CODE':
                    name = 'L' + s.prefix + f'{off:04X}'
            self.labels[key] = name
        return self.labels[key]

    # ---- code
    def run(self):
        work = []
        code = self.byname['CODE']
        e = (self.p.cs, self.p.ip)
        work.append(('CODE', e[1], 'DATA', None))
        self.label('CODE', e[1], 'code')
        for s, o, n in self.h.code:
            work.append((s, o, 'DATA', None))
            self.label(s, o, 'code')
        for s, a_, b_ in self.h.coderanges:
            S = self.byname[s]
            o = a_
            start = True
            while o < b_:
                ci = next(self.md.disasm(bytes(self.p.img[S.base + o:S.base + o + 16]), o), None)
                if ci is None:
                    break
                if start:
                    work.append((s, o, 'DATA', None))
                    self.label(s, o, 'code')
                start = ci.mnemonic in STOP
                o += ci.size
        for s, o, cnt, t in self.h.words:
            base = self.byname[s].base + o
            for i in range(cnt):
                v = struct.unpack_from('<H', self.p.img, base + 2 * i)[0]
                if t == 'CODE':
                    work.append(('CODE', v, 'DATA', None))
                self.label(t, v, 'code' if t == 'CODE' else None)
        while work:
            seg, off, ds, es = work.pop()
            self.trace(seg, off, ds, es, work)
        # pointer variables can lead to more code; repeat until nothing new
        for _ in range(8):
            for seed in self.pointer_vars():
                work.append(seed)
            if not work:
                break
            while work:
                seg, off, ds, es = work.pop()
                self.trace(seg, off, ds, es, work)
        self.far_pointers()

    def pointer_vars(self):
        """Word variables that hold offsets: a variable loaded into a
        register that then addresses memory (or is called) holds offsets
        of that segment, so every constant stored into it is written as an
        offset.  Returns new code seeds."""
        order = sorted(self.insns)
        votes = {}
        for i, k in enumerate(order):
            ins = self.insns[k]
            ci = ins.ci
            ref = ins.refs.get('disp')
            if not ref or not ci.operands:
                continue
            ops = ci.operands
            if ci.mnemonic in ('call', 'jmp') and ops[0].type == x86.X86_OP_MEM \
                    and ops[0].size == 2 and not ops[0].mem.base and not ops[0].mem.index:
                votes.setdefault(ref, set()).add(k[0])
                continue
            if not (ci.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == x86.X86_OP_REG
                    and ops[0].size == 2 and ops[1].type == x86.X86_OP_MEM
                    and not ops[1].mem.base and not ops[1].mem.index):
                continue
            r = ops[0].reg
            for k2 in order[i + 1:i + 16]:
                if k2[0] != k[0]:
                    break
                ins2 = self.insns[k2]
                c2 = ins2.ci
                if c2.mnemonic in ('call', 'jmp') and c2.operands and \
                        c2.operands[0].type == x86.X86_OP_REG and c2.operands[0].reg == r:
                    votes.setdefault(ref, set()).add(k2[0])
                    break
                used = None
                for op in c2.operands:
                    if op.type == x86.X86_OP_MEM and r in (op.mem.base, op.mem.index):
                        used = self.mem_seg(ins2, op)
                        if used is None:
                            used = '?'
                if used:
                    votes.setdefault(ref, set()).add(used)
                    break
                try:
                    written = c2.regs_access()[1]
                except Exception:
                    written = ()
                if r in written or c2.mnemonic in STOP:
                    break
        types = {}
        for var, segs in votes.items():
            if var in self.h.vars:
                continue
            if len(segs) == 1 and '?' not in segs:
                types[var] = next(iter(segs))
        for var, t in self.h.vars.items():
            if t != 'num':
                types[var] = t
        self.ptrvars = types
        seeds = []
        for k in order:
            ins = self.insns[k]
            ci = ins.ci
            ref = ins.refs.get('disp')
            if ref not in types or 'imm' in ins.refs or ci.mnemonic != 'mov' or ci.imm_size != 2:
                continue
            if ci.operands[0].type != x86.X86_OP_MEM:
                continue
            t = types[ref]
            v = int.from_bytes(ci.bytes[ci.imm_offset:ci.imm_offset + 2], 'little')
            T = self.byname[t]
            if v == 0 or v > T.size:
                continue
            ins.refs['imm'] = (t, v)
            if T.cls == 'CODE':
                self.label(t, v, 'code')
                if (t, v) not in self.insns:
                    seeds.append((t, v, 'DATA', None))
            else:
                self.label(t, v)
        return seeds

    def ds_override(self, seg, off):
        for s, a, b, d in self.dsmap:
            if s == seg and a <= off < b:
                return d
        return None

    def trace(self, seg, off, ds, es, work):
        S = self.byname[seg]
        stack = []                 # shadow stack of pushed segment values
        regs = {}                  # 'AX' etc. -> segment name, when known
        while True:
            key = (seg, off)
            if key in self.insns:
                return
            if off >= S.size:
                self.warnings.append(f'{seg}:{off:04X}: code runs off the segment')
                return
            a = S.base + off
            ci = next(self.md.disasm(bytes(self.p.img[a:a + 16]), off), None)
            if ci is None:
                self.warnings.append(f'{seg}:{off:04X}: not an instruction')
                return
            # a segment value the loader relocates can only be an immediate
            rel = [k for k in range(ci.size) if a + k in self.p.relsites]
            if rel and not (ci.imm_size == 2 and rel == [ci.imm_offset]):
                self.warnings.append(f'{seg}:{off:04X}: decoding ran into a relocated word')
                return
            dsh = self.ds_override(seg, off)
            ins = Insn(seg, off, ci, dsh or ds, es)
            self.insns[key] = ins
            self.collect(ins, work)
            m = ci.mnemonic
            # track segment registers
            ops = ci.operands
            if m == 'mov' and len(ops) == 2:
                d, s_ = ops
                if d.type == x86.X86_OP_REG:
                    dn = ci.reg_name(d.reg).upper()
                    val = None
                    if s_.type == x86.X86_OP_IMM:
                        r = self.p.relsites.get(a + ci.imm_offset) if ci.imm_offset else None
                        val = self.byframe[r].name if r is not None and r in self.byframe else None
                    elif s_.type == x86.X86_OP_REG:
                        sn = ci.reg_name(s_.reg).upper()
                        val = {'DS': ds, 'ES': es}.get(sn, regs.get(sn))
                    if dn == 'DS':
                        ds = val
                    elif dn == 'ES':
                        es = val
                    else:
                        regs[dn] = val
            elif m == 'push' and ops and ops[0].type == x86.X86_OP_REG:
                rn = ci.reg_name(ops[0].reg).upper()
                stack.append({'DS': ds, 'ES': es, 'CS': seg}.get(rn, regs.get(rn)))
            elif m == 'pop' and ops and ops[0].type == x86.X86_OP_REG:
                rn = ci.reg_name(ops[0].reg).upper()
                v = stack.pop() if stack else None
                if rn == 'DS':
                    ds = v
                elif rn == 'ES':
                    es = v
                else:
                    regs[rn] = v
            elif m in ('pushaw', 'popaw'):
                regs = {}
            elif m in ('les', 'lds'):
                if m == 'lds':
                    ds = None
                else:
                    es = None
            if m in STOP:
                return
            if m == 'int' and ops[0].imm == 0x20:
                return
            if m == 'int' and ops[0].imm == 0x21 and self.ah_before(seg, off) == 0x4C:
                return
            off += ci.size

    def ah_before(self, seg, off):
        """AH set by the instruction right before (MOV AX,4Cxx / MOV AH,4Ch)"""
        S = self.byname[seg]
        a = S.base + off
        b = self.p.img
        if b[a - 3] == 0xB8:
            return b[a - 1]
        if b[a - 2] == 0xB4:
            return b[a - 1]
        return None

    def mem_seg(self, ins, op):
        """the segment name a memory operand's address belongs to, or None"""
        ci = ins.ci
        sr = op.mem.segment
        if sr == x86.X86_REG_INVALID or sr == 0:
            sr = x86.X86_REG_SS if op.mem.base == x86.X86_REG_BP else x86.X86_REG_DS
        r = SEGREGS.get(sr)
        if r == 'DS':
            return ins.ds
        if r == 'ES':
            return ins.es
        if r == 'CS':
            return ins.seg
        return None

    def collect(self, ins, work):
        ci = ins.ci
        S = self.byname[ins.seg]
        a = S.base + ins.off
        m = ci.mnemonic
        key = (ins.seg, ins.off)
        if m in JUMPS or m == 'call':
            op = ci.operands[0]
            if op.type == x86.X86_OP_IMM:
                t = op.imm & 0xFFFF
                ins.refs['target'] = (ins.seg, t)
                self.label(ins.seg, t, 'code')
                work.append((ins.seg, t, ins.ds if m != 'call' else 'DATA', ins.es if m != 'call' else None))
                return
        if m in ('lcall', 'ljmp') and ci.operands and ci.operands[0].type == x86.X86_OP_IMM:
            o, s = struct.unpack_from('<HH', self.p.img, a + 1)
            T = self.byframe.get(s)
            if T:
                ins.refs['far'] = (T.name, o)
                self.label(T.name, o, 'code')
                work.append((T.name, o, 'DATA', None))
            return
        for op in ci.operands:
            if op.type == x86.X86_OP_MEM and ci.disp_size == 2 and key not in self.h.num:
                has_reg = op.mem.base != 0 or op.mem.index != 0
                sn = self.mem_seg(ins, op)
                if sn is None or sn not in self.byname:
                    continue
                d = op.mem.disp & 0xFFFF
                T = self.byname[sn]
                if has_reg and (d < 0x100 or d >= T.size):
                    continue
                if not has_reg and d > T.size:
                    continue
                ins.refs['disp'] = (sn, d)
                self.label(sn, d)
        if ci.imm_offset and ci.imm_size == 2:
            ia = a + ci.imm_offset
            if ia in self.p.relsites:
                v = self.p.relsites[ia]
                if v in self.byframe:
                    ins.refs['imm'] = 'SEG:' + self.byframe[v].name
                else:
                    self.warnings.append(f'{ins.seg}:{ins.off:04X}: segment value {v:04X} is no segment')
            elif key in self.h.ptr:
                t = self.h.ptr[key]
                v = struct.unpack_from('<H', self.p.img, ia)[0]
                ins.refs['imm'] = (t, v)
                code = t == 'CODE' and key not in self.h.dptr
                self.label(t, v, 'code' if code else None)
                if code:
                    work.append(('CODE', v, 'DATA', None))

    def far_pointers(self):
        """relocated segment words with an offset word before them: DD label"""
        for a, v in self.p.relsites.items():
            T = self.byframe.get(v)
            if T is None:
                continue
            S = self.seg_at(a)
            if (S.name, a - S.base) in self.insns or any(
                    (S.name, a - S.base - k) in self.insns for k in range(1, 6)):
                continue
            o = struct.unpack_from('<H', self.p.img, a - 2)[0]
            if o <= T.size:
                self.farptrs[a - 2] = (T.name, o)
                self.label(T.name, o, 'code' if T.cls == 'CODE' and (T.name, o) in self.insns else None)


# ---------------------------------------------------------------- formatting

SIZEPTR = {1: 'BYTE PTR', 2: 'WORD PTR', 4: 'DWORD PTR', 6: 'FWORD PTR'}
STRINGOPS = {'movsb', 'movsw', 'lodsb', 'lodsw', 'stosb', 'stosw', 'scasb', 'scasw',
             'cmpsb', 'cmpsw', 'insb', 'insw', 'outsb', 'outsw'}
RENAME = {'pushaw': 'PUSHA', 'popaw': 'POPA', 'xlatb': 'XLAT', 'lcall': 'CALL', 'ljmp': 'JMP',
          'pushfw': 'PUSHF', 'popfw': 'POPF', 'iretw': 'IRET', 'int1': 'INT 1',
          'cwde': 'CBW', 'cdq': 'CWD', 'cbw': 'CBW', 'cwd': 'CWD'}
PREFIXES = {0x26: 'ES', 0x2E: 'CS', 0x36: 'SS', 0x3E: 'DS', 0xF2: 'REPNE', 0xF3: 'REP', 0xF0: 'LOCK'}


class Unformattable(Exception):
    pass


class Formatter:
    def __init__(self, an):
        self.an = an

    def sym(self, ref):
        if isinstance(ref, str):
            return ref[4:]
        s, o = ref
        return self.an.labels[(s, o)]

    def prefixes(self, ins):
        b = self.an.p.img[self.an.byname[ins.seg].base + ins.off:]
        out = []
        for x in b[:ins.size]:
            if x in PREFIXES:
                out.append(x)
            else:
                break
        return out

    def memtext(self, ins, op, need_size=True, force_seg=None):
        ci = ins.ci
        m = op.mem
        parts = []
        if m.base:
            parts.append(ci.reg_name(m.base).upper())
        if m.index:
            parts.append(ci.reg_name(m.index).upper())
        seg = ci.reg_name(m.segment).upper() if m.segment else None
        pf = self.prefixes(ins)
        segpf = [PREFIXES[x] for x in pf if x in (0x26, 0x2E, 0x36, 0x3E)]
        if len(segpf) > 1:
            raise Unformattable('two segment prefixes')
        seg = segpf[0] if segpf else None
        disp = m.disp & 0xFFFF
        ref = ins.refs.get('disp')
        if ref:
            parts.append(self.sym(ref))
            dtxt = None
        else:
            dtxt = disp
        if dtxt is not None and (dtxt or not parts) and not (ci.disp_size == 0):
            if parts and ci.disp_size == 1:
                v = m.disp
                parts_s = '+'.join(parts) + (f'+{hexnum(v)}' if v >= 0 else f'-{hexnum(-v)}')
            elif parts:
                parts_s = '+'.join(parts) + f'+{hexnum(dtxt)}'
            else:
                parts_s = hexnum(dtxt)
                if seg is None:
                    seg = 'DS' if m.base != x86.X86_REG_BP else 'SS'
        else:
            parts_s = '+'.join(parts)
        # a symbol alone: a segment override must be written if the prefix is there
        # and differs from what the ASSUMEs give; an extra prefix the assembler would
        # not produce is caught by the byte comparison
        if ref and not isinstance(ref, str) and seg is None:
            want = self.an.mem_seg(ins, op)
            if ref[0] != 'DATA' and want == ref[0] and ins.seg != ref[0]:
                raise Unformattable('label outside DS without override')
        t = f'[{parts_s}]'
        if seg:
            t = f'{seg}:{t}'
        if need_size:
            t = f'{SIZEPTR[op.size]} {t}'
        return t

    def imm(self, ins, op, size):
        ref = ins.refs.get('imm')
        if ref:
            if isinstance(ref, str):
                return ref[4:]
            return 'OFFSET ' + self.sym(ref)
        v = op.imm & ((1 << (8 * size)) - 1)
        return hexnum(v)

    def fmt(self, ins):
        ci = ins.ci
        m = ci.mnemonic
        pf = self.prefixes(ins)
        rep = [PREFIXES[x] for x in pf if x in (0xF2, 0xF3, 0xF0)]
        segpf = [PREFIXES[x] for x in pf if x in (0x26, 0x2E, 0x36, 0x3E)]
        if len(pf) != len(rep) + len(segpf):
            raise Unformattable('prefix')
        base = m.split()[-1]
        ops = ci.operands
        # string instructions
        if base in STRINGOPS:
            r = ''
            if rep:
                if len(rep) > 1:
                    raise Unformattable('rep')
                r = {'REP': 'REPE' if base[:4] in ('scas', 'cmps') else 'REP', 'REPNE': 'REPNE',
                     'LOCK': 'LOCK'}[rep[0]] + ' '
            if not segpf:
                return r + base.upper()
            s = segpf[0]
            w = 'BYTE PTR' if base.endswith('b') else 'WORD PTR'
            if base.startswith('lods'):
                return f'{r}LODS {w} {s}:[SI]'
            if base.startswith('movs'):
                return f'{r}MOVS {w} ES:[DI],{w} {s}:[SI]'
            if base.startswith('cmps'):
                return f'{r}CMPS {w} {s}:[SI],{w} ES:[DI]'
            if base.startswith('outs'):
                return f'{r}OUTS DX,{w} {s}:[SI]'
            raise Unformattable('string op override')
        if rep:
            raise Unformattable('rep on non-string')
        if segpf and not any(o.type == x86.X86_OP_MEM for o in ops):
            raise Unformattable('segment prefix without memory operand')
        mn = RENAME.get(m, m.upper())
        if m in JUMPS or m == 'call':
            op = ops[0]
            if op.type == x86.X86_OP_IMM:
                return f'{mn} {self.sym(ins.refs["target"])}'
        if m in ('lcall', 'ljmp'):
            if ops[0].type == x86.X86_OP_IMM:
                if 'far' not in ins.refs:
                    raise Unformattable('far to unknown segment')
                return f'{mn} FAR PTR {self.sym(ins.refs["far"])}'
            op = ops[0]
            return f'{mn} DWORD PTR {self.memtext(ins, op, need_size=False)}'
        if m == 'int' and ops[0].imm == 3 and ci.bytes[0] == 0xCC:
            return 'INT 3'
        out = []
        regsize = None
        for op in ops:
            if op.type == x86.X86_OP_REG:
                regsize = op.size
        for op in ops:
            if op.type == x86.X86_OP_REG:
                out.append(ci.reg_name(op.reg).upper())
            elif op.type == x86.X86_OP_IMM:
                size = op.size or regsize or 2
                if m in ('int', 'ret', 'retf', 'enter', 'in', 'out'):
                    size = op.size or 1
                    if m in ('ret', 'retf'):
                        size = 2
                out.append(self.imm(ins, op, size))
            elif op.type == x86.X86_OP_MEM:
                need = m not in ('lea', 'les', 'lds') and (regsize is None or m in ('movzx', 'movsx'))
                if m in ('les', 'lds', 'lea'):
                    need = False
                out.append(self.memtext(ins, op, need_size=need))
        # capstone writes the implicit shift count 1 and 'in al, dx' fine; a few need care
        if m in ('shl', 'shr', 'sar', 'sal', 'rol', 'ror', 'rcl', 'rcr') and ci.bytes[0] in (0xD0, 0xD1):
            out = out[:1] + ['1']
        # XCHG reg,reg: the assembler puts the first operand in the reg field
        if m == 'xchg' and len(ops) == 2 and all(o.type == x86.X86_OP_REG for o in ops) \
                and len(ci.bytes) >= 2 and ci.bytes[-2] in (0x86, 0x87):
            modrm = ci.bytes[-1]
            reg = (modrm >> 3) & 7
            names8 = ['AL', 'CL', 'DL', 'BL', 'AH', 'CH', 'DH', 'BH']
            names16 = ['AX', 'CX', 'DX', 'BX', 'SP', 'BP', 'SI', 'DI']
            names = names8 if ci.bytes[-2] == 0x86 else names16
            out = [names[reg], names[modrm & 7]]
        return mn + (' ' + ','.join(out) if out else '')


# ---------------------------------------------------------------- emitting

def is_text(b):
    return 32 <= b < 127 and b not in (0x27,)


def db_lines(data):
    """DB lines for a run of bytes: printable runs as strings"""
    lines = []
    i = 0
    while i < len(data):
        j = i
        while j < len(data) and is_text(data[j]):
            j += 1
        if j - i >= 4:
            lines.append("\tDB '" + data[i:j].decode('ascii') + "'")
            i = j
            continue
        k = i
        chunk = []
        while k < len(data) and len(chunk) < 16:
            j = k
            while j < len(data) and is_text(data[j]):
                j += 1
            if j - k >= 4:
                break
            chunk.append(data[k])
            k += 1
        # runs of one value
        if len(chunk) == 16 and len(set(chunk)) == 1:
            n = 16
            while i + n < len(data) and data[i + n] == chunk[0]:
                n += 1
            lines.append(f'\tDB {hexnum(n)} DUP ({hexnum(chunk[0])})')
            i += n
            continue
        lines.append('\tDB ' + ','.join(hexnum(x) for x in chunk))
        i = k
    return lines


class Emitter:
    def __init__(self, an):
        self.an = an
        self.f = Formatter(an)
        self.lines = []
        self.map = []             # per output line: (seg, off, length) or None

    def out(self, text, where=None):
        self.lines.append(text)
        self.map.append(where)

    def emit(self):
        an = self.an
        self.out('; generated by tools/disasm.py from ' + an.h.exe + ' - do not edit, edit the hints')
        self.out('.186')
        order = an.segs
        for S in order:
            if S.cls == 'CODE':
                self.out(f'{S.name} SEGMENT PARA PUBLIC \'{S.cls}\'')
                self.out(f'\tASSUME CS:{S.name},DS:DATA,ES:NOTHING,SS:NOTHING')
            elif S.stack:
                self.out(f'{S.name} SEGMENT PARA STACK \'{S.cls}\'')
            else:
                self.out(f'{S.name} SEGMENT PARA PUBLIC \'{S.cls}\'')
            self.segment(S)
            self.out(f'{S.name} ENDS')
            self.out('')
        e = an.labels[('CODE', an.p.ip)]
        self.out(f'\tEND {e}')

    def label_lines(self, S, off):
        for c in self.an.h.comments.get((S.name, off), []):
            self.out(f'; {c}')
        n = self.an.labels.get((S.name, off))
        if n:
            if (S.name, off) in self.an.insns:
                self.out(f'{n}:')
            else:
                self.out(f'{n}\tLABEL BYTE')

    def segment(self, S):
        an = self.an
        img = an.p.img
        stored = max(0, min(S.size, len(img) - S.base))
        labels = sorted(o for (s, o) in an.labels if s == S.name)
        # a table of a words hint is written as DWs, with or without a label
        tables = set(o for s, o, cnt, t in an.h.words if s == S.name)
        import bisect
        off = 0
        while off < S.size:
            key = (S.name, off)
            ins = an.insns.get(key)
            if ins and off + ins.size <= S.size:
                self.label_lines(S, off)
                self.inner_labels(S, off, ins.size)
                text = None
                if not ins.raw and key not in an.h.raw:
                    try:
                        text = self.f.fmt(ins)
                    except Unformattable as e:
                        text = None
                        ins.raw = True
                if text is None:
                    b = img[S.base + off:S.base + off + ins.size]
                    text = 'DB ' + ','.join(hexnum(x) for x in b) + f'\t; {ins.ci.mnemonic} {ins.ci.op_str}'
                self.out('\t' + text, (S.name, off, ins.size))
                off += ins.size
                continue
            self.label_lines(S, off)
            # data up to the next label / instruction / far pointer / reloc
            end = off + 1
            while end < S.size and (S.name, end) not in an.insns and (S.name, end) not in an.labels \
                    and S.base + end not in an.farptrs and S.base + end not in an.p.relsites \
                    and end not in tables and not (end == stored):
                end += 1
            a = S.base + off
            if a in an.farptrs:
                t = an.farptrs[a]
                self.inner_labels(S, off, 4)
                self.out(f'\tDD {an.labels[t]}', (S.name, off, 4))
                off += 4
                continue
            if a in an.p.relsites:
                v = an.p.relsites[a]
                T = an.byframe.get(v)
                if T:
                    self.inner_labels(S, off, 2)
                    self.out(f'\tDW {T.name}', (S.name, off, 2))
                    off += 2
                    continue
            if off >= stored:
                self.out(f'\tDB {hexnum(end - off)} DUP (?)', (S.name, off, end - off))
                off = end
                continue
            data = bytes(img[a:S.base + end])
            ws = self.words_at(S, off)
            if ws:
                t, n = ws
                for i in range(n):
                    v = struct.unpack_from('<H', img, a + 2 * i)[0]
                    if i:
                        self.label_lines(S, off + 2 * i)
                    self.inner_labels(S, off + 2 * i, 2)
                    self.out(f'\tDW {an.labels[(t, v)]}', (S.name, off + 2 * i, 2))
                off += 2 * n
                continue
            p = off
            for ln in db_lines(data):
                n = self.db_len(ln)
                self.out(ln, (S.name, p, n))
                p += n
            off = end

    def inner_labels(self, S, off, n):
        """labels inside an item that is written as one line"""
        for k in range(1, n):
            for c in self.an.h.comments.get((S.name, off + k), []):
                self.out(f'; {c}')
            name = self.an.labels.get((S.name, off + k))
            if name:
                self.out(f'{name} = $+{k}')

    def words_at(self, S, off):
        for s, o, cnt, t in self.an.h.words:
            if s == S.name and o == off:
                return t, cnt
        return None

    @staticmethod
    def db_len(ln):
        body = ln.split('DB', 1)[1].strip()
        if body.startswith("'"):
            return len(body) - 2
        m = re.match(r'(\w+) DUP', body)
        if m:
            v = m.group(1)
            return int(v[:-1], 16) if v.endswith('H') else int(v)
        return body.count(',') + 1


def generate(hints_path, raw_extra=()):
    h = Hints(hints_path)
    for r in raw_extra:
        h.raw.add(r)
    prog = Program(os.path.join(game_dir(), h.exe))
    an = Analysis(prog, h)
    an.run()
    em = Emitter(an)
    em.emit()
    return an, em


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hints')
    ap.add_argument('-o', '--out')
    a = ap.parse_args()
    an, em = generate(a.hints)
    out = a.out or os.path.join(ROOT, 'build', os.path.splitext(os.path.basename(a.hints))[0] + '.ASM')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'w', newline='\r\n').write('\n'.join(em.lines) + '\n')
    for w in an.warnings:
        print('warning:', w)
    print(f'{len(an.insns)} instructions, {len(an.labels)} labels -> {out}')


if __name__ == '__main__':
    main()


def gaps(an, seg='CODE', show=3):
    """ranges of a code segment no instruction covers, with a look at them"""
    S = an.byname[seg]
    cov = bytearray(S.size)
    for (s, o), ins in an.insns.items():
        if s == seg:
            cov[o:o + ins.size] = b'\1' * ins.size
    out = []
    o = 0
    while o < S.size:
        if cov[o]:
            o += 1
            continue
        e = o
        while e < S.size and not cov[e]:
            e += 1
        out.append((o, e))
        o = e
    return out
