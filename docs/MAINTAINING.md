# SFX FORGE – the maintainer's guide

**For the person who changes the code.** [The manual](MANUAL.md) says what the instrument does and
[the Unity manual](UNITY.md) what the package does. This page says where each thing is made, how to build and test
it, and what must change together.

It describes the repository at firmware 1.6.0, song format 6, patch format 2.

| I want to … | Go to |
|---|---|
| know what a folder is | [1. The map](#1-the-map) |
| build it and run the tests | [2. Build and test](#2-build-and-test) |
| format the code again | [The formatters](#the-formatters) · [The house style](#the-house-style) |
| find the code for a thing | [3. Reading order](#3-reading-order) |
| understand audio and controls | [4. The two flows](#4-the-two-flows) |
| change something | [5. Recipes](#5-recipes) |
| know what must never break | [6. Invariants](#6-invariants) |
| know what is weak | [7. Known rough places](#7-known-rough-places) |
| look up a word | [8. Glossary](#8-glossary) |

---

## 1. The map

| Folder | What it is |
|---|---|
| `core/` | The engine, three C headers. `sfx_core.h`: a sound (the patch, five engines, one voice). `sfx_wavetable.h`: the six wavetables. `sfx_song.h`: a song, its packed form, the effects, the player. No heap, no libc. Everything else is built on these |
| `sfx-forge/` | The firmware for VIBE FM. One unit, `sfx-forge.c`, which includes 19 parts `fw_*.h` in a fixed order, and `song_way.h`. `firmware.json` is its manifest, `icon.png` its icon |
| `native/` | `sfx_native.c`: the same engine as a DLL for game engines (Windows x64). `build.sh` builds it into the Unity package |
| `unity/com.catsplat.sfxforge/` | The Unity package. `Runtime/Core`: C# with no Unity in it (protocol, patch, link, MIDI, tools). `Runtime`: the components and assets. `Runtime/Plugins/x86_64/sfxforge.dll`: the built native library, committed. `Editor`: inspectors and windows. `Editor/UI`: the kit they are drawn with |
| `unity/Demo/` | A Unity 6.3 project that uses the package. `Assets/SfxDemo`: the first demo. `Assets/Showcase`: the game *Brio: First Harmonic*, with its own README |
| `tests/` | `*.json` + `check-*.mjs`: scenes of key presses and their checks, run by `build.sh`. `host.mjs`, `song-read.mjs`: what the checks share. `stress-song-way.c`: two threads over `song_way.h`. `dotnet/`: the package's `Runtime/Core` tested without Unity. `unity-inspectors/`: a Unity project that tests the editor UI. `data/`: a format-5 song kept as a fixture |
| `docs/` | The manuals, their pictures (`img/`), the scripts that draw the pictures (`shots.mjs`, `figures.py`, `unity_figures.py`) and make the videos (`video/`) |
| `tools/` | `strip_wasm_debug.py` (called by `build.sh`) and `make_unity_package.py` (writes the package repository) |
| `videos/` | The five finished videos, committed. Its README says how each is made |
| `build/` | The built firmware: `sfx-forge.wasm`, `.json`, `.zip`. Git-ignored |
| `out/` | Everything the tests and scripts write. Git-ignored. Safe to delete; `out/unity-work` then has to be made again (below) |
| `.tools/` | The VIBE FM SDK and Zig. Git-ignored. Not ours: never edit it |

How the pieces depend on each other:

```
                      core/sfx_core.h   (it includes core/sfx_wavetable.h)
                             |
                      core/sfx_song.h
                       /            \
      sfx-forge/song_way.h         native/sfx_native.c
               |                           |  sh native/build.sh
      sfx-forge/sfx-forge.c         unity/.../Runtime/Plugins/x86_64/sfxforge.dll
       (+ 19 parts fw_*.h)                 |  P/Invoke: Runtime/Core/SfxNative.cs
               |  sh build.sh              |
      build/sfx-forge.wasm          unity/com.catsplat.sfxforge   (Runtime/Core -> Runtime -> Editor)
               |                           |
               |   MIDI + SysEx            |-- unity/Demo          (uses the package: SfxDemo, Showcase)
               +---- the link -------------+
               |                           |
               +-------------+-------------+
                             |  python tools/make_unity_package.py
                  ../unity-sfx-forge   (the package repository: generated, never edited by hand)

  tests around them:
    build.sh ............... the built .wasm: 20 scene and host checks, vibe-check, the two-thread stress test
    tests/dotnet ........... Runtime/Core + sfxforge.dll, against what the firmware sent in build.sh's run
    tests/unity-inspectors . the package's Editor code (EditMode)
    unity/Demo ............. the showcase's editor checks and PlayMode tests
```

Two things in this picture are written twice, by hand, and must be kept alike:

- **The protocol's numbers**: `sfx-forge/fw_protocol.h` and `Runtime/Core/SfxProtocol.cs`.
- **The patch and the packed song**: C in `core/`, C# in `Runtime/Core/SfxPatch.cs` and `SfxPackedSong.cs`, and
  JavaScript for the tests in `tests/song-read.mjs`.

Nothing checks that at compile time. The tests do: they hand one side's bytes to the other.

---

## 2. Build and test

### What must be installed

| Tool | Version here | For |
|---|---|---|
| Git Bash | any | every `sh` command below (on Windows) |
| VIBE FM SDK | a clone in `.tools/vibe-fm-sdk` | builds the firmware; its `wasmtest` and `vibe-check` run the checks |
| Zig | 0.13.0, in `.tools/zig-*`, on the PATH, or `ZIG=` | the C compiler: firmware, DLL, stress test |
| Node | 18 or later (22.14 here) | the checks, `docs/shots.mjs` |
| Python | 3 (3.13 here) | strips the `.wasm`; with Pillow: the figures |
| .NET SDK | one that builds `net9.0` (10.0 here) | `tests/dotnet` |
| Unity | 6000.3.8f1, the only version ever run | the two Unity test projects |
| ffmpeg, numpy | on the PATH | only the videos and the package script |

```sh
git clone https://github.com/Vibe-FM-SDK/vibe-fm-sdk.git .tools/vibe-fm-sdk
```

To build against another checkout of the SDK: `VIBE_SDK=/path/to/sdk sh build.sh`.

### The firmware and its checks

```sh
sh build.sh            # build + all checks. About 30 seconds
sh build.sh quick      # build only
```

`build.sh` copies `sfx-forge/` and `core/` into the SDK, builds there, copies the result to `build/`, and takes the
copies out again. Then it strips the debug sections from the `.wasm`: they hold this machine's paths.

When it is right:

- It ends with exit code 0, and 22 lines of the output say `PASSED`. The first is `vibe-check`'s, the last is
  `store PASSED`. Count them: `sh build.sh | grep -c PASSED`.
- Every check prints `  ok    …` lines. A failure prints `  FAIL  …` and the run stops there.
- `sha256sum build/sfx-forge.wasm` gives the same hash before and after a change that only moves text. This is how
  the readability pass was proved. With the SDK in `.tools` at the time of writing the hash of 1.6.0 was
  `9f777350…4bb4`.

Each check is a scene `tests/<name>.json` played by the SDK's `wasmtest` into `out/<name>`, then judged by
`tests/check-<name>.mjs`. A check says at its top what it proves and how to run it alone. Five checks have no scene:
they load the `.wasm` themselves through `tests/host.mjs` (`check-identity.mjs`, `check-grid-keys.mjs`,
`check-atomic.mjs`, `check-wire.mjs`, `check-store.mjs`). The README's table "Build" lists what each one checks.

### The native library

```sh
sh native/build.sh     # writes unity/com.catsplat.sfxforge/Runtime/Plugins/x86_64/sfxforge.dll
```

It prints the DLL's `ls -l` line. The same source gives the same DLL but for the build time in its header, so the
file shows as changed in git after every build. Commit it only when `core/` or `native/` changed; otherwise
`git checkout` it.

### The package's core, without Unity

```sh
dotnet run -c Release --project tests/dotnet -- .      # after sh build.sh. About 30 seconds
```

It compiles `Runtime/Core/*.cs` with the tests and loads the committed `sfxforge.dll`. It reads `out/midi.json` and
`out/audio.wav`, which `sh build.sh` wrote, so run that first. When it is right: 205 lines `  ok    …`, and the last
line is `PASSED` (a failure: `FAILED (n)`, exit code 1). `Program.cs` says which file holds which section.

### The two-thread test alone

```sh
sh tests/stress-song-way.sh        # about 12 seconds; build.sh runs it too
```

Last line: `song way stress PASSED`. Run it after any change to `sfx-forge/song_way.h`: no other test runs the two
flows in two threads.

### Unity

Three rules first.

1. **Only one Unity process may have a project open.** If the editor is open on `unity/Demo`, a headless run on
   that folder fails. Use the shadow project then (below).
2. **Which project runs which tests.**

   | Project | Runs | Result when right |
   |---|---|---|
   | `tests/unity-inspectors` | EditMode: the package's inspectors, windows and tools | `total="169" passed="169" failed="0"` |
   | `unity/Demo` | the showcase's editor checks (`ShowcaseBuilder.Verify`) | 32 lines `SHOWCASE: ok`, then `SHOWCASE: PASSED` |
   | `unity/Demo` | PlayMode: the showcase's and the first demo's tests | `total="41" passed="26" failed="0" skipped="15"` |

   The 15 skipped tests record takes or need a device; they run only when asked for by name.
3. **`tests/unity-inspectors` must stay apart from `unity/Demo`.** It holds copies of the demo song and ten presets
   with the same GUIDs.

The editor UI's tests (not `-nographics`: they render pictures into `out/inspector-shots/`):

```sh
UNITY="C:/Program Files/Unity/Hub/Editor/6000.3.8f1/Editor/Unity.exe"   # where your Unity 6000.3 is
"$UNITY" -batchmode -projectPath "$PWD/tests/unity-inspectors" -runTests -testPlatform EditMode \
  -testResults "$PWD/out/inspectors.xml" -logFile out/inspectors.log
grep -o '<test-run [^>]*' out/inspectors.xml
```

The showcase (each run takes one to three minutes):

```sh
PROJECT="$PWD/unity/Demo"          # or the shadow project: "$PWD/out/unity-work/Demo"
OUT="$PWD/out"
"$UNITY" -batchmode -quit -projectPath "$PROJECT" -executeMethod SfxShowcase.Editor.ShowcaseBuilder.Verify \
  -logFile "$OUT/unity-showcase.log"
grep "SHOWCASE:" "$OUT/unity-showcase.log"

"$UNITY" -batchmode -projectPath "$PROJECT" -runTests -testPlatform PlayMode \
  -testResults "$OUT/unity-showcase-playmode.xml" -logFile "$OUT/unity-showcase-playmode.log"
grep -o '<test-run [^>]*' "$OUT/unity-showcase-playmode.xml"
```

Compile errors show in the log as `error CS`. `Verify` writes nothing. `ShowcaseBuilder.SelfTest` rebuilds the
scene and rewrites `Showcase.unity` from top to bottom: run it when the level, the sounds or the art changed, not
to check. [The showcase's README](../unity/Demo/Assets/Showcase/README.md) has the rest.

**The shadow project**, for when the editor is open on `unity/Demo`. It is a second project folder in `out/` whose
`Assets`, `Packages` and `ProjectSettings` are junctions to the real ones, with a `Library` of its own. The demo's
manifest names the package as `file:../../com.catsplat.sfxforge`, so the package needs a junction beside it. Make it
once, from the repository's root, in `cmd`:

```bat
mkdir out\unity-work\Demo
mklink /J out\unity-work\Demo\Assets            unity\Demo\Assets
mklink /J out\unity-work\Demo\Packages          unity\Demo\Packages
mklink /J out\unity-work\Demo\ProjectSettings   unity\Demo\ProjectSettings
mklink /J out\unity-work\com.catsplat.sfxforge  unity\com.catsplat.sfxforge
```

The first run imports everything and takes longer. What a run writes into `Assets` lands in the real folder: the
junction is the same folder. To remove a junction use `rmdir`, never a recursive delete: that would empty the real
folder.

### The link, end to end

Not part of any build. It needs the SDK's emulator in a Chrome, a virtual MIDI cable with two ports (loopMIDI), and
Unity. `tests/emulator-link.mjs` is the device's end; `Tests/LinkTakes.cs` in the showcase is the game's. The
commands are in the showcase's README, "Recording the link".

### The figures and the videos

```sh
node docs/shots.mjs          # the firmware's screenshots into out/doc (after sh build.sh)
python docs/figures.py       # the manual's pictures into docs/img (needs Pillow)
```

Run both and `git diff --stat docs/img` is empty when nothing visible changed. `docs/unity_figures.py` and the
video scripts need recorded takes; [the videos' README](../videos/README.md) says which.

### The formatters

A formatter is the first pass. Read what it made afterwards.

| Files | Config | Command, from the repository's root |
|---|---|---|
| C: `core/`, `native/`, `sfx-forge/`, `tests/*.c` | `.clang-format` | `npx clang-format@1.8.0 -i core/*.h native/*.c sfx-forge/*.c sfx-forge/*.h tests/*.c` |
| JavaScript: `tests/*.mjs`, `docs/shots.mjs` | `.prettierrc`, `.prettierignore` | `npx prettier@3.3.3 --write "tests/*.mjs" docs/shots.mjs` |
| C#: every tracked `.cs` | `.csharpierrc` | see below |
| Python: `docs/`, `tools/`, `tests/unity-inspectors/units_table.py` | `pyproject.toml` | `python -m pip install black==24.10.0`, then `python -m black docs tools tests/unity-inspectors/units_table.py` |
| every editor | `.editorconfig` | nothing to run |

C# in two lines (the first one once):

```sh
dotnet tool install csharpier --version 1.4.1 --tool-path out/fmt/csharpier
git ls-files -co --exclude-standard '*.cs' | tr '\n' '\0' | xargs -0 out/fmt/csharpier/csharpier format
```

Give CSharpier the list of files, never a folder: given a folder it reformats `.csproj` files too.

To check without writing: `clang-format --dry-run --Werror …`, `prettier --check …`, `csharpier check`,
`black --check docs tools`. All four are clean at the time of writing.

What the formatters do **not** do, so you must: braces round every body in JavaScript and C# (clang-format adds
them in C: `InsertBraces`), one declaration a statement, one assignment a statement (`a = b = 0;` becomes two
lines; in Python `a, b = 1, 2` becomes two unless the two are one thing, like `x, y`), moving a long trailing comment
above its line.

Three helpers the readability pass used are worth knowing, because they prove that a change moved text only:

- **C**: `sha256sum build/sfx-forge.wasm` after `sh build.sh quick`, and the DLL built twice into a scratch folder
  under the same file name (the name is inside the file) and compared but for the four bytes of its time stamp.
  Both are sensitive: `a = b = 0;` split as `a = 0; b = 0;` changes the code, `b = 0; a = 0;` does not (the
  chained form stores `b` first). So does a new local for part of an expression.
- **C#**: Unity's own compiler, run by hand on the `.rsp` files Unity leaves in `Library/Bee`, gives the same
  assembly bytes for sources that differ in layout and comments only. No Unity start is needed, and it finds a
  compile error in eight seconds.
- **Python**: `ast.dump(ast.parse(…))` of a file before and after is equal when only comments and layout changed.

A table of data that is aligned by hand is fenced so the formatter leaves it alone: `// clang-format off` …
`// clang-format on` in C, `// prettier-ignore`, `// csharpier-ignore`, and `# fmt: off` … `# fmt: on` in Python.

### The house style

1. One statement a line.
2. Braces round every `if`, `for` and `while` body, on lines of their own.
3. Lines up to 120 characters. Break a long expression at its operators, or name a part of it.
4. A comment that explains a line stands above it. A few words may trail.
5. A blank line between groups of lines that do one thing.
6. A function over about 60 lines gets a comment at its top that lists its parts, and each part a short comment.
7. Every file starts with a comment: what it is for, what it holds, what to read first.
8. Tables of data stay aligned, fenced from the formatter.
9. Public names do not change: C# public members, C functions, protocol constants, test names.
10. A comment says what is true. If you change the code under it, change it too.

---

## 3. Reading order

Every file's header says what it holds. These are the doors.

| To understand | Read, in this order |
|---|---|
| **the sound (the engines)** | `core/sfx_core.h`: the `SFX_P_*` list (what a patch is), then `sfx_voice_render` (the signal chain, top to bottom), then `sfx__control` (where knobs become frequencies). `core/sfx_wavetable.h`, `sfx__wt_harmonic`, for the tables |
| **the song format** | `core/sfx_song.h`: the structs `sfx_pattern` and `sfx_song`, then the block comment of section 4 (the byte layout of format 6 and of formats 1 to 5), then `sfx_song_pack` and `sfx_song_unpack` |
| **the player** | `core/sfx_song.h`: `sfx_player`, then `sfx_player_render` (it has a summary at its top), with `sfx__render` and `sfx__sub`. Effects: `sfx_fx_process`. Automation: `sfx_auto` |
| **the firmware's pages and keys** | `sfx-forge/sfx-forge.c` (the header: the map of the parts). `fw_state.h` for the pages (`PG_*`, `TAB_OF`, `PAGE_PARAMS`, `SOFT`). `fw_events.h`, `on_event`: it names the function for every pad, key and message. Then `fw_pages.h` (`set_page`, `encoder`) and `fw_softkeys.h` (`soft_sound`, `soft_seq`, `soft_song`, `soft_auto`, `soft_mix`). The sequencer's keys: `fw_sequencer.h` |
| **the display** | `fw_draw.h`, `on_draw`: seven numbered parts, one branch a page. The pages' pictures: `fw_draw_pages.h` (`draw_seq`, `draw_song`, `draw_auto`, `draw_mix`, `draw_master`, `draw_link`) and `fw_draw_sound.h` (`scene`). The pieces: `fw_draw_parts.h` (`hud`, `row`, `dial`, `param_text`). The stage: `fw_stage_draw.h` (`on_stage`) and `fw_stage_input.h` (`stage_event`) |
| **the link (SysEx)** | README, "The link". `fw_protocol.h` for the bytes. `fw_link.h`: `sysex_in` (in), `out_pump` with `export_pump`, `ident_pump`, `dump_pump` (out), `song_piece` (a song arriving). In C#: `Runtime/Core/SfxProtocol.cs`, then `SfxLink.cs` and `SfxSongTransfer.cs` |
| **the store** | `fw_store.h`: its header describes the three files; `bank_save`, `bank_load`. The reset: `factory_reset` in `fw_events.h` |
| **the Unity runtime** | `Runtime/Core/SfxPatch.cs`, then `SfxNative.cs` (the door to the DLL). Then the components: `SfxPlayer.cs` (one sound, live), `SfxMusicPlayer.cs` (a song), `SfxVariationPlayer.cs`. Assets: `SfxPreset.cs`, `SfxSong.cs`, `SfxVariationSet.cs` |
| **the Unity editor UI** | `Editor/UI/README.md` (the kit, class by class, and its rules). Then `SfxPresetEditor.cs` (tiny: its static `Build` makes the panel) and `SfxPresetPanel.cs`, the pattern every inspector follows: the constructor builds the controls once, `Refresh` shows the sound, every edit goes through the `SerializedObject`. The comment at the top of the class says how its 110 members are ordered. `Editor/UI/SfxUnits.cs` for every value's text |
| **the song Inspector** | `Editor/UI/README.md`, "The song". Then `SfxSongPanel.cs` (`Build` with `BuildSteps`, `BuildMix`, `BuildMaster`; `Refresh`), `Editor/UI/SongBits.cs` (`StepRow`: a sound's row of 16, 32 or 64 cells) and `SongViews.cs` (`LaneRow`: an automation lane). The bytes it reads: `Runtime/SfxSong.cs` and `Runtime/Core/SfxPackedSong.cs` (the `Head…` constants are the head's layout) |
| **the showcase** | `unity/Demo/Assets/Showcase/README.md`, then `Docs/ARCHITECTURE.md`. The SFX FORGE part is `Scripts/Audio`: `SoundLibrary.cs`, `SoundEffects.cs`, `ContinuousSounds.cs`, `MusicDirector.cs`, `AudioInstaller.cs` |
| **the tests** | `tests/check.mjs` first, then `tests/host.mjs` and `tests/song-read.mjs`. `tests/dotnet/Program.cs` for the C# side |
| **the figures and the videos** | `docs/shots.mjs` (the `Scene` class, then "the scenes") and `docs/figures.py`. `docs/video/make_videos.py` (`Take`, `render`, then `tour` and `song`). `make_unity_videos.py` imports its style from `make_videos.py` and reads takes from `SFXFORGE_CAPTURE_DIR`. `tools/make_unity_package.py`: the comment under its docstring is its map; `build()` at the bottom is the table of contents |

The firmware's parts are included once, in a fixed order, and each uses what the parts before it define. That order
is also the order of the code in the built `.wasm`. Moving a function between parts changes the binary, not what it
does.

---

## 4. The two flows

The host runs the firmware in two flows at once (`VIBE_PARALLEL_AUDIO` in `sfx-forge.c`). On the hardware they are
two real threads.

| Flow | Entry | Owns |
|---|---|---|
| **audio** | `vibe_on_audio` (`fw_audio.h`), once a block | the player `P` with its song `P.song`, the telemetry glide, the click, the scope |
| **controls** | `vibe_on_event`, `vibe_on_draw`, `vibe_on_stage` (end of `sfx-forge.c`), and `vibe_on_init` | everything else: the song being edited (`SONG`), both kits, pages, undo, the link, the store |

**The one rule: a variable is written by one flow only, and the two meet in four places and nowhere else.**

1. **The queue, controls → audio.** `send()` in `fw_audio.h` is the only way the controls make the player do
   anything: a note, a changed patch, PLAY, STOP, a cue, the mode (`M_TRIGGER` … `M_LOOP`). One writer (`qw`), one
   reader (`qr`), a ring of `QN` = 64 messages. A message carries a copy of the sound's patch. A full queue drops
   the message.
2. **The song's way, controls → audio.** The controls edit their own song, `SONG`. After every event and every draw
   `song_hand()` (`fw_state.h`) calls `song_way_hand`, which copies what differs into a third song inside `WAY`. At
   the start of a block `vibe_on_audio` calls `song_way_take`, which copies it on into `P.song`. So the player has
   the song before an edit or after it, never half of one. Two flags keep the two off each other
   (`song_way.h`, "How the two flows keep off each other"): whoever sees the other's flag steps back and comes again
   with its next block or event. Nobody waits.

   A message that starts or changes what plays (`msg_song`: START, STOP, CUE, LOOP) must find the song as it was when
   it was sent. `send()` asks `song_way_need` for a number, the message carries it, and the audio leaves the message
   in the queue until `song_way_has` says that song has arrived.
3. **Single values, audio → controls.** What the display must know of the audio is a handful of 32-bit values that
   only the audio writes, marked `volatile` in `fw_state.h`: `seq_pos`, `seq_pattern`, `seq_arr`, `play_k`,
   `scope_w` (and `seq_step`, which is written and read by nothing at present). The controls read each once into a
   local (`seq_poll`, `on_draw`). `seq_pos` packs the step, the grid and the fraction into one word so they are read
   together.
4. **Single values, controls → audio.** Two kinds. The host's parameters: the controls call `vibe_param_set`
   (the output volume, the intensity, the telemetry a game sends) and `vibe_on_audio` reads them with
   `vibe_param_get` in its parts 2 and 6. And two flags the controls write and the audio only reads: `metro`
   (the metronome is on) and `rec_on` (the audio's copy of `rec`), `volatile` in `fw_state.h`.

What follows from the rule when you write code:

- In the control flow, never touch `P`. To change what plays, `send()`. To change the song, change `SONG`; it
  reaches the player by itself.
- In `vibe_on_audio`, never touch `SONG`, the kits, a page variable, the store or the display. Do not wait, do not
  allocate.
- A new value the display needs from the audio: one `volatile uint32_t`, written in part 4 of `vibe_on_audio`.
- A sound edited on a page reaches the voices through `changed(slot)` (`fw_edit.h`), which sends `M_UPDATE`.
- `patches`, `pad_preset`, `sound_undo` and so `changed`, `preset_load`, `keep_undo` and `send` mean **the kit that
  is chosen** (`patches` is a macro for `kits[kit]`). To reach the other kit, set `kit` first and put it back, as
  `factory_reset` (`fw_events.h`), `sysex_in` (`fw_link.h`) and `factory` (`fw_factory.h`) do.

The tests' hosts call both flows from one thread. Only `tests/stress-song-way.c` sets two real threads on
`song_way.h`.

In Unity the same split exists: `OnAudioFilterRead` in `SfxPlayer.cs` and `SfxMusicPlayer.cs` runs on Unity's audio
thread and calls `SfxNative`; the fields the game sets are read there.

---

## 5. Recipes

After any of them: `sh build.sh`, `dotnet run -c Release --project tests/dotnet -- .`, and the Unity tests if C# of
the package changed. A recipe that changes `core/` or `native/` also needs `sh native/build.sh` before the .NET run.

### Add a parameter to a sound

Know the cost first. **A patch has no spare byte**, and three other things are counted from the number of
parameters. So a 26th value is the largest change there is:

- a patch becomes 29 bytes: patch format 3, every patch message one byte longer, `kits.bin` another size;
- the automation targets move: `SFX_AT_VOL`, `SFX_AT_PAN` and `SFX_AT_MASTER` are counted from `SFX_P_COUNT`
  (`core/sfx_song.h`), and those numbers are stored in every packed song. So it needs a new song format as well
  (the recipe below);
- a sound as text (`SFXF1:` and 56 hex digits) becomes longer.

Cheaper, when it fits: give an existing value a meaning in an engine that ignores it today. `SFX_P_TABLE` and
`SFX_P_SPREAD` are read by the WAVETABLE engine only; A and B already mean something else in every engine. No format
changes then – but check that no factory sound has a value there that now changes how it sounds.

The full way:

1. `core/sfx_core.h`, section 2: add `SFX_P_…` at the **end** of the list, before `SFX_P_COUNT`. Raise
   `SFX_PATCH_VERSION` to 3. Update the block comment of section 6.
2. Same file: a column in `DEFAULTS` (`sfx_patch_init`) and in `LOWEST` and `HIGHEST` (`sfx_patch_random`). Decide
   whether `sfx_patch_mutate` may move it.
3. Same file: `sfx_patch_unpack` must still take a version-2 patch of 28 bytes and give the new value its default.
   Its callers pass a fixed length today; they need the length of what they hold.
4. Same file: use the value in `sfx__control` (section 9) or `sfx_voice_render` (section 10).
5. `sfx-forge/fw_state.h`: a place in `PAGE_PARAMS` (the WAVE row has two free). `fw_draw_parts.h`: `param_name`
   and `param_text`. `fw_pages.h`: `param_nudge` and `param_put` if its range is not 0..127.
6. `sfx-forge/fw_sounds.h`: a column in all 39 rows of `PRESETS`, and in the legend above them.
7. `sfx-forge/fw_link.h`: `sysex_in` takes a patch message only when `n == 7 + SFX_PATCH_BYTES`. Take the old
   length too. `fw_store.h`: `bank_load` reads `kits.bin` only at exactly `BANK_BYTES`. Read the old size too.
8. `native/sfx_native.c`: nothing to write if the functions keep their shape, but raise `sfxn_abi` and `Abi` in
   `SfxNative.cs` together, and rebuild the DLL.
9. C#, `Runtime/Core`: `SfxPatch.cs` (the `SfxParam` list, `ParamCount`, `Version`, the field, the indexer,
   `Label`, `Create`, `Pack`, `TryUnpack`, `Mutate`), `SfxProtocol.cs` (`SfxAutoTarget`), `SfxProfile.cs`
   (`VibeFm1`), `SfxGenerator.cs` (`Recipes`), `SfxPatchText.cs`. `Editor/UI/SfxUnits.cs`: `PageParams`,
   `ParamName`, `ParamText`.
10. Tests: `tests/check-identity.mjs` (the `WANT` bytes: patch format, 25 values), `tests/check.mjs` and
    `check-song-export.mjs` (a dump is 28 bytes), `tests/dotnet` (`PatchAndProtocol`, `Identity`,
    `PatchAsTextAndSyx`), `tests/unity-inspectors` (`SfxUnitsTests` with `units_table.py`, `SfxPresetPanelTests`).
    Then the song files, as in the next recipe, step 8: they begin with 16 patches.
11. Docs: README ("Patch bytes", the identity table), `docs/MANUAL.md` sections 6 and 19, `docs/UNITY.md`, the
    cheat sheet (`docs/figures.py`).

### Add a page or a soft key

**A soft key** on a page that exists:

1. `sfx-forge/fw_state.h`: its caption in the `SOFT` table. If the caption changes with the state: `soft_caption`.
2. `sfx-forge/fw_softkeys.h`: its `case` in `soft_sound`, `soft_seq`, `soft_song`, `soft_auto` or `soft_mix`
   (`f` is 0 for F1). SHIFT is the variable `shift`.
3. What it does:
   - it changes the song: change `SONG`. Call `undo_mark` (`fw_edit.h`) first if F2 should take it back.
   - it changes a sound: `keep_undo(slot)` before, `changed(slot)` after.
   - it throws much away: ask twice with `confirm("…")`.
   - it says something: `say("…")`.
4. `sfx-forge/fw_events.h`: the line in the page's `HELP_…` table (HOME shows it).
5. The stage, if it should have the button: hit test in `stage_event` (`fw_stage_input.h`), drawing in `on_stage`
   (`fw_stage_draw.h`). The same numbers stand in both files.
6. A test: a scene `tests/<name>.json` and `tests/check-<name>.mjs`, and their three lines in `build.sh`
   (`mkdir -p out/<name>`, the `wasmtest` line, the check). Copy the nearest: `edit.json` with `check-edit.mjs`. If
   it needs SysEx or the store, use `tests/host.mjs` as `check-grid-keys.mjs` does.
7. Docs: `docs/MANUAL.md` section 19, README "Playing it", the cheat sheet (`docs/figures.py`, then run it).

**A page.** A page hangs on one of the six K keys: the SDK has `VB_K1` to `VB_K6` and no seventh, and the six
marks in the display's second line fill its width. So a new page is one more press of a key that exists.

**The order of the `PG_` list is used.** `page < PG_SEQ` is a sound page; `PG_SEQ` to `PG_MASTER` are the song's
pages (they mean the MUSIC kit and show LOOP / SONG in the top line); on the stage every tab at or after `PG_SEQ`
but AUTO is taken for MIX or MASTER. The comment over the enum lists the tests. A page that is none of these goes
after `PG_LINK`.

1. `sfx-forge/fw_state.h`: `PG_…` in the enum. A sound page goes **before** `PG_SEQ` and gets a row in
   `PAGE_PARAMS` (`sound_page()` is `page < PG_SEQ`). Then its entry in `TAB_OF` (a missing one stops the build:
   `_Static_assert`), and `TAB_FIRST` / `TAB_NAME` if it has a K key of its own. Look at `mix_page`, `sound_page`
   and `song_keys`: is the new page one of them?
2. `sfx-forge/fw_pages.h`: `tab_key` (which press of which K key opens it), `set_page` (the K key's label, the
   kit, LOOP or SONG), `arrow` (LEFT / RIGHT), `encoder` (what the four encoders turn), and `param_nudge`,
   `param_put` and `mix_value` if its values are none of a sound's.
3. Soft keys: a row in `SOFT`, its case in `soft_set`, `soft_caption` if a caption changes with the state, a
   function `soft_…` in `fw_softkeys.h`, and the branch in `on_event` (`fw_events.h`, "F1-F6: by page").
4. Pads: the page's branch in `on_event`'s pad case.
5. Display: a `draw_…` function in `fw_draw_pages.h`, and `header`'s `pill` there if it is a song page. In
   `on_draw` (`fw_draw.h`):
   - part 2: HOME, with a new `HELP_…` table in `fw_events.h`;
   - part 3: `TITLE` and `FIRST` (a missing entry stops the build), and the `pages` / `at` lines under them, which
     count the pages of each key;
   - part 4: the pads' lights, and the page's number in the lights' cache word `led`;
   - part 5: `vibe_link_page` at its top (the page's name for the host) and the page's branch;
   - part 6: which key is dimmed.
6. The stage shows the sound's pages as tabs: `ST_PAGE` and `ST_N` in `fw_stage_input.h`, the names `TAB` in
   `on_stage` in the same order, `stage_page_follows`, and the two places in each stage file that ask
   `>= PG_SEQ`.
7. `vibe-check` (in `build.sh`) presses the buttons and looks whether the display changes, and whether HOME shows
   something.
8. Tests and docs as for a soft key. The Unity inspectors mirror the sound pages: `SfxUnits.PageNames`,
   `PageTitles`, `PageParams`.

### Add an engine

A sixth engine changes no format by itself: the engine is one byte of the patch. But **a 1.6.0 device refuses engine
5** (`sfx_patch_unpack`), so a sound that uses it cannot go to an older firmware.

1. `core/sfx_core.h`, section 2: the entry in `sfx_engine` before `SFX_ENGINE_COUNT`, and what its knobs A and B do
   in the comment over the `SFX_P_*` list. `sfx_patch_init`, `sfx_patch_pack`, `sfx_patch_unpack` and
   `sfx_patch_random` follow `SFX_ENGINE_COUNT` by themselves.
2. Same file, `sfx__control`: a branch of its own in the chain of `if`s over the engines, and look at the
   modulator line and the drive under it. `sfx_voice_render`: its branch in the chain that makes the source, and
   look at the filter's output. **In both chains the last `else` is IMPACT and SCI-FI FM**: an engine without a
   branch lands there and becomes an FM engine without a word.
3. The prose that says "five": the header of `sfx_core.h`, the comment over `sfx_engine`, "0..4" in section 6.
4. Firmware: every part that names `SFX_ENGINE_…`: `fw_sounds.h` (`ENGINE_NAME`, `ENGINE_RGB`, a factory sound
   that uses it), `fw_draw_parts.h` (`param_name`, `param_text`), `fw_draw_sound.h` (the picture), `fw_pages.h`
   and `fw_softkeys.h` (the WAVE page is for a wavetable only), `fw_draw.h`, `fw_events.h`, `fw_link.h`,
   `fw_stage_draw.h`, `fw_stage_input.h`. `grep -n SFX_ENGINE_ sfx-forge/*.h` lists them.
5. C#: `SfxPatch.cs` (`SfxEngine`), `SfxGenerator.cs` (`Recipes`), `SfxProfile.cs` (the profile's count of
   engines, and a profile for the firmware before), `Editor/UI/SfxUnits.cs` (`EngineNames`, the knobs' names),
   `Editor/UI/SfxKit.cs` (its colour).
6. `sh native/build.sh`, the tests, and the docs: README, `docs/MANUAL.md`, the cheat sheet.

### Add a field to the song (a new song format)

**The rule: a format is only ever added.** `sfx_song_unpack` reads formats 1 to 6 today and must go on reading them,
and an old song must play the same samples. Only the newest format is written.

1. `core/sfx_song.h`, section 2: the field in `sfx_song` (or `sfx_pattern`). Keep `pat` first: `song_way.h`
   asserts it.
2. Section 1: `SFX_SONG_VERSION` 7; the sizes `SFX_SONG_HEAD`, `SFX_SONG_MIN`, `SFX_SONG_MAX`; the
   `_Static_assert`s beside them. Keep the old sizes as they are.
3. Section 4: write the new layout into the block comment first – it is the specification – and move format 6 to
   "the formats before". Then `sfx_song_pack` (writes 7), `sfx__head_ok` (the new value's range) and
   `sfx_song_unpack`: one more line in the format chooser, the read, and **the default an older song gets**. It
   checks everything first and writes after; keep that.
4. Section 3: `sfx_song_init` or `sfx_song_mix_init` gives a new song the default. Then use the field: in
   `sfx_player_render`, or through `sfx_auto` if a lane may move it.
5. Firmware:
   - `fw_protocol.h`: the `_Static_assert` "a song fits its pieces" (`SONG_PIECES_MAX * SONG_PIECE`). The identity
     says the new format by itself (`SFX_SONG_VERSION` in `ident_pump`).
   - `fw_link.h`: `song_piece` and `rx_whole` take a song in pieces and say `NEWER SONG: UPDATE` for a higher
     format; `export_pump` sends. Read both for anything that assumes format 6.
   - `fw_store.h`: follow what 1.6.0 did. A firmware that knows format 6 reads `song6.bin` and would refuse a
     format-7 song there. So save to a new file, read `song6.bin` and `song.bin` and never write them, and let
     `factory_reset` (`fw_events.h`) delete what must go.
   - `fw_factory.h`, `demo_song`: the factory song's value. The page that edits the field
     (the page recipe above). F2 needs nothing: undo keeps the whole song.
6. `native/sfx_native.c`: `sfxn_song_convert` and `sfxn__pack` (older formats as the newest), `sfxn_player_set_song`.
   Raise `sfxn_abi` if a signature changes. `sh native/build.sh`.
7. C#:
   - `Runtime/Core/SfxProtocol.cs`: `SongVersion`, `SongHead`, `SongBytesMin`, `SongBytesMax`, `BuildSong`,
     `BuildSongPieces`, `IsNewerSong`.
   - `Runtime/Core/SfxPackedSong.cs`: `Format`, `IsPlayable` and the step readers. This is a second reader of the
     layout, written by hand.
   - `Runtime/Core/SfxProfile.cs`: `VibeFm1` becomes the new one; add a profile for firmware 1.6 as `VibeFm15` is
     for 1.5. `SfxLink.Refusal` then says in words why a song is not sent to an older device.
   - `Runtime/Core/SfxSongTransfer.cs`: the format a piece names.
   - `Runtime/SfxSong.cs`: accessors for the field, and `CurrentFormat` (the asset's own number: 2 for a format-6
     song). `Runtime/SfxMusicPlayer.cs` if the game may set it. `Editor/SfxSongPanel.cs` to show it.
8. The song files. First copy `unity/Demo/Assets/SfxDemo/DemoSong.bytes` to `tests/data/DemoSong-format6.bytes`:
   it becomes the fixture that proves format 6 is still read. Then, after `sh build.sh quick`:

   ```sh
   node .tools/vibe-fm-sdk/tools/wasmtest.mjs build/sfx-forge.wasm tests/song-export.json out/song
   node tests/make-demo-song.mjs out/song/midi.json
   ```

   `make-demo-song.mjs` writes `DemoSong.bytes` and the `data:` line of `DemoSong.asset` in three places (its
   `PLACES`). It writes the asset format 2: change that in the script if `CurrentFormat` changed.
9. Tests and scripts:
   - `tests/song-read.mjs`: `model()` and the sizes at its top. The third reader of the layout.
   - `docs/video/make_unity_videos.py`, `song_notes()`: a fourth reader, for the videos only (it reads
     format 6 and nothing older).
   - `tests/check-song-export.mjs`, `check-identity.mjs`, `check.mjs`: the song's size, its pieces, the format
     byte (2487 bytes, 11 pieces, 6 today).
   - `tests/check-wire.mjs`, `check-store.mjs`: a part for the format-6 fixture, as there is one for format 5.
   - `tests/dotnet/SongFormatTests.cs`: `OlderFormats` (format 6 joins them), a section for the new format, and a
     fixture with hashes as `Format5Fixture` has: **render the old song before you change the player, keep the
     hashes, and compare after**. `Helpers.cs` packs songs by hand. `IdentityTests.cs`: the identity's bytes.
   - `tests/unity-inspectors`: `SfxSongGridTests`, `SfxSongPanelTests`, `SfxAssetFormatTests`.
   - The showcase's `Verify` compares its song asset with `DemoSong.bytes`.
10. Docs: README (the link table, the identity table, "Two song files", "New in firmware …"), `docs/MANUAL.md`,
    `docs/UNITY.md`, the texts inside `tools/make_unity_package.py`. A new format is a new firmware version: the
    release recipe below.

### Add a SysEx message

1. `sfx-forge/fw_protocol.h`: the next free number (`0x09`) as `SX_…`, and its line in the table comment. A message
   is built as `{SX_HEAD, SX_…, …, 0xF7}`.
2. In: a branch in `sysex_in` (`fw_link.h`). Check the length exactly. A message this firmware does not know is
   ignored without a word; keep it so.
3. Out: the host takes **one SysEx a block**. Do not send from the key's handler and hope: count what is owed and
   send it from `out_pump`, as `ident_pump` (`ident_due`) and `dump_pump` (`dump_due`) do.
4. Expect it twice, and expect your own back. The emulator delivers every message twice and hands a device its own
   output. A message must be harmless when doubled; `sysex_in` shows how an echoed dump is told from a new one.
5. Every byte between `F0` and `F7` is 0..127. A larger number goes as 7-bit groups, lowest first
   (the song's piece number is the example).
6. If a game must know whether a device takes it: a new bit in `ID_FLAGS`, and `Flag…` in `SfxDeviceInfo`
   (`SfxProfile.cs`).
7. C#: the constant and `Build…` / `TryParse…` in `SfxProtocol.cs`; in `SfxLink.cs` the branch in `Take` (`Poll`
   only hands it every message that came), and an event or a `Send…` method.
8. Tests: `tests/check-identity.mjs` has the part "the other messages beside it": add yours (`d.sysex(…)`,
   `d.take()` of `tests/host.mjs`). `tests/dotnet/ProtocolTests.cs` for the C# side.
9. README, "The link": a row in the table. `tools/make_unity_package.py` copies that whole section into the
   package as `LINK-PROTOCOL.md`, so the README is the one place to write it.

### Change the identity

**Fields are only ever added at the end.** A reader takes the first 18 bytes and ignores what follows.

- A number that changed (more lanes, a new format): the firmware's identity follows by itself, it is built from
  `SFX_…` constants in `ident_pump` (`fw_link.h`). Change the C# copies in `SfxProtocol.cs` and
  `SfxProfile.VibeFm1`, and keep a profile for the firmware before (`VibeFm15` is the model).
- A new field: append it in `ident_pump` (the array is `6 + 18` bytes: make it `6 + 19`) and note it in
  `fw_protocol.h`. In C#: read it in `SfxProtocol.TryParseIdentity` **only when it is there**, write it in
  `BuildIdentity`, keep `IdentityBytes` at 18 (it is the least a reader needs), add the field to `SfxDeviceInfo` or
  `SfxProfile`.
- Tests: `WANT` and the hex string under it in `tests/check-identity.mjs`; the same bytes in
  `tests/dotnet/IdentityTests.cs` (which also checks that a longer identity is still read) and in
  `tests/unity-inspectors/…/SfxSongGridTests.cs`.
- README: the table "Identity bytes" and the line with the whole answer.

### Add a Unity component

1. A new file `unity/com.catsplat.sfxforge/Runtime/Sfx….cs`, namespace `CatSplat.SfxForge`, with
   `[AddComponentMenu("SFX Forge/…")]`. `SfxVariationPlayer.cs` is a short model.
2. Its `.meta`: open `tests/unity-inspectors` in Unity once and Unity writes it. By hand it is two lines,
   `fileFormatVersion: 2` and `guid:` with 32 fresh hex digits. Commit it. Never rename or move a file that has a
   `.meta`: scenes and assets name it by that GUID.
3. Logic that needs no Unity goes into `Runtime/Core` (no `UnityEngine` there): `tests/dotnet` compiles that
   folder and can test it.
4. The engine is reached through `SfxNative` only. Ask `SfxNative.Available` first: the DLL exists for Windows x64.
5. If it makes sound in `OnAudioFilterRead`: that runs on the audio thread. No allocation, no Unity API, no lock
   that the main thread holds for long. `SfxPlayer.cs` is the model.
6. An inspector: `Editor/Sfx…Editor.cs` returns one element from a static `Build(SerializedObject)`, made from the
   kit in `Editor/UI`. Change values through the `SerializedObject`, so Unity's undo works.
7. Tests: an EditMode test in `tests/unity-inspectors/Assets/Harness/Editor` (it can build the inspector without a
   window); a PlayMode test in the showcase if it must be heard.
8. Docs: README's table under "Unity", `docs/UNITY.md`.
9. The package script copies `Runtime` and `Editor` whole and stops when a file has no `.meta`.

### Release a new firmware version

The version is written in four places. One check compares the first two; the script checks the third against the
built firmware; the fourth is prose.

1. `sfx-forge/sfx-forge.c`: `FW_MAJOR`, `FW_MINOR`, `FW_PATCH`. They stay in this file:
   `tests/check-identity.mjs` reads them from it.
2. `sfx-forge/firmware.json`: `"version"`.
3. `tools/make_unity_package.py`: `FIRMWARE_VERSION`, and the texts in it that name the version.
4. README ("Status", "New in firmware …", the identity table's "Today" column), `docs/MANUAL.md` ("This manual
   describes firmware …"), `docs/UNITY.md`.

Then:

5. `tests/check-identity.mjs`: the hex string of the whole answer names the version's three bytes. Same in
   `tests/dotnet/IdentityTests.cs`.
6. `sh build.sh`. The line to look for is
   `ok    the firmware's defines (1.6.0) and firmware.json ("1.6.0") say the same version`, and `vibe-check`'s
   `Manifest    SFX FORGE 1.6.0`.
7. `dotnet run -c Release --project tests/dotnet -- .`, and the Unity tests.
8. If pages or sound changed: the screenshots and figures (`docs/shots.mjs`, `docs/figures.py`) and the two device
   videos (`docs/video/make_videos.py`).

The Unity package has its own version: `VERSION` and `RELEASED` in `tools/make_unity_package.py`. The
`package.json` in this repository (0.1.0, `com.catsplat.sfxforge`) is the source's and is rewritten by the script.

### Regenerate the package

```sh
python tools/make_unity_package.py [target folder]     # default: ../unity-sfx-forge
```

It writes the standalone package repository from nothing: `Runtime`, `Editor`, the showcase as `Samples~/Showcase`,
the manuals, the videos with previews, the built firmware, the front page, every `.meta`. It builds in a folder
beside the target and replaces the target only when every check passed. It never starts Unity. Nothing over there
is edited by hand.

What it demands, and stops with a named error when it is missing:

- `build/sfx-forge.zip` exists (`sh build.sh`), has no debug sections and no machine path, and its version is
  `FIRMWARE_VERSION`.
- ffmpeg and ffprobe on the PATH; the SDK and Zig in `.tools` (their licence texts are copied).
- The five videos are in `videos/`, and **each is as long as `VIDEOS` in the script says, within 2 seconds**. A
  video made again with another length stops the run: look at it and choose its preview's place (`clip`, `poster`,
  `seconds`) again.
- The figures are in `docs/img`, the three `README_FIGURES` among them.
- Every file under the package and the showcase has a `.meta`, and no two share a GUID.
- `sfxforge.dll.meta` pins the plugin to the editor and Windows x64.
- The showcase's `DemoSong.bytes` equals the first demo's; its tests' assembly is gated on the Test Framework; its
  scene refers to nothing outside the sample and the package.
- **Exact lines of the manuals and READMEs.** The script rewrites paths with `replace_once`, which stops when the
  line it looks for is gone. After editing `docs/UNITY.md`, `docs/MANUAL.md` or the showcase's README, a stop here
  names the line: update the script's copy of it.
- README still has the section `## The link (device ⇄ game engine)` with `F0 7D 56 42` in it.
- Privacy, over every file and every member of every zip: no e-mail address, no drive path, no path of this
  machine, the owner's name only inside the GitHub address.
- Every relative link in every Markdown file resolves.

The numbers in its front-page texts (205 checks, 169 tests, 20 firmware checks) are prose in the script. Update
them when a count changes.

---

## 6. Invariants

| Never | Why | What catches it |
|---|---|---|
| allocate, or grow memory after start | the firmware has no heap; `core/` calls no library | `vibe-check`: "memory stays the same after init". In `core/` nothing: read your diff |
| wait, lock, log or call the store in the audio flow | a late block is a click | `vibe-check`: "no storage calls in the audio path", "no logging in the audio path", "no block over its time budget" |
| let one flow write what the other owns | half-made songs, torn values | `tests/stress-song-way.c` for the song's way. For anything else: nothing. The single-thread hosts cannot see it |
| send a byte above 127 inside SysEx | MIDI | `sfx_patch_unpack` and `sfx_song_unpack` refuse one. `tests/dotnet`: `Identity` (a byte of `0x80` is no identity), `Format6` and `IsPlayable` (damaged songs are refused) |
| change or drop a format | songs and sounds people saved | `tests/data/DemoSong-format5.bytes` through `check-wire.mjs`, `check-store.mjs` and `SongFormatTests.cs` (`Format5Fixture`, `OlderFormats`) |
| change how an old song sounds | "what you hear here is what the player hears" | `tests/dotnet`: "a format-5 song renders bit for bit as it did before", by hashes; "formats 1 to 5 … play the same samples" |
| take a newer format as broken, or half of it | a newer song is newer, not bad | `check-wire.mjs` (a format-7 piece: `NEWER SONG: UPDATE`, nothing kept), `IdentityTests.cs` |
| move or remove an identity field | games ask before they send | `check-identity.mjs` (the 24 bytes), `IdentityTests.cs` (a longer identity is read; a short one is not one) |
| write `song.bin` | an older firmware must still find its song | `check-store.mjs` |
| change the factory sounds or song without regenerating the song files | the game's demo song is the device's | `check-song-export.mjs`, byte for byte against `DemoSong.bytes` |
| put a job on SHIFT + K1 – K6 or SHIFT + RETURN | on the VIBE those are the VIBE's (they play notes; SDK 28) and never reach a firmware | nothing can: the hosts pass them through. `check-grid-keys.mjs` checks that SHIFT + K1 is K1 and nothing more |
| put a job on SHIFT + DATA turned, or ENTER held | the SDK's FOCUS uses them | `check-grid-keys.mjs`: the kit key in every FOCUS state |
| let the firmware's two versions differ | the identity would lie | `check-identity.mjs`, its first line |
| leave a machine path or an e-mail address in a tracked file | the repository and the package are public | `build.sh` strips the `.wasm`. `make_unity_package.py` checks the package. For this repository's own files: nothing. Look before you commit |
| edit `.tools/`, or the generated package by hand | the next run overwrites it | nothing |
| rename or move a Unity file that has a `.meta` | scenes and assets name it by GUID | the showcase's `Verify`, the package script's GUID check |

Smaller house rules of the SDK that the firmware keeps and `vibe-check` looks at: HOME shows the page's controls;
buttons change the display when they are pressed; the firmware plays when the host does not draw (the plugin with its window
closed) – which is why nothing the audio needs may hang on `on_draw`.

---

## 7. Known rough places

### Left as it is, because changing it changes the binary

The readability pass kept the built `.wasm` byte for byte, so nothing moved. These would help, and each needs a
normal test run instead of a hash to prove it:

1. **The order of the parts follows the old single file, not logic.** `vibe_on_init` sits in `fw_events.h`;
   `HELP_MIX` is apart from the other HOME tables; `fw_state.h` has the audio's variables in two places round
   `SONG`.
2. **Four long functions**: `on_draw` (about 350 lines), `on_stage` (330), `on_event` (260), `stage_event` (190).
   They are numbered and labelled. A function a page, an event type or a region would be better.
3. **`sfx_player_render`** (about 260 lines) and **`sfx_song_unpack`** (about 220, six formats). Same. In
   `sfx_player_render` the idiom "which step is on now, in a grid of `div`" stands four times; one helper would
   serve all four.
4. **The stage is described twice.** Hit-testing (`fw_stage_input.h`) and drawing (`fw_stage_draw.h`) share the
   layout's names (`SP_X`, `TB_PLAY_X`, `SK_Y` …) but each writes its own rectangles, and the sizes of single
   buttons (76 × 36, 104 × 36) are plain numbers in both. One table of rectangles would serve both.
5. **`preview` and `scene`** (`fw_draw_sound.h`) draw the same four shapes at two sizes with their own constants.
   A change belongs in both.
6. **About 25 colours are bare hex numbers**: shades for one state of one thing. The five that say something have
   names (`BLUE`, `YELLOW`, `GREEN`, `ORANGE`, `RED` in `fw_draw_parts.h`), beside `BG`, `PANEL`, `LINE`, `DIM`,
   `TEXT` and the engines'. Two lights in `fw_sequencer.h` still write the number: that part comes before the names.
7. **Short static names**: `P` (the player), `q`, `qw`, `qr` (the queue), `sel`, `spage`; the `song_rx_*` and
   `tour_*` groups want to be structs. Renaming changes the `.wasm`'s name section.
8. **Dead code**: `sound_title` in `fw_draw_pages.h` is never called; `frame` in `on_draw` is computed and thrown
   away.
9. **`step_set` lives in `fw_factory.h`**, because `demo_song` needs it first; the sequencer uses it for every
   step it sets.
10. **The version defines** stay in `sfx-forge.c` because `check-identity.mjs` reads that one file.
11. **The parts are headers, not units.** Real `.c` files with a shared header would need `extern` state; the SDK
    already compiles every `.c` in the folder.
12. **Some expressions could not be taken apart.** A named local for part of an expression, or an `if` chain for a
    `? :` chain, compiled to other code in several places (`on_draw`'s `at` and its soft keys' colours,
    `sfx__head_ok`, `auto_turn`). There the expression stands as it was, one arm a line, with a comment.

In C# and the scripts:

- `Tests.Music` (about 1,100 lines) and `Tests.Identity` (about 840) in `tests/dotnet` are each one function with
  locals that run through hundreds of checks. `Music` also holds the checks of `SfxSongTransfer` (about 480 lines),
  which are not music; three bare `{ … }` blocks in it could not be opened because their names are used twice.
- About 100 C# functions are over 60 lines; the long editor panels (`SfxVariationSetPanel.Refresh`,
  `SfxPresetPanel.ShowTools`, `SfxBatchBake.Run`, `SfxVariationBaker.Bake`, `SfxBakeView.Refresh`) have few
  paragraph comments, and `SfxPresetPanel`'s constructor is 315 lines.
- In `SfxPresetPanel.cs` "grid" is the box of eight variant tiles (the public `GridCount`, `GridSeed`, `GridBase`
  fixed the word), not a pattern's grid.
- The head of a packed song has its offsets in two places in C#: `SfxPackedSong.Head…` (used by `SfxSong.cs` too)
  and `AtBpm` … `AtPan` in `Editor/SfxSongPanel.cs`, which cannot see the first: they are internal to the runtime
  assembly.
- The Python scripts' long drawing functions (`cheat` in `docs/figures.py`, `panel` and `how` in
  `make_unity_videos.py`, `tour` and `song` in `make_videos.py`) are scripts of coordinates and key presses: flat,
  long, and commented by their captions only.
- Long strings were cut into `"…" + "…"`. To find a message, search for a short phrase of it.
- Prettier writes hex in lower case (`0xf0`); comments and the README say `F0`.
- `tools/make_unity_package.py` holds whole documents as text and exact lines of the manuals. It is long and
  breaks when those lines are reworded (above).

### Never tested

- **The hardware and the plugin.** Everything ran in the SDK's `wasmtest`, on `tests/host.mjs`, or in the SDK's
  emulator. Nobody has held a VIBE with this firmware.
- **The video scripts after their last changes.** `make_unity_videos.py` reads the format-6 demo song now, and
  `make_link_video.py` takes the song's size from the take; the committed videos were not made again with them.
- **Two real flows inside the firmware.** Only `song_way.h` was run on two threads. The queue and the `volatile`
  values are correct by reading.
- **A pattern at 1/32 or 1/64 over a MIDI port.** The link tests used songs of sixteenths.
- **A firmware up to 1.5.0** against any of this. What is said of it is read from its code.
- **The real Inspector and windows.** Their tests build the same elements in a test panel and press keys by code.
  Nobody has opened them, and nobody has listened to a generated sound or a variant.
- **What a page or a toast says.** The firmware's checks hear the output and read the song; none reads the display's
  text. Also unchecked: the stage's buttons, ERASE across a change of pattern, undo of RAMP and FILL.
- **Any Unity but 6000.3.8f1, any platform but Windows x64.** MIDI is winmm only; the DLL is Windows only.
- **The showcase's standalone build** was last run before firmware 1.5.

### Fragile spots

- **`FOCUS_KEY` (`fw_events.h`) is an undocumented number of the SDK.** `0x40001000` plus a button is what
  `vf_focus()` returns while FOCUS' frame stands on that button. It is not in the SDK's headers. The firmware uses
  it for one thing: with the frame on F1 – F6, SHIFT + ENTER is SHIFT + that key and not the kit key. If an SDK
  update changes the number, the kit changes there instead. `tests/check-grid-keys.mjs` watches exactly this case:
  if it fails after an SDK update, look here first.
- **The SDK moves.** The firmware's comments already describe SDK 28 (SHIFT + K1 – K6). Build with
  `VIBE_SDK=…` against a new checkout before you replace the one in `.tools`, and compare the 22 lines.
- **One SysEx a block.** Anything sent out of turn is lost without an error. Everything goes through `out_pump`.
- **A full queue drops the message** (`send`, `QN` 64). A burst of more than 64 notes and edits between two blocks
  loses the rest.
- **Echo and twins on the wire.** The receiver's rules for a song's pieces (`song_piece`, and
  `SfxSongTransfer.cs` with the same rules) are subtle: a late twin of the last song must not start a new one.
  Change one side and you must change the other; `check-wire.mjs` and `tests/dotnet` (`Music`) hold the cases.
- **Three readers of the song layout** (C, C#, JavaScript), a fourth in `docs/video/make_unity_videos.py`, and two copies of the protocol's numbers.
- **`build.sh` writes into the SDK's folder** for the length of a build and removes it. A build that is killed
  leaves `sfx-forge/` and `core/` there; the next run removes them first.
- **`sh build.sh` must run before `tests/dotnet`**, and the .NET tests use the committed DLL: after a change in
  `core/`, an old DLL passes tests it should fail. Rebuild it.
- **The x86 code of three places** (`sfx_player_render`'s tempo, a loop increment in `sfx_song_unpack`,
  `sfxn_player_set_master`) is written the dense way on purpose: the readable form compiled to other instructions.
  Each has a comment that says so.

---

## 8. Glossary

| Word | What it means here |
|---|---|
| **patch** | one sound: an engine and 25 values, 28 bytes on the wire (`sfx_patch`) |
| **engine** | the source of a sound's chain: IMPACT, UI BLIP, SCI-FI FM, FOLEY, WAVETABLE |
| **kit** | 16 sounds on the 16 pads. There are two: SFX (the game's effects, played beside the song) and MUSIC (the instruments the song plays, through the mixer). To the player the SFX kit's sounds are slots 16–31 |
| **slot** | a sound's place in a kit, 0–15; in a message 16–31 means the MUSIC kit's pad whatever is shown |
| **pattern** | one bar of music over the 16 sounds, A to H (`sfx_pattern`) |
| **bar** | a pattern's length: 1 to 16 sixteenths, one number for the whole song (`sfx_song.len`) |
| **grid** | how finely a pattern cuts a sixteenth: 0, 1, 2 = 1, 2 or 4 steps, so 16, 32 or 64 steps a full bar. Each pattern has its own. `div` in the code is the steps a sixteenth. In C#, `Grid(pattern)` returns the steps of a full bar (16, 32, 64) and `GridSteps(pattern)` those the bar plays; the number 0, 1, 2 is only an argument of `SongRegrid` |
| **step** | one cell of a pattern in its own grid. It may be on for a sound, with a note, a velocity and a length |
| **window** | the 16 steps of a finer pattern that the pads and tiles show at a time (`view_win`) |
| **lane** | an automation lane: it moves one knob of one sound, or of the master, while a pattern plays. A song has 24. A **point** is a value on a step; it holds until the next |
| **target** | which knob a lane moves (`SFX_AT_*`) |
| **layer** | a sound's threshold (`sfx_song.level`): the sound plays only while the intensity is at or above it |
| **intensity** | one number, 0 to 1, that the game sets (CC 14). It brings the layers in and out. "heat" in a few local names |
| **chain** | walk patterns A to n by itself, a bar each. Used when there is no arrangement |
| **arrangement** | up to 32 bars, a pattern each, played in order and again from the top |
| **cue** | choose a pattern: it takes over at once while the song stands, on the next bar line while it plays (`sfx_player_cue`, CC 15). A cued pattern that waits is "queued" |
| **LOOP / SONG** | what PLAY plays: the pattern round and round, or the arrangement (`loop_mode`, `M_LOOP`) |
| **take** | what is recorded from the first hit until STOP, PLAY or REC. One undo step. In the showcase's tests a take is also one recorded run for a video |
| **telemetry** | what a game moves while a sound plays: pitch, cutoff, timbre, level (CC 10–13, `sfx_mods`) |
| **stage** | the whole instrument for the mouse, 960 × 540, in the browser and the plugin (`on_stage`, `stage_event`) |
| **display** | the device's screen, 480 × 320 (`on_draw`) |
| **toast** | what the display says for a moment (`say`) |
| **flow** | one of the two paths the host runs the firmware in: audio and controls. On the hardware, two threads |
| **the song's way** | how an edited song gets from the control flow to the audio flow, whole (`song_way.h`). A **put** is one hand-over into it; a **take** there is the audio fetching it |
| **piece** | one SysEx message of a song in transfer: up to 240 bytes with its number, the count and the format. "Chunk" in older C# names is the same |
| **dump** | a patch sent by the device (message `01`). **load**: a patch sent to it (`02`) |
| **identity** | the 18 bytes a device answers to request `07`: hardware, firmware version, profile, formats, sizes, flags |
| **profile** | a device's limits as numbers (`SfxProfile`): formats, sounds, patterns, steps, lanes, bars, voices. Profile 1 is SFX FORGE 1.x on VIBE FM |
| **format** | the first byte of a packed patch (2) or song (6): which layout wrote it. The asset format of a Unity asset is a separate number (`SfxAssetFormat`, `SfxSong.CurrentFormat`) |
| **packed song** | a song as bytes 0..127, for the store, SysEx and a game's asset (`sfx_song_pack`) |
| **store** | the device's small file store: `kits.bin`, `song6.bin`, and `song.bin` of older firmwares |
| **scene** | in `tests/`: a script of key presses (`*.json`) that `wasmtest` plays. In the firmware: `scene()`, the picture of a sound page |
| **host** | whatever runs the firmware: the hardware, the plugin, the emulator, `wasmtest`, `tests/host.mjs` |
| **FOCUS** | the SDK's frame that DATA moves from control to control and ENTER presses, for controllers without the keys |
| **kit (editor)** | in `Editor/UI`: the set of UI Toolkit controls the inspectors are built from. Not the 16 sounds |
| **bake** | render a sound to samples or a WAV file (`sfxn_bake`, `SfxBake`) |
| **variant, variation set** | a mutated copy of a sound; an asset that plays one sound as several variants |
