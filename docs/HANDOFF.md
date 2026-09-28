# Handoff

State of 2026-09-28: stage 1 (source from the programs) done for the two
table programs, the intro (T1) and the menu (T2); a headless runner (T3)
runs them; PD.EXE's engine named, commented and its records described
(T4); the implementation in C (T6, `port/`) has every game state
translated and matches the original through a whole game on Steel Wheel;
the menu (DDPCMAIN) is translated too (`port/src/menu.c`) and matches
DDPCMAIN run in tools/run, a table run from it included.

## Start here (next session)

- `master` has the remote `origin` (GitHub, mindphluxnet/pddnative); the
  user pushes, push only when the user asks.
- AGENTS.md has permanent provenance rules: read them before anything
  else.
- Also builds and runs on macOS (2026-09-28): `port/build.sh` and
  `tools/run/build.sh` (cc), the window on SDL2 (`port/src/plat_sdl.c`,
  SDL2.framework in ~/Library/Frameworks), `python3` with capstone. On the
  Mac, GOG's image is inside the app: `/Applications/Pinball Dreams
  Deluxe.app/Contents/Resources/game/Pinball Dreams.app/Contents/Resources/
  Pinball Dreams.boxer/game.cdmedia/game.gog` (for gogx.py). The hints
  and docs have CRLF line ends; `xfer.py` writes LF on the Mac (convert
  back before committing). Checked there:
  check.py all ok; portcmp equal at idle_loop#3000 and through portplay's
  Steel Wheel game (seed 1) at each drain and the game over (again after
  the runner's change of 2026-09-28 below, with PD2's games of tables 1
  and 3).
- The work so far went in numbered items, T1-T14 (the T numbers in this
  file): stage 1 of DDPCINTR (T1) and DDPCMAIN (T2), the runner (T3),
  PD.EXE's engine in the hints (T4), the implementation in C (T6, under
  way), `tools/pdfiles.py` (T5), `tools/ddfiles.py` (T7),
  `tools/gfxfiles.py` (T8), `tools/flifiles.py` and DDFLIPLY (T9), the
  launcher (T11), the FLI player and the quality of life fixes (T14).
  Their notes (findings, addresses, what was checked and what not) are in
  the history: `git show c070cfe:docs/tasks/T9.md` (T1, T2, T5, T7, T8,
  T9, T11, T14). One agent, Claude, works on `master`. The runner and the
  engine: "Running the originals" and "The engine" below; the C:
  port/README.md.
- Where the port is going and what is left: "Next" below. The launcher,
  the FLI animations, the menu and the controllers are not yet tried in
  the window.
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
| `DELUXE/DDFLIPLY.EXE` | FLI player (Borland C++ 1991): `0` the intro FLIs, `1`-`8` the table's FLI (see "DDPCMAIN.EXE") |
| `DELUXE/*.FLI` | Autodesk FLI, 320x200: a BRUN frame, then LC frames, one palette, a ring frame (T9, `tools/flifiles.py`) |
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
left are data (T2's notes list them). Segments: CODE, DATA, FONTS
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
- DDFLIPLY.EXE (T9 and its notes; read, not run): the first
  non-space character of the command tail minus `0`; 1-8 play IGNITION,
  STEELWHL, BEATBOX, NIGHTMRE, NEPTUNE, SAFARI, REVENGE, STALLTRN (table
  at 01F5:00F7), 0 plays SPIN21ST, INTRO_P1, INTRO_P2, INTRO_P3 in turn
  (01F5:013B). A frame waits the header's speed x 41h units of a clock
  of 256 units per BIOS tick (4661 a second): 13.9 ms a jiffy, not 14.3,
  so speed 3 is 41.8 ms (24 pictures a second). The ring frame is not
  shown. A key ends the playback (the callback 01F0:0003 polls kbhit,
  0000:1266, while it waits); the key is not read, so with `0` the
  remaining intro files end at once too, and the buffer is emptied
  before the exit (0188:00A7). With no argument (or a character below
  `0`) BL keeps the low byte of the PSP segment: 0 or 80h-FFh plays the
  intro, others a table entry or beyond the table.
- F10: the options screen; `save_options` writes the 13-byte
  `C:\DELUXE\DDPCOPTN.BIN` (four choice bytes, four key words, one
  resolution byte) when options are saved, not before every EXEC.
- The menu loads the sound driver itself (SOUND.CFG, EXEC of the .SDR)
  and gives it a tick callback (INT 66h AL=0Bh, CODE:4421).
- F9, the history viewer (T7, `tools/ddfiles.py`, and its notes):
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

### The menu run and translated (2026-09-28)

DDPCMAIN runs in tools/run with `-loadfix` (below): DDPCINTR (12.2 s,
pictures; no CD audio), DDFLIPLY with `0` (a key ends it), then the menu
with SBLASTER.SDR playing INTRO.MOD. Without `-loadfix` the driver ends
with "Packed file is corrupt": the menu is small, the driver lands below
64 KB (at 0FB1h), where EXEPACK's unpacker fails. `port/src/menu.c` is
the menu in C (port/README.md), its names in `src/DDPCMAIN.hints` (the
menu's part named for the port: list, pointer, ticker, high scores,
fades). Found on the way, checked in tools/run:

- `draw_hiscore_char` starts its plane loop with AX = the FONTS segment
  (just loaded into DS), not char_x: a glyph lands at char_x rounded down
  to 4 plus (FONTS AND 3), so the high scores' x depends on where DOS
  loaded the menu (FONTS 177Eh under `-loadfix`: 2 pixels left of 1Fh).
- `show_menu_start` leaves menu_start in CX: after the high score show
  the list runs 2D0h passes (the fade_in's count, set after it), after a
  table chosen in the show 3C0h (menu_start), not the 2D0h set before.
- The first tick after `load_sound`: the driver's timer runs from its
  loading, and its next tick calls the routine before the fade's first
  wait (after AL=4 at the menu's start, between AL=0Bh and AL=4 at a
  load after a table: at each of the loads looked at), so the ticker
  moves once before the fade.

#### The menu's tick

The driver's tick moves the pointer and the ticker, so the ticker counts
ticks. Where the menu waits on the keyboard alone (`wait_keys_up`, a
REPE SCASB loop) it goes on as soon as the key's interrupt comes and runs
into the fade's first step (768 MUL/DIV, 8,483 instructions, 1.41 ms at
the runner's 6 M a second); if the driver's timer interrupt comes before
that step reaches its wait, the tick is taken inside it and the wait gets
the next one. portcmp places a key 0.5 pictures before a pass, and the
menu's passes come 4.3 ms after the tick (AL=8 mixes that long in the
runner), so a key let go lands about 2.9 ms before a tick, and the
driver's INT 8 comes 0.15 to 1 ms before its callback: in two of the runs
looked at the interrupt came first (F3 after the pointer moved, let go 5
or 6 pictures after the pass: 51 instructions before the wait), in the
others after. The port (`frame_wait_keys`) models the step as done before
the tick, as a PC much faster than the runner's 6 MIPS would have it; not
checked on a PC.

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
goes to drive 0. Details and addresses: T1's notes.

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

`-loadfix` (2026-09-28) puts a DOS block over the first 64 KB, as DOS's
LOADFIX does, so the programs load above it: DDPCMAIN needs it (its
EXEPACKed sound driver otherwise lands below 64 KB and its unpacker
fails); the menu's CODE is then at 1018h. A timer interrupt used to be
delivered after a batch of up to 256 instructions, a REP string
instruction counted as one: in DDPCMAIN's key wait (REPE SCASB, 32
elements) it came up to 1.4 ms late, and the sound driver's INT 8,
which waits for the retrace's start, then waited a whole frame (a tick
every other frame while a key was held). The batches are now 256
cycles (a REP element one); after the change portcmp is equal again on
Steel Wheel's whole game (portplay seed 1, each drain, the game over)
and PD2's tables 1 and 3 (the games of `build/g21.txt`, `g23.txt`).
`run.py` takes `EXE:NAME` for a name of another program's hints (the
tables the menu runs: `PD.EXE:idle_loop#100`).

Not done: savestates, a window. DDPCINTR runs but without CD audio (no
MSCDEX in the DOS layer). The runner is not part of `check.py` (it needs
a C compiler and the CD).

## Next

1. Where the port is going (the user, 2026-09-28): it modernises the
   game, and parts of the original that get in the way may go. The
   launcher (done: a setup program in text mode with the game's options,
   the sound and the quality of life fixes) comes first; after it the
   original menu, translated, is the hub; Esc from a table goes back to
   where it was started from. Game controllers. The FLI animations are
   played unless the quality of life fix "skip animation" is on.
2. The menu in C (DDPCMAIN): done 2026-09-28 (`port/src/menu.c`, "Play
   from the menu" in the launcher, `pdd -menu`; checked headless against
   tools/run, port/README.md), apart from: F9 (the history viewer) and
   F10 (the options screen; perhaps the launcher's options page instead),
   the mouse (the platform has none yet), the intro DDPCINTR (pictures
   and a CD track; the GOG release has the tracks as ogg). To try in the
   window: the menu, the intro's animations before it, a table and back.
   The table programs' `wait_keys_up` waits a picture with its tick
   (pump_frame) where the menu's now goes on before the tick
   (`frame_wait_keys`, "The menu's tick"); whether the tables need the
   same is not checked (their comparisons are equal as they are).
3. Game controllers: done 2026-09-28 (`port/src/pad.c`, port/README.md):
   SDL2's game controller API in `plat_sdl.c`, XInput in `plat_win32.c`;
   the buttons become the scan codes of keys in the keyboard's queue (in
   a table the four game keys of the options, F1 and P on Start, Esc, Y
   and N; arrows, Enter and Esc in the setup screen and the menu), a
   "Controller" page in the setup screen maps them. Checked: built on the
   Mac without warnings, `pad.c`'s key logic by a small test (two buttons
   on one key, a release after the context changed, stick and D-pad
   together), check.py all ok, portcmp equal through portplay's Steel
   Wheel game (seed 1) after the change. Not checked: with a real
   controller (none here), the Controller page on the screen,
   `plat_win32.c` not compiled (no Windows compiler on the Mac; XInput
   loaded with LoadLibrary, `xinput.h` from the Windows SDK).
4. Later: the sound drivers (DELUXE/*.SDR, SOUND.CFG) described by a
   tool, as the other files are. The port plays the music with micromod
   and needs only SBLASTER.SDR's timing, which is described below ("The
   music's timing").
5. Understanding, into the hints: PD.EXE's engine is done (T4, "Not
   understood" above lists what is left); the collision maps, HISCORES.PD*
   and DDPCOPTN.BIN are described (T5, `tools/pdfiles.py`), the history
   viewer's files (T7, `tools/ddfiles.py`), the DELUXE .VGA pictures, the
   .SPR files and TABLE2M with the palettes the programs set (T8,
   `tools/gfxfiles.py`; PD2's `set_palette` is not PD's, see T8's
   notes), the .FLI files and DDFLIPLY (T9, `tools/flifiles.py`). The
   table data is still DB lines apart from the handler words: a
   `struct`/`dw` hint kind will be needed before data can move.
6. The runner (done, T3) for comparing the C port with the original
   frame by frame will want savestates (start both at the same moment)
   (`-dumpevery` dumps chosen variables at a fixed interval).
7. The implementation in C (`port/`, T6): started 2026-09-27; every game
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
   reached. The handlers no game reached were fired by putting their
   object onto the event stack with `--poke` (port/README.md), in a game
   of portplay's, and are equal after each (2026-09-28): on Steel Wheel
   double_score, double_bonus, mult_4, collect_jackpot, countdown,
   light_locks, add_hurry_value, score_to_best (also with three players,
   player 2 up); on Beat Box the same with mult_8 and mult_10; on
   Nightmare lock_jackpot (with the two bytes its test reads as they are:
   "JACKPOT 2 X", one of them lies in the collision map and is not 0; and
   both set: "3 X") and nightmare_switch; on PD2.EXE's table 3 the same
   set with nightmare_switch. mult_5, mult_6 and mult_7 are the same
   routine with another number and were not fired.
   The user plays games by hand in the window (`pdd -record`) and hands
   over the recordings (`build/play*.rec`); `portcmp.py --record` replays
   them in the original (port/README.md). Equal so far: Nightmare
   (play1.rec), Steel Wheel (play-table1.rec) and Ignition
   (play-table0.rec) to the attract show after the initials. Ignition's
   high score entry was at first not reached by the original: its keys were then
   placed as st_game_over#1+K over 1,460 pictures, and the picture rate
   there is not 70.087 (Steel Wheel: 70.155 from the game over to the
   700th idle frame), so a key lands a picture off. The checkpoint
   `initials_loop` (a pass every 6 pictures, CODE:269A) was added for
   that; the recompare with it (`portcmp.py --table 0 --record
   build/play-table0.rec --until 900 st_game_over#1 initials_loop#100
   idle_loop#700`) is equal at all three (on the Mac, 2026-09-28).
   Fixed on the way: jingles (the driver's pattern-jump callback, AL=13h,
   and AL=10h's return value, from SBLASTER.SDR), stuck flippers in the
   window (Windows sends no key-up for the first of two Shift keys), and
   portcmp's key times over long spans (re-anchored every 1000 passes).
   The user played all eight tables in the window on the Mac
   (2026-09-28), some for long, and found nothing wrong that the original
   does not do as well (PD2's tables only started). The headless build
   moves the module as SBLASTER.SDR mixes it (2026-09-28), so XDATA's
   music_pos is compared too (portcmp no longer leaves it out); see the
   next point. The window's sound can be shaped (`-fx`, `audiofx.c`:
   shelves, oomph, a headphone mode) and has keys in the game (+ - * /,
   `hud.c`, a box drawn over the picture); neither reaches the game's
   memory or the headless build.
- The music's timing (2026-09-28): SBLASTER.SDR (quality 0, loaded at
  1ABE under PD.EXE and 1AC8 under PD2.EXE in tools/run) mixes in
  240-sample chunks at 12 kHz ([795], one module tick at 125 BPM) into a
  960-sample DMA buffer (CS:[0B35]). Play (AL=4, 0611h) mixes four ticks
  and starts the DMA; each AL=8 (from `timer_callback`, once a program
  tick) mixes from the write position (DS:[0897]) up to the DMA's read
  position (0C7Ah; 15F4h reads the DMA count), a whole tick when 240 are
  free, else what is free if at least 50h, a part up to the next tick
  boundary when the write position is inside a tick; a tick is stepped
  (CS:[0B58] counts them) when a chunk starts on a boundary. The DSP runs
  single-cycle blocks at time constant 173 (1e6/83 Hz); in tools/run the
  read position moves 171.9043 samples a program tick (12048.136 Hz
  against 70.0863 Hz). `port/src/sound.c` models this for the headless
  build (the comment there has the phase and the accuracy). The driver's
  INT 8 handler (1C2Fh) walks a list of timed calls synchronised to the
  vertical retrace and can mix itself when the buffer runs low; with AL=8
  every tick that did not happen in the runs looked at. Not modelled: the
  window build, where the audio device's buffer sets how far ahead
  micromod plays. AL=10h (a tune or jingle started) sets the driver's
  position at once and its row counter to 1, so the jump follows at the
  next tick after one more row of the old pattern, and a second AL=10h
  before that tick gets the new position minus one (the handler at
  01FBh of the unpacked driver, the row code at 0D73h-0E04h). The port's
  `modplay_jump` does the same; with a jump at the row's end the jingles
  of a game's start went back to the attract tune (heard by the user,
  2026-09-28).
- The sound effects (2026-09-28; the user heard none on Beat Box, the port
  had AL=11h empty): SBLASTER.SDR is EXEPACKed (unpacked by hand for
  reading; the INT 66h dispatcher at 0049h of the unpacked code, DS
  segment 01E2h). It converts the module's pattern cells at loading to 3
  bytes (note 1-36 by the place of the period in its table 856..113,
  sample, effect, parameter) and plays a row through one routine per
  channel (0E05h). AL=11h builds such a cell and hands it to that routine
  at once: DL the channel (from 1), BL the note, CL the sample, BH the
  volume as effect C (0: none). The entries read (the first 19 or 20
  of each of the eight tables' lists) all use channel 4, mostly note 13
  (C-2) at volume 64. The port does
  the same in micromod (`modplay_note`). Checked: the effects are called
  on Beat Box in play (a trace build) and micromod plays a sample then;
  portcmp on Beat Box equal at st_play#100, the drain and the next
  ball. Heard in the window on the Mac (the user, 2026-09-28). Not
  checked: how the driver's own mixing of an effect against the module's next row on
  channel 4 compares with micromod's beyond using the same cell.
