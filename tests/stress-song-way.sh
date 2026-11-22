#!/bin/sh
# Two real threads over sfx-forge/song_way.h (tests/stress-song-way.c says what it shows). Needs Zig 0.13.0, as build.sh.
#   sh tests/stress-song-way.sh [seconds each way, 3]
set -e
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
if [ -z "$ZIG" ]; then for z in "$ROOT"/.tools/zig-*/zig.exe "$ROOT"/.tools/zig-*/zig; do [ -x "$z" ] && ZIG="$z"; done; fi
mkdir -p "$ROOT/out/stress"
EXE="$ROOT/out/stress/stress-song-way"; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE="$EXE.exe" ;; esac
"${ZIG:-zig}" cc -O2 -Wall -o "$EXE" "$ROOT/tests/stress-song-way.c" $( [ "${EXE%.exe}" = "$EXE" ] && echo -lpthread )
"$EXE" "$@"
