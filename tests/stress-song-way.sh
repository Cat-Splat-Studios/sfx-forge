#!/bin/sh
# Two real threads over sfx-forge/song_way.h (tests/stress-song-way.c says what it shows): builds that test and runs it.
#
#   sh tests/stress-song-way.sh [seconds each way, 3]
#
# Needs Zig 0.13.0, as build.sh: in .tools/, on your PATH, or ZIG=/path/to/zig.
# The last line it prints is "song way stress PASSED" or "song way stress FAILED" (exit code 0 or 1).
set -e

# the repository's root, as a path Windows programs understand too (pwd -W: Git Bash)
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"

# Zig: ZIG= if given, else the one in .tools/, else the one on the PATH
if [ -z "$ZIG" ]; then
  for z in "$ROOT"/.tools/zig-*/zig.exe "$ROOT"/.tools/zig-*/zig; do
    [ -x "$z" ] && ZIG="$z"
  done
fi

# the program: out/stress/stress-song-way, with .exe on Windows
mkdir -p "$ROOT/out/stress"
EXE="$ROOT/out/stress/stress-song-way"
THREADS="-lpthread"
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*)
    EXE="$EXE.exe"
    # (Windows has its threads without a library)
    THREADS=""
    ;;
esac

"${ZIG:-zig}" cc -O2 -Wall -o "$EXE" "$ROOT/tests/stress-song-way.c" $THREADS
"$EXE" "$@"
