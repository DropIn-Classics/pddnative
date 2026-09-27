# Handoff

State of 2026-09-27: stage 1 (source from the programs) done for the two
table programs; a headless runner (T3) runs them; nothing understood in
depth yet.

## Start here (next session)

- `master` has the remote `origin` (GitHub, mindphluxnet/pddnative); the
  user pushes, push only when the user asks.
- Muse is working on T1 (DDPCINTR.EXE) in `../pddnative-muse` on
  `muse/T1-ddpcintr`. Do not touch that checkout. When the user says
  "Review T1", follow CLAUDE.md (review steps) and AGENTS.md (who writes
  what where).
- T3 (the runner) is done, see "Running the originals" below. Claude's
  next task is T4 (PD.EXE's engine core into the hints), now with the
  runner to check each claim against.
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

Checked by running (tools/run.py, 2026-09-27):

- The digit on the command line picks the table, from 0: PD.EXE 0-3 load
  `TABLE2M.IGN`, `.STW`, `.BBX`, `.NTM` (Ignition, Steel Wheel, Beat Box,
  Nightmare); PD2.EXE 0-3 load `.UND` (the underwater table, presumably
  Neptune), `.SFR` (Safari), `.MNG` (presumably the one REVENGE.FLI
  belongs to), `.STT` (Stall Turn).
- `[8A8A]` is the game state: 1 at start, 2 once the sound driver is in,
  3 after F1 (a game started).
- Keys: INT 9 (`CODE:2AFB`) keeps a 256-bit map at `DATA:96BC`, bit =
  scancode, E0-prefixed keys + 80h (the shifts' E0 fake codes dropped).
  Tested as byte index + mask words: Esc (1), F1-F8, P (pause, state 8)
  directly; four configurable words at `DATA:9A9A..9AA1`: left flipper
  (LShift), right flipper (RShift), nudge (Space; `CODE:1251`: shakes the
  table through `[972B]`, counts in `[9738]`), plunger (E0 Down;
  `CODE:1387`: held, `[9767]` counts up to 20h, released: launch). These
  are the values when `C:\DELUXE\DDPCOPTN.BIN` (written by the menu,
  presumably) is missing, as it is on the CD.
- Start-up: `C:\DELUXE\HISCORES.PD1` and `DDPCOPTN.BIN`, the table's
  `TBLDETLO`, `TBLDETHI`, `TABLE2M`, `FLIPPERS.SPR` (current directory),
  then drive C:, `\DELUXE`, SOUND.CFG, EXEC of the driver named there,
  back to the drive it started on; the driver then loads `LEVELn.MOD`
  from the current directory of that drive. So the programs must start on
  another drive than C: (the CD's).
- With `-sound sb` SBLASTER.SDR plays the table's music (12 kHz at
  quality 0); with NOSOUND.SDR the frame timer runs as well.
- The picture the runner shows is 320x200 (plain Mode X timing); the
  tweaked modes at `CODE:4E7D`/`4EC1` are not used in these runs, perhaps
  a detail option from DDPCOPTN.BIN (not checked).

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

## Running the originals (tools/run)

`build/pddrun.exe` (C, MSVC; `tools/run/build.bat`, or let `run.py`
build it) is pfemu's emulation core without its Fantasies parts: a 386
real-mode CPU, VGA (planar, Mode X, the retrace latch of the start
address), PIT/PIC/keyboard, BIOS, the 8237 DMA and the Sound Blaster
DSP, so the real .SDR drivers run. The DOS layer is pfemu's for memory,
PSPs, EXEC and resident drivers (released with the program that loaded
them); the file side is new: every drive letter is the same tree, the CD
(`game/`) with a writable layer (`build/run/state`) over it, each drive
with its own current directory; programs start on D:. `-sound none|sb`
writes `C:\DELUXE\SOUND.CFG` into the layer (the CD has none).

Everything runs on the emulated clock (6 M instructions a second by
default); nothing reads the host's clock (DOS date fixed at 1994-10-01),
so a run repeats exactly: the report ends with hashes of RAM and video
memory, the same for the same arguments. On this machine a run goes at
about 30 M instructions a second, five times real time.

`run.py` passes everything on and translates addresses of `-break`,
`-log`, `-watch`, `-dump` from the hints' names (`CODE:1387`, `DATA:9AA0`,
`L13B2`, `D9AA0`, `name`s, `+N`) into the runner's `PD.EXE+08BD:9AA0`
(frame relative to the program's load segment). Useful: `-dos` (every
INT 21h call with its file name), `-watch` (who writes a byte, with the
time), `-log ADDR` (registers at each pass), `-trace FILE N` (N
instructions from the first -break/-log hit), `-shotevery DT PREFIX`
(picture n is at n*DT), `-wav`. A key script: `-key T KEY` (tap),
`KEY+`/`KEY-` (down/up), or `-keys FILE`.

Not done: savestates, a window, the menu programs (DDPCMAIN, DDPCINTR:
untried; DDPCINTR wants MSCDEX for CD audio, which the DOS layer does not
have). The runner is not part of `check.py` (it needs MSVC and the CD).

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
3. The runner (done, T3) for comparing the C port with the original
   frame by frame will want savestates (start both at the same moment)
   and a way to dump chosen variables every frame.
4. The port, on pfnative's platform code.
