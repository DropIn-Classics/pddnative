#!/usr/bin/env python3
"""Read a program's CodeView debug information (NB08, packed).

    cv4.py HINTS [--hints]

Prints every symbol with its segment and offset.  With --hints, prints
`name` hints for tools/disasm.py instead.  HINTS names the program (as
for tools/build.py); the segments in the debug information are mapped
to the hints' segments by their frames.

The debug information is the tail after the program image (kept with
the `keeptail` hint): the `NB08` signature at the very end points back
to its start (the dword after it is the size from the first `NB08` to
the end of the file).  The format is Microsoft's CodeView 4
("Microsoft Symbol and Type Information"): a directory of subsections
(sstModule, sstAlignSym, sstSrcModule, sstGlobalPub, sstSegMap,
sstSegName and others); symbols are length-prefixed records
(S_LPROC16 0104, S_GPROC16 0105, S_LDATA16 0101, S_GDATA16 0102, code
labels 0109, publics 0103, blocks 0107, S_END 0006).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disasm

# subsection types (packed, NB08)
SST_MODULE = 0x120
SST_ALIGNSYM = 0x125
SST_SRCMODULE = 0x127
SST_GLOBALPUB = 0x12A
SST_SEGMAP = 0x12D
SST_SEGNAME = 0x12E

# symbol record types we care about (16-bit, CV4 OMF)
S_LDATA16 = 0x0101
S_GDATA16 = 0x0102
S_LPROC16 = 0x0104
S_GPROC16 = 0x0105
S_LABEL = 0x0109
S_PUB = 0x0103
S_BLOCK16 = 0x0107
S_END = 0x0006


def find_cv(tail):
    """base, the tail offset of the first NB08."""
    if len(tail) < 8 or tail[-8:-4] != b'NB08':
        raise SystemExit('no NB08 signature at the end of the program')
    (size,) = struct.unpack_from('<I', tail, len(tail) - 4)
    base = len(tail) - size
    if base < 0 or base + 4 > len(tail) or tail[base:base + 4] != b'NB08':
        raise SystemExit(f'NB08 footer points outside ({size:#x})')
    return base


def directory(tail, base):
    """[(sst, imod, lfo, cb)] with lfo relative to base."""
    (dir_off,) = struct.unpack_from('<I', tail, base + 4)
    dpos = base + dir_off
    h0, h1, h2 = struct.unpack_from('<HHH', tail, dpos)
    # h0 is the header size (16), h1 the entry size (12), h2 the count
    if h1 < 12:
        raise SystemExit(f'bad directory entry size {h1}')
    pos = dpos + h0
    out = []
    for _ in range(h2):
        sst, imod, lfo, cb = struct.unpack_from('<HHII', tail, pos)
        out.append((sst, imod, lfo, cb))
        pos += h1
    return out


def segmap(tail, base, lfo, cb):
    """[descs] with descs as (frame, segName, className, cb)."""
    start = base + lfo
    data = tail[start:start + cb]
    cseg, csegl = struct.unpack_from('<HH', data, 0)
    descs = []
    pos = 4
    for _ in range(cseg):
        flags, ovl, group, frame, sname, cname, off, cbs = \
            struct.unpack_from('<HHHHHHII', data, pos)
        descs.append((frame, sname, cname, cbs))
        pos += 20
    return descs


def segnames(tail, base, lfo, cb):
    start = base + lfo
    return tail[start:start + cb].split(b'\x00')


def parse_alignsym(data):
    """Parse an sstAlignSym subsection.

    Returns (events, skipped): events is a list of
    (kind, off, seg, name, routine) with kind proc/data/label/block/end
    (block/end carry no address); skipped counts records of other types.
    Scopes nest: PROC and BLOCK push, END pops, so a BLOCK's END does not
    end the enclosing routine."""
    # first 4 bytes are the subsection header
    pos = 4
    stack = []
    skipped = {}
    events = []
    while pos + 4 <= len(data):
        ln, typ = struct.unpack_from('<HH', data, pos)
        if ln < 2 or pos + 2 + ln > len(data):
            raise SystemExit(f'bad symbol record at {pos:04X}')
        rec = data[pos + 4:pos + 2 + ln]
        cur = stack[-1] if stack else None
        if typ in (S_LPROC16, S_GPROC16):
            if len(rec) < 26:
                raise SystemExit(f'short proc record at {pos:04X}')
            off, seg = struct.unpack_from('<HH', rec, 18)
            namelen = rec[25]
            name = rec[26:26 + namelen].decode('ascii')
            stack.append(name)
            events.append(('proc', off, seg, name, name))
        elif typ in (S_LDATA16, S_GDATA16):
            off, seg, _tidx = struct.unpack_from('<HHH', rec, 0)
            namelen = rec[6]
            name = rec[7:7 + namelen].decode('ascii')
            events.append(('data', off, seg, name, cur))
        elif typ == S_LABEL:
            off, seg = struct.unpack_from('<HH', rec, 0)
            namelen = rec[5]
            name = rec[6:6 + namelen].decode('ascii')
            events.append(('label', off, seg, name, cur))
        elif typ == S_BLOCK16:
            # a block opens a scope like a proc (its locals belong to it);
            # its name, if any, is not a routine, so push a marker keeping
            # the enclosing routine for @@ prefixing
            stack.append(cur)
            events.append(('block', None, None, None, cur))
        elif typ == S_END:
            if stack:
                stack.pop()
            events.append(('end', None, None, None,
                            stack[-1] if stack else None))
        else:
            skipped[typ] = skipped.get(typ, 0) + 1
        pos += 2 + ln
    return events, skipped


def parse_pubs(data):
    """Yield (off, seg, name) from an sstGlobalPub subsection."""
    # header: symhash, addrhash, cbSymbol, ... (16 bytes)
    _symhash, _addrhash, cbsym = struct.unpack_from('<HHI', data, 0)
    pos = 16
    end = 16 + cbsym
    while pos + 4 <= end:
        ln, typ = struct.unpack_from('<HH', data, pos)
        if ln < 2 or pos + 2 + ln > end:
            raise SystemExit(f'bad public record at {pos:04X}')
        if typ != S_PUB:
            raise SystemExit(f'expected public at {pos:04X}, found {typ:04X}')
        rec = data[pos + 4:pos + 2 + ln]
        off, seg, _tidx = struct.unpack_from('<HHH', rec, 0)
        namelen = rec[6]
        name = rec[7:7 + namelen].decode('ascii')
        yield (off, seg, name)
        pos += 2 + ln


def sanitize(name):
    out = []
    for c in name:
        if c.isalnum() or c == '_':
            out.append(c)
        else:
            out.append('_')
    s = ''.join(out)
    if not s or s[0].isdigit():
        s = '_' + s
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hints')
    ap.add_argument('--hints', dest='as_hints', action='store_true',
                    help='print name hints for disasm.py')
    a = ap.parse_args()
    h = disasm.Hints(a.hints)
    prog = disasm.Program(os.path.join(disasm.game_dir(), h.exe))
    tail = prog.tail
    base = find_cv(tail)
    entries = directory(tail, base)
    by = {}
    for sst, imod, lfo, cb in entries:
        by.setdefault(sst, []).append((imod, lfo, cb))
    # segment mapping: debug index (1-based in segmap order) -> frame
    seg_entry = next(e for e in entries if e[0] == SST_SEGMAP)
    descs = segmap(tail, base, seg_entry[2], seg_entry[3])
    name_entry = next(e for e in entries if e[0] == SST_SEGNAME)
    stab = segnames(tail, base, name_entry[2], name_entry[3])

    def tab(off):
        # stab is split on NUL; find the piece starting at off
        pos = 0
        for piece in stab:
            if pos == off:
                return piece.decode('ascii', errors='replace')
            pos += len(piece) + 1
        return f'<{off}>'

    def hints_seg(dbg):
        if dbg < 1 or dbg > len(descs):
            return None
        frame, _, _, cbs = descs[dbg - 1]
        if cbs == 0:
            return None     # the dummy trailing segment, no code or data
        for s in h.segs:
            if s.frame == frame:
                return s.name
        return None

    # collect symbols: (dbgseg, off, raw, kind, routine)
    syms = []
    skipped = {}
    for imod, lfo, cb in by.get(SST_ALIGNSYM, []):
        data = tail[base + lfo:base + lfo + cb]
        events, skip = parse_alignsym(data)
        for typ, n in skip.items():
            skipped[typ] = skipped.get(typ, 0) + n
        for kind, off, seg, name, cur in events:
            if kind in ('end', 'block'):
                continue
            syms.append((seg, off, name, kind, cur))
    pubs = []
    for imod, lfo, cb in by.get(SST_GLOBALPUB, []):
        data = tail[base + lfo:base + lfo + cb]
        for off, seg, name in parse_pubs(data):
            pubs.append((seg, off, name))

    if not a.as_hints:
        print(f'debug info at file offset {len(prog.file) - len(tail) + base:#x} '
              f'({len(tail) - base} bytes from first NB08 to end)')
        print(f'{len(descs)} debug segments:')
        for i, (frame, sname, cname, cbs) in enumerate(descs, 1):
            hs = hints_seg(i) or '-'
            print(f'  {i}: frame {frame:04X} size {cbs:04X} '
                  f'segname {tab(sname)!r} class {tab(cname)!r} -> hints {hs}')
        print(f'{len(syms)} symbols, {len(pubs)} publics:')
        if skipped:
            kinds = ', '.join(f'{n}x {t:04X}' for t, n in sorted(skipped.items()))
            print(f'skipped records of other types: {kinds}')
        for seg, off, name, kind, cur in syms:
            hs = hints_seg(seg) or f'<seg {seg}>'
            extra = f' in {cur}' if cur and name.startswith('@@') else ''
            print(f'  {hs}:{off:04X} {name} ({kind}{extra})')
        for seg, off, name in pubs:
            hs = hints_seg(seg) or f'<seg {seg}>' if seg else '<abs>'
            print(f'  {hs}:{off:04X} {name} (public)')
        return

    # --hints: sanitized unique names, locals as <routine>_<local>
    used = {}           # lower -> (dbgseg, off)
    at_addr = {}        # (hints_seg, off) -> name printed
    out = []

    def emit(dbg, off, raw, routine):
        hs = hints_seg(dbg)
        if hs is None:
            print(f'; skip {raw} (debug seg {dbg} maps nowhere)',
                  file=sys.stderr)
            return
        base_name = raw
        if raw.startswith('@'):
            stripped = raw.lstrip('@')
            stripped = sanitize(stripped)
            if routine:
                base_name = sanitize(routine) + '_' + stripped
            else:
                base_name = stripped
        else:
            base_name = sanitize(raw)
        # unique (assembler is case-insensitive)
        name = base_name
        i = 2
        while name.lower() in used and used[name.lower()] != (hs, off):
            name = f'{base_name}_{i}'
            i += 1
        key = (hs, off)
        if key in at_addr:
            if at_addr[key] == name:
                return
            print(f'; {hs}:{off:04X} also named {raw}'
                  f' (kept {at_addr[key]})', file=sys.stderr)
            out.append(f'; name {hs}:{off:04X} {name}\t; also {raw}, '
                       f'kept {at_addr[key]}')
            return
        used[name.lower()] = (hs, off)
        at_addr[key] = name
        comment = ''
        if raw != name:
            comment = f'\t; {raw}' + (f' in {routine}' if routine and raw.startswith('@') else '')
        out.append(f'name {hs}:{off:04X} {name}{comment}')

    for seg, off, name, kind, cur in syms:
        # data with type names like lum/track are globals; @@ names
        # are locals of the enclosing proc (cur)
        routine = cur if name.startswith('@') else None
        # labels like cvlp/wke010 inside a proc are globals: no prefix
        emit(seg, off, name, routine)
    for seg, off, name in pubs:
        if seg == 0:
            continue    # _end/__end/_edata/__edata: absolute, no address
        emit(seg, off, name, None)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
