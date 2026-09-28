pddnative
=========

A native compatibility implementation requiring an installed copy of
Pinball Dreams: this package contains only the program, our own code.
The game's data (the tables, pictures, music) comes from the player's
Pinball Dreams Deluxe of GOG.com and is read from it each time the
program runs; without it nothing can be played.

Starting
--------

Start pdd (pdd.exe on Windows). The first time it looks for the GOG
release's game.gog: next to the program, where GOG installed it on
Windows (found through the registry, or \GOG Games\Pinball Dreams Deluxe
on any drive), the GOG app in /Applications or ~/Applications on a Mac,
a few usual Wine folders elsewhere. It offers to copy the game's files
from it into a folder "game" beside the program (or, where that cannot
be written, ~/Library/Application Support/Pinball Dreams on a Mac,
~/.local/share/pinball-dreams on Linux). If it is not found, copy
game.gog next to pdd, or name it:

    pdd -gog /path/to/game.gog

Then the setup screen comes first: the tables, the game's menu, the
options.

Windows: pdd.exe needs nothing else.

macOS: keep SDL2.framework next to pdd. The program is not signed: the
first time, open it with a right click and "Open", or remove the
quarantine with

    xattr -dr com.apple.quarantine pddnative-macos

Linux: SDL2 must be installed (Debian and Ubuntu: libsdl2-2.0-0; Fedora:
SDL2).

The source, and how the port was made and checked:
https://github.com/mindphluxnet/pddnative

MOD playback: micromod, by Martin Cameron (LICENCE-micromod.txt).
