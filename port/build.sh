#!/bin/sh
# Builds build/pdd (the window, on SDL2) and build/pdd-headless (for tests
# and scripted runs) with cc (clang or gcc), on macOS and Linux; build.bat
# is the same for Windows.  SDL2 is looked for as SDL2.framework in
# ~/Library/Frameworks or /Library/Frameworks on a Mac, else through
# sdl2-config or pkg-config; without it only pdd-headless is built.
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
CFLAGS="-O2 -Wall -Wextra"
ENGINE="src/pd_main.c src/pd1.c src/pd2.c src/pd_video.c src/pd_sprite.c src/pd_text.c src/pd_keys.c src/pd_files.c src/pd_sound.c src/pd_idle.c src/pd_ball.c src/pd_lights.c src/pd_game.c src/pd_events.c src/pd_play.c src/pd_rules.c src/pd_bcd.c src/pd_lost.c src/pd_handlers.c src/pd_over.c src/code.c src/mem.c"
CORE="src/main.c src/frame.c src/sound.c src/audiofx.c src/hud.c src/launcher.c src/gog.c src/textmode.c src/modplay.c src/vga.c src/sys.c src/sha256.c"

mkdir -p build
$CC $CFLAGS -o build/pdd-headless $ENGINE $CORE src/plat_null.c -lm

sdl=
for fw in "$HOME/Library/Frameworks" /Library/Frameworks; do
    if [ "$(uname)" = Darwin ] && [ -d "$fw/SDL2.framework" ]; then
        sdl="-I$fw/SDL2.framework/Headers -F$fw -framework SDL2 -Wl,-rpath,$fw"
        break
    fi
done
if [ -z "$sdl" ] && command -v sdl2-config >/dev/null 2>&1; then
    sdl="$(sdl2-config --cflags --libs)"
elif [ -z "$sdl" ] && pkg-config --exists sdl2 2>/dev/null; then
    sdl="$(pkg-config --cflags --libs sdl2)"
fi
if [ -z "$sdl" ]; then
    echo "SDL2 not found: built build/pdd-headless only." >&2
    exit 0
fi
# $sdl unquoted: it is a list of options
$CC $CFLAGS -o build/pdd $ENGINE $CORE src/plat_sdl.c $sdl -lm
