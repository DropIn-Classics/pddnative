# Tasks

Kept on master by Claude and the user. Status: `open` (free to take),
`working`, `review` (branch ready), `changes` (review findings in
`docs/reviews/<id>.md`), `done`, `later` (not to be started yet).
Whoever works on a task keeps its notes in `docs/tasks/<id>.md` on the
task's branch (see AGENTS.md).

| id | for | status | what |
|---|---|---|---|
| T1 | Muse | done | DDPCINTR.EXE: stage 1 with the names from its debug information |
| T2 | Muse, Sol | done | DDPCMAIN.EXE: stage 1 |
| T3 | Claude | done | a headless runner for the original programs (C) |
| T4 | Claude | done | PD.EXE: the engine's core (state machine, objects, ball) into the hints |
| T6 | Claude | working | the implementation in C (port/): the table programs' engine, routine by routine |
| T5 | Sol | done | tools/pdfiles.py: the table programs' data files (collision maps, high scores, options) |
| T7 | Sol | open | tools/ddfiles.py: the history viewer's files (HISTORY/*.HOP, *.IDX, HISTORY.FNT, the pictures) |

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
and, later, to compare the C port with the original. C, built with MSVC:
a 386 real-mode CPU, VGA, PIT/PIC/keyboard, BIOS, Sound Blaster and DMA,
and a DOS layer whose `C:\` is the unpacked CD with a writable layer in
`build/`.

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

## T4: PD.EXE's engine core into the hints (Claude)

Names and comments in `src/PD.hints` for what the table programs do,
each claim checked with the runner where it can be, so that the port
(stage 3) can be written from the hints and the generated source.

Files: `src/PD.hints`, `src/PD2.hints` (own part and the carried block),
`tools/` where the tools need it, docs/HANDOFF.md.

Done (998468c, 92c0422, 531ea8f, c0c88b5): every called routine named,
the records described at their users, the collision map format (checked
against all fifteen files) and the level switch seen in runs, the XDATA
and TDATA code with labels (disasm.py: `es` hints, ASSUME following DS),
the objects' handler pointers as `words` hints (PD 183, PD2 188). What
is still not understood: docs/HANDOFF.md, "The engine".

Done when:

- Every routine the code calls or reaches through a table has a name
  (seven helpers are left: `L0FD0`, `L2314`, `L2320`, `L2335`, `L27F4`,
  `L2B8E`, `L349D`), the main ones a one-line comment.
- The records the engine walks are written down field by field where
  their users are (objects, event records, lights, hit rectangles,
  lanes, locks, the table descriptor, the sprite and flipper records),
  every field the code touches named or listed as not understood.
- The collision map's format (TBLDETLO/TBLDETHI: the row index, the
  run coding, what a value means) is described, and the switch to the
  upper level (map code FEh) seen in a run, or it is said why it
  could not be.
- The code that runs with DS = XDATA (the sound routines from 5315h on)
  and the display font read through DS = TDATA are written with labels
  of those segments (`ds`/`ptr` hints), the build still IDENTICAL.
- The names are carried to PD2.hints, its "not mapped" names placed by
  hand where they matter; `tools/check.py` says `all ok`.
- docs/HANDOFF.md says in a page how the engine works, with the
  addresses, and what is still not understood.

## T5: tools/pdfiles.py, the table programs' data files (Sol)

A tool that reads the data files of PD.EXE/PD2.EXE by the formats the
hints describe, so the port can use them and the descriptions get
checked against the files.

Files: `tools/pdfiles.py` (new), `docs/tasks/T5.md`. Branch
`sol/T5-pdfiles`.

Formats (read them in `src/PD.hints`; the comments are the reference):

- collision maps `TBLDETLO.xxx`, `TBLDETHI.xxx` (DREAMS1 and DREAMS2):
  the comments at `map_test`, `index_map`, `map_special`, `map_code`;
- `DELUXE/HISCORES.PD1`, `HISCORES.PD2`: the comments at `hiscores`,
  `check_hiscore`, `load_hiscores`;
- `C:\DELUXE\DDPCOPTN.BIN` (not on the CD; the menu writes it): the
  `opt_*` and `key_*` names and `load_options`.

Steps:

1. `pdfiles.py map FILE`: parse a collision map, print per surface value
   the number of pixels, and the 1Fh pixels' angle bytes (codes and event
   numbers) with a count each; `--png OUT` writes the map as a 320x512
   picture in false colours (one colour per surface value; standard
   library only: zlib and struct) into `build/`.
2. An encoder for the same format; the tool checks that parsing and
   encoding again gives the file's bytes.
3. `pdfiles.py hiscores FILE` and `pdfiles.py options FILE`: print the
   entries (table, letters, score) and the options by their names.

Done when: `pdfiles.py map` parses every TBLDET file of DREAMS1 and
DREAMS2 to its last byte and re-encodes each to identical bytes (say
the counts in the notes); the pictures of two maps (one LO, one HI) look
like tables (describe them in the notes, do not commit them); both
HISCORES files print sensible names and scores; `tools/check.py` says
`all ok`; the notes list every place where a file disagreed with a
comment in PD.hints (or say that none did).

## T7: tools/ddfiles.py, the history viewer's files (Sol)

The F9 History of Pinball viewer of DDPCMAIN.EXE reads the files in
`HISTORY/`: per language a `.HOP` and a `.IDX` (ENGLISH, FRENCH, GERMAN,
ITALIAN, SPANISH), `HISTORY.FNT` (2,048 bytes), `DDPCHIST.VGA`, and per
table pictures with the extensions `.016` (153,600 bytes each) and
`.256` (307,968 bytes each). Their formats are not described yet. Find
them from DDPCMAIN.EXE, as T5 did for the table programs, and write a
tool that reads them.

Files: `src/DDPCMAIN.hints` (comments and names at the routines that
read these files), `tools/ddfiles.py` (new), `docs/tasks/T7.md`.
Branch `sol/T7-history`.

Starting points in `src/DDPCMAIN.hints`: `history_screen`,
`history_init`, `history_loop`, `select_history_table`,
`filter_history_records`, the `words` tables at `HISTORY:0D4A..1445`
(language directories, .HOP/.IDX names, the name lists), and
`load_vga_image`. Which picture files are opened, and how their names
are made, is part of the task: some files in `HISTORY/` may not be read
at all (say which, and how you know).

Steps:

1. Follow the viewer from `history_screen` to every file it opens; for
   each, the routine that reads it and what it does with the bytes.
   Write that as `comment`/`name` hints (a guess says it is one). The
   build must stay identical.
2. `ddfiles.py idx FILE`, `ddfiles.py hop FILE`: print the index entries
   and the text records (per record which table it belongs to, as the
   viewer links them); `ddfiles.py font FILE --png OUT` and
   `ddfiles.py picture FILE --png OUT`: the font's glyphs and a picture
   as PNG into `build/` (standard library only, as in pdfiles.py).
3. An encoder for each format the tool parses; the tool checks that
   parsing and encoding again gives the file's bytes.

Done when: every .IDX and .HOP of the five languages, HISTORY.FNT, and
every .016 and .256 picture the viewer opens parse to their last byte
and re-encode to identical bytes (the counts in the notes); the text of
one table's record prints readably in two languages; two pictures (one
.016, one .256) and the font look right (describe them in the notes, do
not commit them); `tools/check.py` says `all ok`; the notes say which
statements were checked by running (`tools/run`, if at all) and which
were only read.
