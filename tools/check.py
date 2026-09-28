#!/usr/bin/env python3
"""Everything that must hold before a commit.

    check.py [HINTS...]

  * every program with a hints file in src/ (or the ones named) rebuilds
    byte for byte (tools/build.py);
  * PD2.hints' block carried over from PD.hints is up to date;
  * port/src/gen/pdnames.h and ddnames.h (tools/portmap.py) are up to date.

Exit status 0 when all holds.  The pre-commit hook (tools/hooks) runs it."""
import glob, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))


def run(args):
    r = subprocess.run([sys.executable] + args, cwd=ROOT, capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


def main():
    t0 = time.time()
    hints = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'src', '*.hints')))
    bad = []
    for h in hints:
        rc, out = run([os.path.join(HERE, 'build.py'), h])
        last = out.splitlines()[-1] if out else ''
        name = os.path.basename(h)
        if rc or 'IDENTICAL' not in last:
            bad.append(name)
            print(f'FAIL {name}\n' + '\n'.join('     ' + l for l in out.splitlines()[-15:]))
        else:
            print(f'ok   {name}: {last.split(";")[0]}')
    if not sys.argv[1:]:
        pd, pd2 = (os.path.join(ROOT, 'src', n) for n in ('PD.hints', 'PD2.hints'))
        if os.path.exists(pd) and os.path.exists(pd2):
            rc, out = run([os.path.join(HERE, 'xfer.py'), pd, pd2, '--check'])
            if rc:
                bad.append('PD2.hints (carried block)')
                print('FAIL ' + out.splitlines()[-1])
            else:
                print('ok   PD2.hints carried block up to date')
            rc, out = run([os.path.join(HERE, 'portmap.py'), '--check'])
            if rc:
                bad.append('port/src/gen (portmap.py)')
                print('FAIL ' + out.splitlines()[-1])
            else:
                print('ok   ' + out.splitlines()[-1])
    print(f'{"FAILED: " + ", ".join(bad) if bad else "all ok"} ({time.time() - t0:.0f} s)')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
