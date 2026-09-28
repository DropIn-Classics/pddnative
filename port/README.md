# port: Pinball Dreams in C

A native compatibility implementation requiring an installed copy of
Pinball Dreams: the table programs PD.EXE and PD2.EXE and the menu
DDPCMAIN.EXE translated to C, routine by routine, from the source that
`tools/disasm.py` generates from the player's own copy. The game's data (the tables' records, pictures,
collision maps, music) is read from that copy at run time; none of it is
in this repository.

## State

Started 2026-09-27. Translated: start-up, the main loop, state 1 (loading
the table: files, the table's set-up for all eight tables, lights, screen
mode, the table picture, the flipper sprites, the palette, the music),
state 2 (the attract show with the idle texts and Ignition's tour, the
lamps, the display messages, Esc to quit), a new game and a new ball (the
players, lamps, objects and sequences reset, the event stack), state 3
(the ball in the plunger lane: the jingles, "player n", the plunger, the
nudge, the scroll following the ball), state 5 (a frame of play: hit
rectangles, lanes, holes and locks, the scores in BCD, the event stack and
its objects, the lamp groups, the display's messages and hurry-up) and the
ball's physics (`ball_step`: the flippers' shapes, the collision map, the
bounce), the event objects' handlers (the tables' own code: multipliers,
jackpots, locks, the roulette, the hurry-up ...), state 6 (the ball lost:
the bonus times the multiplier counted, the boosters, the next player, a
locked ball back into play). The program stops at the first routine not
translated yet, with its name, and leaves its last picture on the screen.
All the states are translated: also 7 (game over: the high scores with
the initials entered, the players' scores in turn), 4 (a ball locked),
8 (pause), 9 (tilt) and 12 (set nowhere).

Checked: the port's memory equals the original's in tools/run (DATA, BSS,
TDATA, XDATA and all of video memory byte for byte; different only where
the port has no reason to be the same: the saved INT 9 vector, the scratch
space of the driver's EXEC in CODE, the stack)

- at the entry of `st_idle` for Steel Wheel (PD.EXE) and Safari (PD2.EXE);
- at the 3000th frame of state 2 (the idle loop's head, CODE:016A in
  PD.EXE, CODE:0169 in PD2.EXE) for all eight tables; Ignition also at the
  6000th and 9000th, through its tour (t = 44 s to 120 s);
- on Steel Wheel with F1 seen in the 260th idle frame (port: picture 265,
  runner: -key 5.443 f1) and the plunger pulled for 30 frames (port:
  pictures 744 to 774, runner: 12.67774 to 13.10576 s): at the 1st, 60th,
  100th and 132nd frame of state 3's loop (CODE:0553), the last before the
  ball leaves the lane in both; then at the 1st, 100th, 300th and 615th
  frame of state 5 (`st_play`), the last before the ball drains (18,000
  points scored on the way, the flippers not touched); at the entry of
  state 6 and of the next ball's state 3 (the bonus count between); ball 2
  (plunger: port pictures 2208 to 2238, runner 33.80946 to 34.23749 s) at
  its 1st, 185th and 461st frame of play (event handlers run on the way),
  the third ball (plunger: pictures 3518 to 3548, 52.74323 to 53.17125 s)
  to the drain; state 7 at its entry and, after three taps of Space for
  the initials (pictures 4700, 4760, 4820, 8 pictures each; the runner's
  times from state 7's entry, t = 68.472871 s at picture 4621), the
  attract show again at the 261st and 700th idle frame: a whole game, the
  score (1,052,000) in the high scores' fourth place. The ball's bounces
  on the table and the kickers are thus checked.
- the pause (P at picture 926, A at 1000; runner times 13.869301 +
  (picture - 827.5) / 70.09 s): equal at the 100th, 101st and 300th frame
  of play; tilt (Space tapped at play frames 230, 240, 250, 260, 270, in
  one window of nudge_timer): equal at state 9's entry, the drain and the
  next ball's start. State 12 is not checked by a run (nothing sets it).
- state 4 (a ball locked), entered with game_state set to 4 at the 100th
  frame of play (`--poke`, below), on Ignition and on PD2.EXE's second
  table: in the 320x200 mode (the default) its scroll loop does not end
  in the original either (equal at its 1st and 300th pass; see the hints
  at `st_ball_locked`); in the 350-line mode (opt_screen 2, from a
  DDPCOPTN.BIN) it ends after 73 passes, and the next ball is equal at
  its plunger and at the 1st and 300th frame of play. The attract show in
  the 350-line mode is equal at its 200th and 700th frame. A ball locked
  by play, on Nightmare (locks_lit set at the first frame of play, then
  129 random flipper taps; the ball goes into the lock at the 500th
  frame): equal at the 250th and 499th frame of play, at state 4's entry
  and at its loop's 100th pass.
- the flippers: 91 random taps of the Shift keys (4 to 14 pictures each,
  3 to 25 apart, from picture 900; the runner's times as above, given
  with -keys): equal at the 200th, 500th and 992nd frame of play (the ball
  drains after it) and at the next ball's start.
- all eight tables (PD.EXE's and PD2.EXE's) with `tools/portcmp.py` and
  one key script (F1, the plunger, 60 random flipper taps in play): equal
  at the 200th frame of play, the drain and the next ball's start.
- whole games, from F1 to the game over, with `tools/portcmp.py` at each
  drain and at state 7's entry: all equal.  With random flipper taps
  (built on the port until they are placed where the ball is in play):
  Nightmare (a ball locked, 2,841 frames of play).  With
  `tools/portplay.py`: Steel Wheel (seed 1: the roulette, `hold_bonus`,
  six trips to the upper level; 187 s), PD2.EXE's table 3, .STT (seed 2:
  the roulette, locks, an extra ball, four balls lost; 300 s) and table 1,
  .SFR (seed 1: `count_message`).
- games played by hand in the window (`pdd -record`, replayed with
  `portcmp.py --record`), equal at each drain, the game over and the
  700th idle frame after the initials: Nightmare (three locks, seven
  trips to the upper level, 8,845 frames of play) and Steel Wheel (eight
  trips up, `mult_3`, `count_message`, 11,850 frames; its third ball
  alone 10,700 frames: equal also at the frame where a key had first been
  placed a picture late in the original, before portcmp counted a key's
  time on over 1000 passes at most).
- the event objects' handlers no game reached, fired by `--poke`
  (the object onto the event stack: `event_stack 0000` and the object's
  offset, `event_sp` the stack + 4) at a frame of play of a portplay game,
  equal after each: on Steel Wheel double_score, double_bonus, mult_4,
  collect_jackpot, countdown, light_locks, add_hurry_value, score_to_best
  (also with three players); on Beat Box the same with mult_8 and mult_10;
  on Nightmare lock_jackpot (the bytes its test reads as they are, and
  both set with `map_flags+4C0E`, `map_flags+4C14`) and nightmare_switch;
  on PD2.EXE's table 3 the same set with nightmare_switch.
- A ball can come to rest in the original: on Steel Wheel at (279, 25),
  top right, its speed changing while it stays (random taps, from the
  1,100th frame of play on); the port equal at the 1,000th, 1,100th and
  1,500th frame.
- XDATA's `music_pos` (the module's position INT 66h AL=10h returns when
  a tune or jingle starts): equal in the whole games above since the
  headless build moves the module as SBLASTER.SDR mixes it (2026-09-28,
  `src/sound.c`); without that it differs (Steel Wheel's portplay game:
  0Fh against 2Fh at the second drain). The driver's mixing is read from
  its code and checked against it in tools/run: fed with the DMA
  position the driver read, the model steps the module's ticks as the
  driver did at each of 2,681 AL=8 calls. With the read position as the
  port computes it (the program's ticks, a phase, 0.53 ms later when
  `draw_sprites` drew) the ticks so far are one off at 1 to 5 of some
  4,000 calls in a minute, so `music_pos` can still differ now and then;
  only the sound routines read it. AL=10h sets the driver's position to
  one less at once and its row counter to 1: the next tick plays the
  current pattern's next row and then goes to the new position
  (`modplay_jump`). It matters at a game's start: F1 sets the main tune
  and five frames later jingle 2 keeps the position AL=10h returns as
  the one to go back to after the jingles, the main tune's. `music_pos`
  is equal at st_ball_start#1 and ball_start_loop#1 after F1 on all
  eight tables.

### The menu (DDPCMAIN.EXE)

Translated (2026-09-28, `src/menu.c`): the list of the tables with the
pointer the keyboard moves (the arrows two pixels a picture, Enter a
click; the list scrolls when the pointer is at its top or bottom), F1-F8,
the ticker line, the high score show after 870h pictures without a choice
(both programs' high scores, read from the HISCORES files anew after each
table), the fades, the music (INTRO.MOD, as the menu's load_sound has
the driver play it), a table run in the same process: the menu's memory
is kept aside while the table's program runs where tools/run loads it,
and put back after it, as under DOS the table ran above the menu. Not
translated: the intro (DDPCINTR.EXE, a CD audio track and four
pictures), the history (F9) and the options screen (F10; the setup
screen has the options): chosen, the menu fades out and comes back, as
it does when the history's language screen is left with Esc. No mouse
(the program sees no mouse driver, as in tools/run).

The box (the quality of life fix "menu box", `-menu -qol`; the port's
own): no pointer; a frame (a dark line, three pixels of yellow and white
in turn every 24 pictures, a dark line) drawn into the list's picture
around the entry of `menu_ranges` chosen, the pixels under it kept and
put back when it moves. Up and Down move it among the eight tables (held,
after 20 pictures every 6), Enter runs the table, F1-F8 as before (the
box goes to the table); the list scrolls up to 6 rows a picture until
the entry is on the screen, and at once after a table and the high score
show. A move starts the count to the high score show anew; in the show
Enter and the arrows go back to the list. The box stays on its table
while the program runs. Checked headless (`PD_DUMP` pictures): the box on
F1 at the start, five Downs to Safari with the list scrolled, F8 after
nine, Enter on F2 runs Steel Wheel, on F8 Stall Turn and back (Esc, Y)
with the box on F8, Down in the high score show back to the list; without
`-qol` portcmp `--menu` still equal (Down held, menu_loop#100, #230,
hiscore_show_loop#700). Not tried in the window or with a controller.

Checked with `tools/portcmp.py --menu` (below) against DDPCMAIN in
tools/run (`-loadfix`), the menu's segments and all of video memory at
the passes named: equal, apart from what DOS keeps for the program (the
command tail and stack of its EXECs, the driver name from SOUND.CFG,
the drive and directory load_sound keeps, the saved INT 9 vector, the
block the picture is read into, the Space that ended the runner's
intro), at

- menu_loop#100, #2160, #2161, #2500, #2880, #2881 and
  hiscore_show_loop#1, #700, #1440, #1441, #2000 without a key: the list,
  the first high score show, the list for 2D0h passes, the second show;
- with the Down arrow held for 100 passes (the list scrolls at the
  bottom), Right, Up: at menu_loop#150, #230, #450;
- the pointer on the first row and Enter: Ignition runs (Esc, Y in its
  attract show), back in the menu at menu_loop#121 and #1500;
- F6 in the high score show: Safari (PD2.EXE), back at menu_loop#2161,
  the list for 3C0h passes to #3120, the next show at
  hiscore_show_loop#301 and #1000;
- F3 after the pointer moved (the list scrolled): Beat Box, back at
  menu_loop#502 the ticker is a step apart (ticker_x, the ticker's
  buffer and its pixels; the rest equal), with F3 let go 5 or 6 pictures
  after the pass: in the runner the driver's timer interrupt came into
  the fade's first step there, before its wait (docs/HANDOFF.md, "The
  menu's tick").

Not tried in the window: the menu, the intro's animations before it.

## Build and run

    port\build.bat
    port\build\pdd.exe -game game -prog 1 -table 1

`build.bat` needs MSVC (the Visual Studio 2019 Build Tools, or what
vswhere finds) and builds `build\pdd.exe` (a window) and
`build\pdd-headless.exe` (no window or sound, a virtual clock; see
`src/plat_null.c` for `PD_FRAMES`, `PD_KEYS`, `PD_DUMP`).

On macOS and Linux `port/build.sh` builds `build/pdd` and
`build/pdd-headless` with `cc`; the window is SDL2 (`src/plat_sdl.c`):
SDL2.framework in `~/Library/Frameworks` or `/Library/Frameworks` on a
Mac, else what `sdl2-config` or `pkg-config sdl2` give. Without SDL2 it
builds the headless program only. `-game` is the
unpacked CD (`python tools/gogx.py`), `-prog 1` PD.EXE's tables, `-prog 2`
PD2.EXE's, `-table` 0-3. `-menu` starts the game's menu instead (without
the setup screen and the animations; Esc in the menu ends the program).

The sound in the window: `-fx BASS,TREBLE,OOMPH,HEADPHONE` shapes it
(`src/audiofx.c`: bass and treble shelves -12 to 12 dB at 200 Hz and
4 kHz, oomph 0 to 12 dB at 120 Hz, a headphone mode that gives the mono
music a narrow stereo image with crossfeed; `0,0,0,0`, the default, is
the sound as it was apart from a DC blocker). During the game `+` and
`-` set the volume, `*` mutes, `/` switches the shaping off and on
(the characters, from the keypad or where the layout has them; the
keypad's keys do not reach the game); a box at the top of the picture
shows what was set (`src/hud.c`, drawn into the picture after the VGA's
scan-out, not into video memory). They are kept in `pdd.cfg` (below).

### The setup screen

`pdd` without `-prog` and `-table` starts with a setup screen in the
style of a DOS setup program (`src/launcher.c`: an 80x25 text screen,
`src/textmode.c`, drawn with a font of our own into a 640x400 picture in
the game's window). With `-prog` or `-table` the table starts at once, as
before; the headless build never shows it. Arrows, Enter and Esc; F1-F8
start a table from the first two pages.

- Play a table: the eight tables by name (F1-F8 as in the game's menu;
  F1-F4 PD.EXE's tables 0-3, F5-F8 PD2.EXE's). Esc in the table (the
  game's own quit: Esc, then Y) comes back to the setup screen; the
  program is loaded anew for each table (`mem_load`, `pd_run`).
- Play from the menu: the game's menu (`src/menu.c`), the intro's
  animations before it (`fli_intro`: SPIN21ST, INTRO_P1-P3, as DDFLIPLY
  plays them for `0`, then faded out) unless the animation is skipped;
  a table chosen there gets its animation as from the table page. Esc in
  the menu (the menu's own: it ends DDPCMAIN) comes back to the setup
  screen.
- Game options: what the menu's F10 screen sets, with its ranges (its hit
  boxes at DDPCMAIN's OPTIONS:0009): balls (3 or 5), music (the tunes and
  jingles, or the main tune only), colours (colour or grey), table angle
  (1-3: gravity 9, 11, 13), screen (320x200 or 320x350), the keys for the
  flippers, the nudge and the plunger. Written at each change as the 13
  bytes of `DDPCOPTN.BIN` into `save/DELUXE` of the data folder
  (`sys_data_dir()`, `PD_DATA_DIR`), where `load_options` reads it;
  `tools/pdfiles.py options` prints it. Keys the table reads itself (Esc,
  F1-F8, P), the sound keys, and a key already set for another of the four
  are refused.
- Sound and window: the volume and the shaping (as `+ - * /` set them in
  the game), bass, treble, oomph, the headphone mode (`-fx`), full screen.
  Kept in `pdd.cfg` in the data folder (`name = value` lines), with the
  last table started; what the sound keys and Alt+Enter change in the
  game is written there when the table ends. `-fx` is kept like a setting
  made on the screen.
- Quality of life fixes, a page of four, each on its own and on by
  default (`skipanimation`, `quickball`, `quickbonus`, `menubox` in
  `pdd.cfg`); off,
  the game as the original has it. Skip animation: off, before a table
  its animation, as the menu has DDFLIPLY.EXE play it (`src/fli.c`: the
  DELUXE folder's .FLI files, DDFLIPLY's timing, a key ends it; then the
  last picture held 46h frames and faded out in 32, as the menu does).
  The other two make the table wait less where the original holds the
  game still (`pd_qol`, pd.h). Quick next ball (`QOL_NEXT_BALL`): before
  the next ball only until the jingle playing has ended (the pattern jump
  that ends it; at most the original's 69h and B4h frames), no 19h silent
  frames before the ball-lost jingle, "ball lost" 23h double frames
  instead of 46h. Quick bonus (`QOL_BONUS`): the bonus shown 1Eh frames
  before it is counted instead of 46h, counted a step every 2 frames
  instead of 4, the multiplier's steps 14h frames instead of 1Eh, held
  0Fh + 0Fh after the count instead of 1Eh + 1Eh (and the 14h after it
  left out), "bonus held" 28h instead of 46h, 0Ah after the boosters
  instead of 23h; the countdown in play (`countdown`) the same. Measured
  with an audio capture of the headless build (one ball drained without
  the flippers on each of the eight tables): from the ball lost to the
  next ball in the lane 717-765 pictures before, 371-517 with the fixes,
  of which silent 259-407 before, 19-85 with them; from F1 to the ball
  429 pictures before, 259-384 with them (both engine fixes on; measured
  while AL=10h's jump still waited for the row's end). With `-prog`/`-table` the table runs as the
  original's unless `-qol` is given (both engine fixes); a game played
  with them does not replay in the original (`-record`). Menu box
  (`menu_box`, menu.h): in the menu a frame around one table's entry in
  place of the pointer (below, "The menu").
- Controller: what each button of a game controller does in a table
  (below), one line a button, Left and Right choose; a mark before a
  button while it is held shows which is which. Kept in `pdd.cfg`
  (`pada = 3` ...).

Game controllers (`src/pad.c`): the buttons become keys, pushed into the
same queue as the keyboard's, so the programs see keys and `-record`
writes them like typed ones (a game played with a controller replays in
the headless build and the original). The window build finds the
controllers there at the start and those plugged in later: SDL2's game
controller API (`plat_sdl.c`, any controller SDL knows, named by the
Xbox layout), XInput on Windows (`plat_win32.c`, the four XInput slots,
an empty one asked every two seconds, nothing while another window is
in front). In the setup screen and the menu the D-pad and the left stick
are the arrow keys, A and Start Enter, B and Back Esc. In a table, by
default: the shoulders and the triggers the flippers, A and D-pad down
the plunger, X and D-pad up the nudge (the keys the game options set for
them), Start F1 and P together (F1 starts a game for one player where
the table waits for one, P pauses one being played; neither key is read
where the other is), Back Esc, Y and B the keys Y and N (the table's
"quit?" question). A key held by two buttons goes up with the last; a
key held by a button and the keyboard at once goes up with the first.
While a game key is being set in the setup screen the controller does
nothing.

When no game files are found (no `-game`, no `PDD_GAME`, no folder `game`
with a DREAMS1 in it beside the program, in the current directory or in
the data folder), the window first looks for the installed GOG release
(`src/gog.c`): its `game.gog` beside the program, in the Mac application
in `/Applications` or `~/Applications`, on Windows where GOG's registry
entries (`GOG.com\Games\*`, value `path`) point and in `\GOG Games\Pinball
Dreams Deluxe` on any drive, elsewhere in a few usual Wine folders. It
offers to copy the files into `game` in the data folder: the image's
ISO 9660 file system unpacked as `tools/gogx.py` does it, into
`game.part` first and renamed when complete. `-gog FILE` names the image
instead.

Comparing with the original: stop both at the same place and compare
their memory by the names of the hints. The port stops at the first routine
not translated yet or, with `PD_STOP=where#N`, the Nth time it passes
`checkpoint(where)`; `where` is a name of the hints, so the runner's
`-break where#N` stops the original at the same place: `idle_loop` (state
2's loop), `ball_start_loop` (state 3's), `st_play`, `st_ball_lost`,
`st_ball_start`, `st_game_over`, `st_tilt`, `st_ball_locked`,
`ball_locked_loop` (state 4's loop), `initials_loop` (the high score
entry's, a pass every 6 pictures). `PD_TRACE` prints each checkpoint
passed with the pictures shown so far, and as `note` lines each event
object's handler run and each switch of the ball's level (`PD_TRACE_BALL`:
the ball's pixel and speed at each frame of play); `PD_POKE="where#N OFF HEX"` writes
bytes at DATA:OFF the Nth time `where` is passed (the runner's `-poke`);
`PD_DATA_DIR` is where the saved files are looked for and written
(`save/` in it).

`tools/portcmp.py` does all of it: it takes key events tied to a
checkpoint's pass (`st_play#20 2A+`: left Shift down, seen first by the
20th frame of play), places them in both programs (the port's picture,
the runner's time), runs both to the stops given and compares:

    python tools\portcmp.py --table 1 --keys keys.txt st_play#615 st_ball_start#2

`--poke st_play#100 game_state 0400` writes a variable in both at the
same pass; `--options HEX` gives both a DDPCOPTN.BIN. Each run starts
with nothing saved (in `build/portcmp/`, a directory per run of the tool).

The results listed under "State" were found by hand this way before the
tool; it reproduces them.

`--menu` does the same with the menu: the port's `-menu`, DDPCMAIN.EXE
in the runner with `-loadfix` and a Space at 13 s that ends the intro's
animations; the checkpoints `menu_loop` and `hiscore_show_loop`, and
keys may be placed at the checkpoints of the table the menu runs (a
table of `--prog`'s program):

    python tools\portcmp.py --menu --prog 2 --keys keys.txt menu_loop#2161

`tools/portplay.py` plays a game on the port with a simple player (both
flippers tapped when the ball comes down over their tips) and writes its
keys file; `portcmp.py --keys` then runs the original with it:

    python tools\portplay.py --table 1 --seed 1 build\game.txt
    python tools\portcmp.py --table 1 --keys build\game.txt st_ball_lost#1 st_game_over#1

It prints what the game reached (the handlers run, the level switches).
A game longer than 300 emulated seconds needs `--until` for portcmp.

A game played by hand in the window replays in the original the same
way: `pdd -record FILE` writes each keyboard byte with the picture it
was handed to the program at, and `portcmp.py --record FILE` turns that
into a keys file (a key between a checkpoint's passes as `WHERE#N+K`,
written to `build\portcmp_record.txt`) and compares.  The game must start
from nothing saved, as portcmp's runs do (an empty `PD_DATA_DIR`):

    $env:PD_DATA_DIR = "build\play1"
    port\build\pdd.exe -prog 1 -table 3 -record build\play1.rec
    python tools\portcmp.py --table 3 --record build\play1.rec --until 900 st_ball_lost#1 st_game_over#1

## How it works

- `src/mem.c`: the program's memory as under DOS: one megabyte, the
  program loaded from the player's PD.EXE or PD2.EXE (checked by SHA-256)
  where tools/run loads it, relocations applied, the DOS blocks it
  allocates behind it; DDPCMAIN.EXE where tools/run loads it with
  `-loadfix` (its CODE at 1018h). The tables are data in this memory (records that
  point at each other with 16-bit offsets), so they are used where they
  lie.
- `src/gen/pdnames.h` (from `tools/portmap.py`, checked by
  `tools/check.py`): every name of the hints with its address in both
  programs. The C refers to variables and routines by these names
  (`rb(V(table_num))`), so one engine runs both programs; PD.EXE and
  PD2.EXE differ in 13 routines, handled where they are.
- `src/pd_*.c`: the engine, under the names of the hints; `pd1.c` and
  `pd2.c` each program's own set-up of its four tables; `code.c` the
  routines whose offsets the program keeps in its data (the state table,
  frame_callback, later the objects' handlers).
- `src/vga.c`: the part of a VGA the programs use (planar memory, the
  registers, the DAC) and its picture; `src/frame.c`: one picture per
  tick of the sound driver (70.087 a second in the 320x200 mode, measured
  in tools/run), which the program's `timer_callback` gets;
  `frame_wait_keys` for a loop that waits on the keyboard alone (the
  menu's wait_keys_up): when a key came, the program goes on before the
  tick, which the next wait gets.
- `src/menu.c`: the menu, on DDPCMAIN.EXE's memory, under the names of
  `src/DDPCMAIN.hints` (`src/gen/ddnames.h`, from `tools/portmap.py`);
  it shares the engine's routines that are the same code in both
  programs (the mode, the planes, the file names), and runs a table
  through `main.c`'s `play_table`.
- `src/sound.c`, `src/modplay.c`: the INT 66h functions the programs call,
  on micromod (`third_party/micromod`): loading a module, playing,
  positions, the volume, the pattern-jump callback (AL=13h) and the
  effects (AL=11h: a note put into a channel of the module at once), as
  SBLASTER.SDR does them. The window's audio device moves the module on;
  without one (the headless build) AL=8, once a tick, does it as the
  driver mixes: 240-sample ticks at 12 kHz, up to four ahead of the DMA
  (the comment in `sound.c`).
- `src/audiofx.c`, `src/hud.c`: the output's shaping and the sound keys
  with their box; the headless build has no output, so neither touches
  what is compared with the original.
- `src/fli.c`: the FLI animations as DDFLIPLY.EXE plays them (fli.h),
  shown by the window build before a table unless the quality of life
  fix "skip animation" is on; the engine's fixes are `pd_qol` (pd.h).
- `src/platform.h`: window, keys, clock, audio; `plat_win32.c` (the
  Windows SDK alone), `plat_sdl.c` (SDL2: macOS, Linux, the Steam Deck,
  where it starts fullscreen), `plat_null.c` (headless).
