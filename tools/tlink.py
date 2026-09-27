#!/usr/bin/env python3
"""A TLINK/LINK work-alike for the objects tasm.py produces: it lays out
the segments and writes the MZ program (tools/build.py links each
generated source this way).

Layout rules:
  * segments are grouped by class, classes in order of first appearance,
    segments within a class in order of first appearance;
  * public segments of the same name are concatenated, each piece aligned to
    its own alignment, in link order;
  * the frame of a segment is the paragraph of its first piece.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class Piece:
    def __init__(self, seg, cls, align, combine, data, init, fixups, owner):
        self.seg = seg
        self.cls = cls
        self.align = align
        self.combine = combine
        self.data = bytes(data)
        self.init = bytes(init) if init is not None else b'\1' * len(data)
        self.fixups = fixups      # (offset, kind, target, addend)
        self.owner = owner        # module object
        self.base = None          # absolute image offset of the piece
        self.relocs = []          # offsets in the piece that hold paragraph values


class Module:
    """A linkable module: pieces + publics.  publics: name -> (segname, offset in piece)."""
    def __init__(self, name):
        self.name = name
        self.pieces = []
        self.publics = {}
        self.entry = None        # (segname, offset)


def module_from_asm(asm, name):
    m = Module(name)
    for sn in asm.segorder:
        s = asm.segments[sn]
        p = Piece(sn, s.cls or '', s.align, s.combine, s.data[:s.size], s.init[:s.size],
                  sorted(s.fixups), m)
        p.linestarts = sorted(set(l[0] for l in asm.linemap.get(sn, [])))
        m.pieces.append(p)
    for key, sym in asm.syms.items():
        if sym.kind in ('label', 'var') and key in asm.publics:
            m.publics[key] = (sym.seg, sym.value)
    if asm.entry is not None:
        e = asm.entry
        m.entry = (e.rel[1], e.num)
    m.externs = dict(asm.externs)
    return m


class Image:
    pass


def layout(modules):
    """Assign every piece an absolute offset.  Returns (segments, order) where
    segments maps segname -> dict(start, frame, pieces, cls)."""
    classes = []
    segs_in_class = {}
    segs = {}
    for m in modules:
        for p in m.pieces:
            key = p.seg if p.combine != 'PRIVATE' else (p.seg, id(m))
            if key not in segs:
                segs[key] = {'name': p.seg, 'cls': p.cls, 'pieces': []}
                if p.cls not in segs_in_class:
                    classes.append(p.cls)
                    segs_in_class[p.cls] = []
                segs_in_class[p.cls].append(key)
            segs[key]['pieces'].append(p)
    pos = 0
    order = []
    for c in classes:
        for key in segs_in_class[c]:
            sg = segs[key]
            first = True
            for p in sg['pieces']:
                a = p.align
                pos = (pos + a - 1) // a * a
                if first:
                    sg['start'] = pos
                    sg['frame'] = pos >> 4
                    first = False
                p.base = pos
                pos += len(p.data)
            sg['end'] = pos
            order.append(key)
    return segs, order


RECORD_LIMIT = [498]   # TASM's LEDATA data limit, bytes (measured: 494..503)


def record_breaks(p):
    """Start offsets of the LEDATA records of a piece.  A record ends where
    uninitialised bytes (DB ?) begin, and before a source line whose bytes
    would take the record past RECORD_LIMIT."""
    gaps = set()
    for i in range(1, len(p.init)):
        if p.init[i] and not p.init[i - 1]:
            gaps.add(i)
    lines = sorted(set(getattr(p, 'linestarts', ()) or ()))
    if not lines:
        return [0] + sorted(gaps)
    L = RECORD_LIMIT[0]
    starts = [0]
    cur = 0
    ends = lines[1:] + [len(p.data)]
    for s, e in zip(lines, ends):
        g = [x for x in gaps if cur < x <= s]
        if g:
            cur = max(g)
            starts.append(cur)
        if e - cur > L and s > cur:
            cur = s
            starts.append(s)
    for x in gaps:
        if x > cur:
            starts.append(x)
    return sorted(set(starts))


def record_of(starts, off):
    import bisect
    return bisect.bisect_right(starts, off) - 1


def link(modules, stack_from=None):
    segs, order = layout(modules)
    byname = {}
    for key in order:
        byname.setdefault(segs[key]['name'], segs[key])
    publics = {}
    for m in modules:
        for n, (sn, off) in m.publics.items():
            publics[n] = (m, sn, off)
    total = max(segs[k]['end'] for k in order)
    img = bytearray(total)
    initmask = bytearray(total)
    relocs = []

    def seg_of(m, sn):
        for p in m.pieces:
            if p.seg == sn:
                return p
        raise KeyError(f'{m.name}: no segment {sn}')

    def resolve(m, target):
        """-> (absolute address, frame) of a fixup target"""
        kind, name = target
        if kind == 'S':
            p = seg_of(m, name)
            sg = byname[name]
            return p.base, sg['frame']
        if kind == 'X':
            if name not in publics:
                raise KeyError(f'unresolved external {name} (in {m.name})')
            pm, sn, off = publics[name]
            p = seg_of(pm, sn)
            return p.base + off, byname[sn]['frame']
        raise ValueError(target)

    for key in order:
        sg = segs[key]
        for p in sg['pieces']:
            img[p.base:p.base + len(p.data)] = p.data
            initmask[p.base:p.base + len(p.data)] = p.init
    for m in modules:
        for p in m.pieces:
            fr = byname[p.seg]['frame'] if p.combine != 'PRIVATE' else p.base >> 4
            for off in p.relocs:
                a = p.base + off
                relocs.append((a, fr, m.name))
            # TASM writes one LEDATA record per run of initialised bytes, and in
            # each record's FIXUPP it lists the fixups to segments it had not
            # seen yet after all the others
            recs = record_breaks(p)
            pend = []
            for off, kind, target, addend in p.fixups:
                a = p.base + off
                tabs, tframe = resolve(m, target)
                if kind == 'OFF':
                    val = tabs - tframe * 16 + addend
                    struct.pack_into('<H', img, a, val & 0xFFFF)
                elif kind in ('SEG', 'SEGL'):
                    struct.pack_into('<H', img, a, tframe)
                    rec = record_of(recs, off)
                    while pend and pend[0][0] != rec:
                        relocs.append(pend.pop(0)[1])
                    if kind == 'SEGL':
                        pend.append((rec, (a, fr, m.name)))
                    else:
                        relocs.append((a, fr, m.name))
                elif kind == 'REL':
                    val = tabs + addend - (a + 2)
                    struct.pack_into('<H', img, a, val & 0xFFFF)
                else:
                    raise ValueError(kind)
                initmask[a:a + 2] = b'\1\1'
            relocs.extend(x[1] for x in pend)
    out = Image()
    out.img = img
    out.init = initmask
    out.relocs = relocs
    out.segs = segs
    out.order = order
    out.byname = byname
    out.publics = publics
    ent = next((m.entry for m in modules if m.entry), None)
    if ent:
        sg = byname[ent[0]]
        p = seg_of(next(m for m in modules if m.entry), ent[0])
        out.cs = sg['frame']
        out.ip = p.base - sg['frame'] * 16 + ent[1]
    st = next((segs[k] for k in order if any(p.combine == 'STACK' for p in segs[k]['pieces'])), None)
    if st:
        out.ss = st['frame']
        out.sp = st['end'] - st['frame'] * 16
    return out


def write_mz(out, path=None, reloc_order=None, version=0x50):
    """Build the EXE bytes the way TLINK lays out its header."""
    img = out.img
    # trailing uninitialised bytes are not stored
    end = len(img)
    while end > 0 and not out.init[end - 1]:
        end -= 1
    stored = bytes(img[:end])
    uninit = len(img) - end
    rel = out.relocs if reloc_order is None else reloc_order
    nrel = len(rel)
    hdr_len = 0x3E + 4 * nrel
    hdr_len = (hdr_len + 511) // 512 * 512
    size = hdr_len + len(stored)
    last = size % 512
    pages = (size + 511) // 512
    minalloc = (uninit + 15) // 16
    h = bytearray(hdr_len)
    struct.pack_into('<2s13H', h, 0, b'MZ', last, pages, nrel, hdr_len // 16, minalloc,
                     0xFFFF, out.ss, out.sp, 0, out.ip, out.cs, 0x3E, 0)
    h[0x1C:0x22] = bytes([0x01, 0x00, 0xFB, version, 0x6A, 0x72])   # TLINK's signature
    for i, (a, fr, _) in enumerate(rel):
        struct.pack_into('<HH', h, 0x3E + 4 * i, a - fr * 16, fr)
    data = bytes(h) + stored
    if path:
        open(path, 'wb').write(data)
    return data
