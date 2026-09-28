#!/usr/bin/env python3
"""Play a game on the C implementation (port/) with a simple player and
write its keys file, for tools/portcmp.py to run the original with.

    portplay.py [--prog 1|2] [--table N] [--seed S] [--max N] KEYFILE

F1 starts a one-player game; each ball is launched with the plunger
(Down held 30 to 40 pictures, seen first by the 50th frame of the plunger
lane); in play both flippers are tapped (6 to 12 pictures) whenever the
ball comes down over the flippers' tips.  The ball is followed with the
port's PD_TRACE_BALL, one run of the port for each tap: the tap is placed
from the run that found it, so the keys file replays the same game.
--max ends the game after N frames of play (default 30000); --seed
chooses the plunger's and the taps' lengths.  At the end the tool prints
what the game reached: the frames of play, the balls lost, and the notes
of the port's trace (the event objects' handlers run, the level switches)
with their counts.

The zone is found from runs of the eight tables, not from the tables'
data: over y 425 to 460 (the flipper shapes lie at y 1B0h..1BEh), x 70 to
200.  It keeps the ball in play on most tables; Nightmare's layout it
does not suit.
"""
import argparse, os, random, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import portcmp
from disasm import game_dir

ZONE = (70, 200, 425, 460)          # x1, x2, y1, y2 over the flippers' tips
CHECKS = ('idle_loop', 'ball_start_loop', 'st_play', 'st_ball_lost', 'st_ball_start', 'st_game_over',
          'st_tilt', 'st_ball_locked', 'ball_locked_loop')


def run(args, ev, pics, stop, frames):
    """a run of the port with the events at their pictures: the checkpoint
    passes and notes {(where, n): picture} and the ball at each frame of
    play {n: (x, y, vx, vy)}"""
    d = os.path.join(portcmp.BUILD, 'portplay', '%d-%d-%d' % (args.prog, args.table, args.seed))  # games apart
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, 'save', 'DELUXE'))
    env = dict(os.environ, PD_DATA_DIR=d, PD_TRACE='1', PD_TRACE_BALL='1', PD_FRAMES=str(frames),
               PD_STOP=stop, PD_KEYS=' '.join(portcmp.keys_of(ev, pics)))
    for k in ('PD_POKE', 'PD_RAM', 'PD_VRAM'):
        env.pop(k, None)
    r = subprocess.run([portcmp.PORT, '-game', game_dir(), '-prog', str(args.prog), '-table', str(args.table)],
                       env=env, capture_output=True, text=True)
    trace, count, balls = {}, {}, {}
    for l in r.stderr.splitlines():
        m = re.match(r'(?:note )?(\w+) picture (\d+)$', l)
        if m:
            count[m.group(1)] = count.get(m.group(1), 0) + 1
            trace[m.group(1), count[m.group(1)]] = int(m.group(2))
            continue
        m = re.match(r'ball (-?\d+) (-?\d+) (-?\d+) (-?\d+)$', l)
        if m:
            balls[count.get('st_play', 0)] = tuple(int(v) for v in m.groups())
    return trace, balls


def first_after(trace, where, pic):
    """the first pass of where after picture pic"""
    return min([n for (w, n), p in trace.items() if w == where and p > pic] or [None])


def over_tips(b):
    x1, x2, y1, y2 = ZONE
    return x1 <= b[0] <= x2 and y1 <= b[1] <= y2 and b[3] >= 0


def play(args):
    """the events of a game and the trace of its last run"""
    rnd = random.Random(args.seed)
    whole = lambda: run(args, ev, pics, 'st_play#%d' % (args.max + 1), 60000)
    ev, pics = [], []
    trace, balls = run(args, ev, pics, 'idle_loop#300', 60000)
    ev = [('idle_loop', 260, 0x3B, True), ('idle_loop', 270, 0x3B, False)]
    pics = [trace['idle_loop', 260], trace['idle_loop', 270]]
    start, n = 1, 0
    while True:
        trace, balls = whole()
        s = trace.get(('st_ball_start', start))
        if s is None:
            break
        c = first_after(trace, 'ball_start_loop', s) + 49
        pull = rnd.randint(30, 40)
        ev += [('ball_start_loop', c, 0xE050, True), ('ball_start_loop', c + pull, 0xE050, False)]
        pics += [trace['ball_start_loop', c], trace['ball_start_loop', c + pull]]
        trace, balls = whole()
        n0 = first_after(trace, 'st_play', pics[-1])
        if n0 is None:
            break
        n = max(n, n0 + 5)
        while True:
            hit = min([k for k in balls if k >= n and over_tips(balls[k])] or [None])
            if hit is None:
                break                   # the ball left play (drained, locked), or --max
            a, z = hit + 1, hit + 1 + rnd.randint(6, 12)
            if ('st_play', z) not in trace:
                break
            for down, k in ((True, a), (False, z)):
                for code in (0x2A, 0x36):
                    ev.append(('st_play', k, code, down))
                    pics.append(trace['st_play', k])
            n = z + 8
            trace, balls = run(args, ev, pics, 'st_play#%d' % min(args.max + 1, z + 3000), pics[-1] + 3000)
        start += 1
    return ev, whole()[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--prog', type=int, default=1, choices=(1, 2))
    ap.add_argument('--table', type=int, default=0)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--max', type=int, default=30000, help='frames of play at most (default 30000)')
    ap.add_argument('keyfile')
    args = ap.parse_args()
    if not os.path.exists(portcmp.PORT):
        raise SystemExit('portplay.py: build the port first (port\\build.bat)')
    ev, trace = play(args)
    with open(args.keyfile, 'w') as f:
        f.write('# portplay.py --prog %d --table %d --seed %d --max %d\n' % (args.prog, args.table, args.seed, args.max))
        for where, n, code, down in ev:
            f.write('%s#%d %0*X%s\n' % (where, n, 4 if code > 0xFF else 2, code, '+' if down else '-'))
    notes = {}
    for w, n in trace:
        if w not in CHECKS:
            notes[w] = notes.get(w, 0) + 1
    print('%d frames of play, %d balls lost, game over: %s; %d key events' % (
        max([n for w, n in trace if w == 'st_play'] or [0]), max([n for w, n in trace if w == 'st_ball_lost'] or [0]),
        ('st_game_over', 1) in trace, len(ev)))
    print(' '.join('%s*%d' % kv for kv in sorted(notes.items())))


if __name__ == '__main__':
    main()
