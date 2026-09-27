# Handoff

State of 2026-09-27: stage 1 (source from the programs) done for the two
table programs; nothing understood in depth yet.

## Start here (next session)

- `master` has the remote `origin` (GitHub, mindphluxnet/pddnative); the
  user pushes, push only when the user asks.
- Muse is working on T1 (DDPCINTR.EXE) in `../pddnative-muse` on
  `muse/T1-ddpcintr`. Do not touch that checkout. When the user says
  "Review T1", follow CLAUDE.md (review steps) and AGENTS.md (who writes
  what where).
- Claude's next task is open: T4 (PD.EXE's engine core into the hints)
  or T3 (a headless 8086 runner). The user has not chosen yet; ask.
- Before any change to `tools/` or the hints: `python tools/check.py`
  must stay `all ok` (the hook enforces it on commit). `game/` holds the
  unpacked CD (`python tools/gogx.py` if it is missing).
- Useful to know about the tools (also in their comments): capstone names
  98h/99h `cwde`/`cdq` in 16-bit mode (disasm.py renames them); tasm.py
  got a fix for `[BX+VAR]` and three switches for the original's
  encodings (`lea_smart`, `alu_ax_short`, `test_form`, set in build.py);
  `xfer.py` only carries a code address when the next ten instructions
  match, so after PD.hints changes run it and look at the "not mapped"
  lines in PD2.hints.

## The CD (game.gog)

| Folder | What |
|---|---|
| `DELUXE/DDPCMAIN.EXE` | menu (F1-F8 tables, F9 history, F10 options), the "History of Pinball" viewer (640x480 pictures, texts in 5 languages), mouse |
| `DELUXE/DDPCINTR.EXE` | intro; CodeView (NB08) debug information appended |
| `DELUXE/DDFLIPLY.EXE` | FLI player (Borland C++ 1991), plays the `*.FLI` per table |
| `DREAMS1/PD.EXE` | the four tables of Pinball Dreams (Ignition, Steel Wheel, Beat Box, Nightmare), Spidersoft's PC version |
| `DREAMS2/PD2.EXE` | Spidersoft's own four (Neptune, Safari, Stall Turn, and a fourth: REVENGE.FLI, files `.MNG`) |
| `*/TABLE2M.xxx` | a table's picture: 320x512, 8 bits a pixel, raw |
| `*/TBLDETLO.xxx`, `TBLDETHI.xxx` | detail for the low/high screen modes (format not known; Ignition has no HI file) |
| `*/LEVELn.MOD`, `INTRO.MOD` | ProTracker modules (M.K.) |
| `HISTORY/*.256`, `*.016` | 640x480 pictures: 256 colours + 768-byte palette / 16 colours |
| `DELUXE/*.SDR`, `SETSOUND.EXE` | the INT 66h sound drivers (Frontline Design), mostly the same files as Pinball Fantasies' |
| MUSIC tracks 2, 3 | CD audio (ogg in the GOG release) |

DELUXE.BAT runs `INSTALL.COM` and then `DDPCMAIN.EXE`, which names PD.EXE
and PD2.EXE. The table programs read a digit from their command line
(`CODE:4CEE`, into `[971A]`), presumably the table the menu picked.

## How the programs were made

From the bytes:

- Microsoft LINK: word 1 at 1Ch, relocations from 1Eh, header padded to
  512 bytes. The relocation table lists DATA's sites before CODE's.
- MASM 6, or TASM in NOSMART mode with /m: DDPCINTR's debug
  information has TASM-style local labels (`@@a`), so TASM is the better
  guess for that program at least. Every jump has its shortest form (no NOP
  padding as TASM's single pass leaves), `LEA reg,[addr]` stays LEA, ALU
  with AX and a small constant takes the sign-extended byte form (83h),
  `TEST r,r` puts the first register in r/m. `tasm.py` has switches for
  these (`lea_smart`, `alu_ax_short`, `test_form`), set by `build.py`.
- 186 instructions (PUSHA, shifts by a count).
- Written as DB because tasm.py picks other bytes: 8 `CMP r8,r8` in the
  38h form (the rest use 3Ah: two authors or hand edits?), 3 `ADD SI,20H`
  with a word immediate (a constant defined later in the source).

Hand-written assembly, small: about 8,100 instructions in 21.8 KB of code
per table program.

## PD.EXE

Segments (frames): CODE 0000 (to 54FAh), STACK 0550 (200h), TDATA 0570
(per-table data such as palettes), DATA 08BD, XDATA 1268 (file names,
sound set-up), BSS 12A6 (6EF2h, uninitialised). PD2.EXE has the same
segments without the STACK in the middle (its stack follows BSS).

What is known of the engine so far (addresses PD.EXE):

- Main loop at `CODE:0089`: calls the routine for the game state
  `[8A8A] AND 0Fh` from the table at `DATA:8188`.
- `[845A]`: a routine pointer (set to 4BE4h / 421Ah); found by the
  pointer-variable analysis.
- Per-table set-up routines (e.g. before `CODE:08EC`) fill some forty
  word variables `[8494]..[84E0]` with offsets of the table's data in
  DATA, `[8CEA]` with the palette's offset in TDATA.
- Table objects: records with a handler at +16h, light pointers at +18h
  and +1Eh, flags at +23h (dispatch at `CODE:3443..349C`). Hit rectangles:
  list at `[6531]`, entries x1,y1,x2,y2,record (`CODE:3044`). The ball's
  position is compared from `[9785]`/`[9787]`.
- Screen: mode 13h unchained (Mode X), with CRTC tables for tweaked
  modes at `CODE:4F05` and `CODE:4F19` (misc output E3h and A7h),
  latch copies through the graphics controller. The sound driver's timer
  callback sets `[83AE]`, which the frame wait polls.
- Sound: reads SOUND.CFG, loads the .SDR driver itself (INT 21h 4Bh; the
  parameter block and file names are in CODE at `4F71..`), INT 66h calls
  as in Pinball Fantasies (see pfnative's docs/sound-driver.md).

Nothing of this is checked by running anything yet.

## The tools and the hints

`build.py` regenerates the source from the hints each time and says,
line by line, where the assembler picked other bytes (then add a `raw`
hint or teach disasm.py). `gaps.py` lists what of a code segment is not
reached; every gap was looked at: what remains in both programs is
data (CS variables, the CRTC tables, the EXEC block, zeros).

Symbols: direct addresses are written as labels of the segment DS/ES
hold there (tracked along the code); immediates only where a hint or the
pointer-variable analysis says so. Numbers that really are addresses may
still be left: `ptrscan.py` finds some. This does not show in the byte
comparison; it shows once the source is changed (moving data) or when
code is translated.

`xfer.py` carries PD.hints over to PD2.hints: of 8,276 and 8,228
instructions 8,042 align by their shape. A code address is carried only if
the next ten instructions look the same at the new place; what cannot be
carried is left as a comment. Run it again after changing PD.hints (it
replaces its own block in PD2.hints, keeps the lines above it).

## Working with a second agent

Muse (in OpenCode) works in the worktree `../pddnative-muse` on
`muse/*` branches; the rules are in AGENTS.md, the tasks in
docs/TASKS.md. `tools/check.py` guards every commit through the hook.

## Next

1. The menu and intro programs (DDPCMAIN, DDPCINTR) the same way; the
   FLI player needs no disassembly (FLI is documented).
2. Understanding, into the hints: names of variables and routines, the
   structures (table descriptor, objects, hit rectangles, lights), the
   formats (TBLDET*, FLIPPERS.SPR, DDPCICON.SPR, HISTORY *.HOP/*.IDX,
   HISCORES.PD*). A `struct`/`dw` hint kind will be needed for data.
3. A way to run the original headless (a small 8086 interpreter with
   the few DOS/BIOS calls, the VGA from pfnative and a stand-in INT 66h)
   to check assumptions and, later, to compare the C port with the
   original frame by frame.
4. The port, on pfnative's platform code.
