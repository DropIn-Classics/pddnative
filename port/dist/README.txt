pddnative
=========

A native compatibility implementation requiring an installed copy of
Pinball Dreams: this package contains only the program, our own code.
The game's data (the tables, pictures, music) comes from the player's
Pinball Dreams Deluxe of GOG.com and is read from it each time the
program runs; without it nothing can be played.

Starting
--------

Start pdd (pdd.exe on Windows, pddnative.app on a Mac). The first time
it looks for the GOG release's game.gog: next to the program, where GOG
installed it on Windows (found through the registry, or \GOG Games\Pinball
Dreams Deluxe on any drive), the GOG app in /Applications or
~/Applications on a Mac, a few usual Wine folders elsewhere. It offers to
copy the game's files from it into a folder "game" beside the program
(or, where that cannot be written, ~/Library/Application Support/Pinball
Dreams on a Mac, ~/.local/share/pinball-dreams on Linux). If it is not
found, copy game.gog next to pdd (on a Mac into ~/Library/Application
Support/Pinball Dreams), or name it:

    pdd -gog /path/to/game.gog

Then the setup screen comes first: the tables, the game's menu, the
options.

Windows: pdd.exe needs nothing else.

macOS (10.13 or newer, Intel and Apple silicon): pddnative.app needs
nothing else; move it to Applications if you like. It is not signed by
Apple, so the first start is refused ("cannot be verified"): close that
message, open System Settings > Privacy & Security, click "Open Anyway"
at the bottom and confirm. After that it starts with a double click. On
macOS 14 and older a right click on the app, "Open" and "Open" again
does the same. Or, in the Terminal, in the folder of the app:

    xattr -cr pddnative.app

Linux and the Steam Deck: keep libSDL2-2.0.so.0 next to pdd (the package
brings SDL2 along; nothing needs to be installed). On the Deck the game
starts fullscreen; in Game Mode add pdd as a non-Steam game. The GOG
release installed with Heroic is looked for in ~/Games/Heroic.

The source, and how the port was made and checked:
https://github.com/mindphluxnet/pddnative

MOD playback: micromod, by Martin Cameron (LICENCE-micromod.txt).
Linux and macOS: SDL2, by Sam Lantinga and others (LICENCE-SDL2.txt).
