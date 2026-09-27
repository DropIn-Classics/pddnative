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
lamps, the display messages, Esc to quit) and the ball's physics it runs
every frame (`ball_step`: the flippers' shapes, the collision map, the
bounce). The program stops at the first routine not translated yet, with
its name (now `new_game`, after F1-F8), and leaves its last picture on the
screen.

Checked: at the entry of `st_idle` the port's memory equals the
original's in tools/run for Steel Wheel (PD.EXE) and Safari (PD2.EXE),
and at the 3000th frame of state 2 (the idle loop's head, CODE:016A in
PD.EXE, CODE:0169 in PD2.EXE) for all eight tables; Ignition also at the
6000th and 9000th, through its tour (t = 44 s to 120 s). DATA, BSS,
TDATA, XDATA and all of video memory byte for byte; different only where
the port has no reason to be the same: the saved INT 9 vector, the scratch
space of the driver's EXEC in CODE, the stack. The ball lies still in
state 2, so the physics' bounces are not checked yet.

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
(`idle_loop`: the head of state 2's loop), the runner with
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
