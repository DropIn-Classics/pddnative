# Working on pddnative

Read README.md (what this is) and docs/HANDOFF.md (the state, what was
learned, what is next) first; this file is the rules.

## Rules

1. No game data in the repository: nothing from `game/` or `build/`, no
   program or data files, no bytes of them pasted into sources or docs
   (hints hold addresses, names and comments). The pre-commit hook
   refuses the usual cases; the rule holds beyond them.
2. `python tools/check.py` must say `all ok` before every commit (the hook
   runs it when `src/` or `tools/` changed). Do not skip the hook
   (`--no-verify`).
3. Work on `master`, commit in small steps with messages like the ones in
   `git log` (what changed and why, in plain words; English). Push only
   when the user asks.
4. Say what you did not verify. A guess in a hints comment says it is a
   guess ("presumably", "not checked"). Numbers and addresses are copied
   from tool output, not from memory.
5. What was learned goes into docs/HANDOFF.md, what the port does and how
   it was checked into port/README.md, with the change that brought it.
6. The block of `src/PD2.hints` below `; ==== carried over` is written by
   `tools/xfer.py` only.

## Provenance (permanent)

pddnative is a native compatibility implementation requiring an
installed copy of Pinball Dreams: it contains only our code, and the
player's installed GOG release supplies the game data at run time. Say it
so; never call it a "completely native" or "standalone" version, or
anything that implies it contains or replaces the game's data.

1. Pinball Fantasies, pfemu and pfnative may serve as references only for
   independently written, generic platform infrastructure: platform
   abstraction, files, display output, palettes, audio output and mixing,
   MOD playback, input, timing, the build system.
2. Nothing of Pinball Fantasies' game logic, or of code that came from or
   was derived from its original source, is used, translated, adapted,
   copied, inspected or taken as a model: no gameplay, physics, table
   logic, state machines, scoring, game data structures or algorithms,
   function decomposition or names, constants or tables, and no
   structurally equivalent routines.
3. Everything Pinball Dreams does is found from PD.EXE, PD2.EXE and the
   other Dreams programs, their data files, and runs of Dreams itself
   (tools/run). That Fantasies does something alike is never evidence for
   how Dreams does it.
4. No references to Pinball Fantasies, pfemu or pfnative in the tree:
   sources, documentation, comments, identifiers, user-facing text.
5. Version-control history is never rewritten. Code and documents are
   left in their natural final form, without comments about why
   something was removed or changed.

## Technical conventions

- Python 3, standard library plus `capstone`. The style of the existing
  tools: a module docstring saying what the tool is for and how to call
  it, short functions, comments where the reason is not obvious, no
  frameworks.
- Hints files: syntax in the docstring of `tools/disasm.py`. The code
  segment must be named `CODE` and the one DS normally holds `DATA`
  (the generated ASSUME uses these names). Addresses are
  `SEG:OFFSET` in hex, four digits.
- Adding a program (stage 1): find its segments (relocation values,
  header SS:SP, `python tools/build.py` output), write `src/NAME.hints`,
  then repeat `tools/build.py` and `tools/gaps.py`: every gap is either
  code reached through a pointer (find the pointer; add `code`/`ptr`/
  `words`/`coderange` hints with a comment saying where it comes from)
  or data (leave it). `tools/ptrscan.py` finds immediates that are
  addresses (check each by eye before adding a `dptr`).
- Everything the tools write goes to `build/` (ignored). The unpacked CD
  is found automatically (`$PDD_GAME`, else `game/` here).
- Write in English in the repository.
