"""8086/80286 instruction encoder for tasm.py.

Shapes: pass 1 decides the size-relevant choices of every instruction with
backward knowledge only and stores them in asm.shape[seq]; pass 2 reuses them.
That is how TASM's single pass behaves, and it is where the NOP padding after
short forward jumps comes from.
"""
from tasm import AsmError, Val, REG8, REG16, SREG, tokenize, split_toks, INSTR_NAMES

ALU = {'ADD': 0, 'OR': 1, 'ADC': 2, 'SBB': 3, 'AND': 4, 'SUB': 5, 'XOR': 6, 'CMP': 7}
SHIFT = {'ROL': 0, 'ROR': 1, 'RCL': 2, 'RCR': 3, 'SHL': 4, 'SAL': 4, 'SHR': 5, 'SAR': 7}
GRP3 = {'NOT': 2, 'NEG': 3, 'MUL': 4, 'IMUL': 5, 'DIV': 6, 'IDIV': 7}
JCC = {'JO': 0x70, 'JNO': 0x71, 'JB': 0x72, 'JC': 0x72, 'JNAE': 0x72, 'JAE': 0x73,
       'JNB': 0x73, 'JNC': 0x73, 'JE': 0x74, 'JZ': 0x74, 'JNE': 0x75, 'JNZ': 0x75,
       'JBE': 0x76, 'JNA': 0x76, 'JA': 0x77, 'JNBE': 0x77, 'JS': 0x78, 'JNS': 0x79,
       'JP': 0x7A, 'JPE': 0x7A, 'JNP': 0x7B, 'JPO': 0x7B, 'JL': 0x7C, 'JNGE': 0x7C,
       'JGE': 0x7D, 'JNL': 0x7D, 'JLE': 0x7E, 'JNG': 0x7E, 'JG': 0x7F, 'JNLE': 0x7F}
LOOPS = {'LOOP': 0xE2, 'LOOPE': 0xE1, 'LOOPZ': 0xE1, 'LOOPNE': 0xE0, 'LOOPNZ': 0xE0,
         'JCXZ': 0xE3}
SIMPLE = {'CLI': [0xFA], 'STI': [0xFB], 'CLC': [0xF8], 'STC': [0xF9], 'CMC': [0xF5],
          'CLD': [0xFC], 'STD': [0xFD], 'CBW': [0x98], 'CWD': [0x99], 'LAHF': [0x9F],
          'SAHF': [0x9E], 'PUSHF': [0x9C], 'POPF': [0x9D], 'PUSHA': [0x60],
          'POPA': [0x61], 'NOP': [0x90], 'HLT': [0xF4], 'XLAT': [0xD7], 'XLATB': [0xD7],
          'AAA': [0x37], 'AAS': [0x3F], 'DAA': [0x27], 'DAS': [0x2F], 'WAIT': [0x9B],
          'FWAIT': [0x9B], 'INTO': [0xCE], 'IRET': [0xCF], 'LEAVE': [0xC9],
          'MOVSB': [0xA4], 'MOVSW': [0xA5], 'CMPSB': [0xA6], 'CMPSW': [0xA7],
          'STOSB': [0xAA], 'STOSW': [0xAB], 'LODSB': [0xAC], 'LODSW': [0xAD],
          'SCASB': [0xAE], 'SCASW': [0xAF], 'INSB': [0x6C], 'INSW': [0x6D],
          'OUTSB': [0x6E], 'OUTSW': [0x6F], 'PUSHAW': [0x60], 'POPAW': [0x61]}
PREFIX = {'REP': 0xF3, 'REPE': 0xF3, 'REPZ': 0xF3, 'REPNE': 0xF2, 'REPNZ': 0xF2,
          'LOCK': 0xF0}
SEGPFX = {'ES': 0x26, 'CS': 0x2E, 'SS': 0x36, 'DS': 0x3E}
RM16 = {('BX', 'SI'): 0, ('BX', 'DI'): 1, ('BP', 'SI'): 2, ('BP', 'DI'): 3,
        (None, 'SI'): 4, (None, 'DI'): 5, ('BP', None): 6, ('BX', None): 7}

INSTR_NAMES.update(ALU, SHIFT, GRP3, JCC, LOOPS, SIMPLE, PREFIX)
INSTR_NAMES.update(['MOV', 'PUSH', 'POP', 'INC', 'DEC', 'TEST', 'XCHG', 'LEA', 'LDS', 'LES',
                    'JMP', 'CALL', 'RET', 'RETN', 'RETF', 'INT', 'IN', 'OUT', 'ENTER', 'BOUND',
                    'AAM', 'AAD', 'MOVS', 'LODS', 'STOS', 'CMPS', 'SCAS', 'SMSW', 'LMSW'])


def fits8(n):
    return -128 <= n <= 127


class Enc:
    def __init__(self, asm, seg, seq):
        self.a = asm
        self.seg = seg
        self.seq = seq
        self.out = bytearray()
        self.fix = []      # (pos, kind, target, addend)
        self.fwdname = None
        self.rel = []
        self.pad = 0
        self.passlen = None
        self.shape = asm.shape.get(seq) if asm.emit else None
        self.newshape = {}

    # shape memory: decide(key, fn) returns the pass-1 decision in pass 2
    def decide(self, key, value):
        if self.a.emit:
            if self.shape is None or key not in self.shape:
                raise AsmError(f'internal: no shape {key} for instruction {self.seq}')
            return self.shape[key]
        self.newshape[key] = value
        return value

    def b(self, *xs):
        for x in xs:
            self.out.append(x & 0xFF)

    def w(self, n):
        self.out += (n & 0xFFFF).to_bytes(2, 'little')

    def relbytes(self, n):
        """The next n bytes are a relative displacement."""
        self.rel.append((len(self.out), n))

    def finish(self):
        for pos, n in self.rel:
            self.seg.relmask.append((self.seg.pc + pos, n))
        if not self.a.emit:
            self.a.shape[self.seq] = self.newshape
        if self.pad:
            self.out += bytes([0x90]) * self.pad
        seg = self.seg
        base = seg.pc
        for pos, kind, target, addend in self.fix:
            seg.fixups.append((base + pos, kind, target, addend))
        seg.emit(bytes(self.out))


# ---------------------------------------------------------------- operands

def classify(v):
    if FORCE_WORD[0] and BARE_AS_MEM[0] and v.fwdbare and not v.reg:
        return 'mem'      # single-pass TASM: an unknown name is a variable
    if v.reg:
        if v.reg in REG8:
            return 'r8'
        if v.reg in REG16:
            return 'r16'
        return 'sreg'
    if v.mem or v.hasbr or v.base or v.index:
        return 'mem'
    if v.label and not v.isoffset and v.rel is not None:
        return 'mem'
    if LENIENT[0] and v.unknown and v.label and not v.isoffset and v.rel is None:
        return 'mem'
    return 'imm'


def parse_operands(asm, toks):
    ops = split_toks(toks, ',')
    return [asm.eval_toks(o) for o in ops]


def segment_prefix(e, v):
    """Which override prefix a memory operand needs (None for none)."""
    a = e.a
    default = 'SS' if v.base == 'BP' else 'DS'
    if v.ovr:
        if isinstance(v.ovr, tuple):
            name = v.ovr[1]
            for r in ('DS', 'SS', 'ES', 'CS') if default == 'DS' else ('SS', 'DS', 'ES', 'CS'):
                if a.assume.get(r) == name:
                    return None if r == default else r
            raise AsmError(f'no segment register assumed to {name}')
        return None if v.ovr == default else v.ovr
    if v.vseg is None:
        return None
    target = v.vseg
    if a.assume.get(default) == target:
        return None
    for r in ('DS', 'SS', 'ES', 'CS'):
        if a.assume.get(r) == target:
            return r
    return None


def emit_modrm(e, v, regfield, pfx_out=None):
    """Append modrm+disp for operand v.  Returns nothing; prefix handled by caller."""
    if v.reg:
        idx = (REG16.index(v.reg) if v.reg in REG16 else REG8.index(v.reg))
        e.b(0xC0 | (regfield << 3) | idx)
        return
    base, index = v.base, v.index
    reloc = v.rel is not None
    if base is None and index is None:
        e.b(0x06 | (regfield << 3))
        pos = len(e.out)
        if reloc:
            e.fix.append((pos, 'OFF', v.rel, v.num))
        e.w(v.num)
        return
    rm = RM16[(base, index)]
    if reloc or v.unknown:
        dsz = 16
    elif v.num == 0 and not (base == 'BP' and index is None):
        dsz = 0
    elif fits8(v.num):
        dsz = 8
    else:
        dsz = 16
    dsz = e.decide('disp', dsz)
    if dsz == 0 and v.num != 0:
        raise AsmError('phase error in displacement')
    if dsz == 8 and not fits8(v.num) and not reloc:
        raise AsmError('phase error: displacement grew')
    mod = {0: 0, 8: 1, 16: 2}[dsz]
    e.b((mod << 6) | (regfield << 3) | rm)
    if dsz == 8:
        e.b(v.num)
    elif dsz == 16:
        pos = len(e.out)
        if reloc:
            e.fix.append((pos, 'OFF', v.rel, v.num))
        e.w(v.num)


def mem_prefix(e, v):
    r = segment_prefix(e, v)
    r = e.decide('seg', r)
    if r:
        e.b(SEGPFX[r])


def imm(e, v, size):
    """Emit an immediate of size 1 or 2."""
    if size == 1:
        if v.rel is not None or v.segrel is not None:
            raise AsmError('relocatable byte immediate')
        e.b(v.num)
        return
    pos = len(e.out)
    if v.segrel is not None:
        # a segment TASM has not seen yet in this pass: its fixup is written late
        e.fix.append((pos, 'SEGL' if v.fwd else 'SEG', v.segrel, 0))
        e.w(0)
    elif v.rel is not None:
        e.fix.append((pos, 'OFF', v.rel, v.num))
        e.w(v.num)
    else:
        e.w(v.num)


def opsize(a, b=None, default=None):
    for v in (a, b):
        if v is None:
            continue
        c = classify(v)
        if c == 'r8':
            return 1
        if c in ('r16', 'sreg'):
            return 2
    for v in (a, b):
        if v is not None and classify(v) == 'mem' and v.size in (1, 2):
            if FORCE_WORD[0] and (v.fwd or v.unknown):
                return 2
            return v.size
    for v in (a, b):
        if v is not None and FORCE_WORD[0] and v.fwdbare and not v.reg:
            return 2
    for v in (a, b):
        if v is not None and classify(v) == 'mem' and v.size == 4:
            return 4
    # an OFFSET or a segment value is a word
    for v in (a, b):
        if v is not None and classify(v) == 'imm' and (v.rel is not None or v.segrel is not None):
            return 2
    if LENIENT[0]:
        return 2
    return default


LENIENT = [False]   # pass 0 guesses sizes of forward references
FORCE_WORD = [False]  # single-pass TASM takes a forward variable for a word
BARE_AS_MEM = [True]  # ... and an unknown name for a variable


def is_small_imm(v):
    # in a word operation TASM reads 0FF80h..0FFFFh as -128..-1
    n = v.num - 0x10000 if 0xFF80 <= v.num <= 0xFFFF else v.num
    return v.rel is None and v.segrel is None and not v.unknown and fits8(n)


# ---------------------------------------------------------------- main

def encode(asm, seg, seq, mn, toks):
    LENIENT[0] = asm.passno == 0
    e = Enc(asm, seg, seq)
    # prefixes written on the same line: REP MOVSB, LOCK ...
    while mn in PREFIX or mn in SEGPFX and toks and toks[0].k == 'id':
        e.b(PREFIX[mn] if mn in PREFIX else SEGPFX[mn])
        if not toks:
            e.finish()
            return
        mn = toks[0].v
        toks = toks[1:]
    try:
        f = HANDLERS.get(mn)
        if f is None:
            for tbl, h in ((ALU, enc_alu), (SHIFT, enc_shift), (GRP3, enc_grp3),
                           (JCC, enc_jcc), (LOOPS, enc_loop), (SIMPLE, enc_simple)):
                if mn in tbl:
                    f = h
                    break
        if f is None:
            raise AsmError(f'unknown instruction or directive {mn}')
        if not asm.multipass and not asm.emit and asm.passno > 0:
            # what length did TASM's single pass give this instruction?
            t = Enc(asm, seg, seq)
            FORCE_WORD[0] = True
            try:
                try:
                    f(t, mn, toks)
                except AsmError:
                    # e.g. both operands would be memory: then the name was a number
                    t = Enc(asm, seg, seq)
                    BARE_AS_MEM[0] = False
                    f(t, mn, toks)
            finally:
                FORCE_WORD[0] = False
                BARE_AS_MEM[0] = True
            e.passlen = len(t.out) + len(e.out)
        f(e, mn, toks)
        want = e.decide('len', e.passlen)
        if want is not None and len(e.out) < want:
            e.out += bytes([0x90]) * (want - len(e.out))
    except AsmError:
        raise
    e.finish()


def enc_simple(e, mn, toks):
    if toks:
        # e.g. XLAT table or string ops with operands: ignore operand but honour override
        vs = parse_operands(e.a, toks)
        for v in vs:
            if v.ovr and not isinstance(v.ovr, tuple) and v.ovr != 'DS':
                e.b(SEGPFX[v.ovr])
    e.out += bytes(SIMPLE[mn])


def enc_strop(e, mn, toks):
    vs = parse_operands(e.a, toks)
    size = None
    for v in vs:
        if v.size:
            size = v.size
    base = {'MOVS': 0xA4, 'CMPS': 0xA6, 'STOS': 0xAA, 'LODS': 0xAC, 'SCAS': 0xAE}[mn]
    # source operand override
    src = vs[-1] if mn in ('MOVS', 'LODS', 'CMPS') else None
    if src is not None:
        r = segment_prefix(e, src) if src.ovr else None
        if r:
            e.b(SEGPFX[r])
    e.b(base + (1 if size == 2 else 0))


def enc_alu(e, mn, toks):
    d, s = parse_operands(e.a, toks)
    n = ALU[mn]
    cd, cs = classify(d), classify(s)
    size = opsize(d, s)
    if cs == 'imm':
        if size is None:
            raise AsmError('operand size unknown')
        if cd in ('r8', 'r16') and d.reg in ('AL', 'AX'):
            if size == 1:
                e.b(n * 8 + 4)
                imm(e, s, 1)
                return
            # TASM always takes the accumulator form for AX; MASM (and so
            # PD.EXE) the sign-extended byte form where the value fits
            if not getattr(e.a, 'alu_ax_short', True) and is_small_imm(s) and s.rel is None:
                e.b(0x83)
                emit_modrm(e, d, n)
                e.b(s.num & 0xFF)
                return
            e.b(n * 8 + 5)
            imm(e, s, 2)
            return
        if cd == 'mem':
            mem_prefix(e, d)
        if size == 1:
            e.b(0x80)
            emit_modrm(e, d, n)
            imm(e, s, 1)
            return
        # single-pass TASM reserves the word form for a constant it has not
        # seen yet, and pads the byte form with a NOP later
        small = e.decide('imm8', is_small_imm(s) and not (FORCE_WORD[0] and s.fwd))
        if small:
            e.b(0x83)
            emit_modrm(e, d, n)
            e.b(s.num & 0xFF)
        else:
            e.b(0x81)
            emit_modrm(e, d, n)
            imm(e, s, 2)
        return
    w = 1 if size == 2 else 0
    if cd in ('r8', 'r16') and cs in ('r8', 'r16'):
        # TASM: reg,reg uses the "r/m, reg" form? decided by EXPERIMENT below
        if e.a.regreg_form == 'rm_reg':
            e.b(n * 8 + w)
            emit_modrm(e, d, regno(s))
        else:
            e.b(n * 8 + 2 + w)
            emit_modrm(e, s, regno(d))
        return
    if cd in ('r8', 'r16'):
        mem_prefix(e, s)
        e.b(n * 8 + 2 + w)
        emit_modrm(e, s, regno(d))
        return
    if cs in ('r8', 'r16'):
        mem_prefix(e, d)
        e.b(n * 8 + w)
        emit_modrm(e, d, regno(s))
        return
    raise AsmError('bad operands')


def regno(v):
    if v.reg in REG16:
        return REG16.index(v.reg)
    if v.reg in REG8:
        return REG8.index(v.reg)
    return SREG.index(v.reg)


def enc_mov(e, mn, toks):
    d, s = parse_operands(e.a, toks)
    cd, cs = classify(d), classify(s)
    if cd == 'r16' and s.isoffset and not s.mem and (s.base or s.index):
        # TASM turns MOV reg,OFFSET x[BX] into LEA reg,x[BX] (an OFFSET
        # alias indexed afterwards, cos[bx], stays a load)
        s = s.copy()
        s.mem = True
        e.b(0x8D)
        emit_modrm(e, s, regno(d))
        return
    size = opsize(d, s)
    if cd == 'sreg':
        if cs == 'sreg':
            raise AsmError('mov sreg,sreg')
        if cs == 'mem':
            mem_prefix(e, s)
        e.b(0x8E)
        emit_modrm(e, s, regno(d))
        return
    if cs == 'sreg':
        if cd == 'mem':
            mem_prefix(e, d)
        e.b(0x8C)
        emit_modrm(e, d, regno(s))
        return
    if cs == 'imm':
        if cd in ('r8', 'r16'):
            e.b((0xB0 if cd == 'r8' else 0xB8) + regno(d))
            imm(e, s, 1 if cd == 'r8' else 2)
            return
        if size is None:
            raise AsmError('operand size unknown')
        mem_prefix(e, d)
        e.b(0xC6 if size == 1 else 0xC7)
        emit_modrm(e, d, 0)
        imm(e, s, size)
        return
    w = 1 if size == 2 else 0
    if cd in ('r8', 'r16') and cs in ('r8', 'r16'):
        if e.a.regreg_form == 'rm_reg':
            e.b(0x88 + w)
            emit_modrm(e, d, regno(s))
        else:
            e.b(0x8A + w)
            emit_modrm(e, s, regno(d))
        return
    if cd in ('r8', 'r16'):
        # accumulator, direct address
        mem_prefix(e, s)
        if d.reg in ('AL', 'AX') and s.base is None and s.index is None:
            e.b(0xA0 + w)
            pos = len(e.out)
            if s.rel is not None:
                e.fix.append((pos, 'OFF', s.rel, s.num))
            e.w(s.num)
            return
        e.b(0x8A + w)
        emit_modrm(e, s, regno(d))
        return
    if cs in ('r8', 'r16'):
        mem_prefix(e, d)
        if s.reg in ('AL', 'AX') and d.base is None and d.index is None:
            e.b(0xA2 + w)
            pos = len(e.out)
            if d.rel is not None:
                e.fix.append((pos, 'OFF', d.rel, d.num))
            e.w(d.num)
            return
        e.b(0x88 + w)
        emit_modrm(e, d, regno(s))
        return
    raise AsmError('bad mov operands')


def enc_pushpop(e, mn, toks):
    # TASM allows several operands separated by blanks: PUSH AX BX CX
    if any(t.v == ',' for t in toks):
        raise AsmError('comma in push/pop')
    groups = [[]]
    glue = {'PTR', 'OFFSET', 'SEG', 'BYTE', 'WORD', 'DWORD', 'SHORT', 'NEAR', 'FAR',
            'MOD', 'SHL', 'SHR', 'AND', 'OR', 'XOR', 'NOT', 'HIGH', 'LOW', 'SIZE', 'TYPE',
            'LENGTH', 'EQ', 'NE', 'LT', 'LE', 'GT', 'GE'}
    prev = None
    for t in toks:
        if prev is not None and prev.k != 'op' and t.k != 'op' and                 prev.v not in glue and t.v not in glue:
            groups.append([])
        groups[-1].append(t)
        prev = t
    push = mn == 'PUSH'
    for g in groups:
        v = e.a.eval_toks(g)
        c = classify(v)
        if c == 'r16':
            e.b((0x50 if push else 0x58) + regno(v))
        elif c == 'sreg':
            if push:
                e.b([0x06, 0x0E, 0x16, 0x1E][regno(v)])
            else:
                e.b([0x07, None, 0x17, 0x1F][regno(v)])
        elif c == 'mem':
            mem_prefix(e, v)
            e.b(0xFF if push else 0x8F)
            emit_modrm(e, v, 6 if push else 0)
        elif c == 'imm' and push:
            small = e.decide('imm8', is_small_imm(v))
            if small:
                e.b(0x6A, v.num & 0xFF)
            else:
                e.b(0x68)
                imm(e, v, 2)
        else:
            raise AsmError(f'bad {mn} operand')


def enc_incdec(e, mn, toks):
    (v,) = parse_operands(e.a, toks)
    c = classify(v)
    n = 0 if mn == 'INC' else 1
    if c == 'r16':
        e.b(0x40 + n * 8 + regno(v))
        return
    if c == 'r8':
        e.b(0xFE)
        emit_modrm(e, v, n)
        return
    size = opsize(v)
    if size is None:
        raise AsmError('operand size unknown')
    mem_prefix(e, v)
    e.b(0xFE if size == 1 else 0xFF)
    emit_modrm(e, v, n)


def enc_grp3(e, mn, toks):
    vs = parse_operands(e.a, toks)
    if mn == 'IMUL' and len(vs) >= 2:
        d = vs[0]
        if len(vs) == 2:
            src, im = d, vs[1]
        else:
            src, im = vs[1], vs[2]
        if classify(src) == 'mem':
            mem_prefix(e, src)
        small = e.decide('imm8', is_small_imm(im))
        e.b(0x6B if small else 0x69)
        emit_modrm(e, src, regno(d))
        if small:
            e.b(im.num)
        else:
            imm(e, im, 2)
        return
    (v,) = vs
    size = opsize(v)
    if size is None:
        raise AsmError('operand size unknown')
    if classify(v) == 'mem':
        mem_prefix(e, v)
    e.b(0xF6 if size == 1 else 0xF7)
    emit_modrm(e, v, GRP3[mn])


def enc_test(e, mn, toks):
    d, s = parse_operands(e.a, toks)
    cd, cs = classify(d), classify(s)
    size = opsize(d, s)
    w = 1 if size == 2 else 0
    if cs == 'imm':
        if d.reg in ('AL', 'AX'):
            e.b(0xA8 + w)
            imm(e, s, size)
            return
        if cd == 'mem':
            mem_prefix(e, d)
        e.b(0xF6 + w)
        emit_modrm(e, d, 0)
        imm(e, s, size)
        return
    if cd == 'mem':
        mem_prefix(e, d)
        e.b(0x84 + w)
        emit_modrm(e, d, regno(s))
        return
    if cs == 'mem':
        mem_prefix(e, s)
        e.b(0x84 + w)
        emit_modrm(e, s, regno(d))
        return
    # MASM (Assembler.test_form = 'rm_reg') puts the first register in r/m
    if getattr(e.a, 'test_form', e.a.regreg_form) == 'rm_reg':
        e.b(0x84 + w)
        emit_modrm(e, d, regno(s))
    else:
        e.b(0x84 + w)
        emit_modrm(e, s, regno(d))


def enc_xchg(e, mn, toks):
    d, s = parse_operands(e.a, toks)
    cd, cs = classify(d), classify(s)
    size = opsize(d, s)
    if cd == 'r16' and cs == 'r16' and ('AX' in (d.reg, s.reg)):
        other = s if d.reg == 'AX' else d
        e.b(0x90 + regno(other))
        return
    w = 1 if size == 2 else 0
    if cd == 'mem':
        mem_prefix(e, d)
        e.b(0x86 + w)
        emit_modrm(e, d, regno(s))
        return
    if cs == 'mem':
        mem_prefix(e, s)
        e.b(0x86 + w)
        emit_modrm(e, s, regno(d))
        return
    e.b(0x86 + w)
    emit_modrm(e, s, regno(d))


def enc_lea(e, mn, toks):
    d, s = parse_operands(e.a, toks)
    s = s.copy()
    s.mem = True
    op = {'LEA': 0x8D, 'LDS': 0xC5, 'LES': 0xC4}[mn]
    if mn == 'LEA' and s.base is None and s.index is None and getattr(e.a, 'lea_smart', True):
        # TASM turns LEA of a direct address into MOV reg,OFFSET (MASM,
        # and so PD.EXE, keeps the LEA: Assembler.lea_smart = False)
        e.b(0xB8 + regno(d))
        pos = len(e.out)
        if s.rel is not None:
            e.fix.append((pos, 'OFF', s.rel, s.num))
        e.w(s.num)
        return
    if mn != 'LEA':
        mem_prefix(e, s)
    else:
        # LEA ignores segment overrides except written ones
        pass
    e.b(op)
    emit_modrm(e, s, regno(d))


def enc_shift(e, mn, toks):
    d, c = parse_operands(e.a, toks)
    size = opsize(d)
    if size is None:
        raise AsmError('operand size unknown')
    w = 1 if size == 2 else 0
    n = SHIFT[mn]
    if classify(d) == 'mem':
        mem_prefix(e, d)
    if c.reg == 'CL':
        e.b(0xD2 + w)
        emit_modrm(e, d, n)
        return
    if c.num == 1 and not c.unknown:
        e.b(0xD0 + w)
        emit_modrm(e, d, n)
        return
    if e.a.cpu < 186:
        raise AsmError('shift by immediate needs .186')
    e.b(0xC0 + w)
    emit_modrm(e, d, n)
    e.b(c.num)


def enc_inout(e, mn, toks):
    a, b = parse_operands(e.a, toks)
    if mn == 'IN':
        acc, port = a, b
    else:
        port, acc = a, b
    w = 1 if acc.reg == 'AX' else 0
    if port.reg == 'DX':
        e.b((0xEC if mn == 'IN' else 0xEE) + w)
    else:
        e.b((0xE4 if mn == 'IN' else 0xE6) + w, port.num)


def enc_int(e, mn, toks):
    (v,) = parse_operands(e.a, toks)
    if v.num == 3:
        e.b(0xCC)
    else:
        e.b(0xCD, v.num)


def enc_ret(e, mn, toks):
    far = mn == 'RETF'
    if mn == 'RET':
        far = bool(e.a.proc_stack) and e.a.proc_stack[-1][1] == 'FAR'
    if toks:
        (v,) = parse_operands(e.a, toks)
        e.b(0xCA if far else 0xC2)
        e.w(v.num)
    else:
        e.b(0xCB if far else 0xC3)


def enc_aam(e, mn, toks):
    n = 10
    if toks:
        n = parse_operands(e.a, toks)[0].num
    e.b(0xD4 if mn == 'AAM' else 0xD5, n)


def enc_enter(e, mn, toks):
    a, b = parse_operands(e.a, toks)
    e.b(0xC8)
    e.w(a.num)
    e.b(b.num)


def enc_smsw(e, mn, toks):
    (v,) = parse_operands(e.a, toks)
    e.b(0x0F, 0x01)
    emit_modrm(e, v, 4 if mn == 'SMSW' else 6)


# ---------------------------------------------------------------- control flow

def target_info(e, v):
    """For a direct jump target: (known, same_segment, offset)."""
    a = e.a
    if v.rel is None:
        if v.unknown and a.passno == 0:
            return ('fwd', True)
        raise AsmError('jump to a constant')
    if v.rel[0] == 'X':
        return ('extern', None)
    if v.unknown or v.fwd:
        return ('fwd', v.rel[1] == e.seg.name)
    return ('known', v.rel[1] == e.seg.name)


def fwd_fits(e, v):
    """TASM's estimate for a forward jump: the distance in its previous pass."""
    if e.a.passno == 0 or v.unknown or not e.a.multipass:
        return False
    prev = e.a.prev_pc.get(e.seq)
    if prev is None or prev[0] != e.seg.name:
        return False
    ok = fits8(v.num - (prev[1] + 2))
    if ok and not e.a.emit and e.fwdname:
        e.a.fwd_checks.append((e.seq, e.fwdname, e.seg.name))
    return ok


def rel_to(e, v, instr_len):
    """Displacement from the end of the instruction (at e.seg.pc + instr_len)."""
    return v.num - (e.seg.pc + instr_len)


def jump_name(e, toks):
    ids = [t.v for t in toks if t.k == 'id' and t.v not in ('SHORT', 'NEAR', 'FAR', 'PTR')]
    if len(ids) == 1:
        e.fwdname = e.a.mangle(ids[0])


def enc_jcc(e, mn, toks):
    jump_name(e, toks)
    (v,) = parse_operands(e.a, toks)
    op = JCC[mn]
    kind, same = target_info(e, v)
    if kind == 'extern' or not same:
        raise AsmError('conditional jump to another segment')
    if v.short or not e.a.jumps:
        shape = 'short'
    elif kind == 'fwd':
        shape = 'short' if fwd_fits(e, v) else 'fwd5'
    else:
        shape = 'short' if fits8(rel_to(e, v, 2)) else 'long'
    shape = e.decide('j', shape)
    if shape == 'short':
        d = rel_to(e, v, 2)
        if not fits8(d) and e.a.emit:
            raise AsmError(f'jump out of range by {abs(d) - 128} bytes')
        e.b(op)
        e.relbytes(1)
        e.b(d)
    elif shape == 'fwd5':
        d = rel_to(e, v, 2)
        if fits8(d):
            e.b(op)
            e.relbytes(1)
            e.b(d, 0x90, 0x90, 0x90)
        else:
            e.b(op ^ 1, 3, 0xE9)
            e.relbytes(2)
            e.w(rel_to(e, v, 5))
    else:
        e.b(op ^ 1, 3, 0xE9)
        e.relbytes(2)
        e.w(rel_to(e, v, 5))


def enc_loop(e, mn, toks):
    jump_name(e, toks)
    # with JUMPS, TASM extends an out-of-range LOOP to
    #   LOOP $+4 / JMP SHORT $+5 / JMP NEAR target
    (v,) = parse_operands(e.a, toks)
    kind, same = target_info(e, v)
    if v.short or not e.a.jumps:
        shape = 'short'
    elif kind == 'fwd':
        shape = 'short' if fwd_fits(e, v) else 'fwd7'
    else:
        shape = 'short' if fits8(rel_to(e, v, 2)) else 'long'
    shape = e.decide('j', shape)
    d = rel_to(e, v, 2)
    if shape == 'short' or shape == 'fwd7' and fits8(d):
        if e.a.emit and not fits8(d):
            raise AsmError(f'{mn} out of range')
        e.b(LOOPS[mn])
        e.relbytes(1)
        e.b(d)
        if shape == 'fwd7':
            e.b(0x90, 0x90, 0x90, 0x90, 0x90)
    else:
        e.b(LOOPS[mn], 2, 0xEB, 3, 0xE9)
        e.relbytes(2)
        e.w(rel_to(e, v, 7))


def enc_jmp(e, mn, toks):
    jump_name(e, toks)
    (v,) = parse_operands(e.a, toks)
    c = classify(v) if not (v.label and not v.hasbr and not v.mem) else 'label'
    if c == 'label' and v.rel is None and not v.unknown:
        raise AsmError('jump to a constant')
    if c in ('mem', 'r16'):
        far = v.dist == 'FAR' or v.size == 4
        if c == 'mem':
            mem_prefix(e, v)
        e.b(0xFF)
        if mn == 'JMP':
            emit_modrm(e, v, 5 if far else 4)
        else:
            emit_modrm(e, v, 3 if far else 2)
        return
    kind, same = target_info(e, v)
    far = (v.dist == 'FAR' and not same) or (kind != 'extern' and not same and kind != 'fwd') \
        or (kind == 'extern' and v.dist == 'FAR')
    if mn == 'CALL':
        if kind == 'known' and v.dist == 'FAR':
            far = True
        shape = e.decide('c', 'far' if far else 'near')
        if shape == 'far':
            e.b(0x9A)
            pos = len(e.out)
            e.fix.append((pos, 'OFF', v.rel, v.num))
            e.w(v.num)
            e.fix.append((pos + 2, 'SEG', v.rel, 0))
            e.w(0)
            return
        e.b(0xE8)
        e.relbytes(2)
        if kind == 'extern':
            e.fix.append((len(e.out), 'REL', v.rel, v.num))
            e.w(0)
        else:
            e.w(rel_to(e, v, 3))
        return
    # JMP
    if far and v.dist == 'FAR' and kind == 'known':
        shape = 'far'
    elif v.short:
        shape = 'short'
    elif kind == 'extern':
        shape = 'near'
    elif kind == 'fwd':
        shape = 'short' if fwd_fits(e, v) else 'fwd3'
    else:
        shape = 'short' if fits8(rel_to(e, v, 2)) else 'near'
    shape = e.decide('j', shape)
    if shape == 'far':
        e.b(0xEA)
        pos = len(e.out)
        e.fix.append((pos, 'OFF', v.rel, v.num))
        e.w(v.num)
        e.fix.append((pos + 2, 'SEG', v.rel, 0))
        e.w(0)
    elif shape == 'short':
        d = rel_to(e, v, 2)
        if e.a.emit and not fits8(d):
            raise AsmError('short jump out of range')
        e.b(0xEB)
        e.relbytes(1)
        e.b(d)
    elif shape == 'fwd3':
        d = rel_to(e, v, 2)
        if fits8(d):
            e.b(0xEB)
            e.relbytes(1)
            e.b(d, 0x90)
        else:
            e.b(0xE9)
            e.relbytes(2)
            e.w(rel_to(e, v, 3))
    else:
        e.b(0xE9)
        e.relbytes(2)
        if kind == 'extern':
            e.fix.append((len(e.out), 'REL', v.rel, v.num))
            e.w(0)
        else:
            e.w(rel_to(e, v, 3))


HANDLERS = {
    'MOV': enc_mov, 'PUSH': enc_pushpop, 'POP': enc_pushpop, 'INC': enc_incdec,
    'DEC': enc_incdec, 'TEST': enc_test, 'XCHG': enc_xchg, 'LEA': enc_lea,
    'LDS': enc_lea, 'LES': enc_lea, 'IN': enc_inout, 'OUT': enc_inout, 'INT': enc_int,
    'RET': enc_ret, 'RETN': enc_ret, 'RETF': enc_ret, 'JMP': enc_jmp, 'CALL': enc_jmp,
    'AAM': enc_aam, 'AAD': enc_aam, 'ENTER': enc_enter, 'SMSW': enc_smsw, 'LMSW': enc_smsw,
    'MOVS': enc_strop, 'LODS': enc_strop, 'STOS': enc_strop, 'CMPS': enc_strop,
    'SCAS': enc_strop,
}
