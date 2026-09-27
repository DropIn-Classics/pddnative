# pddnative

Pinball Dreams Deluxe (DOS, CD-ROM 1994, 21st Century / Spidersoft; the
GOG.com release) taken apart, with the aim of a native program like
`../pfnative` did for Pinball Fantasies. Unlike Pinball Fantasies there is
no published source: the programs are reverse engineered.

1. **Source from the programs.** `tools/disasm.py` turns a shipped program
   into assembly source that assembles and links back to the same bytes
   (with `tasm.py`/`tlink.py` from pfnative, taught MASM's encodings). What
   the analysis cannot see by itself (entry points reached only through
   pointers, tables, names, comments) goes into a hints file. Done for
   both table programs, PD.EXE and PD2.EXE: byte for byte identical.
2. **Understanding** (next): names, structures, the rules of the tables,
   written into the hints.
3. **Port** (later): readable C on pfnative's platform layer (VGA planes,
   the INT 66h sound driver on micromod, GOG import, launcher).

No game files go into this repository, and no bytes of them: the hints
hold addresses, names and comments only, and the source is made from the
player's own copy each time (into `build/`).

## Use

    python tools/gogx.py                  # game.gog -> game/ (241 files)
    python tools/build.py src/PD.hints    # build/PD.ASM, build/PD.EXE, compare
    python tools/build.py src/PD2.hints

`gogx.py` takes the image's path as its first argument if GOG is not
installed at `C:\GOG Games\Pinball Dreams Deluxe`. Python 3 with
`capstone` (`pip install capstone`).

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
  - `xfer.py`: carries hints from PD.hints to PD2.hints (the two programs
    share the engine; about 97 % of the instructions align).
  - `tasm.py`, `x86enc.py`, `tlink.py`: from pfnative (commit 35cb5ee),
    with MASM switches and one fix, see their comments.
- `docs/HANDOFF.md`: state, what was learned, what is next.
