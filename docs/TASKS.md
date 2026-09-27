# Tasks

Kept on master by Claude and the user. Status: `open` (free to take),
`working`, `review` (branch ready), `changes` (review findings in
`docs/reviews/<id>.md`), `done`, `later` (not to be started yet).
Whoever works on a task keeps its notes in `docs/tasks/<id>.md` on the
task's branch (see AGENTS.md).

| id | for | status | what |
|---|---|---|---|
| T1 | Muse | done | DDPCINTR.EXE: stage 1 with the names from its debug information |
| T2 | Muse | open | DDPCMAIN.EXE: stage 1 |
| T3 | Claude | done | a headless runner for the original programs (C, from pfemu's core) |
| T4 | Claude | later | PD.EXE: the engine's core (state machine, objects, ball) into the hints |

## T1: DDPCINTR.EXE, stage 1 with names

DDPCINTR.EXE is the Deluxe intro (pictures, fades, CD audio through
MSCDEX). After its program image it carries 11,934 bytes of CodeView
debug information (signature `NB08`, written by Microsoft LINK) with the
names of its routines and labels, locals included (`@@a`, `@@fd_01`: TASM's
local labels).

Files: `src/DDPCINTR.hints` (new), `tools/cv4.py` (new),
`docs/tasks/T1.md`. Branch `muse/T1-ddpcintr`.

Starting point (a first try by Claude, checked: it builds IDENTICAL):

    exe DELUXE/DDPCINTR.EXE
    segment CODE  0000 CODE
    segment XSEG  0097 FAR_DATA
    segment DATA  0158 DATA
    segment STACK 01AF STACK stack size=800
    relocorder CODE
    keeptail

With it the build is identical, with `CODE:008F` written as DB (a word
displacement 3Ah where the assembler takes a byte: add it as a `raw` hint
with a comment); `tools/gaps.py` shows 940 of 2416 code bytes not reached.
The segment names XSEG and the classes are placeholders: name them after
what they hold once known.

Steps:

1. `tools/cv4.py`: reads the CodeView information at the end of a program
   (the `NB08` signature at the very end points back to its start; the
   format is Microsoft's CodeView 4, documented in "Microsoft Symbol and
   Type Information" / CV4 spec), and prints every symbol with its
   segment and offset. With an option it prints `name` hints. Names must
   be valid assembler identifiers and unique: write a local label as
   `<routine>_<local>` without the `@@` (e.g. `fadedn_fd_01`); say in the
   notes how segments in the debug information map to the frames in the
   hints (check a few by looking at the disassembly: a routine's name must
   sit on the first instruction of something that looks like that
   routine).
2. Put the names into `src/DDPCINTR.hints`, then close the gaps as
   AGENTS.md describes (the names help: every named routine is code).
3. Comments on the main routines where the disassembly makes their job
   clear (one line each; "presumably" where it does not).

Done when: `tools/check.py` says `all ok` with DDPCINTR among the
programs; every routine and label from the debug information appears in
the generated source under its name; `tools/gaps.py` leaves only data,
and the notes list each remaining gap and what it is; the notes say in a
few lines what the program does, step by step, as far as the code shows
it.

## T2: DDPCMAIN.EXE, stage 1

DDPCMAIN.EXE is the Deluxe menu: F1-F8 start a table (through PD.EXE and
PD2.EXE), F9 the "History of Pinball" viewer (640x480 pictures, texts in
five languages), F10 the options; it uses the mouse. Unlike DDPCINTR it
has no debug information (the file ends with the program image), so the
names must come from the code.

Files: `src/DDPCMAIN.hints` (new), `docs/tasks/T2.md`. Branch
`muse/T2-ddpcmain`.

Starting point (a first try by Claude, checked: it builds IDENTICAL,
4333 instructions):

    exe DELUXE/DDPCMAIN.EXE
    segment CODE  0000 CODE
    segment DATA  0448 DATA
    segment SEG1  0766 FAR_DATA
    segment SEG2  0B5E FAR_DATA
    segment SEG3  0BC2 FAR_DATA
    segment STACK 0E41 STACK stack size=800
    relocorder DATA CODE

The frames are the relocation values; the entry loads DS and ES with
0448h, so that one is DATA. DATA holds 7 relocation sites itself (far
pointers), listed before CODE's in the table. `tools/gaps.py` shows 38
gaps, 7232 of 17536 code bytes not reached. SEG1..SEG3 are placeholders:
name them after what they hold once known (check what the code reads
from them).

Steps:

1. Close the gaps as AGENTS.md describes: for each, find the pointer
   that reaches it (jump tables, key or menu dispatch tables, INT
   vectors set with INT 21h AH=25h, `PUSH`/`RET`) and add
   `words`/`ptr`/`code`/`coderange` hints with a comment saying where the
   pointer is; or leave it as data. Run `tools/ptrscan.py` and check each
   candidate by eye.
2. Names for the main routines and the variables whose job the code
   makes clear (what they do with INT 21h, INT 33h (mouse), INT 10h, the
   ports, the file names in DATA), one-line comments; "presumably" where
   it is a guess.
3. Find how the menu starts a table program (the EXEC call, the command
   line it builds) and what it writes before (DDPCOPTN.BIN?); say it in
   the notes with the addresses.

Done when: `tools/check.py` says `all ok` with DDPCMAIN among the
programs; `tools/gaps.py` leaves only data and the notes list each
remaining gap and what it is; the segments have names that say what
they hold (or the notes say why not); the notes say in a few lines what
the program does, step by step, as far as the code shows it, and how it
calls PD.EXE/PD2.EXE (command line, files written before).

## T3: a headless runner (Claude)

Runs the shipped programs without a window to check what the hints claim
and, later, to compare the C port with the original. C, built with MSVC,
from pfemu's emulation core (`../pfemu`, the same author's Pinball
Fantasies emulator: 386 real-mode CPU, VGA, PIT/PIC/keyboard, BIOS, Sound
Blaster and DMA) without its Fantasies, replay and launcher parts; a DOS
layer whose `C:\` is the unpacked CD with a writable layer in `build/`.

Files: `tools/run/*` (new), `tools/run.py` (new), README.md and
docs/HANDOFF.md (a section each).

Done when:

- `tools\run\build.bat` builds `build\pddrun.exe` from a clean checkout.
- PD.EXE and PD2.EXE run to a table with sound off and with the Sound
  Blaster driver; a screenshot at a given emulated time shows it.
- A key script starts a game and the ball can be seen moving in
  screenshots.
- Two runs with the same arguments give the same RAM at the end (the
  runner prints a hash).
- `tools/run.py` stops at an address given by its name in the hints
  (`CODE:4CEE`, a label) and prints the registers and memory asked for.
