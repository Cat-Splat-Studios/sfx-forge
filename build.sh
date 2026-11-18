#!/bin/sh
# Builds SFX FORGE with the VIBE FM SDK, checks it and renders the test performance.
#   sh build.sh            build + vibe-check + test render (out/audio.wav, out/*.png, out/midi.json)
#   sh build.sh quick      build only
# Needs: .tools/vibe-fm-sdk (git clone https://github.com/Vibe-FM-SDK/vibe-fm-sdk.git) and Zig 0.13.0
# (in .tools/, on your PATH, or ZIG=/path/to/zig), Node 18+ for the checks.
set -e
ROOT="$(cd "$(dirname "$0")" && (pwd -W 2>/dev/null || pwd))"
SDK="${VIBE_SDK:-$ROOT/.tools/vibe-fm-sdk}"
[ -f "$SDK/tools/build.sh" ] || { echo "VIBE FM SDK not found at $SDK"; exit 1; }
if [ -z "$ZIG" ]; then
  for z in "$ROOT"/.tools/zig-*/zig.exe "$ROOT"/.tools/zig-*/zig; do [ -x "$z" ] && ZIG="$z"; done
fi
export ZIG="${ZIG:-zig}"

# the SDK builds a folder inside its own tree: hand it a copy (the folder's name is the firmware's id)
rm -rf "$SDK/sfx-forge" "$SDK/core"
cp -r "$ROOT/sfx-forge" "$SDK/sfx-forge"; cp -r "$ROOT/core" "$SDK/core"
(cd "$SDK" && sh tools/build.sh sfx-forge)
rm -rf "$SDK/sfx-forge" "$SDK/core"
mkdir -p "$ROOT/build" "$ROOT/out/daw"
cp "$SDK/build/sfx-forge.wasm" "$SDK/build/sfx-forge.json" "$ROOT/build/"
[ -f "$SDK/build/sfx-forge.zip" ] && cp "$SDK/build/sfx-forge.zip" "$ROOT/build/"
[ "$1" = "quick" ] && exit 0

node "$SDK/tools/vibe-check.mjs" "$ROOT/build/sfx-forge.wasm" --manifest "$ROOT/sfx-forge/firmware.json"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/tour.json" "$ROOT/out"
node "$ROOT/tests/check.mjs" "$ROOT/out"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/daw.json" "$ROOT/out/daw"
node "$ROOT/tests/check-daw.mjs" "$ROOT/out/daw"
