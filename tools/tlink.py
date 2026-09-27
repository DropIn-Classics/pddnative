#!/usr/bin/env python3
"""A TLINK work-alike for the objects tasm.py produces, plus the library
extractor that recovers the missing .LIB contents from a shipped program.

Layout rules (what TLINK does for these programs):
  * segments are grouped by class, classes in order of first appearance,
    segments within a class in order of first appearance;
  * public segments of the same name are concatenated, each piece aligned to
    its own alignment, in link order (objects first, then library modules);
  * the frame of a segment is the paragraph of its first piece.

The library: the developers' .LIB files are not part of the source release.
What they contributed is still inside every shipped program, so
extract_lib() cuts it out: whatever a reference program holds beyond our own
pieces of each segment is the library's piece, and our fixups to externs say
where each library symbol sits.  The result is a Library object that link()
treats like one more module.
"""
import json, os, struct, sys
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
        self.relocs = []          # for library pieces: offsets holding paragraph values


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


# ---------------------------------------------------------------- library extraction

def extract_lib(modules, ref, name='LIBRARY'):
    """Recover the library contribution from a reference program.

    modules: our own modules, in link order.
    ref:     mzinfo.load() of the reference program.

    The library may sit between our modules as well as after them (in the
    table programs the sine table and fonts come between FANTASIE and
    BALLCODE, which was itself a library module).  Returns (libs, report):
    libs[i] is a Module to link right after modules[i] (None if empty).
    Each holds one piece per segment, the relocation sites inside those
    pieces, and a public for every external our modules use that it defines.
    """
    img = ref['img']
    ref_rel = sorted(seg * 16 + off for off, seg in ref['rels'])
    segs, order = layout(modules)
    names = [segs[k]['name'] for k in order]
    frame = {}
    base = {}                 # id(piece) -> absolute offset in the reference
    for m in modules:
        if m.entry:
            frame[m.entry[0]] = ref['cs']
    st = next((segs[k]['name'] for k in order
               if any(p.combine == 'STACK' for p in segs[k]['pieces'])), None)
    if st:
        frame[st] = ref['ss']
    extabs = {}               # external name -> absolute address in the reference
    extframe = {}
    pubs = {n: (m, sn, off) for m in modules for n, (sn, off) in m.publics.items()}

    def piece_of(m, sn):
        for p in m.pieces:
            if p.seg == sn:
                return p
        return None

    changed = True
    while changed:
        changed = False
        # the first module's pieces open their segments
        for p in modules[0].pieces:
            if p.seg in frame and id(p) not in base:
                base[id(p)] = frame[p.seg] * 16
                changed = True
        for m in modules:
            for p in m.pieces:
                if id(p) not in base:
                    continue
                b = base[id(p)]
                fr_site = frame.get(p.seg)
                for off, kind, target, addend in p.fixups:
                    val = struct.unpack_from('<H', img, b + off)[0]
                    if kind in ('SEG', 'SEGL'):
                        if target[0] == 'S' and target[1] not in frame:
                            frame[target[1]] = val
                            changed = True
                        elif target[0] == 'X':
                            extframe[target[1]] = val
                    elif target[0] == 'X' and target[1] not in extabs:
                        n = target[1]
                        if kind == 'REL' and fr_site is not None:
                            extabs[n] = fr_site * 16 + ((b - fr_site * 16 + off + 2 + val - addend) & 0xFFFF)
                        elif kind == 'OFF':
                            fr = extframe.get(n)
                            if fr is None:
                                decl = m.externs[n].seg if n in m.externs else None
                                fr = frame.get(decl) if decl else None
                            if fr is None and n in pubs:
                                fr = frame.get(pubs[n][1])
                            if fr is not None:
                                extabs[n] = fr * 16 + ((val - addend) & 0xFFFF)
                        if n in extabs:
                            changed = True
        # a public of one of our modules, seen from another: places that module's piece
        for n, a in list(extabs.items()):
            if n in pubs:
                pm, sn, off = pubs[n]
                p = piece_of(pm, sn)
                if p is not None and id(p) not in base:
                    base[id(p)] = a - off
                    changed = True
    report = []
    known = sorted(set(frame[n] * 16 for n in names if n in frame))
    libs = [None] * len(modules)

    def lib_for(slot):
        if libs[slot] is None:
            libs[slot] = Module(f'{name}.{slot}')
            libs[slot].externs = {}
        return libs[slot]

    for n in names:
        if n not in frame:
            report.append(f'segment {n}: frame unknown (no SEG fixup reaches it)')
            continue
        start = frame[n] * 16
        later = names[names.index(n) + 1:]
        if any(x in frame and frame[x] * 16 == start for x in later):
            continue      # empty segment sharing its frame with the next one
        nxt = [s for s in known if s > start]
        end = nxt[0] if nxt else len(img)
        ours = [(base[id(p)], mi, p) for mi, m in enumerate(modules) for p in m.pieces
                if p.seg == n and id(p) in base]
        missing = [(mi, p) for mi, m in enumerate(modules) for p in m.pieces
                   if p.seg == n and id(p) not in base and len(p.data)]
        for mi, p in missing:
            report.append(f'segment {n}: piece of {modules[mi].name} not located')
        ours.sort()
        cls = ours[0][2].cls if ours else ''
        comb = ours[0][2].combine if ours else 'PUBLIC'
        cursor, prev_mi = start, 0
        spans = []
        for b, mi, p in ours:
            if b > cursor:
                spans.append((cursor, b, mi - 1 if mi > 0 else 0))
            cursor = max(cursor, b + len(p.data))
            prev_mi = mi
        if end > cursor:
            spans.append((cursor, end, len(modules) - 1))
        for lo, hi, slot in spans:
            pad_end = (lo + 15) // 16 * 16
            if pad_end <= hi and not any(img[lo:pad_end]):
                lstart, align = pad_end, 16
            else:
                lstart, align = lo, 1
            if lstart >= hi:
                continue
            lib = lib_for(slot)
            pc = Piece(n, cls, align, comb, img[lstart:hi], None, [], lib)
            pc.relocs = [a - lstart for a in ref_rel if lstart <= a < hi]
            pc.ref_start = lstart
            lib.pieces.append(pc)
            report.append(f'segment {n}: library piece after {modules[slot].name}: '
                          f'{lstart:#07x}..{hi:#07x} ({hi - lstart:#x} bytes, {len(pc.relocs)} relocations)')

    def locate(a):
        for lib in libs:
            if lib is None:
                continue
            for pc in lib.pieces:
                if pc.ref_start <= a < pc.ref_start + len(pc.data):
                    return lib, pc.seg, a - pc.ref_start
        return None
    for n, a in extabs.items():
        if n in pubs:
            continue
        loc = locate(a)
        if loc is None:
            report.append(f'external {n}: {a:#x} is not inside a library piece')
            continue
        loc[0].publics[n] = (loc[1], loc[2])
    for n, fr in extframe.items():
        if n not in extabs and n not in pubs:
            loc = locate(fr * 16)
            if loc:
                loc[0].publics[n] = (loc[1], loc[2])
    # an EXTRN written inside an otherwise empty segment names that segment's
    # start (INTRO: "fontseg segment / extrn font:byte / ends")
    for m in modules:
        for n, sym in m.externs.items():
            if n in extabs or n in pubs or sym.seg is None or sym.seg not in frame:
                continue
            if any(len(p.data) for mm in modules for p in mm.pieces if p.seg == sym.seg):
                continue
            loc = locate(frame[sym.seg] * 16)
            if loc and loc[2] == 0:
                loc[0].publics[n] = (loc[1], loc[2])
    defined = set(pubs)
    for lib in libs:
        if lib is not None:
            defined |= set(lib.publics)
    for m in modules:
        for n in m.externs:
            if n not in defined:
                report.append(f'external {n}: not located')
    return libs, report


def save_libs(libs, path):
    os.makedirs(path, exist_ok=True)
    meta = {'slots': []}
    for slot, lib in enumerate(libs):
        if lib is None:
            meta['slots'].append(None)
            continue
        d = {'name': lib.name, 'pieces': [],
             'publics': {k: list(v) for k, v in sorted(lib.publics.items())}}
        for i, p in enumerate(lib.pieces):
            fn = f'{slot}_{i:02d}_{p.seg}.bin'
            open(os.path.join(path, fn), 'wb').write(p.data)
            d['pieces'].append({'seg': p.seg, 'cls': p.cls, 'align': p.align,
                                'combine': p.combine, 'file': fn, 'relocs': p.relocs})
        meta['slots'].append(d)
    json.dump(meta, open(os.path.join(path, 'lib.json'), 'w'), indent=1)


def load_libs(path):
    meta = json.load(open(os.path.join(path, 'lib.json')))
    libs = []
    for d in meta['slots']:
        if d is None:
            libs.append(None)
            continue
        lib = Module(d['name'])
        for pd in d['pieces']:
            data = open(os.path.join(path, pd['file']), 'rb').read()
            p = Piece(pd['seg'], pd['cls'], pd['align'], pd['combine'], data, None, [], lib)
            p.relocs = pd['relocs']
            lib.pieces.append(p)
        lib.publics = {k: tuple(v) for k, v in d['publics'].items()}
        lib.externs = {}
        libs.append(lib)
    return libs


def interleave(modules, libs):
    out = []
    for i, m in enumerate(modules):
        out.append(m)
        if i < len(libs) and libs[i] is not None:
            out.append(libs[i])
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('src', nargs='+')
    ap.add_argument('-o', '--out')
    a = ap.parse_args()


if __name__ == '__main__':
    main()
