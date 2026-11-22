#!/bin/sh
# Builds the native library for the Unity package (Windows x64):
#   native/sfx_native.c (with core/*.h)  ->  unity/com.catsplat.sfxforge/Runtime/Plugins/x86_64/sfxforge.dll
#
#   sh native/build.sh
#
# Needs Zig 0.13.0: in .tools/, on your PATH, or ZIG=/path/to/zig.
# The same source gives the same DLL but for the four bytes of the build's time in its header.
set -e

# the repository's root, as a path Windows programs understand too (pwd -W: Git Bash)
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"

# Zig: ZIG= if given, else the one in .tools/, else the one on the PATH
if [ -z "$ZIG" ]; then
  for z in "$ROOT"/.tools/zig-*/zig.exe "$ROOT"/.tools/zig-*/zig; do
    [ -x "$z" ] && ZIG="$z"
  done
fi

OUT="$ROOT/unity/com.catsplat.sfxforge/Runtime/Plugins/x86_64"
mkdir -p "$OUT"

# -shared: a DLL · -O2: optimised · -s: no symbols, no debug information
"${ZIG:-zig}" cc -target x86_64-windows-gnu -shared -O2 -s -o "$OUT/sfxforge.dll" "$ROOT/native/sfx_native.c"

# the linker leaves an import library and a debug file beside it: the package needs neither
rm -f "$OUT"/*.lib "$OUT"/*.pdb
ls -l "$OUT/sfxforge.dll"
