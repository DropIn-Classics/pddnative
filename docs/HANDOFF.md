# Handoff

State of 2026-09-27: stage 1 (source from the programs) done for the two
table programs, the intro (T1) and the menu (T2); a headless runner (T3)
runs them; PD.EXE's engine named, commented and its records described
(T4); the implementation in C (T6, `port/`) has every game state
translated and matches the original through a whole game on Steel Wheel.

## Start here (next session)

- `master` has the remote `origin` (GitHub, mindphluxnet/pddnative); the
  user pushes, push only when the user asks.
- AGENTS.md has permanent provenance rules: read them before anything
  else.
- T1 (DDPCINTR.EXE, by Muse) and T2 (DDPCMAIN.EXE, begun by Muse and
  finished by Sol) are merged, see below. Sol, the second agent now,
  works in `../pddnative-muse` on `sol/*` (older branches `muse/*`); do
  not touch that checkout. T5 (`tools/pdfiles.py`) and T7 (`tools/ddfiles.py`) are
  merged; T8 (the pictures and sprites) is open for Sol. When the user says
  "Review Tn", follow CLAUDE.md (review steps) and AGENTS.md (who writes
  what where).
- T3 (the runner) and T4 (PD.EXE's engine into the hints) are done; see
  "Running the originals" and "The engine" below. T6, the implementation
  in C, is under way: port/README.md and "Next" below.
- Before any change to `tools/` or the hints: `python tools/check.py`
  must stay `all ok` (the hook enforces it on commit). `game/` holds the
  unpacked CD (`python tools/gogx.py` if it is missing).
- `xfer.py` does not carry a name to an address that would get two
  (PD2.hints names those in its own part).
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
| `*/TBLDETLO.xxx`, `TBLDETHI.xxx` | the collision maps of the table's lower and upper level (ramps); only the surface pixels, see "The engine"; Ignition has no HI file |
| `*/LEVELn.MOD`, `INTRO.MOD` | ProTracker modules (M.K.) |
| `HISTORY/*.256`, `*.016` | 640x480 pictures: 256 colours + 768-byte palette / 16 colours |
| `DELUXE/*.SDR`, `SETSOUND.EXE` | the INT 66h sound drivers (Frontline Design) and their set-up program |
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
- The table's records (objects, lights, hit rectangles ...): see "The
  engine" below.
- Screen: mode 13h unchained (Mode X), with CRTC tables for tweaked
  modes at `CODE:4F05` and `CODE:4F19` (misc output E3h and A7h),
  latch copies through the graphics controller. The sound driver's timer
  callback sets `[83AE]`, which the frame wait polls.
- Sound: reads SOUND.CFG, loads the .SDR driver itself (INT 21h 4Bh; the
  parameter block and file names are in CODE at `4F71..`). The INT 66h
  calls PD.EXE makes, read from its code (what the driver does with them
  is presumed from the arguments, not checked): AL=0 stop (`stop_sound`),
  4 play, 6 volume (CX), 8 once a tick (`timer_callback`), 0Bh the tick
  callback (ES:DX), 10h a module position (BX), 11h an effect (BL, BH,
  CL, DL from the table's effect list), 12h load a module (DS:DX), 13h a
  second callback (ES:DX, `module_callback`).

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
- The collision maps (`tools/pdfiles.py map`, T5; all fifteen TBLDET
  files parse and re-encode to identical bytes): besides the surfaces
  the engine tests (low nibble for the response and surface_table,
  high nibble 10h/20h kickers, xFh passages and lines) they hold 33h and
  37h (DREAMS1) and 30h, 36h, 37h, 42h (DREAMS2); nothing tests a high
  nibble of 30h or 40h, so these act as their low nibble (read in
  `surface_params`, `collision_response`; the same in PD2.EXE; not
  checked by running). Low nibbles 0 and 1 share surface_table's first
  entry. TBLDETHI.BBX has one 1Fh pixel with angle byte 80h; it becomes
  event 0 (`map_event_run` doubles the byte). Three rows repeat an x;
  `map_test` takes the first.
- The option byte `DATA:9AA2` (the last of DDPCOPTN.BIN's 13 bytes; the
  menu insists on 13, PD.EXE's defaults write a word there)
  picks the screen: 1 is 320x200 Mode X, 2 the tweaked mode at
  `CODE:4EC1` (misc output A7h), 320x350, where the table scrolls less.
  Checked with an options file written into a state layer. `CODE:4E7D`
  (misc E3h, 480 lines) is not called.

### The engine (T4; names are those of PD.hints)

What the hints say, in one place. Read from the code unless it says
"checked" (by a run with tools/run.py). The comments in PD.hints have
the details and the field lists; PD2.EXE is the same engine (its names
are carried, its own differences are in PD2.hints' own part).

**States** (`main_loop` calls `state_table[game_state]`): 0 quit (save
the high scores), 1 load, 2 idle (F1-F8 = 1-8 players), 3 ball start
(plunger lane), 4 a ball was locked, 5 play, 6 ball lost (bonus,
boosters, next player), 7 game over (high score entry), 8 pause, 9 tilt,
10 exit, 11/13-15 back to 1; 12 is set nowhere.

**A frame of play** (`st_play`): `scroll_follow`, `ball_frame` (three
`ball_step`s), `nudge`, `plunger`, `play_frame`, `scroll_step` (waits for
the frame: the driver's timer tick sets `timer_tick`). The sprites
(ball, flippers, plunger) are drawn by `timer_callback` in the tick,
`upload_lights` sends the lights' palette there too.

**The ball** (`ball_step`, 3 a frame): the flippers are swept from the
drawn frame to the wanted one and tested against the ball
(`flipper_sweep`, `shape_test`); then the ball moves pixel by pixel
(`ball_step_size`, `ball_collide`) through the collision map
(`map_test`); a surface pixel gives the surface type and angle,
`collision_response` sets `hit_x`/`hit_y` and `bounce` reflects the
speed with the surface's five constants (`surface_table`), in the frame
of the moving flipper (`surface_vx`/`vy`) and the nudge. Position 24
bits, pixels * 400h; gravity 9 + 2 * (`opt_slope` - 1), 10h/9 from the
map codes FCh/FBh (checked: position and launch speed against the
screen).

**The collision map** (TBLDETLO lower level, TBLDETHI upper, loaded to
BSS:0960 and BSS:47E0; `index_map` builds a row index at BSS:0): only
the pixels where there is a surface. Per row (512) two halves, x < 256
and x >= 256; a half is the byte FFh (empty) or n pixels as three arrays
of n bytes: surface (bit 7 set on the last one), angle (100h a turn),
x (low 8 bits). Checked: all fifteen files (DREAMS1 and DREAMS2) parse to their last
byte this way. Surface: low nibble the response and `surface_table` entry (3, 5,
6, 7 in the maps), 10h/20h kickers (score); 0Fh a one-way passage (a
wall from one side), 1Fh a line whose angle byte is a code (FBh/FCh
gravity, FEh to the upper level, FDh back) or an event number (0-0Fh,
the hole `map_events[n]`). `map_flags` makes a line act once per
crossing. The level switch changes the map, the hit rectangles and
lanes (`td_*_upper`), and `ball_level` (80h/FFh: the ball is drawn only
over colours below it, so it passes behind the upper level's pixels).
Checked: runs with random flipper keys went up and down on Steel Wheel
(t=24.3 s to 26.4 s: `ball_level` FFh, `hit_rects` = the upper list)
and Nightmare.

**Tables are data.** `setup_*` fill the table descriptor (`td_*`,
DATA:8494..84E0) with the table's lists; the lists end with a negative
word. The records (DATA, below 4Cxxh), each described at its user:

| record | size | fields (short) | described at |
|---|---|---|---|
| lamp | 8 | flags, lit bit per player, blink phase, state for flag 2, light (palette slot), next | `update_lights` |
| light group | 1Ah | a group + blink counter, period, chase phase | `blink_lights` |
| group | 16h | first member, sound/jingle, points (BCD 8), bonus points (BCD 8), sequence | `test_hit_rects` |
| target | 0Eh | type (1/8 lit by a hit, 7 debounced), hit bits, lamp, next, x1 y1 x2 y2 | `test_hit_rects` |
| hit rectangle | 0Ah | x1 y1 x2 y2, group | `test_hit_rects` |
| lane | 0Ah | x1 y1 x2 y2 (exclusive), object | `test_lanes_a` |
| hole | 8 or 12h | type (5 timed, 6 lock, 14h sequence), busy, lamp, group, repeat, count, ball speed and place | `map_event_run` |
| lock | 6 | hole, lamp, full | `td_locks` |
| event object | 2Ah/2Eh | type 1Eh/1Fh, what arming and firing do (sounds, lamps, points, messages, a handler at +16h), armed bits, flags, timer | `run_events` |
| sequence | var. | type 2Ah (a list) or 28h/29h (a step per player through a list of objects) | `run_event` |
| message | 8 | flags (scroll, flash, count-up), state, text | `message_step` |

Hits and holes push event objects onto the event stack (`run_event`,
directly or through a group's sequence); `run_events` pops them each
frame: an object is armed (FFFFh on the stack) or fired (0). The
table-specific code is the handlers (+16h), 183 objects in PD's DATA
point at them (found as words, now `words` hints): multipliers, jackpot,
extra ball, locks, roulette, bonus countdown, hurry-up, `advance_object`
(steps a type 29h sequence and a lamp chain).

**Players**: eight records of 98h bytes (`player_1`..): score (6 bytes
BCD, low first; checked), bonus, two hit counts (td_count_obj,
td_count_obj2 = boosters), what each lamp shows (+18h..). Lights are
palette slots from colour 40h: `light_on`/`light_off` copy the table's
on or off colours (TDATA) into `light_palette`.

**The screen**: Mode X 320x200 (or 320x350 by `opt_screen`), the table
picture (320x512) in video memory, scrolled by the CRTC start address
(`scroll`, `scroll_speed`); the display is a split screen below it,
20 characters (`draw_char`, `display_font`, TDATA). Sprites
(`sprites`, 4 records of 20h bytes) are drawn by `draw_sprites` through
an area buffer in video memory at BB10h.

**Sound**: INT 66h driver loaded by `load_sound_driver`; the routines
from `play_tune` on run with DS = XDATA: `music_files`, `main_tunes`,
`jingle_lists`, `effect_lists` per table; `tune_request`,
`jingle_request`, `sound_request` (DATA) are what the game asks for.

**Not understood / not checked**:

- `lock_jackpot` tests `[BX+974Eh]` where the lock's state `[BX+4]`
  must have been meant (only Nightmare uses it; reads BSS). `add_booster`
  adds the booster value at player record +4, so it lands above the
  shown digits. Both read, not checked by running.
- The one-way passage's direction test (`map_special`), the rows of the
  flipper shapes, the layout of `display_dots`, how the 17 flipper
  frames map to the 10 sprite frames, what the five `surface_table`
  constants are, `bounce`'s arithmetic (`mul_fix` etc.) in detail.
- `module_callback` (set with INT 66h AL=13h; presumably the end of a
  pattern), 9782h (the largest step count), byte 7CE6h.
- Not seen running: locks, the hurry-up, the roulette, tilt, jackpots,
  the high score entry.

## DDPCMAIN.EXE (T2)

Built IDENTICAL from `src/DDPCMAIN.hints` (5338 instructions); the gaps
left are data (docs/tasks/T2.md lists them). Segments: CODE, DATA, FONTS
(the menu's glyphs), OPTIONS (the F10 screen and DDPCOPTN.BIN), HISTORY
(the F9 viewer: file names, the 53 table records with manufacturer, year,
designer, the filters), STACK. From the code, not run:

- At start: its INT 9 handler, DDPCINTR.EXE with `2` or `3` (bit 0 of
  the PIT's channel 1: presumably a random choice of the CD track),
  DDFLIPLY.EXE once with `0`, then the menu (`select.vga`, both
  HISCORES files, INT 33h mouse).
- F1-F8 (`run_selection`, `run_table` at CODE:0B59): INT 9 back to the
  old handler, DDFLIPLY.EXE with the key's digit, 46h frames, then CHDIR
  `..\DREAMS1` and EXEC PD.EXE for F1-F4, `..\DREAMS2` and PD2.EXE for
  F5-F8, with a command tail of one digit `0`-`3` (the table), then
  CHDIR `..\deluxe`. So the table programs' digit is the table.
- F10: the options screen; `save_options` writes the 13-byte
  `C:\DELUXE\DDPCOPTN.BIN` (four choice bytes, four key words, one
  resolution byte) when options are saved, not before every EXEC.
- The menu loads the sound driver itself (SOUND.CFG, EXEC of the .SDR)
  and gives it a tick callback (INT 66h AL=0Bh, CODE:4421).
- F9, the history viewer (T7, `tools/ddfiles.py`; docs/tasks/T7.md):
  `history_init` changes to `..\HISTORY`, shows DDPCHIST.VGA in Mode X
  (its palette is `history_palette` in the program), detects VESA mode
  101h (`vesa_available`), loads HISTORY.FNT (256 8x8 glyphs) and the
  language's .IDX (53 five-byte records: HOP offset, end marker offset,
  strings minus one); `load_history_hop` loads the .HOP when the table
  browser is entered. A nine-byte table record in HISTORY selects the
  IDX record and one or two picture names: `.016` (four VGA planes,
  640x480, mode 12h with the default palette) or, with VESA, the same
  name as `.256` (768-byte DAC palette, then 640x480 bytes). CR/LF after
  a HOP string adds a row only in the 16-colour view. Not opened by any
  path: BIGBRAVE.025, LIZARD_1.1, CLRFILE.CV4, INTRO2.MOD (the only
  `load_sound` call passes index 0). All from the code and the files,
  not run.

## DDPCINTR.EXE (T1)

Built IDENTICAL from `src/DDPCINTR.hints` with 153 of the 155 names from
its CodeView (NB08) tail; `tools/cv4.py HINTS [--hints]` reads it
(docstring: the layout). Debug segments 1-4 are the frames CODE, PALSEG
(four 768-byte palettes and a table of their offsets), DATA (debug name
VARIABLES), STACK. Not named yet: `delaytable` (DATA:003A) and
`paltable` (PALSEG:0C00), both reached only as displacements (`[BX+3Ah]`,
`LEA SI,[0C00h]`), which no hint kind names yet. The program, from the
code (not run; the runner has no MSCDEX): shrink memory, read one digit
from the command line (the CD track), Mode X, own INT 9, load four
pictures into VRAM, start the track, show three pictures with fades and a
time each, the fourth while MSCDEX reports the drive busy, fade out,
stop, exit with code 0 in Mode X (no text mode). For the port: its
failure exits before the INT 9 hook restore INT 9 to 0000:0000 (the saved
vector is still zero), and every stop request before the drive search
goes to drive 0. Details and addresses: docs/tasks/T1.md.

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

The source says `ASSUME DS:` wherever the tracked DS (or a `ds` hint)
holds another segment of the program, so code running with DS = XDATA
or TDATA gets labels of those segments; `es` hints do for ES what `ds`
hints do. A `ptr` hint on an instruction without immediate names its
address operand (a LEA of what is later read with another DS). A
`words` table is written as DWs with or without a label at its start.

`xfer.py` carries PD.hints over to PD2.hints: of 8,276 and 8,228
instructions 8,042 align by their shape. A code address is carried only if
the next ten instructions look the same at the new place; what cannot be
carried is left as a comment. Run it again after changing PD.hints (it
replaces its own block in PD2.hints, keeps the lines above it). A
`words` table of code pointers is carried only when PD2's words at the
mapped place are the mapped routines: the objects of each program's own
tables are listed in its own part (PD2: 188). Comments carry too, and a
`SEG:OFF` in a comment's text is mapped like the address: a comment
whose text has an address that cannot be mapped is left out (write
`BSS:0000h`, not `BSS:0`, when the address is only prose).

## Running the originals (tools/run)

`build/pddrun.exe` (C, MSVC; `tools/run/build.bat`, or let `run.py`
build it) is a headless PC: a 386 real-mode CPU, VGA (planar, Mode X,
the retrace latch of the start address), PIT/PIC/keyboard, BIOS, the 8237
DMA and the Sound Blaster DSP, so the real .SDR drivers run. The DOS layer
keeps memory, PSPs, EXEC and resident drivers (released with the program
that loaded them); every drive letter is the same tree, the CD
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
(picture n is at n*DT), `-dumpevery DT` (the `-dump` regions every DT
seconds: variables as a time series), `-wav`. Only one `-watch` at a
time. A key script: `-key T KEY` (tap),
`KEY+`/`KEY-` (down/up), or `-keys FILE`.

To get the ball somewhere (a ramp, a hole) without a window: key
scripts with random flipper presses and a launch every 10 s, many seeds
in parallel (8 runs of 200 emulated seconds take 1.5 minutes here), a
`-log` on the routine that should be reached; then repeat the seed that
hit with `-dump`/`-shot`. Found the level switch this way in 3 of 24.

Not done: savestates, a window, the menu programs (DDPCMAIN, DDPCINTR:
untried; DDPCINTR wants MSCDEX for CD audio, which the DOS layer does not
have). The runner is not part of `check.py` (it needs MSVC and the CD).

## Working with a second agent

Sol works in the worktree `../pddnative-muse` on `sol/*` branches
(earlier tasks: `muse/*`); the rules are in AGENTS.md, the tasks in
docs/TASKS.md. `tools/check.py` guards every commit through the hook.

## Next

1. The menu program DDPCMAIN the same way (T2, Sol); DDPCINTR is done
   (T1). The FLI player needs no disassembly (FLI is documented).
2. Understanding, into the hints: PD.EXE's engine is done (T4, "Not
   understood" above lists what is left); the formats FLIPPERS.SPR,
   DDPCICON.SPR, the DELUXE .VGA pictures and TABLE2M (T8) are not
   described yet; the collision maps, HISCORES.PD* and DDPCOPTN.BIN are
   (T5, `tools/pdfiles.py`), the history viewer's files too (T7,
   `tools/ddfiles.py`). The table data is still DB lines apart from the handler words: a
   `struct`/`dw` hint kind will be needed before data can move.
3. The runner (done, T3) for comparing the C port with the original
   frame by frame will want savestates (start both at the same moment)
   (`-dumpevery` dumps chosen variables at a fixed interval).
4. The implementation in C (`port/`, T6): started 2026-09-27; every game
   state and the event objects' handlers are translated, and checked
   against runs of the original stopped at the same place (the idle show
   and a ball with the flippers on all eight tables; on Steel Wheel a
   whole game: three balls, the bonus count, the initials, pause and
   tilt; state 4 entered by writing game_state, in both screen modes;
   `tools/portcmp.py` runs such comparisons from a key script, and can
   write variables and give an options file; port/README.md says where
   and how). A run confirmed that a locked ball never ends state 4 in the
   320x200 mode (the default) in the original: its scroll loop cannot
   reach its end (see the hints at `st_ball_locked`). A ball locked by
   play is equal too (Nightmare, with locks_lit written). Whole games are
   equal at each drain and the game over (port/README.md): `tools/portplay.py`
   plays the port (flippers tapped over their tips, one port run a tap) and
   writes the keys file; the roulette, hold_bonus, extra_ball_award,
   count_message, the locks and the level switches ran on the way. The port
   traces the handlers it runs (PD_TRACE notes), so a game shows what it
   reached. Not reached by any compared game yet: collect_jackpot,
   lock_jackpot, add_hurry_value, countdown, double_score, double_bonus,
   score_to_best, nightmare_switch, mult_4 and up, light_locks.
   Next: those (more portplay seeds, or --poke into the state before
   them), Nightmare (portplay's zone does not suit it); then the sound: the driver's tick is 70.09 a second in the
   320x200 mode (measured, SBLASTER.SDR); what AL=11h (effects) and
   AL=13h (`module_callback`) do for the driver is still to be found, and
   the port's player does not advance in the headless build (XDATA's
   music_pos differs).
