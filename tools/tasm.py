#!/usr/bin/env python3
"""A Turbo Assembler (MASM mode) work-alike, big enough for the generated
sources of tools/disasm.py, that reproduces TASM's byte choices (and, with
the switches build.py sets, those of the programs examined here).

TASM is a one-pass assembler: an instruction's size is fixed when TASM first
reads it, knowing only the symbols defined above it.  A forward jump gets the
room its worst case needs and is padded with NOPs when the target turns out to
be close.  To get byte-identical output this assembler runs twice: pass 1
decides every instruction's shape with backward knowledge only, pass 2 fills
in the values and keeps the shapes.

Output is an object: segments with bytes and fixups.  tlink.py combines
objects into an MZ program the way TLINK does.
"""
import os, re, sys

# ----------------------------------------------------------------------------
# errors

class AsmError(Exception):
    pass

# ----------------------------------------------------------------------------
# lexer

REG8 = ['AL', 'CL', 'DL', 'BL', 'AH', 'CH', 'DH', 'BH']
REG16 = ['AX', 'CX', 'DX', 'BX', 'SP', 'BP', 'SI', 'DI']
SREG = ['ES', 'CS', 'SS', 'DS']
REGS = set(REG8 + REG16 + SREG)

IDCHARS = set('ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_@$?')


class Tok:
    __slots__ = ('k', 'v', 's')     # kind: id num str op ; value ; source text

    def __init__(self, k, v, s):
        self.k, self.v, self.s = k, v, s

    def __repr__(self):
        return f'{self.k}:{self.s}'


def parse_number(s):
    u = s.upper()
    try:
        if u.endswith('H'):
            return int(u[:-1], 16)
        if u.endswith('B') and all(c in '01' for c in u[:-1]):
            return int(u[:-1], 2)
        if u.endswith('D') and u[:-1].isdigit():
            return int(u[:-1])
        if (u.endswith('O') or u.endswith('Q')) and u[:-1].isdigit():
            return int(u[:-1], 8)
        return int(u, 10)
    except ValueError:
        raise AsmError(f'bad number {s}')


def tokenize(text):
    toks = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in ' \t\x1a\x0c':
            i += 1
            continue
        if c == ';':
            break
        if c in '\'"':
            j = i + 1
            buf = []
            while True:
                if j >= n:
                    raise AsmError('unterminated string')
                if text[j] == c:
                    if j + 1 < n and text[j + 1] == c:
                        buf.append(c)
                        j += 2
                        continue
                    break
                buf.append(text[j])
                j += 1
            toks.append(Tok('str', ''.join(buf), text[i:j + 1]))
            i = j + 1
            continue
        if c.isdigit():
            j = i
            while j < n and (text[j].isalnum()):
                j += 1
            toks.append(Tok('num', parse_number(text[i:j]), text[i:j]))
            i = j
            continue
        if c in IDCHARS:
            j = i
            while j < n and text[j] in IDCHARS:
                j += 1
            s = text[i:j]
            toks.append(Tok('id', s.upper(), s))
            i = j
            continue
        if c == '.' and i + 1 < n and (text[i + 1].isalpha()) and (not toks or toks[-1].k == 'op' and toks[-1].v not in (']', ')')):
            # directive such as .286 is handled by caller; field access "x.y" too
            pass
        if c == '.' and i + 1 < n and text[i + 1].isdigit() and not toks:
            j = i + 1
            while j < n and text[j].isalnum():
                j += 1
            toks.append(Tok('id', text[i:j].upper(), text[i:j]))
            i = j
            continue
        toks.append(Tok('op', c, c))
        i += 1
    return toks


def strip_comment(line):
    """Cut a ';' comment, respecting quotes."""
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in '\'"':
            q = c
        elif c == ';':
            return line[:i]
    return line


# ----------------------------------------------------------------------------
# values

class Sym:
    """kind: const, text, label, var, seg, extern, struc, field, macro, proc(label)"""
    __slots__ = ('name', 'kind', 'value', 'seg', 'size', 'dist', 'text',
                 'defined_pass', 'defseq', 'extra', 'redef')

    def __init__(self, name, kind):
        self.name = name
        self.kind = kind
        self.value = 0
        self.seg = None
        self.size = None
        self.dist = None
        self.text = None
        self.defined_pass = 0
        self.defseq = -1
        self.extra = None
        self.redef = False


class Val:
    """Result of an expression.

    num      constant part / displacement
    rel      None, ('S', segname) offset in a segment, ('X', extname) offset of an extern
    segrel   SEG reference: ('S', segname) or ('X', extname) (value is a paragraph)
    base, index   registers of a memory operand
    mem      True if it is a memory reference
    size     1/2/4/6/8/10 or None
    ovr      explicit segment override register
    reg      register operand
    vseg     segment the referenced variable lives in (for ASSUME)
    unknown  contains a forward reference unresolved in this pass
    dist     'NEAR'/'FAR' for code labels
    isimm    pure number/offset
    """
    __slots__ = ('num', 'rel', 'segrel', 'base', 'index', 'mem', 'size', 'ovr',
                 'reg', 'vseg', 'unknown', 'dist', 'label', 'short', 'hasbr',
                 'isoffset', 'strlen', 'fwd', 'fwdbare')

    def __init__(self, num=0):
        self.num = num
        self.rel = None
        self.segrel = None
        self.base = None
        self.index = None
        self.mem = False
        self.size = None
        self.ovr = None
        self.reg = None
        self.vseg = None
        self.unknown = False
        self.dist = None
        self.label = False
        self.short = False
        self.hasbr = False
        self.isoffset = False
        self.strlen = 0
        self.fwd = False
        self.fwdbare = False      # a forward name used without OFFSET/SEG

    def copy(self):
        v = Val()
        for a in Val.__slots__:
            setattr(v, a, getattr(self, a))
        return v

    def is_const(self):
        return (self.rel is None and self.segrel is None and not self.mem
                and self.reg is None and self.base is None and self.index is None)

    def __repr__(self):
        return ('Val(' + ', '.join(f'{a}={getattr(self, a)!r}' for a in Val.__slots__
                                   if getattr(self, a) not in (None, False, 0)) + ')')


SIZE_KW = {'BYTE': 1, 'WORD': 2, 'DWORD': 4, 'FWORD': 6, 'PWORD': 6, 'QWORD': 8,
           'TBYTE': 10, 'NEAR': 0xFF02, 'FAR': 0xFF05, 'SHORT': 0xFF01}
DATA_SIZE = {'DB': 1, 'DW': 2, 'DD': 4, 'DF': 6, 'DP': 6, 'DQ': 8, 'DT': 10}

# ----------------------------------------------------------------------------
# expression parser


class ExprParser:
    def __init__(self, asm, toks):
        self.a = asm
        self.t = toks
        self.i = 0

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else None

    def peekv(self):
        p = self.peek()
        return p.v if p and p.k in ('op', 'id') else None

    def take(self):
        tk = self.t[self.i]
        self.i += 1
        return tk

    def expect(self, v):
        tk = self.peek()
        if not tk or tk.v != v:
            raise AsmError(f'expected {v!r}, got {tk.s if tk else "end"}')
        self.i += 1

    def at_end(self):
        return self.i >= len(self.t)

    # precedence climbing, lowest first
    def parse(self):
        v = self.p_or()
        return v

    def p_or(self):
        v = self.p_and()
        while self.peekv() in ('OR', 'XOR'):
            op = self.take().v
            w = self.p_and()
            v = self.binop(op, v, w)
        return v

    def p_and(self):
        v = self.p_not()
        while self.peekv() == 'AND':
            self.take()
            w = self.p_not()
            v = self.binop('AND', v, w)
        return v

    def p_not(self):
        if self.peekv() == 'NOT':
            self.take()
            v = self.p_not()
            return self.const(~v.num & 0xFFFF if v.num >= 0 else ~v.num, v)
        return self.p_rel()

    def p_rel(self):
        v = self.p_add()
        while self.peekv() in ('EQ', 'NE', 'LT', 'LE', 'GT', 'GE'):
            op = self.take().v
            w = self.p_add()
            a, b = v.num, w.num
            r = {'EQ': a == b, 'NE': a != b, 'LT': a < b, 'LE': a <= b,
                 'GT': a > b, 'GE': a >= b}[op]
            v = self.const(0xFFFF if r else 0, v, w)
        return v

    def p_add(self):
        v = self.p_mul()
        while self.peekv() in ('+', '-'):
            op = self.take().v
            w = self.p_mul()
            v = self.addsub(op, v, w)
        return v

    def p_mul(self):
        v = self.p_unary()
        while self.peekv() in ('*', '/', 'MOD', 'SHL', 'SHR'):
            op = self.take().v
            w = self.p_unary()
            v = self.binop(op, v, w)
        return v

    def p_unary(self):
        pv = self.peekv()
        if pv == '-':
            self.take()
            v = self.p_unary()
            if not v.is_const():
                raise AsmError('negating a relocatable value')
            return self.const(-v.num, v)
        if pv == '+':
            self.take()
            return self.p_unary()
        if pv in ('HIGH', 'LOW'):
            self.take()
            v = self.p_unary()
            r = self.const((v.num >> 8) & 0xFF if pv == 'HIGH' else v.num & 0xFF, v)
            return r
        if pv in ('OFFSET',):
            self.take()
            v = self.p_unary()
            w = v.copy()
            w.mem = False
            w.size = None
            w.label = False
            w.isoffset = True
            w.fwdbare = False
            w.dist = None
            if w.ovr and w.rel is None:
                w.ovr = None
            w.ovr = None
            return w
        if pv == 'SEG':
            self.take()
            v = self.p_unary()
            w = Val()
            w.unknown = v.unknown
            w.fwd = v.fwd
            if v.segrel is not None:
                w.segrel = v.segrel
            elif v.rel is not None:
                w.segrel = v.rel
            else:
                w.segrel = ('?', None)
            return w
        if pv in ('SIZE', 'LENGTH', 'TYPE', 'WIDTH', 'MASK'):
            self.take()
            return self.sizeop(pv)
        if pv == 'SHORT':
            self.take()
            v = self.p_unary()
            v = v.copy()
            v.short = True
            return v
        if pv == 'THIS':
            self.take()
            tk = self.take()
            w = self.a.here_val()
            w.size = SIZE_KW.get(tk.v)
            if w.size and w.size >= 0xFF00:
                w.dist = 'NEAR' if tk.v == 'NEAR' else 'FAR'
                w.size = None
                w.label = True
            else:
                w.mem = True
            return w
        # type PTR expr
        tk = self.peek()
        if tk and tk.k == 'id' and self.peek(1) and self.peek(1).v == 'PTR':
            self.take()
            self.take()
            v = self.p_unary()
            v = v.copy()
            sz = self.a.type_size(tk.v)
            if tk.v in ('NEAR', 'FAR'):
                v.dist = tk.v
                v.size = None if not v.mem else (2 if tk.v == 'NEAR' else 4)
            else:
                v.size = sz
            return v
        # segment override  reg: expr   or  segname: expr
        if tk and tk.k == 'id' and self.peek(1) and self.peek(1).v == ':':
            name = tk.v
            if name in SREG:
                self.take()
                self.take()
                v = self.p_unary()
                v = v.copy()
                v.ovr = name
                return v
            s = self.a.lookup(name)
            if s is not None and s.kind in ('seg', 'group'):
                self.take()
                self.take()
                v = self.p_unary()
                v = v.copy()
                v.ovr = ('SEGNAME', name)
                return v
        return self.p_postfix()

    def p_postfix(self):
        v = self.p_primary()
        while True:
            pv = self.peekv()
            if pv == '[':
                self.take()
                w = self.parse()
                self.expect(']')
                w = w.copy()
                if w.reg:
                    r, w.reg = w.reg, None
                    self.addreg(w, r)
                w.mem = True
                w.hasbr = True
                v = self.addsub('+', v, w)
                v.mem = True
                v.hasbr = True
            elif pv == '.':
                self.take()
                w = self.p_primary()
                v = self.addsub('+', v, w, field=True)
                if w.size:
                    v.size = w.size
                v.mem = True
            elif pv == '(' and False:
                pass
            else:
                return v

    def p_primary(self):
        tk = self.peek()
        if tk is None:
            raise AsmError('missing operand')
        if tk.k == 'op' and tk.v == '(':
            self.take()
            v = self.parse()
            self.expect(')')
            return v
        if tk.k == 'op' and tk.v == '[':
            self.take()
            v = self.parse()
            self.expect(']')
            v = v.copy()
            if v.reg:
                r, v.reg = v.reg, None
                self.addreg(v, r)
            v.mem = True
            v.hasbr = True
            return v
        self.take()
        if tk.k == 'num':
            return Val(tk.v)
        if tk.k == 'str':
            s = tk.v
            n = 0
            for ch in s:
                n = (n << 8) | (ord(ch) & 0xFF)
            v = Val(n)
            v.strlen = len(s)
            return v
        if tk.k == 'op':
            if tk.v == '$':
                return self.a.here_val()
            raise AsmError(f'unexpected {tk.s!r}')
        name = tk.v
        if name == '$':
            return self.a.here_val()
        if name in REGS:
            v = Val()
            v.reg = name
            return v
        return self.a.sym_val(name)

    # ---- helpers
    def const(self, n, *srcs):
        v = Val(n)
        v.unknown = any(s.unknown for s in srcs)
        v.fwd = any(s.fwd for s in srcs)
        return v

    def sizeop(self, op):
        # operand: symbol name (possibly struc)
        tk = self.peek()
        if tk and tk.v == '(':
            self.take()
            v = self.sizeop(op)
            self.expect(')')
            return v
        tk = self.take()
        s = self.a.lookup(tk.v)
        r = Val(0)
        if s is None:
            r.unknown = True
            return r
        if s.kind == 'struc':
            n = s.size
            r.num = n if op in ('SIZE', 'TYPE') else 1
            return r
        if s.kind in ('var', 'extern'):
            unit = s.size or 0
            cnt = s.extra or 1
            if op == 'TYPE':
                r.num = unit
            elif op == 'LENGTH':
                r.num = cnt
            else:
                r.num = unit * cnt
            return r
        if s.kind == 'label':
            r.num = 0xFFFF if s.dist == 'NEAR' else 0xFFFE
            return r
        r.num = 0
        return r

    def binop(self, op, v, w):
        if not (v.is_const() and w.is_const()):
            # allow e.g. (LAST-FIRST)/2 already reduced; otherwise complain
            if op in ('/', '*') and v.rel is not None and w.is_const() and op == '/':
                pass
            raise AsmError(f'{op} on relocatable value')
        a, b = v.num, w.num
        if op == '*':
            r = a * b
        elif op == '/':
            if b == 0:
                if v.unknown or w.unknown:
                    r = 0
                else:
                    raise AsmError('division by zero')
            else:
                r = int(a / b) if (a < 0) != (b < 0) else a // b
        elif op == 'MOD':
            r = a % b if b else 0
        elif op == 'SHL':
            r = a << b
        elif op == 'SHR':
            r = (a & 0xFFFFFFFF) >> b
        elif op == 'AND':
            r = a & b
        elif op == 'OR':
            r = a | b
        elif op == 'XOR':
            r = a ^ b
        else:
            raise AsmError(op)
        return self.const(r, v, w)

    def addsub(self, op, v, w, field=False):
        r = v.copy()
        if op == '+':
            r.num = v.num + w.num
            if r.reg and (w.reg or w.base or w.index or w.mem or w.hasbr or field):
                reg, r.reg = r.reg, None
                self.addreg(r, reg)
            for reg in (w.base, w.index):
                if reg:
                    self.addreg(r, reg)
            if w.reg:
                self.addreg(r, w.reg)
                r.reg = None
            if v.reg and r.reg and (w.mem or w.hasbr or field):
                # (not when the first case above has moved it already:
                # [BX+VAR] would get BX twice)
                r.reg = None
                self.addreg(r, v.reg)
            if w.rel is not None:
                if r.rel is not None:
                    raise AsmError('adding two relocatable values')
                r.rel = w.rel
                r.vseg = w.vseg
                if not r.size:
                    r.size = w.size
                if w.label:
                    r.label = True
                    r.dist = w.dist
            if w.segrel is not None:
                r.segrel = w.segrel
            r.mem = v.mem or w.mem
            r.hasbr = v.hasbr or w.hasbr
            if not r.size and w.size and not field:
                r.size = w.size
            if w.ovr and not r.ovr:
                r.ovr = w.ovr
        else:
            if w.base or w.index or w.reg:
                raise AsmError('subtracting a register')
            if w.rel is not None:
                if v.rel == w.rel:
                    r.rel = None
                    r.vseg = None
                    r.mem = False
                    r.label = False
                    r.size = None
                    r.dist = None
                elif v.unknown or w.unknown:
                    r.rel = None
                    r.mem = False
                    r.label = False
                else:
                    raise AsmError('subtracting values in different segments')
            r.num = v.num - w.num
        r.unknown = v.unknown or w.unknown
        r.fwd = v.fwd or w.fwd
        # "name+2" is still a variable; "576-name" can only be a number
        r.fwdbare = v.fwdbare or (w.fwdbare and op == '+')
        return r

    def addreg(self, r, reg):
        if reg in ('BX', 'BP'):
            if r.base:
                raise AsmError('two base registers')
            r.base = reg
        elif reg in ('SI', 'DI'):
            if r.index:
                raise AsmError('two index registers')
            r.index = reg
        else:
            raise AsmError(f'{reg} cannot address memory')
        r.mem = True


# ----------------------------------------------------------------------------
# segments


class Segment:
    def __init__(self, name, align, combine, cls):
        self.name = name
        self.align = align
        self.combine = combine
        self.cls = cls
        self.data = bytearray()
        self.init = bytearray()   # 1 where a byte was explicitly initialised
        self.pc = 0
        self.size = 0
        self.fixups = []          # (offset, kind, target, addend)
        self.relmask = []         # (offset, n): relative jump displacements
        self.order = 0

    def emit(self, b, initialised=True):
        end = self.pc + len(b)
        if end > len(self.data):
            self.data.extend(bytes(end - len(self.data)))
            self.init.extend(bytes(end - len(self.init)))
        self.data[self.pc:end] = b
        if initialised:
            self.init[self.pc:end] = b'\x01' * len(b)
        self.pc = end
        if end > self.size:
            self.size = end

    def reserve(self, n):
        end = self.pc + n
        if end > len(self.data):
            self.data.extend(bytes(end - len(self.data)))
            self.init.extend(bytes(end - len(self.init)))
        self.pc = end
        if end > self.size:
            self.size = end


class Struc:
    def __init__(self, name):
        self.name = name
        self.size = 0
        self.fields = []     # (name, offset, size, init-bytes-text)
        self.init = bytearray()
        self.mask = bytearray()  # 1 where a field has an initial value (not ?)


class MacroDef:
    def __init__(self, name, params, body):
        self.name = name
        self.params = params
        self.body = body     # list of raw lines


class Frame:
    """An input source: list of lines with a position."""
    def __init__(self, lines, where, kind='file', locals_map=None):
        self.lines = lines
        self.pos = 0
        self.where = where    # (filename, first line number)
        self.kind = kind
        self.exitm = False


# ----------------------------------------------------------------------------
# assembler


class Assembler:
    def __init__(self, path, defines=None, incdirs=None, trace=False, multipass=True):
        self.path = path
        self.defines = defines or {}
        self.incdirs = incdirs or [os.path.dirname(os.path.abspath(path))]
        self.trace = trace
        self.multipass = multipass     # TASM /m
        self.shape = {}         # instruction seq -> decided size/shape from pass 1
        self.pass1_syms = None
        self.prev_syms = None
        self.prev_pc = {}
        self.emit = False
        self.fwd_checks = []
        self.regreg_form = 'reg_rm'
        self.linemap = {}
        self.collect = None      # a list: keep going and collect errors
        self.local_names = {}    # (site, macro, local, n) -> ??nnnn, kept across passes

    # ------------------------------------------------------------ file access
    def find_include(self, name):
        base = name.replace('\\', '/').split('/')[-1]
        for d in self.incdirs:
            for f in os.listdir(d):
                if f.upper() == base.upper():
                    return os.path.join(d, f)
        raise AsmError(f'include file {name} not found')

    def read_lines(self, path):
        raw = open(path, 'rb').read()
        text = raw.decode('latin-1')
        text = text.split('\x1a')[0]
        return text.replace('\r\n', '\n').replace('\r', '\n').split('\n')

    # ------------------------------------------------------------ driver
    def assemble(self, max_passes=8):
        """Run TASM's passes.  Pass 0 knows nothing ahead; each following pass
        sizes forward jumps from the addresses of the pass before.  When a
        pass's choices hold up with its own addresses, a last pass emits the
        bytes with those choices frozen."""
        self.prev_syms = None
        self.prev_pc = {}
        self.emit = False
        self.run_pass(0)
        if not self.multipass:
            # plain TASM: one pass; pass 1 here repeats it knowing what each
            # symbol is, but sizes every forward reference for the worst case
            max_passes = 1
        for k in range(1, max_passes + 1):
            self.prev_syms = self.syms
            self.prev_pc = self.seq_pc
            self.shape = {}
            self.fwd_checks = []
            self.run_pass(k)
            bad = self.broken_fwd()
            if not bad:
                break
        else:
            raise AsmError(f'no stable layout after {max_passes} passes: {bad[:5]}')
        self.passes = k
        self.pass1_syms = self.prev_syms = self.syms
        self.prev_pc = self.seq_pc
        self.emit = True
        self.run_pass(k + 1)
        return self

    def broken_fwd(self):
        """Forward jumps sized short whose target moved out of reach."""
        bad = []
        for seq, name, segname in self.fwd_checks:
            s = self.syms.get(name)
            at = self.seq_pc.get(seq)
            if s is None or at is None or s.kind != 'label':
                continue
            if s.seg != segname:
                continue
            d = s.value - (at[1] + 2)
            if not -128 <= d <= 127:
                bad.append((name, d))
        return bad

    def reset(self):
        self.syms = {}
        self.segments = {}
        self.segorder = []
        self.segstack = []
        self.struc = None
        self.assume = {'CS': None, 'DS': None, 'ES': None, 'SS': None}
        self.jumps = False
        self.locals_prefix = None
        self.local_scope = 0
        self.proc_stack = []
        self.cpu = 86
        self.frames = []
        self.cond = []            # stack of [active, taken, parent_active]
        self.seq = 0              # instruction sequence number
        self.seq_pc = {}          # seq -> (segment, pc) in this pass
        self.local_seen = {}
        self.publics = set()
        self.externs = {}
        self.entry = None
        self.lineinfo = []        # (seg, offset, file, line, text)
        self.cur_where = ('?', 0)
        self.comment_delim = None
        self.macro_rec = None     # [name, params, body, depth, kind]
        self.symseq = 0
        self.ended = False
        # TASM keeps macros across its passes, so a macro defined late in
        # the file is usable earlier from the second pass on
        if self.prev_syms:
            for k, v in self.prev_syms.items():
                if v.kind == 'macro':
                    self.syms[k] = v
        for k, v in self.defines.items():
            s = Sym(k.upper(), 'const')
            s.value = v
            s.redef = True
            s.defined_pass = 99
            self.syms[k.upper()] = s

    def run_pass(self, p):
        self.passno = p
        self.reset()
        self.frames.append(Frame(self.read_lines(self.path), os.path.basename(self.path)))
        while self.frames and not self.ended:
            fr = self.frames[-1]
            if fr.pos >= len(fr.lines) or fr.exitm:
                self.frames.pop()
                if fr.kind == 'macro':
                    self.local_scope_pop_macro(fr)
                continue
            line = fr.lines[fr.pos]
            fr.pos += 1
            if fr.kind == 'file':
                self.cur_where = (fr.where, fr.pos)
                self.cur_text = line
            if self.emit and self.segstack and self.struc is None                     and self.macro_rec is None and self.comment_delim is None                     and strip_comment(line).strip():
                sg = self.segments[self.segstack[-1]]
                self.linemap.setdefault(sg.name, []).append(
                    (sg.pc, self.cur_where[0], self.cur_where[1], line))
            try:
                self.do_line(line)
            except AsmError as e:
                if self.collect is not None:
                    self.collect.append((self.passno, self.cur_where, str(e), line.strip()))
                    continue
                raise AsmError(f'{self.cur_where[0]}:{self.cur_where[1]}: {e}\n    {self.cur_text.strip()}'
                               + (f'\n    (expanded: {line.strip()})' if line != self.cur_text else ''))
        if self.segstack:
            pass

    def local_scope_pop_macro(self, fr):
        pass

    # ------------------------------------------------------------ symbols
    def lookup(self, name):
        name = self.mangle(name)
        return self.syms.get(name)

    def mangle(self, name):
        if self.locals_prefix and name.startswith(self.locals_prefix) and len(name) > len(self.locals_prefix):
            return f'{name}#{self.local_scope}'
        return name

    def define(self, name, kind, **kw):
        key = self.mangle(name)
        s = self.syms.get(key)
        if s is not None and not (s.kind == kind and (s.redef or kind in ('const',))) and s.kind != 'extern':
            if s.kind == 'const' and kind == 'const':
                pass
            elif s.kind == 'seg' and kind == 'seg':
                return s
            else:
                raise AsmError(f'symbol {name} already defined as {s.kind}')
        if s is None or s.kind == 'extern' and kind != 'extern':
            s = Sym(key, kind)
            self.syms[key] = s
        s.kind = kind
        for k, v in kw.items():
            setattr(s, k, v)
        s.defined_pass = self.passno
        s.defseq = self.symseq
        self.symseq += 1
        return s

    def sym_val(self, name):
        s = self.lookup(name)
        unknown = False
        if s is None:
            s = self.prev_syms.get(self.mangle(name)) if self.prev_syms else None
            if s is None:
                if self.passno == 0:
                    v = Val(0)
                    v.unknown = True
                    v.label = True
                    return v
                raise AsmError(f'undefined symbol {name}')
            unknown = True
        v = self.val_of(s)
        if unknown:
            # a forward reference: TASM knows it from its previous pass
            v.fwd = True
            v.fwdbare = True
        return v

    def val_of(self, s):
        v = Val()
        k = s.kind
        if k == 'const':
            v.num = s.value
        elif k == 'text':
            raise AsmError(f'text macro {s.name} in expression')
        elif k in ('label', 'var'):
            v.num = s.value
            v.rel = ('S', s.seg)
            v.vseg = s.seg
            if k == 'label':
                v.label = True
                v.dist = s.dist
            else:
                v.mem = True
                v.size = s.size
        elif k == 'extern':
            v.rel = ('X', s.name)
            v.vseg = s.seg
            if s.size in (0xFF02, 0xFF05) or s.dist:
                v.label = True
                v.dist = s.dist or ('NEAR' if s.size == 0xFF02 else 'FAR')
            else:
                v.mem = True
                v.size = s.size
        elif k == 'offs':
            v.num = s.value
            v.rel = s.seg
            v.isoffset = True
        elif k == 'seg':
            v.segrel = ('S', s.name)
        elif k == 'group':
            v.segrel = ('G', s.name)
        elif k == 'field':
            v.num = s.value
            v.size = s.size
        elif k == 'struc':
            v.num = s.size
        else:
            raise AsmError(f'{s.name} ({k}) in expression')
        return v

    def here_val(self):
        seg = self.curseg()
        v = Val(seg.pc)
        v.rel = ('S', seg.name)
        v.vseg = seg.name
        v.label = True
        v.dist = 'NEAR'
        return v

    def type_size(self, name):
        if name in SIZE_KW:
            return SIZE_KW[name] if SIZE_KW[name] < 0xFF00 else None
        s = self.lookup(name)
        if s is None and self.prev_syms:
            s = self.prev_syms.get(name)       # a structure defined further down
        if s and s.kind == 'struc':
            return s.size
        if s is None and self.passno == 0:
            return None
        raise AsmError(f'unknown type {name}')

    def curseg(self):
        if not self.segstack:
            raise AsmError('code or data outside a segment')
        return self.segments[self.segstack[-1]]

    # ------------------------------------------------------------ text equates
    def subst_text(self, toks, depth=0):
        if depth > 20:
            raise AsmError('text macro recursion')
        out = []
        changed = False
        for t in toks:
            if t.k == 'id':
                s = self.syms.get(t.v)
                if s is not None and s.kind == 'text':
                    out.extend(tokenize(s.text))
                    changed = True
                    continue
            out.append(t)
        if changed:
            return self.subst_text(out, depth + 1)
        return out

    # ------------------------------------------------------------ conditionals
    def active(self):
        return all(c[0] for c in self.cond)

    # ------------------------------------------------------------ line handling
    def do_line(self, line):
        # block comment
        if self.comment_delim is not None:
            if self.comment_delim in line:
                self.comment_delim = None
            return
        # macro/rept recording
        if self.macro_rec is not None:
            self.record_macro_line(line)
            return
        text = strip_comment(line)
        if not text.strip():
            return
        toks = tokenize(text)
        if not toks:
            return
        first = toks[0].v if toks[0].k == 'id' else None
        second = toks[1].v if len(toks) > 1 and toks[1].k == 'id' else None

        # conditionals are processed even when inactive
        if first in ('IF', 'IFE', 'IFDEF', 'IFNDEF', 'IFB', 'IFNB', 'IFIDN', 'IFIDNI',
                     'IFDIF', 'IFDIFI', 'IF1', 'IF2'):
            parent = self.active()
            val = self.eval_cond(first, toks[1:], text) if parent else False
            self.cond.append([bool(val), bool(val), parent])
            return
        if first in ('ELSEIF', 'ELSEIFE'):
            c = self.cond[-1]
            if c[1] or not c[2]:
                c[0] = False
            else:
                val = self.eval_cond('IF' if first == 'ELSEIF' else 'IFE', toks[1:], text)
                c[0] = bool(val)
                c[1] = c[1] or c[0]
            return
        if first == 'ELSE':
            c = self.cond[-1]
            c[0] = (not c[1]) and c[2]
            c[1] = True
            return
        if first == 'ENDIF':
            self.cond.pop()
            return
        if not self.active():
            # still have to track MACRO definitions? skipped text is skipped wholesale
            return

        if first == 'COMMENT':
            rest = text.lstrip()[7:].lstrip()
            delim = rest[0]
            if delim in rest[1:]:
                return
            self.comment_delim = delim
            return

        # macro definition
        if second == 'MACRO':
            params = [p.strip().upper() for p in text.split(None, 2)[2].split(',')] if len(text.split(None, 2)) > 2 else []
            params = [p.split(':')[0] for p in params if p]
            self.macro_rec = [toks[0].v, params, [], 0, 'macro']
            return
        if first in ('REPT', 'IRP', 'IRPC'):
            self.macro_rec = [None, toks[1:], [], 0, first]
            return
        if first == 'EXITM':
            for fr in reversed(self.frames):
                if fr.kind == 'macro':
                    fr.exitm = True
                    break
            return
        if first == 'LOCAL':
            return   # handled at expansion time
        if first == 'PURGE':
            return

        # macro invocation
        if first:
            m = self.syms.get(first)
            if m is not None and m.kind == 'macro':
                argtext = text.lstrip()[len(toks[0].s):]
                self.expand_macro(m.extra, argtext)
                return
        # label: macro
        if len(toks) >= 3 and toks[1].v == ':' and toks[2].k == 'id':
            m = self.syms.get(toks[2].v)
            if m is not None and m.kind == 'macro':
                self.def_label(toks[0].v, 'NEAR')
                rest = text.split(':', 1)[1].lstrip()
                argtext = rest[len(toks[2].s):]
                self.expand_macro(m.extra, argtext)
                return

        self.statement(toks, text)

    def eval_cond(self, kw, toks, text):
        if kw in ('IF', 'IFE'):
            v = self.eval_toks(self.subst_text(toks))
            r = v.num != 0
            return r if kw == 'IF' else not r
        if kw in ('IFDEF', 'IFNDEF'):
            name = toks[0].v
            d = self.lookup(name) is not None
            return d if kw == 'IFDEF' else not d
        if kw in ('IF1', 'IF2'):
            return (self.passno == 1) == (kw == 'IF1')
        rest = text.split(None, 1)[1] if len(text.split(None, 1)) > 1 else ''
        args = split_args(rest)
        if kw in ('IFB', 'IFNB'):
            a = args[0].strip() if args else ''
            a = strip_angle(a)
            return (a.strip() == '') == (kw == 'IFB')
        a = strip_angle(args[0].strip()) if args else ''
        b = strip_angle(args[1].strip()) if len(args) > 1 else ''
        if kw.endswith('I'):
            a, b = a.upper(), b.upper()
        same = a.strip() == b.strip()
        return same if kw.startswith('IFIDN') else not same

    def record_macro_line(self, line):
        rec = self.macro_rec
        t = strip_comment(line)
        tk = tokenize(t) if t.strip() else []
        f = tk[0].v if tk and tk[0].k == 'id' else None
        s = tk[1].v if len(tk) > 1 and tk[1].k == 'id' else None
        if len(tk) >= 3 and tk[1].v == ':' and tk[2].k == 'id' and tk[2].v == 'ENDM':
            # "LABEL: ENDM"
            if rec[3] == 0:
                rec[2].append(t[:t.index(':') + 1])
                self.macro_rec = None
                self.finish_macro(rec)
                return
            rec[3] -= 1
            rec[2].append(line)
            return
        if s == 'MACRO' or f in ('REPT', 'IRP', 'IRPC'):
            rec[3] += 1
        elif f == 'ENDM' or s == 'ENDM':
            if rec[3] == 0:
                self.macro_rec = None
                self.finish_macro(rec)
                return
            rec[3] -= 1
        rec[2].append(line)

    def finish_macro(self, rec):
        name, params, body, _, kind = rec
        if kind == 'macro':
            s = self.syms.get(name)
            if s is None or s.kind != 'macro':
                s = Sym(name, 'macro')
                self.syms[name] = s
            s.extra = MacroDef(name, params, body)
            return
        if kind == 'REPT':
            n = self.eval_toks(self.subst_text(rec[1])).num
            lines = []
            for _ in range(n):
                lines.extend(body)
            self.push_lines(lines, 'rept')
            return
        if kind in ('IRP', 'IRPC'):
            # IRP param,<a,b,c>
            text = ' '.join(t.s for t in rec[1])
            p, rest = text.split(',', 1)
            p = p.strip().upper()
            rest = strip_angle(rest.strip())
            items = split_args(rest) if kind == 'IRP' else list(rest)
            lines = []
            for it in items:
                lines.extend(subst_params(body, [p], [it.strip()]))
            self.push_lines(lines, 'rept')

    def push_lines(self, lines, kind):
        fr = Frame(lines, self.cur_where, kind)
        self.frames.append(fr)

    def expand_macro(self, md, argtext):
        args = split_args(argtext, blanks=True)
        args = [strip_angle(a.strip()) for a in args]
        body = md.body
        # LOCAL names
        locs = []
        for ln in body:
            t = strip_comment(ln).strip()
            if t.upper().startswith('LOCAL') and (len(t) == 5 or t[5] in ' \t'):
                locs.extend(x.strip().upper() for x in t[5:].split(',') if x.strip())
        names = list(md.params) + locs
        vals = list(args) + [''] * (len(md.params) - len(args))
        vals = vals[:len(md.params)]
        for l in locs:
            # named after the invocation site, so every pass gets the same
            # name even when an earlier pass skipped some macro
            key = (self.cur_where, md.name, l)
            n = self.local_seen.get(key, 0)
            self.local_seen[key] = n + 1
            name = self.local_names.get(key + (n,))
            if name is None:
                name = f'??{len(self.local_names):04X}'
                self.local_names[key + (n,)] = name
            vals.append(name)
        lines = subst_params(body, names, vals)
        self.push_lines(lines, 'macro')

    # ------------------------------------------------------------ statements
    def statement(self, toks, text):
        # label:
        if len(toks) >= 2 and toks[0].k == 'id' and toks[1].v == ':' and \
                not (toks[0].v in SREG):
            self.def_label(toks[0].v, 'NEAR')
            toks = toks[2:]
            if not toks:
                return
            text = None
        first = toks[0]
        second = toks[1] if len(toks) > 1 else None
        fv = first.v if first.k == 'id' else None
        sv = second.v if second is not None and second.k == 'id' else None

        # named directives: NAME DIRECTIVE ...
        if sv is not None and fv is not None:
            if sv in ('EQU',):
                return self.do_equ(fv, toks[2:], text)
            if sv in DATA_SIZE:
                return self.do_data(fv, DATA_SIZE[sv], toks[2:])
            if sv == 'SEGMENT':
                return self.do_segment(fv, toks[2:])
            if sv == 'ENDS':
                return self.do_ends(fv)
            if sv == 'PROC':
                return self.do_proc(fv, toks[2:])
            if sv == 'ENDP':
                return self.do_endp()
            if sv == 'LABEL':
                return self.do_labeldir(fv, toks[2:])
            if sv in ('STRUC', 'STRUCT'):
                return self.do_struc(fv)
            if sv == 'GROUP':
                s = self.define(fv, 'group')
                s.extra = [t.v for t in toks[2:] if t.k == 'id']
                return
            if sv == 'RECORD':
                raise AsmError('RECORD not supported')
            st = self.lookup(sv)
            if st is not None and st.kind == 'struc' and fv not in self.instr_names():
                return self.do_struc_inst(fv, st, toks[2:])
        if second is not None and second.v == '=' and fv is not None:
            return self.do_assign(fv, toks[2:])

        if fv is None:
            raise AsmError(f'syntax error at {first.s}')
        if fv in DATA_SIZE:
            return self.do_data(None, DATA_SIZE[fv], toks[1:])
        st = self.lookup(fv)
        if st is not None and st.kind == 'struc':
            return self.do_struc_inst(None, st, toks[1:])
        if fv.startswith('.'):
            return self.do_dotdir(fv)
        h = getattr(self, 'd_' + fv, None)
        if h is not None:
            return h(toks[1:], text)
        return self.instruction(fv, toks[1:])

    # ---- directives
    def do_dotdir(self, name):
        if name in ('.286', '.286P', '.286C'):
            self.cpu = 286
        elif name in ('.8086', '.8087', '.186', '.287'):
            if name == '.186':
                self.cpu = 186
            elif name == '.8086':
                self.cpu = 86
        elif name in ('.386', '.386P'):
            self.cpu = 386
        else:
            raise AsmError(f'unsupported {name}')

    def d_JUMPS(self, toks, text):
        self.jumps = True

    def d_NOJUMPS(self, toks, text):
        self.jumps = False

    def d_LOCALS(self, toks, text):
        self.locals_prefix = toks[0].s.upper() if toks else '@@'
        if toks and toks[0].k == 'id':
            self.locals_prefix = toks[0].v

    def d_NOLOCALS(self, toks, text):
        self.locals_prefix = None

    def d_MASM(self, toks, text):
        pass

    def d_INCLUDELIB(self, toks, text):
        pass

    def d_PAGE(self, toks, text):
        pass

    def d_TITLE(self, toks, text):
        pass

    def d_DISPLAY(self, toks, text):
        pass

    def d_NAME(self, toks, text):
        pass

    def d_INCLUDE(self, toks, text):
        fn = text.split(None, 1)[1].strip()
        p = self.find_include(fn)
        self.frames.append(Frame(self.read_lines(p), os.path.basename(p)))

    def d_END(self, toks, text):
        if toks:
            v = self.eval_toks(self.subst_text(toks))
            self.entry = v
        self.ended = True

    def d_PUBLIC(self, toks, text):
        for t in toks:
            if t.k == 'id':
                self.publics.add(t.v)

    def d_EXTRN(self, toks, text):
        # extrn a:byte, b, c:near
        i = 0
        seg = self.segstack[-1] if self.segstack else None
        while i < len(toks):
            t = toks[i]
            if t.k != 'id':
                i += 1
                continue
            name = t.v
            size = None
            dist = None
            if i + 2 < len(toks) + 1 and i + 1 < len(toks) and toks[i + 1].v == ':':
                ty = toks[i + 2].v
                if ty in ('NEAR', 'PROC'):
                    dist = 'NEAR'
                elif ty == 'FAR':
                    dist = 'FAR'
                elif ty == 'ABS':
                    size = None
                else:
                    size = self.type_size(ty)
                i += 3
            else:
                i += 1
            s = self.syms.get(name)
            if s is not None and s.kind != 'extern':
                continue   # defined here after all
            s = Sym(name, 'extern')
            # TASM lets "extrn x" go without a type; the sources use such
            # names as words
            s.size = size if (size or dist) else 2
            s.dist = dist
            s.seg = seg
            s.defined_pass = self.passno
            self.syms[name] = s
            self.externs[name] = s
    d_EXTERN = d_EXTRN

    def d_ASSUME(self, toks, text):
        i = 0
        while i < len(toks):
            if toks[i].k == 'id' and i + 2 < len(toks) + 1 and i + 1 < len(toks) and toks[i + 1].v == ':':
                r = toks[i].v
                tgt = toks[i + 2].v
                if tgt == 'NOTHING':
                    self.assume[r] = None
                else:
                    s = self.lookup(tgt)
                    if s is None:
                        # forward segment name: assume it will be a segment
                        self.assume[r] = tgt
                    elif s.kind == 'seg' or s.kind == 'group':
                        self.assume[r] = tgt
                    else:
                        self.assume[r] = tgt
                i += 3
            elif toks[i].v == 'NOTHING':
                for r in self.assume:
                    self.assume[r] = None
                i += 1
            else:
                i += 1

    def d_ALIGN(self, toks, text):
        n = self.eval_toks(toks).num
        seg = self.curseg()
        pad = (-seg.pc) % n
        if seg.cls and 'CODE' in (seg.cls or '').upper():
            seg.emit(b'\x90' * pad)
        else:
            seg.emit(b'\x00' * pad) if pad else None

    def d_EVENDATA(self, toks, text):
        seg = self.curseg()
        if seg.pc & 1:
            seg.emit(b'\x00')

    def d_EVEN(self, toks, text):
        seg = self.curseg()
        if seg.pc & 1:
            if (seg.cls or '').upper() == 'CODE':
                seg.emit(b'\x90')
            else:
                seg.emit(b'\x00')

    def d_ORG(self, toks, text):
        v = self.eval_toks(toks)
        seg = self.curseg()
        seg.pc = v.num
        if seg.pc > seg.size:
            seg.reserve(0)

    def d_ENDS(self, toks, text):
        return self.do_ends(None)

    def d_ENDP(self, toks, text):
        return self.do_endp()

    def d_ENDM(self, toks, text):
        raise AsmError('ENDM without MACRO')

    def do_equ(self, name, toks, text):
        """A constant when the expression is a number, an OFFSET alias when it
        is a plain offset (TASM evaluates "cos equ offset sin+1024" on the
        spot), and a text macro otherwise (registers, PTR expressions)."""
        raw = toks
        s0 = self.syms.get(name)
        try:
            st = self.subst_text(raw)
            if len(st) == 1 and st[0].k == 'id' and st[0].v in REGS:
                raise AsmError('register')
            if any(t.k == 'id' and t.v == 'PTR' for t in st):
                raise AsmError('text')
            v = self.eval_toks(st)
            if v.unknown:
                raise AsmError('not yet known')
            if v.is_const():
                if s0 is not None and s0.kind == 'const':
                    s0.value = v.num
                    return
                self.define(name, 'const', value=v.num)
                return
            if v.rel is not None and v.isoffset and not (v.base or v.index or v.reg):
                s = self.syms.get(name)
                if s is None or s.kind != 'offs':
                    s = self.define(name, 'offs')
                s.value, s.seg, s.extra = v.num, v.rel, False
                s.defined_pass = self.passno
                return
            raise AsmError('not a value')
        except AsmError:
            txt = ' '.join(t.s for t in raw)
            s = self.syms.get(name)
            if s is None or s.kind != 'text':
                s = Sym(name, 'text')
                self.syms[name] = s
            s.text = txt
            s.defined_pass = self.passno

    def do_assign(self, name, toks):
        v = self.eval_toks(self.subst_text(toks))
        if not v.is_const():
            # e.g.  x = $ - start   inside one segment gives a const; otherwise keep the offset
            if v.rel is not None and not v.mem:
                s = self.syms.get(name)
                if s is None or s.kind != 'offs':
                    s = self.define(name, 'offs')
                s.value, s.seg, s.extra = v.num, v.rel, v.label
                s.redef = True
                s.defined_pass = self.passno
                return
            raise AsmError('= needs a constant')
        s = self.syms.get(name)
        if s is None or s.kind != 'const':
            s = self.define(name, 'const', value=v.num)
        s.value = v.num
        s.redef = True
        s.defined_pass = self.passno

    def do_segment(self, name, toks):
        if self.struc is not None:
            raise AsmError('segment inside struc')
        align, combine, cls = 16, 'PRIVATE', None
        for t in toks:
            if t.k == 'str':
                cls = t.v.upper()
            elif t.k == 'id':
                u = t.v
                if u in ('BYTE', 'WORD', 'DWORD', 'PARA', 'PAGE'):
                    align = {'BYTE': 1, 'WORD': 2, 'DWORD': 4, 'PARA': 16, 'PAGE': 256}[u]
                elif u in ('PUBLIC', 'STACK', 'COMMON', 'MEMORY', 'PRIVATE', 'AT'):
                    combine = u
        seg = self.segments.get(name)
        if seg is None:
            seg = Segment(name, align, combine, cls)
            seg.order = len(self.segorder)
            self.segments[name] = seg
            self.segorder.append(name)
            s = self.syms.get(name)
            if s is None or s.kind != 'seg':
                s = Sym(name, 'seg')
                self.syms[name] = s
            s.defined_pass = self.passno
        self.segstack.append(name)

    def do_ends(self, name):
        if self.struc is not None and (name is None or name == self.struc.name):
            return self.end_struc()
        if not self.segstack:
            raise AsmError('ENDS without SEGMENT')
        if name is not None and self.segstack[-1] != name:
            raise AsmError(f'ENDS {name} closes {self.segstack[-1]}')
        self.segstack.pop()

    def do_proc(self, name, toks):
        dist = 'NEAR'
        for t in toks:
            if t.v in ('FAR', 'NEAR'):
                dist = t.v
        self.def_label(name, dist)
        self.proc_stack.append((name, dist))

    def do_endp(self):
        if self.proc_stack:
            self.proc_stack.pop()

    def do_labeldir(self, name, toks):
        ty = toks[0].v
        if ty in ('NEAR', 'FAR', 'PROC'):
            self.def_label(name, 'FAR' if ty == 'FAR' else 'NEAR', nonlocal_reset=False)
        else:
            seg = self.curseg()
            self.define(name, 'var', value=seg.pc, seg=seg.name, size=self.type_size(ty), extra=1)

    def def_label(self, name, dist, nonlocal_reset=True):
        if self.struc is not None:
            raise AsmError('label inside struc')
        seg = self.curseg()
        is_local = self.locals_prefix and name.startswith(self.locals_prefix)
        if not is_local and nonlocal_reset:
            self.local_scope += 1
        s = self.define(name, 'label', value=seg.pc, seg=seg.name, dist=dist)
        self.check_stable(s)
        self.lineinfo.append((seg.name, seg.pc, self.cur_where, name))

    def check_stable(self, s):
        if self.emit and self.pass1_syms is not None:
            p = self.pass1_syms.get(s.name)
            if p is not None and p.kind in ('label', 'var') and (p.value != s.value or p.seg != s.seg):
                raise AsmError(f'phase error: {s.name} moved from {p.value:#x} to {s.value:#x}')

    # ---- structures
    def do_struc(self, name):
        self.struc = Struc(name)
        s = self.define(name, 'struc', size=0)
        s.extra = self.struc

    def end_struc(self):
        st = self.struc
        self.struc = None
        s = self.syms[st.name]
        s.size = st.size
        s.extra = st

    def do_struc_inst(self, name, st, toks):
        # name STRUCNAME <...>  or  name STRUCNAME n DUP (<...>)
        seg = self.curseg()
        stobj = st.extra
        count = 1
        dup = [i for i, t in enumerate(toks) if t.k == 'id' and t.v == 'DUP']
        if dup:
            count = self.eval_toks(toks[:dup[0]]).num
            toks = [t for t in toks[dup[0] + 1:] if t.v not in ('(', ')')]
        inits = []
        if toks and toks[0].v == '<':
            depth, cur, j = 0, [], 1
            txt = ' '.join(t.s for t in toks)
            inner = txt[txt.index('<') + 1:txt.rindex('>')]
            inits = split_args(inner) if inner.strip() else []
        if name:
            self.define(name, 'var', value=seg.pc, seg=seg.name, size=stobj.size, extra=count)
        if count != 1:
            if any(x.strip() and x.strip() != '?' for x in inits):
                raise AsmError('structure DUP with field values')
            for _ in range(count):
                self.emit_struc_default(seg, stobj)
            return
        if any(x.strip() for x in inits):
            # field overrides
            for idx, (fname, off, fsize, ftoks, fcount) in enumerate(stobj.fields):
                if idx < len(inits) and inits[idx].strip():
                    self.emit_data_items(fsize, tokenize(inits[idx]))
                else:
                    seg.emit(bytes(stobj.init[off:off + fsize * fcount]))
        else:
            self.emit_struc_default(seg, stobj)

    def emit_struc_default(self, seg, stobj):
        """One instance with the structure's own initial values; fields
        declared ? stay uninitialised."""
        i = 0
        n = len(stobj.init)
        while i < n:
            j = i
            m = stobj.mask[i] if i < len(stobj.mask) else 1
            while j < n and (stobj.mask[j] if j < len(stobj.mask) else 1) == m:
                j += 1
            seg.emit(bytes(stobj.init[i:j]), initialised=bool(m))
            i = j

    # ---- data
    def do_data(self, name, size, toks):
        toks = self.subst_text(toks)
        if self.struc is not None:
            st = self.struc
            off = st.size
            # evaluate count to know the field length
            before = len(st.init)
            fake = Segment('__struc', 1, None, None)
            fake.pc = 0
            saved = self.segstack
            self.segments['__struc'] = fake
            self.segstack = saved + ['__struc']
            try:
                n = self.emit_data_items(size, toks)
            finally:
                self.segstack = saved
                del self.segments['__struc']
            st.init.extend(fake.data)
            st.mask.extend(fake.init[:len(fake.data)])
            st.size += len(fake.data)
            if name:
                s = self.define(name, 'field', value=off, size=size)
                st.fields.append((name, off, size, toks, len(fake.data) // size if size else 1))
            return
        seg = self.curseg()
        start = seg.pc
        if name:
            s = self.define(name, 'var', value=seg.pc, seg=seg.name, size=size, extra=1)
            self.check_stable(s)
        n = self.emit_data_items(size, toks)
        if name:
            s.extra = max(1, n)

    def emit_data_items(self, size, toks):
        """Emit a comma separated list; returns the number of units of the
        first item (for LENGTH)."""
        items = split_toks(toks, ',')
        first_count = None
        for it in items:
            c = self.emit_data_item(size, it)
            if first_count is None:
                first_count = c
        return first_count or 1

    def emit_data_item(self, size, it):
        seg = self.curseg()
        if not it:
            raise AsmError('empty data item')
        # n DUP (...)
        for k, t in enumerate(it):
            if t.k == 'id' and t.v == 'DUP':
                cnt = self.eval_toks(it[:k]).num
                inner = it[k + 1:]
                if inner and inner[0].v == '(' and inner[-1].v == ')':
                    inner = inner[1:-1]
                if len(inner) == 1 and inner[0].v == '?':
                    seg.reserve(cnt * size) if False else None
                    self.emit_uninit(cnt * size)
                    return cnt
                # emit once, then replicate the bytes (and fixups)
                pc0 = seg.pc
                nfix = len(seg.fixups)
                self.emit_data_items(size, inner)
                chunk = bytes(seg.data[pc0:seg.pc])
                initm = bytes(seg.init[pc0:seg.pc])
                fixes = seg.fixups[nfix:]
                ln = seg.pc - pc0
                for r in range(1, cnt):
                    base = seg.pc
                    seg.emit(chunk)
                    seg.init[base:base + ln] = initm
                    for (o, kd, tg, ad) in fixes:
                        seg.fixups.append((o + r * ln, kd, tg, ad))
                if cnt == 0:
                    seg.pc = pc0
                    del seg.fixups[nfix:]
                return cnt * (ln // size if size else 1)
        if len(it) == 1 and it[0].v == '?':
            self.emit_uninit(size)
            return 1
        if len(it) == 1 and it[0].k == 'str' and size == 1:
            seg.emit(it[0].v.encode('latin-1'))
            return len(it[0].v)
        if len(it) == 1 and it[0].k == 'str' and size > 1 and len(it[0].v) > 2:
            raise AsmError('string too long')
        v = self.eval_toks(it)
        self.emit_value(v, size)
        return 1

    def emit_uninit(self, n):
        seg = self.curseg()
        seg.emit(bytes(n), initialised=False)

    def emit_value(self, v, size):
        seg = self.curseg()
        if v.reg or v.base or v.index:
            raise AsmError('register in data')
        if size == 1:
            if v.rel is not None or v.segrel is not None:
                raise AsmError('relocatable byte')
            n = v.num
            if not (-256 < n < 256) and not v.unknown:
                raise AsmError(f'value {n} does not fit a byte')
            seg.emit(bytes([n & 0xFF]))
        elif size == 2:
            if v.segrel is not None:
                # a segment not seen yet: TASM writes its fixup late
                self.fixup(seg, 'SEGL' if v.fwd else 'SEG', v.segrel, 0)
                seg.emit(b'\0\0')
            elif v.rel is not None:
                self.fixup(seg, 'OFF', v.rel, v.num)
                seg.emit((v.num & 0xFFFF).to_bytes(2, 'little'))
            else:
                seg.emit((v.num & 0xFFFF).to_bytes(2, 'little'))
        elif size == 4:
            if v.rel is not None:
                # far pointer
                self.fixup(seg, 'OFF', v.rel, v.num)
                seg.emit((v.num & 0xFFFF).to_bytes(2, 'little'))
                self.fixup(seg, 'SEG', v.rel, 0)
                seg.emit(b'\0\0')
            elif v.segrel is not None:
                raise AsmError('SEG in DD')
            else:
                seg.emit((v.num & 0xFFFFFFFF).to_bytes(4, 'little'))
        else:
            seg.emit((v.num & ((1 << (8 * size)) - 1)).to_bytes(size, 'little'))

    def fixup(self, seg, kind, target, addend):
        """Record a fixup at seg.pc.  kind OFF (16-bit offset of target),
        SEG (paragraph of target, needs a relocation), REL (16-bit self-relative
        displacement to target, value = target - (pc+2))."""
        seg.fixups.append((seg.pc, kind, target, addend))

    # ---- expression evaluation
    def eval_toks(self, toks):
        p = ExprParser(self, toks)
        v = p.parse()
        if not p.at_end():
            raise AsmError(f'junk after expression: {" ".join(t.s for t in toks[p.i:])}')
        return v

    def instr_names(self):
        return INSTR_NAMES

    # ---- instructions
    def instruction(self, mn, toks):
        from x86enc import encode   # local import keeps this file readable
        toks = self.subst_text(toks)
        if self.struc is not None:
            raise AsmError('instruction inside struc')
        seg = self.curseg()
        seq = self.seq
        self.seq += 1
        self.seq_pc[seq] = (seg.name, seg.pc)
        try:
            encode(self, seg, seq, mn, toks)
        except AsmError as e:
            # a macro defined further down: TASM's first pass skips the line
            if self.passno == 0 and str(e).startswith('unknown instruction'):
                return
            raise


INSTR_NAMES = set()


def split_toks(toks, sep):
    out, cur, depth = [], [], 0
    for t in toks:
        if t.k == 'op' and t.v in '([<':
            depth += 1
        elif t.k == 'op' and t.v in ')]>':
            depth -= 1
        if depth == 0 and t.k == 'op' and t.v == sep:
            out.append(cur)
            cur = []
        else:
            cur.append(t)
    out.append(cur)
    if out == [[]]:
        return []
    return out


def split_args(text, blanks=False):
    """Split macro arguments on top-level commas.  With blanks, whitespace
    between two arguments separates them too, as in a MASM macro call
    (ENDFLASH 19 IF FASTFLASH! passes 19)."""
    out, cur, depth, q = [], [], 0, None
    gap = False
    for c in text:
        if q:
            cur.append(c)
            if c == q:
                q = None
            continue
        if blanks and depth == 0 and c in ' \t':
            if ''.join(cur).strip():
                gap = True
            continue
        if gap and c not in ',;':
            out.append(''.join(cur))
            cur = []
        gap = False
        if c in '\'"':
            q = c
            cur.append(c)
            continue
        if c in '<([':
            depth += 1
        elif c in '>)]':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(''.join(cur))
            cur = []
            continue
        if c == ';' and depth == 0:
            break
        cur.append(c)
    last = ''.join(cur)
    if last.strip() or out:
        out.append(last)
    return out


def strip_angle(a):
    a = a.strip()
    if a.startswith('<') and a.endswith('>'):
        return a[1:-1]
    return a


_idre = re.compile(r"(&?)([A-Za-z_@$?][A-Za-z0-9_@$?]*)(&?)")


def subst_params(lines, names, vals):
    if not names:
        return list(lines)
    table = {n.upper(): v for n, v in zip(names, vals)}
    out = []
    for ln in lines:
        res = []
        i, q = 0, None
        code = strip_comment(ln)
        # substitute outside quotes; inside quotes only &name&
        seg_start = 0
        parts = []
        cur = []
        for c in code:
            if q:
                cur.append(c)
                if c == q:
                    parts.append(('s', ''.join(cur)))
                    cur = []
                    q = None
                continue
            if c in '\'"':
                parts.append(('c', ''.join(cur)))
                cur = [c]
                q = c
                continue
            cur.append(c)
        parts.append(('s' if q else 'c', ''.join(cur)))

        def rep(m):
            name = m.group(2).upper()
            if name in table:
                return table[name]
            return m.group(0)

        def rep_str(m):
            name = m.group(2).upper()
            if (m.group(1) or m.group(3)) and name in table:
                return table[name]
            return m.group(0)

        for kind, txt in parts:
            if kind == 'c':
                res.append(_idre.sub(rep, txt))
            else:
                res.append(_idre.sub(rep_str, txt))
        out.append(''.join(res))
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('-D', action='append', default=[])
    ap.add_argument('-I', action='append', default=[])
    a = ap.parse_args()
    defs = {}
    for d in a.D:
        k, _, v = d.partition('=')
        defs[k] = int(v or '1', 0)
    asm = Assembler(a.src, defs, a.I or None)
    asm.assemble()
    for n in asm.segorder:
        s = asm.segments[n]
        print(f'{n:12s} {s.cls or "":8s} align={s.align:3d} {s.combine:8s} size={s.size:#07x} fixups={len(s.fixups)}')


if __name__ == '__main__':
    # run through the module name so x86enc and this file share one AsmError
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import tasm
    tasm.main()
