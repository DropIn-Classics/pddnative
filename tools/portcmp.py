#!/usr/bin/env python3
"""Run the C implementation (port/) and the original (tools/run) with the
same keys, stop both at the same places and compare their memory.

    portcmp.py [--prog 1|2] [--table N] [--keys FILE | --record FILE]
               [--until T] [--all] [--poke WHERE#N NAME HEX] [--options HEX]
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
up.  WHERE#N+K is K pictures after that (for keys between the passes of
a checkpoint: the bonus count, the initials).  The tool finds where each
event lands in both: the port's picture is that of the pass in a run with
the keys before it (all events are placed from one run as far as it
reaches and checked by a run with them; from the first that moved on,
again); the runner's time is taken from a run of the original to the
same pass, unless the last such run was at the same checkpoint and the
port passed it once a picture since (70.09 pictures a second): then the
time is counted on from there (as is the port's picture for a pass the
loop was left before: a key let go after it).  The port must be built
(port/build.bat).

--record takes a game played in the port's window (pdd -record FILE: a
line PICTURE:HEX per keyboard byte) instead of a keys file: a run of the
port with those keys gives each its last checkpoint pass, and the keys
file made so is written to build/portcmp_record.txt.

--poke writes the bytes HEX ("0400") at the DATA variable NAME in both,
the Nth time they pass WHERE (the runner's -poke, the port's PD_POKE):
a way into a state the keys do not easily reach (game_state 4 at
st_play#100).  It may be given more than once.

Every run starts from nothing written: the port's saved files
(PD_DATA_DIR) and the runner's layer over C: (-state) are made afresh in
build/portcmp/, so the high scores a game writes do not reach the next
run.  --options puts a DDPCOPTN.BIN with the bytes HEX into both (13
bytes: the options in the order of load_options; 01 01 02 01 05 04 06 40
07 02 1A 01 02 is the defaults with opt_screen 2).
"""
import argparse, bisect, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
from disasm import Hints, game_dir

PORT = os.path.join(ROOT, 'port', 'build', 'pdd-headless.exe')
BUILD = os.path.join(ROOT, 'build')
RATE = 70.09
EXES = {1: ('DREAMS1/PD.EXE', 'src/PD.hints'), 2: ('DREAMS2/PD2.EXE', 'src/PD2.hints')}
# the port's checkpoints (the trace's other names are notes)
CHECKPOINTS = ('idle_loop', 'ball_start_loop', 'st_play', 'st_ball_lost', 'st_ball_start', 'st_game_over',
               'st_tilt', 'st_ball_locked', 'ball_locked_loop')
# memcmp lines of differences that are expected (see the docstring)
EXPECTED = re.compile(r'^\s+(STACK:|CODE:\S+ load_sound_driver\+|DATA:\S+ old_int9\+|XDATA:\S+ music_pos\+)')


def parse_keys(path):
    """[(where, n, code, down, k)] from the keys file"""
    events = []
    for line in open(path):
        if line.lstrip().startswith('#'):
            continue
        m = re.match(r'\s*(\w+)#(\d+)(?:\+(\d+))?\s+([0-9A-Fa-f]{2,4})([+-])', line)
        if m:
            events.append((m.group(1), int(m.group(2)), int(m.group(4), 16), m.group(5) == '+',
                           int(m.group(3) or 0)))
        elif line.strip():
            raise SystemExit('portcmp.py: cannot read the key line: ' + line.strip())
    return events


def after(e):
    """an event's pictures after its pass (the K of WHERE#N+K)"""
    return e[4] if len(e) > 4 else 0


def port_key(picture, code, down):
    """the event in PD_KEYS' form"""
    if code > 0xFF:
        return '%d:E0-%02X' % (picture, (code & 0x7F) | (0 if down else 0x80))
    return '%d:%02X' % (picture, code | (0 if down else 0x80))


def runner_key(t, code, down):
    return '%.5f %0*X%s' % (t, 4 if code > 0xFF else 2, code, '+' if down else '-')


def fresh(args, which):
    """an empty layer for one run of `which` ('port' or 'run'), with the
    options file when --options gave one; its path"""
    d = os.path.join(BUILD, 'portcmp', which)
    shutil.rmtree(d, ignore_errors=True)
    opt = os.path.join(d, 'save', 'DELUXE') if which == 'port' else os.path.join(d, 'DELUXE')
    os.makedirs(opt)
    if args.options:
        with open(os.path.join(opt, 'DDPCOPTN.BIN'), 'wb') as f:
            f.write(bytes.fromhex(args.options))
    return d


def port_trace(args, keys, stop=None, ram=None, vram=None):
    """the port's run: {(where, n): picture} for every checkpoint pass (and
    for the notes: handlers, level switches)"""
    env = dict(os.environ, PD_DATA_DIR=fresh(args, 'port'), PD_TRACE='1', PD_KEYS=' '.join(keys),
               PD_FRAMES=str(int(args.until * RATE)),
               PD_POKE=';'.join('%s %04X %s' % (w, data_offset(args, n), h) for w, n, h in args.poke))
    for k in ('PD_STOP', 'PD_RAM', 'PD_VRAM'):
        env.pop(k, None)
    if stop:
        env['PD_STOP'] = stop
    if ram:
        env['PD_RAM'], env['PD_VRAM'] = ram, vram
    r = subprocess.run([PORT, '-game', game_dir(), '-prog', str(args.prog), '-table', str(args.table)],
                       env=env, capture_output=True, text=True)
    trace, count = {}, {}
    for l in r.stderr.splitlines():
        m = re.match(r'(?:note )?(\w+) picture (\d+)$', l)
        if m:
            count[m.group(1)] = count.get(m.group(1), 0) + 1
            trace[m.group(1), count[m.group(1)]] = int(m.group(2))
    return trace


def run_port(args, keys, stop, ram=None, vram=None):
    """the port's picture count at `stop` (None when it does not get there)"""
    where, n = stop.split('#')
    return port_trace(args, keys, stop, ram, vram).get((where, int(n)))


def run_original(args, keyfile, stop, ram=None, vram=None):
    """the runner's time at `stop` (None when it does not get there)"""
    cmd = [sys.executable, os.path.join(HERE, 'run.py'), '-state', fresh(args, 'run'), '-sound', 'sb',
           '-until', str(args.until),
           '-keys', keyfile, '-break', stop]
    for w, n, h in args.poke:
        cmd += ['-poke', w, n, h]
    if ram:
        cmd += ['-ram', ram, '-vram', vram]
    r = subprocess.run(cmd + [EXES[args.prog][0], str(args.table)], capture_output=True, text=True)
    m = re.search(r'^stop break t=([\d.]+)', r.stdout, re.M)
    return float(m.group(1)) if m else None


def data_offset(args, name):
    """the offset of the DATA variable `name` in the program's hints"""
    h = Hints(os.path.join(ROOT, EXES[args.prog][1]))
    for (seg, off), n in h.names.items():
        if n == name and seg == 'DATA':
            return off
    raise SystemExit('portcmp.py: no DATA name %s in %s' % (name, EXES[args.prog][1]))


def write_keys(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def picture_at(trace, where, n):
    """the picture of where#n in trace; for a pass after the loop was left
    (a key let go after it) counted on from its last pass"""
    if (where, n) in trace:
        return trace[where, n]
    last = max([c for w, c in trace if w == where] or [0])
    return trace[where, last] + n - last if last else None


def place_port(args, events, pics=None, trace=None):
    """the port's picture for each event, and the trace of a run with them
    all.  The events are placed from the trace of a run with the keys
    placed so far (pics: the first events', trace: that run's, when known),
    as far as it reaches; a run with them all checks them: up to the first
    that lands elsewhere they hold (a key changes only what comes after
    it), from there on they are placed again from a run with the ones that
    hold."""
    pics = list(pics or [])
    if trace is None:
        trace = port_trace(args, keys_of(events, pics))
    while len(pics) < len(events):
        cand = []
        for e in events[len(pics):]:
            p = picture_at(trace, e[0], e[1])
            if p is None:
                break
            cand.append(p + after(e))
        if not cand:
            raise SystemExit('portcmp.py: %s#%d is not reached by the port' % events[len(pics)][:2])
        trace = port_trace(args, keys_of(events, pics + cand))
        for p in cand:
            e = events[len(pics)]
            q = picture_at(trace, e[0], e[1])
            if q is None or q + after(e) != p:
                trace = port_trace(args, keys_of(events, pics))
                break
            pics.append(p)
    return pics, trace


def keys_of(events, pics):
    return [port_key(p - 1, e[2], e[3]) for p, e in zip(pics, events)]


def place(args, events, keyfile):
    """the events as port keys and runner key lines"""
    pk, rk, anchor = [], [], None
    for e, pic in zip(events, place_port(args, events)[0]):
        where, n, code, down, k = e[:4] + (after(e),)
        pic -= k                                # the pass's picture
        if anchor and anchor[0] == where and pic - anchor[2] == n - anchor[1]:
            t = anchor[3] + (n - anchor[1]) / RATE
        else:
            write_keys(keyfile, rk)
            t = run_original(args, keyfile, '%s#%d' % (where, n))
            if t is None:
                raise SystemExit('portcmp.py: %s#%d is not reached by the original' % (where, n))
            anchor = (where, n, pic, t)
        pk.append(port_key(pic + k - 1, code, down))
        rk.append(runner_key(t + (k - 0.5) / RATE, code, down))
    return pk, rk


def parse_record(args, path):
    """the events of a game recorded by pdd -record, each at its last
    checkpoint pass (from a run of the port with the recorded keys)"""
    keys, prefix = [], False
    for line in open(path):
        pic, b = line.split(':')
        b = int(b, 16)
        if b == 0xE0:
            prefix = True
            continue
        keys.append((int(pic), (0xE000 if prefix else 0) | (b & 0x7F), not b & 0x80))
        prefix = False
    trace = port_trace(args, [port_key(p, c, d) for p, c, d in keys])
    passes = sorted((p, w, n) for (w, n), p in trace.items() if w in CHECKPOINTS)
    pictures = [p for p, w, n in passes]
    events = []
    for p, code, down in keys:
        i = bisect.bisect_right(pictures, p + 1)        # the last pass by picture p + 1
        if not i:
            print('portcmp.py: a key before the first checkpoint is left out (picture %d)' % p)
            continue
        q, where, n = passes[i - 1]
        events.append((where, n, code, down, p + 1 - q))
    return events


def write_events(path, events):
    with open(path, 'w') as f:
        for e in events:
            f.write('%s#%d%s %0*X%s\n' % (e[0], e[1], '+%d' % after(e) if after(e) else '',
                                          4 if e[2] > 0xFF else 2, e[2], '+' if e[3] else '-'))


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
    ap.add_argument('--record', help='a game recorded with pdd -record, instead of --keys')
    ap.add_argument('--until', type=float, default=300, help='emulated seconds at most (default 300)')
    ap.add_argument('--all', action='store_true', help='show the expected differences too')
    ap.add_argument('--options', help='DDPCOPTN.BIN as hex bytes, for both')
    ap.add_argument('--poke', nargs=3, action='append', default=[], metavar=('WHERE#N', 'NAME', 'HEX'))
    ap.add_argument('stops', nargs='+')
    args = ap.parse_args()
    if not os.path.exists(PORT):
        raise SystemExit('portcmp.py: build the port first (port\\build.bat)')
    os.makedirs(BUILD, exist_ok=True)
    keyfile = os.path.join(BUILD, 'portcmp_keys.txt')
    events = parse_keys(args.keys) if args.keys else []
    if args.record:
        events = parse_record(args, args.record)
        write_events(os.path.join(BUILD, 'portcmp_record.txt'), events)
    pk, rk = place(args, events, keyfile)
    write_keys(keyfile, rk)
    ok = all([compare(args, pk, keyfile, stop) for stop in args.stops])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
