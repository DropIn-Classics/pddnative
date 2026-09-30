#!/bin/sh
# Makes DIR/pddnative.app from build/pdd (built with SDL2_STATIC=1, so
# that the program is all the bundle needs) and signs the bundle ad hoc:
# the linker's signature covers the program only, and Gatekeeper calls a
# bundle whose Info.plist is not sealed damaged instead of offering to
# open it.  The version is $PORT_VERSION, else the tag of the commit, without
# its "v".
#   sh port/dist/macapp.sh DIR
set -e
mkdir -p "${1:?usage: macapp.sh DIR}"
# DIR as given from where the script was called, before the cd below
out=$(cd "$1" && pwd)
cd "$(dirname "$0")/.."
VERSION=${PORT_VERSION:-$(git describe --tags --exact-match 2>/dev/null || true)}
app="$out/pddnative.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS"
cp build/pdd "$app/Contents/MacOS/"
VERSION=${VERSION#v}
sed "s/@VERSION@/${VERSION:-0}/" dist/Info.plist > "$app/Contents/Info.plist"
printf 'APPL????' > "$app/Contents/PkgInfo"
codesign --force --sign - "$app"
