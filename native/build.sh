#!/bin/sh
# Builds the native library for the Unity package (Windows x64). Needs Zig 0.13.0 (.tools/, PATH or ZIG=).
set -e
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
if [ -z "$ZIG" ]; then for z in "$ROOT"/.tools/zig-*/zig.exe "$ROOT"/.tools/zig-*/zig; do [ -x "$z" ] && ZIG="$z"; done; fi
OUT="$ROOT/unity/com.catsplat.sfxforge/Runtime/Plugins/x86_64"
mkdir -p "$OUT"
"${ZIG:-zig}" cc -target x86_64-windows-gnu -shared -O2 -s -o "$OUT/sfxforge.dll" "$ROOT/native/sfx_native.c"
rm -f "$OUT/sfxforge.lib" "$OUT/sfxforge.pdb"
ls -l "$OUT/sfxforge.dll"
