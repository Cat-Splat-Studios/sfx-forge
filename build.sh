#!/bin/sh
# Builds SFX FORGE with the VIBE FM SDK, checks it and renders the test performance.
#   sh build.sh            build + vibe-check + test render (out/audio.wav, out/*.png, out/midi.json) + all checks
#   sh build.sh quick      build only
# Needs: .tools/vibe-fm-sdk (git clone https://github.com/Vibe-FM-SDK/vibe-fm-sdk.git) and Zig 0.13.0
# (in .tools/, on your PATH, or ZIG=/path/to/zig), Node 18+ for the checks, Python 3 to strip the debug sections.
#
# How it reads: 1. where things are, 2. the build, 3. the checks - each a scene (tests/<name>.json) that the SDK's
# wasmtest plays into out/<name>, and a check (tests/check-<name>.mjs) that judges what came out. A check says at its
# top what it proves. The run ends at the first check that fails (set -e); when all pass, 23 lines say PASSED.
set -e

# ---- 1. where things are ------------------------------------------------------------------------------------------
# the repository (on Windows as a path Node understands), the SDK, Zig
ROOT="$(cd "$(dirname "$0")" && (pwd -W 2>/dev/null || pwd))"
SDK="${VIBE_SDK:-$ROOT/.tools/vibe-fm-sdk}"
if [ ! -f "$SDK/tools/build.sh" ]; then
  echo "VIBE FM SDK not found at $SDK"
  exit 1
fi

# Zig: the one named in ZIG, else one in .tools/, else the one on the PATH
if [ -z "$ZIG" ]; then
  for z in "$ROOT"/.tools/zig-*/zig.exe "$ROOT"/.tools/zig-*/zig; do
    if [ -x "$z" ]; then
      ZIG="$z"
    fi
  done
fi
export ZIG="${ZIG:-zig}"

# the names used below
WASM="$ROOT/build/sfx-forge.wasm"
WASMTEST="$SDK/tools/wasmtest.mjs"
# the factory sounds and song as the Unity demo holds them (format 6), and the song as firmware 1.5 packed it
DEMO_SONG="$ROOT/unity/Demo/Assets/SfxDemo/DemoSong.bytes"
OLD_SONG="$ROOT/tests/data/DemoSong-format5.bytes"

# ---- 2. the build -------------------------------------------------------------------------------------------------
# the SDK builds a folder inside its own tree: hand it a copy (the folder's name is the firmware's id)
rm -rf "$SDK/sfx-forge" "$SDK/core"
cp -r "$ROOT/sfx-forge" "$SDK/sfx-forge"
cp -r "$ROOT/core" "$SDK/core"
(cd "$SDK" && sh tools/build.sh sfx-forge)
rm -rf "$SDK/sfx-forge" "$SDK/core"

# what it built comes back into build/
mkdir -p "$ROOT/build" "$ROOT/out/daw" "$ROOT/out/guide"
cp "$SDK/build/sfx-forge.wasm" "$SDK/build/sfx-forge.json" "$ROOT/build/"
if [ -f "$SDK/build/sfx-forge.zip" ]; then
  cp "$SDK/build/sfx-forge.zip" "$ROOT/build/"
fi

# the SDK's build leaves debug sections in the .wasm, with the full paths of this machine: take them out of our copy
# (and of the zip), so that build/ can be given away. The code is untouched; the checks below run on the result.
PY="${PYTHON:-python}"
if ! command -v "$PY" >/dev/null 2>&1; then
  PY=python3
fi
if [ -f "$ROOT/build/sfx-forge.zip" ]; then
  "$PY" "$ROOT/tools/strip_wasm_debug.py" "$WASM" "$ROOT/build/sfx-forge.zip"
else
  "$PY" "$ROOT/tools/strip_wasm_debug.py" "$WASM"
fi

if [ "$1" = "quick" ]; then
  exit 0
fi

# ---- 3. the checks ------------------------------------------------------------------------------------------------
# the SDK's own check of the firmware against its manifest
node "$SDK/tools/vibe-check.mjs" "$WASM" --manifest "$ROOT/sfx-forge/firmware.json"

# the test performance: every pad, telemetry, the MIDI that goes out, the sequencer
node "$WASMTEST" "$WASM" "$ROOT/tests/tour.json" "$ROOT/out"
node "$ROOT/tests/check.mjs" "$ROOT/out"

# a DAW's clock and song position
node "$WASMTEST" "$WASM" "$ROOT/tests/daw.json" "$ROOT/out/daw"
node "$ROOT/tests/check-daw.mjs" "$ROOT/out/daw"

# the built-in tour
node "$WASMTEST" "$WASM" "$ROOT/tests/guide.json" "$ROOT/out/guide"
node "$ROOT/tests/check-guide.mjs" "$ROOT/out/guide"

# the factory reset
mkdir -p "$ROOT/out/reset"
node "$WASMTEST" "$WASM" "$ROOT/tests/reset.json" "$ROOT/out/reset"
node "$ROOT/tests/check-reset.mjs" "$ROOT/out/reset"

# the metronome
mkdir -p "$ROOT/out/metronome"
node "$WASMTEST" "$WASM" "$ROOT/tests/metronome.json" "$ROOT/out/metronome"
node "$ROOT/tests/check-metronome.mjs" "$ROOT/out/metronome"

# the sequencer: LOOP and SONG, a take over several bars, undo, erase, the keys that ask twice, COPY
mkdir -p "$ROOT/out/loop" "$ROOT/out/modes" "$ROOT/out/take" "$ROOT/out/edit" "$ROOT/out/song"
node "$WASMTEST" "$WASM" "$ROOT/tests/loop.json" "$ROOT/out/loop"
node "$ROOT/tests/check-loop.mjs" "$ROOT/out/loop"
node "$WASMTEST" "$WASM" "$ROOT/tests/modes.json" "$ROOT/out/modes"
node "$ROOT/tests/check-modes.mjs" "$ROOT/out/modes"
node "$WASMTEST" "$WASM" "$ROOT/tests/take.json" "$ROOT/out/take"
node "$ROOT/tests/check-take.mjs" "$ROOT/out/take" "$DEMO_SONG"
node "$WASMTEST" "$WASM" "$ROOT/tests/edit.json" "$ROOT/out/edit"
node "$ROOT/tests/check-edit.mjs" "$ROOT/out/edit"

# what F2 takes back and what it leaves; takes with a knob turned, a pattern chosen, REC switched off in between
mkdir -p "$ROOT/out/undo" "$ROOT/out/retake"
node "$WASMTEST" "$WASM" "$ROOT/tests/undo.json" "$ROOT/out/undo"
node "$ROOT/tests/check-undo.mjs" "$ROOT/out/undo" "$DEMO_SONG"
node "$WASMTEST" "$WASM" "$ROOT/tests/retake.json" "$ROOT/out/retake"
node "$ROOT/tests/check-retake.mjs" "$ROOT/out/retake" "$DEMO_SONG"

# a pattern's grid (1.6.0): 16, 32 or 64 steps, the 16 the pads show, FILL at a finer grid; a finer grid sounds the
# same, a coarser one asks and F2 brings it back; a take at 64 steps keeps the player's timing
# (the three scenes are written by tests/make-grid-scenes.mjs)
mkdir -p "$ROOT/out/grid" "$ROOT/out/regrid" "$ROOT/out/take64"
node "$WASMTEST" "$WASM" "$ROOT/tests/grid.json" "$ROOT/out/grid"
node "$ROOT/tests/check-grid.mjs" "$ROOT/out/grid" "$DEMO_SONG"
node "$WASMTEST" "$WASM" "$ROOT/tests/regrid.json" "$ROOT/out/regrid"
node "$ROOT/tests/check-regrid.mjs" "$ROOT/out/regrid" "$DEMO_SONG"
node "$WASMTEST" "$WASM" "$ROOT/tests/take64.json" "$ROOT/out/take64"
node "$ROOT/tests/check-take64.mjs" "$ROOT/out/take64" "$DEMO_SONG"

# the question that names what it drops, and the windows of a short bar (a host of its own: tests/host.mjs)
node "$ROOT/tests/check-grid-keys.mjs" "$WASM"

# the PERFORM page (1.7.0): the whole pattern as a grid, and the pads as steps, the kit, mutes or patterns
node "$ROOT/tests/check-perform.mjs" "$WASM"

# a whole pattern or song rewritten while it plays (a grid change, COPY, UNDO, CLR PAT, a song over SysEx) reaches the
# player whole, in time, with the playhead where it was; edits with no block between them. Then the same hand-over
# between two real threads (the hosts above call the firmware in one flow: only this can show the two keep off each
# other)
node "$ROOT/tests/check-atomic.mjs" "$WASM"
sh "$ROOT/tests/stress-song-way.sh"

# the factory sounds and song as SEND gives them: still the bytes the Unity demo's song asset holds
node "$WASMTEST" "$WASM" "$ROOT/tests/song-export.json" "$ROOT/out/song"
node "$ROOT/tests/check-song-export.mjs" "$ROOT/out/song" "$DEMO_SONG"

# the identity, and the other messages of the protocol beside it
node "$ROOT/tests/check-identity.mjs" "$WASM" "$ROOT/sfx-forge"

# the song on the wire (both shapes, the longest song) and in the device's store (song6.bin, and the song.bin of
# before)
node "$ROOT/tests/check-wire.mjs" "$WASM" "$OLD_SONG" "$DEMO_SONG"
node "$ROOT/tests/check-store.mjs" "$WASM" "$OLD_SONG" "$DEMO_SONG"
