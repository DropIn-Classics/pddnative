# pddnative

Pinball Dreams Deluxe (DOS, CD-ROM 1994, 21st Century / Spidersoft; the
GOG.com release) taken apart, with the aim of a native compatibility
implementation requiring an installed copy of Pinball Dreams: our own C
code, which runs the game with the data of the player's GOG release.
There is no published source: the programs are reverse engineered.

1. **Source from the programs.** `tools/disasm.py` turns a shipped program
   into assembly source that assembles and links back to the same bytes
   (with `tasm.py`/`tlink.py`, work-alikes of the assembler and linker). What
   the analysis cannot see by itself (entry points reached only through
   pointers, tables, names, comments) goes into a hints file. Done for
   both table programs, PD.EXE and PD2.EXE: byte for byte identical.
2. **Understanding** (next): names, structures, the rules of the tables,
   written into the hints.
3. **The implementation** (started, `port/`): readable C translated from
   the programs, running over their memory image loaded from the player's
   PD.EXE/PD2.EXE, on a platform layer (window, VGA, sound with MOD
   playback, input).

No game files go into this repository, and no bytes of them: the hints
hold addresses, names and comments only, and the source is made from the
player's own copy each time (into `build/`).

## Use

    python tools/gogx.py                  # game.gog -> game/ (241 files)
    python tools/build.py src/PD.hints    # build/PD.ASM, build/PD.EXE, compare
    python tools/build.py src/PD2.hints

`gogx.py` takes the image's path as its first argument if GOG is not
installed at `C:\GOG Games\Pinball Dreams Deluxe`. Python 3 with
`capstone` (`pip install capstone`). `$PDD_GAME` names another folder
for the unpacked CD.

The original programs can be run headless, on an emulated clock, to
check what the hints claim:

    python tools/run.py -sound sb -until 16 -key 3 f1 -key 11 down+ -key 12 down- ^
        -shotevery 0.5 build/run/shot -watch DATA:8A8A DREAMS1/PD.EXE 1

(the Steel Wheel table, a game started and the ball launched, a picture
every half second, every write to the game state printed). The runner is
C: MSVC (the Visual Studio 2019 Build Tools) on Windows, `cc` (clang or
gcc) on macOS and Linux; `run.py` builds it when needed
(`tools/run/build.bat`, `build.sh`). Its options are at the top of
`tools/run/main.c`.

On macOS and Linux the commands are the same with `python3`, `/` and
`\` for `^` (`gogx.py` then needs the image's path).

## Layout

- `src/*.hints`: what is known about each program (the real work).
- `tools/`:
  - `gogx.py`: unpacks the CD image (MODE2/2352, ISO 9660).
  - `disasm.py`: analysis and source generator (hints syntax in its
    docstring).
  - `build.py`: generate, assemble, link (Microsoft LINK's header), compare
    line by line and as a whole.
  - `gaps.py`: what of a code segment is not reached yet, and where the
    addresses of the gaps appear.
  - `ptrscan.py`: immediates that look like addresses.
  - `check.py`: everything that must hold before a commit (all programs
    identical, PD2's carried hints up to date); `hooks/pre-commit` runs it
    (`git config core.hooksPath tools/hooks` once per clone).
  - `xfer.py`: carries hints from PD.hints to PD2.hints (the two programs
    share the engine; about 97 % of the instructions align).
  - `portmap.py`: the hints' names with their addresses in both programs,
    for the C (`port/src/gen/pdnames.h`).
  - `pdfiles.py`: reads and re-encodes the table programs' data files
    (collision maps, high scores, options); pictures of the maps.
  - `memcmp.py`: compares the memory of the C and of the original (tools/run)
    stopped at the same point, by the names of the hints.
  - `tasm.py`, `x86enc.py`, `tlink.py`: assembler and linker work-alikes,
    with switches for the original's encodings (see their comments).
  - `run/`: `pddrun`, a headless PC that runs the shipped programs (386
    real-mode CPU, VGA, timer, keyboard, Sound Blaster, a small DOS whose
    `C:` is the CD with a writable layer in `build/run/state`), see
    `run/pddrun.h`.
    `run.py`: its front end, takes addresses by their names in the hints.
- `port/`: the implementation in C (its README says how far it is).
- `docs/HANDOFF.md`: state, what was learned, what is next.
- `AGENTS.md`: rules for the agents working on this (two at a time, each
  in its own git worktree); `docs/TASKS.md`: who does what.
