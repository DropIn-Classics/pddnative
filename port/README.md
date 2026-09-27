# port: Pinball Dreams in C

A native compatibility implementation requiring an installed copy of
Pinball Dreams: the table programs PD.EXE and PD2.EXE translated to C,
routine by routine, from the source that `tools/disasm.py` generates from
the player's own copy. The game's data (the tables' records, pictures,
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
  on the table and the kickers are thus checked; the flippers' are not
  yet.
- the pause (P at picture 926, A at 1000; runner times 13.869301 +
  (picture - 827.5) / 70.09 s): equal at the 100th, 101st and 300th frame
  of play; tilt (Space tapped at play frames 230, 240, 250, 260, 270, in
  one window of nudge_timer): equal at state 9's entry, the drain and the
  next ball's start. State 4 is not checked by a run (no ball locked yet),
  nor state 12.
- XDATA's `music_pos` can differ from the original's: INT 66h AL=10h
  returns the position the driver's player has reached in the module, and
  the port's player does not advance in the headless build (no audio
  device). Only the sound routines read it.

## Build and run

    port\build.bat
    port\build\pdd.exe -game game -prog 1 -table 1

`build.bat` needs MSVC (the Visual Studio 2019 Build Tools, or what
vswhere finds) and builds `build\pdd.exe` (a window) and
`build\pdd-headless.exe` (no window or sound, a virtual clock; see
`src/plat_null.c` for `PD_FRAMES`, `PD_KEYS`, `PD_DUMP`). `-game` is the
unpacked CD (`python tools/gogx.py`), `-prog 1` PD.EXE's tables, `-prog 2`
PD2.EXE's, `-table` 0-3.

Comparing with the original: stop both at the same place and compare
their memory by the names of the hints. The port stops at the first routine
not translated yet or, with `PD_STOP=where#N`, the Nth time it passes `checkpoint(where)`
(`idle_loop`: the head of state 2's loop, `ball_start_loop`: state 3's,
`play`: the entry of `st_play`, `ball_lost`, `ball_start`, `game_over`,
`tilt`, `ball_locked`: of states 6, 3, 7, 9 and 4;
`PD_TRACE` prints each with the pictures shown so far), the runner with
`-break ADDR#N`.

    set PD_RAM=build\port_ram.bin& set PD_VRAM=build\port_vram.bin
    port\build\pdd-headless.exe -game game -prog 1 -table 1
    python tools\run.py -sound sb -until 5 -break st_idle -ram build\orig_ram.bin ^
        -vram build\orig_vram.bin DREAMS1/PD.EXE 1
    python tools\memcmp.py src\PD.hints build\orig_ram.bin build\port_ram.bin ^
        --vram build\orig_vram.bin build\port_vram.bin

## How it works

- `src/mem.c`: the program's memory as under DOS: one megabyte, the
  program loaded from the player's PD.EXE or PD2.EXE (checked by SHA-256)
  where tools/run loads it, relocations applied, the DOS blocks it
  allocates behind it. The tables are data in this memory (records that
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
  tick of the sound driver (70.09 a second in the 320x200 mode, measured
  in tools/run), which the program's `timer_callback` gets.
- `src/sound.c`, `src/modplay.c`: the INT 66h functions the programs call,
  on micromod (`third_party/micromod`). Loading a module, playing,
  positions and the volume work; the effects (AL=11h) and the second
  callback (AL=13h) wait until the driver's side of them is established
  for Dreams.
- `src/platform.h`: window, keys, clock, audio; `plat_win32.c` (the
  Windows SDK alone), `plat_null.c` (headless).
