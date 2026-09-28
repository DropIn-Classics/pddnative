#!/usr/bin/env python3
"""Run a shipped program on the headless runner (tools/run, build/pddrun[.exe])
with addresses given by their names in the hints.

    run.py [options] PROGRAM [ARGS]
    run.py -until 20 -key 3 f1 -break CODE:1251 -dump DATA:9AA0 2 DREAMS1/PD.EXE 1

PROGRAM is a path on the CD (DREAMS1/PD.EXE); the hints file whose `exe`
is that program gives the names.  The options are the runner's (see the
top of tools/run/main.c); run.py only
  * finds the unpacked CD as the other tools do and passes -game,
  * builds the runner when it is missing or older than its sources,
  * translates the ADDR of -break, -log, -watch, -dump and -poke (both
      of its addresses) when it is
      SEG:OFF   with SEG a segment of the hints (CODE:4CEE, DATA:8A8A),
      a label   of the generated source (L4CEE, D8A8A, C4F05) or a `name`
                or `code` name of the hints,
    optionally with +N (hex) added: DATA:9A9A+4.  An address in the
    runner's own form (a linear address, SEG:OFF with a hex segment,
    PROG+SEG:OFF) is passed on as it is.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
from disasm import Hints, game_dir

EXE = os.path.join(ROOT, 'build', 'pddrun.exe' if os.name == 'nt' else 'pddrun')
SRC = os.path.join(HERE, 'run')
ADDR_OPTS = {'-break': 1, '-log': 1, '-watch': 1, '-dump': 1, '-poke': 2}
# options and how many arguments they take (to find PROGRAM)
OPTS = {'-game': 1, '-state': 1, '-sound': 1, '-until': 1, '-ips': 1, '-key': 2, '-keys': 1,
        '-shot': 2, '-shotevery': 2, '-break': 1, '-log': 1, '-watch': 1, '-trace': 2,
        '-dump': 2, '-poke': 3, '-dumpevery': 1, '-ram': 1, '-vram': 1, '-wav': 1, '-dos': 0, '-intwatch': 1, '-prof': 0,
        '-v': 0}


def build():
    """Build the runner when a source is newer than the program."""
    newest = max(os.path.getmtime(os.path.join(SRC, f)) for f in os.listdir(SRC))
    if os.path.exists(EXE) and os.path.getmtime(EXE) >= newest:
        return
    if os.name == 'nt':
        cmd = ['cmd', '/c', os.path.join(SRC, 'build.bat')]
    else:
        cmd = ['sh', os.path.join(SRC, 'build.sh')]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stdout.write(r.stdout + r.stderr)
        raise SystemExit('run.py: the runner did not build')


def hints_for(program):
    """The hints file whose `exe` is this program, or None."""
    want = program.replace('\\', '/').upper()
    src = os.path.join(ROOT, 'src')
    for f in sorted(os.listdir(src)):
        if f.endswith('.hints'):
            h = Hints(os.path.join(src, f))
            if h.exe and h.exe.replace('\\', '/').upper() == want:
                return h
    return None


class Names:
    """Resolves a name of the hints or the generated source to (segment, offset)."""

    def __init__(self, hints, program):
        self.h = hints
        self.base = os.path.basename(program.replace('\\', '/')).upper()
        self.segs = {s.name: s for s in hints.segs}
        self.byname = {}
        for key, name in hints.names.items():
            self.byname[name.upper()] = key
        for s, o, name in hints.code:
            if name:
                self.byname[name.upper()] = (s, o)

    def lookup(self, t):
        m = re.fullmatch(r'([A-Za-z_]\w*):([0-9A-Fa-f]{1,4})', t)
        if m and m.group(1).upper() in self.segs:
            return m.group(1).upper(), int(m.group(2), 16)
        if t.upper() in self.byname:
            return self.byname[t.upper()]
        m = re.fullmatch(r'(L?)([A-Za-z])([0-9A-Fa-f]{4})', t)
        if m:
            code, letter, off = m.group(1), m.group(2).upper(), int(m.group(3), 16)
            if not code and letter == 'L':
                return 'CODE', off                      # L1234: code in CODE
            segs = [s for s in self.h.segs if s.prefix.upper() == letter]
            if len(segs) == 1:
                return segs[0].name, off
        return None

    def translate(self, t):
        """ADDR[#N] as run.py takes it -> the runner's form, or t unchanged."""
        count = ''
        if '#' in t:
            t, count = t.split('#', 1)
            count = '#' + count
        add = 0
        m = re.fullmatch(r'(.+?)\+([0-9A-Fa-f]+)', t)
        if m and '+' not in m.group(1) and self.lookup(m.group(1)):
            t, add = m.group(1), int(m.group(2), 16)
        r = self.lookup(t)
        if r is None:
            return t + count
        seg, off = r
        return f'{self.base}+{self.segs[seg].frame:04X}:{(off + add) & 0xFFFF:04X}{count}'


def main():
    args = sys.argv[1:]
    # find PROGRAM: the first argument that is not an option or an option's argument
    i, prog, pi = 0, None, len(args)
    while i < len(args):
        a = args[i]
        if a in OPTS:
            i += 1 + OPTS[a]
            continue
        prog, pi = a, i
        break
    if prog is None:
        raise SystemExit(__doc__)
    hints = hints_for(prog)
    names = Names(hints, prog) if hints else None
    out = []
    i = 0
    while i < len(args):
        a = args[i]
        if i < pi and a in OPTS:
            n = OPTS[a]
            vals = args[i+1:i+1+n]
            if a in ADDR_OPTS and names:
                for k in range(ADDR_OPTS[a]):
                    vals[k] = names.translate(vals[k])
            out += [a] + vals
            i += 1 + n
        else:
            out.append(a)
            i += 1
    if '-game' not in out:
        out = ['-game', game_dir()] + out
    if '-state' not in out:
        out = ['-state', os.path.join(ROOT, 'build', 'run', 'state')] + out
    build()
    sys.stdout.flush()
    sys.exit(subprocess.run([EXE] + out).returncode)


if __name__ == '__main__':
    main()
