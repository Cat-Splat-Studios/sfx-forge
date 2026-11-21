#!/bin/sh
# Builds SFX FORGE with the VIBE FM SDK, checks it and renders the test performance.
#   sh build.sh            build + vibe-check + test render (out/audio.wav, out/*.png, out/midi.json)
#   sh build.sh quick      build only
# Needs: .tools/vibe-fm-sdk (git clone https://github.com/Vibe-FM-SDK/vibe-fm-sdk.git) and Zig 0.13.0
# (in .tools/, on your PATH, or ZIG=/path/to/zig), Node 18+ for the checks, Python 3 to strip the debug sections.
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
mkdir -p "$ROOT/build" "$ROOT/out/daw" "$ROOT/out/guide"
cp "$SDK/build/sfx-forge.wasm" "$SDK/build/sfx-forge.json" "$ROOT/build/"
[ -f "$SDK/build/sfx-forge.zip" ] && cp "$SDK/build/sfx-forge.zip" "$ROOT/build/"
# the SDK's build leaves debug sections in the .wasm, with the full paths of this machine: take them out of our copy
# (and of the zip), so that build/ can be given away. The code is untouched; the checks below run on the result.
PY="${PYTHON:-python}"; command -v "$PY" >/dev/null 2>&1 || PY=python3
if [ -f "$ROOT/build/sfx-forge.zip" ]; then "$PY" "$ROOT/tools/strip_wasm_debug.py" "$ROOT/build/sfx-forge.wasm" "$ROOT/build/sfx-forge.zip"
else "$PY" "$ROOT/tools/strip_wasm_debug.py" "$ROOT/build/sfx-forge.wasm"; fi
[ "$1" = "quick" ] && exit 0

node "$SDK/tools/vibe-check.mjs" "$ROOT/build/sfx-forge.wasm" --manifest "$ROOT/sfx-forge/firmware.json"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/tour.json" "$ROOT/out"
node "$ROOT/tests/check.mjs" "$ROOT/out"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/daw.json" "$ROOT/out/daw"
node "$ROOT/tests/check-daw.mjs" "$ROOT/out/daw"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/guide.json" "$ROOT/out/guide"
node "$ROOT/tests/check-guide.mjs" "$ROOT/out/guide"
mkdir -p "$ROOT/out/reset"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/reset.json" "$ROOT/out/reset"
node "$ROOT/tests/check-reset.mjs" "$ROOT/out/reset"
mkdir -p "$ROOT/out/metronome"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/metronome.json" "$ROOT/out/metronome"
node "$ROOT/tests/check-metronome.mjs" "$ROOT/out/metronome"
# the sequencer: LOOP and SONG, a take over several bars, undo, erase, the keys that ask twice, COPY
mkdir -p "$ROOT/out/loop" "$ROOT/out/modes" "$ROOT/out/take" "$ROOT/out/edit" "$ROOT/out/song"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/loop.json" "$ROOT/out/loop"
node "$ROOT/tests/check-loop.mjs" "$ROOT/out/loop"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/modes.json" "$ROOT/out/modes"
node "$ROOT/tests/check-modes.mjs" "$ROOT/out/modes"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/take.json" "$ROOT/out/take"
node "$ROOT/tests/check-take.mjs" "$ROOT/out/take" "$ROOT/unity/Demo/Assets/SfxDemo/DemoSong.bytes"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/edit.json" "$ROOT/out/edit"
node "$ROOT/tests/check-edit.mjs" "$ROOT/out/edit"
# what F2 takes back and what it leaves; takes with a knob turned, a pattern chosen, REC switched off in between
mkdir -p "$ROOT/out/undo" "$ROOT/out/retake"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/undo.json" "$ROOT/out/undo"
node "$ROOT/tests/check-undo.mjs" "$ROOT/out/undo" "$ROOT/unity/Demo/Assets/SfxDemo/DemoSong.bytes"
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/retake.json" "$ROOT/out/retake"
node "$ROOT/tests/check-retake.mjs" "$ROOT/out/retake" "$ROOT/unity/Demo/Assets/SfxDemo/DemoSong.bytes"
# the factory sounds and song as SEND gives them: still the bytes the Unity demo's song asset holds
node "$SDK/tools/wasmtest.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/tests/song-export.json" "$ROOT/out/song"
node "$ROOT/tests/check-song-export.mjs" "$ROOT/out/song" "$ROOT/unity/Demo/Assets/SfxDemo/DemoSong.bytes"
node "$ROOT/tests/check-identity.mjs" "$ROOT/build/sfx-forge.wasm" "$ROOT/sfx-forge"
