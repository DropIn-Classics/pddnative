#!/usr/bin/env python3
"""Run the C implementation (port/) and the original (tools/run) with the
same keys, stop both at the same places and compare their memory.

    portcmp.py [--prog 1|2] [--table N] [--keys FILE] [--until T] [--all]
               WHERE#N ...

WHERE is a checkpoint of the port (a name of the hints: idle_loop,
ball_start_loop, st_play, st_ball_lost, st_ball_start, st_game_over,
st_tilt, st_ball_locked; the port's PD_STOP and the runner's -break take
the same name); WHERE#N its Nth pass.  Each is compared with
tools/memcmp.py; the differences the port has no reason to share (the
stack, the driver's EXEC scratch in CODE, the saved INT 9 vector,
XDATA's music_pos: see port/README.md) are left out unless --all.

The keys file has a line per key event, in the order they happen:

    idle_loop#260 3B+        F1 down, seen first by the 260th idle frame
    idle_loop#270 3B-
    ball_start_loop#49 E050+ the Down key (E0 50) ...

The key is a scan code in hex (E0xx for the extended keys), + down, -
up.  The tool finds where each event lands in both: for the first event
of a run of events at the same checkpoint it runs both programs (with
the keys before it) to that pass and takes the port's picture count and
the runner's time; the later events of the run are placed from there at
one pass a picture (70.09 pictures a second, the 320x200 mode's), which
holds for the loops above.  The port must be built (port/build.bat).
"""
import argparse, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
from disasm import game_dir

PORT = os.path.join(ROOT, 'port', 'build', 'pdd-headless.exe')
BUILD = os.path.join(ROOT, 'build')
RATE = 70.09
EXES = {1: ('DREAMS1/PD.EXE', 'src/PD.hints'), 2: ('DREAMS2/PD2.EXE', 'src/PD2.hints')}
# memcmp lines of differences that are expected (see the docstring)
EXPECTED = re.compile(r'^\s+(STACK:|CODE:\S+ load_sound_driver\+|DATA:\S+ old_int9\+|XDATA:\S+ music_pos\+)')


def parse_keys(path):
    """[(where, n, code, down)] from the keys file"""
    events = []
    for line in open(path):
        if line.lstrip().startswith('#'):
            continue
        m = re.match(r'\s*(\w+)#(\d+)\s+([0-9A-Fa-f]{2,4})([+-])', line)
        if m:
            events.append((m.group(1), int(m.group(2)), int(m.group(3), 16), m.group(4) == '+'))
        elif line.strip():
            raise SystemExit('portcmp.py: cannot read the key line: ' + line.strip())
    return events


def port_key(picture, code, down):
    """the event in PD_KEYS' form"""
    if code > 0xFF:
        return '%d:E0-%02X' % (picture, (code & 0x7F) | (0 if down else 0x80))
    return '%d:%02X' % (picture, code | (0 if down else 0x80))


def runner_key(t, code, down):
    return '%.5f %0*X%s' % (t, 4 if code > 0xFF else 2, code, '+' if down else '-')


def run_port(args, keys, stop, ram=None, vram=None):
    """the port's picture count at `stop` (None when it does not get there)"""
    env = dict(os.environ, PD_TRACE='1', PD_STOP=stop, PD_KEYS=' '.join(keys),
               PD_FRAMES=str(int(args.until * RATE)))
    for k in ('PD_RAM', 'PD_VRAM'):
        env.pop(k, None)
    if ram:
        env['PD_RAM'], env['PD_VRAM'] = ram, vram
    where, n = stop.split('#')
    r = subprocess.run([PORT, '-game', game_dir(), '-prog', str(args.prog), '-table', str(args.table)],
                       env=env, capture_output=True, text=True)
    passes = [int(l.split()[2]) for l in r.stderr.splitlines() if l.startswith(where + ' picture ')]
    return passes[-1] if len(passes) >= int(n) else None


def run_original(args, keyfile, stop, ram=None, vram=None):
    """the runner's time at `stop` (None when it does not get there)"""
    cmd = [sys.executable, os.path.join(HERE, 'run.py'), '-sound', 'sb', '-until', str(args.until),
           '-keys', keyfile, '-break', stop]
    if ram:
        cmd += ['-ram', ram, '-vram', vram]
    r = subprocess.run(cmd + [EXES[args.prog][0], str(args.table)], capture_output=True, text=True)
    m = re.search(r'^stop break t=([\d.]+)', r.stdout, re.M)
    return float(m.group(1)) if m else None


def write_keys(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def place(args, events, keyfile):
    """the events as port keys and runner key lines"""
    pk, rk, anchor = [], [], None
    for i, (where, n, code, down) in enumerate(events):
        if anchor is None or anchor[0] != where:
            write_keys(keyfile, rk)
            stop = '%s#%d' % (where, n)
            pic, t = run_port(args, pk, stop), run_original(args, keyfile, stop)
            if pic is None or t is None:
                raise SystemExit('portcmp.py: %s is not reached by %s' %
                                 (stop, 'the port' if pic is None else 'the original'))
            anchor = (where, n, pic, t)
        _, n0, pic, t = anchor
        pk.append(port_key(pic + (n - n0) - 1, code, down))
        rk.append(runner_key(t + (n - n0 - 0.5) / RATE, code, down))
    return pk, rk


def compare(args, pk, keyfile, stop):
    f = {k: os.path.join(BUILD, 'portcmp_%s.bin' % k) for k in ('pram', 'pvram', 'oram', 'ovram')}
    pic = run_port(args, pk, stop, f['pram'], f['pvram'])
    t = run_original(args, keyfile, stop, f['oram'], f['ovram'])
    if pic is None or t is None:
        print('%s: not reached by %s' % (stop, 'the port' if pic is None else 'the original'))
        return False
    r = subprocess.run([sys.executable, os.path.join(HERE, 'memcmp.py'), EXES[args.prog][1],
                        f['oram'], f['pram'], '--vram', f['ovram'], f['pvram']],
                       capture_output=True, text=True, cwd=ROOT)
    diffs = [l for l in r.stdout.splitlines()
             if (l.startswith('   ') and (args.all or not EXPECTED.match(l))) or
             (l.startswith('vram') and not l.endswith(' 0 bytes differ'))]
    print('%s: port picture %d, original t=%.5f: %s' % (stop, pic, t, 'different' if diffs else 'equal'))
    for l in diffs:
        print(l)
    return not diffs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--prog', type=int, default=1, choices=(1, 2))
    ap.add_argument('--table', type=int, default=0)
    ap.add_argument('--keys')
    ap.add_argument('--until', type=float, default=300, help='emulated seconds at most (default 300)')
    ap.add_argument('--all', action='store_true', help='show the expected differences too')
    ap.add_argument('stops', nargs='+')
    args = ap.parse_args()
    if not os.path.exists(PORT):
        raise SystemExit('portcmp.py: build the port first (port\\build.bat)')
    os.makedirs(BUILD, exist_ok=True)
    keyfile = os.path.join(BUILD, 'portcmp_keys.txt')
    events = parse_keys(args.keys) if args.keys else []
    pk, rk = place(args, events, keyfile)
    write_keys(keyfile, rk)
    ok = all([compare(args, pk, keyfile, stop) for stop in args.stops])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
