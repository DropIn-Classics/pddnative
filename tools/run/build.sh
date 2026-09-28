#!/bin/sh
# Builds build/pddrun (the headless runner, see main.c) with cc (clang or
# gcc), on macOS and Linux; build.bat is the same for Windows.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build
# -ffp-contract=off: no fused multiply-add, the timing is doubles, compared
# exactly (as /fp:precise with MSVC)
${CC:-cc} -O2 -Wall -ffp-contract=off -o build/pddrun \
    tools/run/main.c tools/run/cpu.c tools/run/vga.c tools/run/dev.c tools/run/bios.c \
    tools/run/dos.c tools/run/sound.c tools/run/vgafont.c tools/run/png.c -lm
