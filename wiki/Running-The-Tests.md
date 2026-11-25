# Running The Tests

This page lists every test suite, the command that runs it, how long it takes and what a pass looks like. It also
shows how a firmware check is written, so you can add one.

All commands run from the repository's root, in Git Bash on Windows. The numbers are those of the last run the
reference documents record; what the suites do **not** cover is on [What Is Tested](What-Is-Tested).

## The suites at a glance

```mermaid
flowchart TB
  fw["build/sfx-forge.wasm"]
  way["sfx-forge/song_way.h"]
  corecs["Runtime/Core (C#) and sfxforge.dll"]
  editor["the package's Editor code"]
  demo["unity/Demo: the showcase and the first demo"]
  t1["sh build.sh<br/>vibe-check, 24 checks<br/>and a short fuzz run:<br/>26 lines PASSED"]
  t2["sh tests/stress-song-way.sh<br/>two real threads"]
  t3["tests/dotnet<br/>205 checks"]
  t4["tests/unity-inspectors<br/>171 EditMode tests"]
  t5["Verify: 32 editor checks<br/>PlayMode: 41 tests"]
  t1 --> fw
  t2 --> way
  t1 -->|"runs it too"| t2
  t1 -->|"writes out/midi.json, out/audio.wav"| t3
  t3 --> corecs
  t4 --> editor
  t5 --> demo
```

| Suite | Command | Time | A pass looks like |
|---|---|---|---|
| the firmware | `sh build.sh` | about 30 s | exit code 0; 26 lines say `PASSED`, the last is `store PASSED` |
| the two-thread test alone | `sh tests/stress-song-way.sh` | about 12 s | the last line is `song way stress PASSED` |
| the package's core, without Unity | `dotnet run -c Release --project tests/dotnet -- .` | about 30 s | 205 lines `  ok    ...`; the last line is `PASSED` |
| the editor UI (EditMode) | Unity on `tests/unity-inspectors`, below | not recorded | `total="171" passed="171" failed="0"` |
| the showcase's editor checks | Unity on `unity/Demo`, `ShowcaseBuilder.Verify`, below | 1 to 3 minutes | 32 lines `SHOWCASE: ok`, then `SHOWCASE: PASSED` |
| PlayMode | Unity on `unity/Demo`, below | 1 to 3 minutes | `total="41" passed="26" failed="0" skipped="15"` |

## Run everything, in order

Do this once on a fresh clone, before you change anything. A failure after your change is then yours.

1. `sh build.sh`. It builds the firmware and runs its checks. Wait for exit code 0 and `store PASSED` as the last
   line. It also writes `out/midi.json` and `out/audio.wav`, which step 3 reads.
2. Only after a change in `core/` or `native/`: `sh native/build.sh`. Step 3 loads the DLL that is in the package's
   folder, and an old one passes tests it should fail. On a fresh clone the committed DLL is the right one.
3. `dotnet run -c Release --project tests/dotnet -- .` The last line is `PASSED`.
4. The three Unity runs under [The Unity suites](#the-unity-suites), one after the other. Each needs the project
   it opens to be closed in the Unity editor.

Steps 1 to 3 need no Unity and take about a minute together. Which tools each step needs:
[Building From Source](Building-From-Source#what-must-be-installed). New words here (scene, check, EditMode,
PlayMode, shadow project): [Glossary](Glossary#code-and-files).

## The firmware: `sh build.sh`

It builds, then runs these in order. Each ends with a line `... PASSED`; the first that fails stops the run.

| # | Line | What it checks, in a few words |
|---|---|---|
| 1 | `vibe-check` | the SDK's own check of the firmware against its manifest |
| 2 | `tour` | a walk through the pages: the song in time, a cue on the bar line, SEND as 16 sounds and the song |
| 3 | `daw` | the sequencer follows a DAW's tempo and position |
| 4 | `guide` | the tour (MENU) plays what its captions say |
| 5 | `reset` | the factory reset |
| 6 | `metronome` | the click on the beats while REC is on |
| 7 to 10 | `loop`, `modes`, `take`, `edit` | LOOP and SONG, a take over several bars, the keys that ask twice, COPY, CLEAR |
| 11, 12 | `undo`, `retake` | what F2 takes back; takes with a knob turned or a pattern chosen |
| 13 to 15 | `grid`, `regrid`, `take64` | a pattern's grid of 16, 32 or 64 steps |
| 16 | `grid keys` | the question before a step is dropped; the windows of a short bar |
| 17 | `perform` | the PERFORM page |
| 18 | `meter` | the beat and the time signature |
| 19 | `mixer` | the mixer as the host's parameters: level, pan, mute, solo and the selected sound, both ways; what is not taken; the tour leaves every sound as it was |
| 20 | `stress-fuzz` | hard and wrong use: a short run of the fuzzer, 8 seeds of 1 device-minute each ([Stress tests](#stress-tests)) |
| 21 | `atomic` | a pattern or song rewritten while it plays reaches the player whole |
| 22 | `song way stress` | the same hand-over between two real threads |
| 23 | `song export` | SEND gives, byte for byte, the Unity demo's song file |
| 24 | `identity` | the identity and the other messages; the two version numbers agree |
| 25 | `wire` | the song on the wire: both shapes, pieces out of order and doubled, the longest song |
| 26 | `store` | what the device stores: `song6.bin`, `song.bin`, a full store |

That is `vibe-check`, 24 checks and the fuzz run.

Count them:

```sh
sh build.sh | grep -c PASSED        # 26
sh build.sh | grep -c "  ok  "      # 274 in a whole run of firmware 1.7.2 (8 of them are the fuzz run's seeds)
```

Every check prints `  ok    ...` lines. A failure prints `  FAIL  ...`. What each check proves is written at its
top; the README's table under "Build" has a row for each.

## How a firmware check is written

There are two kinds.

```mermaid
flowchart LR
  scene["tests/name.json<br/>a scene: key presses with times"]
  wt["the SDK's wasmtest"]
  outdir["out/name/<br/>audio.wav, midi.json, pictures"]
  chk["tests/check-name.mjs"]
  host["tests/host.mjs<br/>a small host"]
  chk2["tests/check-name.mjs<br/>drives the device itself"]
  wasm["build/sfx-forge.wasm"]
  scene --> wt
  wasm --> wt
  wt --> outdir --> chk
  wasm --> host --> chk2
```

**A scene and a check.** A scene is a JSON script of events with their times. `wasmtest` plays it into the built
firmware and writes the sound, the MIDI the firmware sent, and any pictures asked for. The check reads those files.

```json
{"seconds": 4, "float": true, "steps": [
  {"t": 0.1, "ev": [3, 14, 1]},
  {"t": 0.15, "ev": [3, 14, 0]},
  {"t": 1.2, "shot": "seq"}
]}
```

An event is `[type, id, down, value]` with the SDK's event types (`vibe.h`): 1 is a pad, 3 a button, 4 an encoder,
5 a raw MIDI message, 9 a note. This scene presses and lets go button 14 (K5), then takes a picture named
`seq.png`. The buttons' numbers are listed as `BUTTON` in `tests/host.mjs`. To run one scene and its check
alone:

```sh
node .tools/vibe-fm-sdk/tools/wasmtest.mjs build/sfx-forge.wasm tests/edit.json out/edit
node tests/check-edit.mjs out/edit        # ok lines, then: edit PASSED
```

**A check on the small host.** Eight checks have no scene: `check-identity`, `check-grid-keys`, `check-perform`,
`check-meter`, `check-mixer`, `check-atomic`, `check-wire` and `check-store`. They load the `.wasm` themselves
through `tests/host.mjs`, which can do what `wasmtest` cannot: send SysEx into the firmware, give it stored files,
and set and read the host's parameters. A device `d` has `button`, `shift`, `pad`, `turn`, `sysex`, `run`, `listen`,
`heard`, `take`, `screen` and `song`, and for the parameters `params`, `param`, `setParam` and `changed`.
[Firmware Add A Page Or Key](Firmware-Add-A-Page-Or-Key#how-to-test-it) has a complete small example.

```sh
node tests/check-perform.mjs build/sfx-forge.wasm     # ok lines, then: perform PASSED
```

Shared by the checks: `tests/song-read.mjs` reads a packed song (`stepList`, `tempo`, `chain`, `same` ...).

## Stress tests

Three tests of hard and wrong use, new with firmware 1.7.2. Each says at the top of its file what it shows and what
it cannot see. What they found, and what was run: [What Is Tested](What-Is-Tested#hard-and-wrong-use).

```sh
# 1. a seeded fuzzer as the host of the built .wasm, in one flow. The last line: stress-fuzz PASSED
node tests/stress-fuzz.mjs build/sfx-forge.wasm                        # 20 seeds, 2 device-minutes each
node tests/stress-fuzz.mjs build/sfx-forge.wasm --seeds 40 --jobs 8    # seeds 1 to 40, eight at a time
node tests/stress-fuzz.mjs build/sfx-forge.wasm --seed 7 --minutes 30  # one seed, long

# 2. the whole firmware built for this computer, on two real threads. The last line: flows stress PASSED
sh tests/stress-flows.sh                    # seeds 1 to 5, 20 seconds each
sh tests/stress-flows.sh 120 7              # seed 7 for 120 seconds
sh tests/stress-flows.sh 20 1 hostile wild  # also values no host should send

# 3. heavy and hostile MIDI to a MIDI port, for the real plugin. The last line: midi-torture PASSED
dotnet run -c Release --project tests/midi-torture -- --list
dotnet run -c Release --project tests/midi-torture -- --port "name" --minutes 5 --seed 1 --level 1
```

| Test | What it is | Part of `sh build.sh` |
|---|---|---|
| `tests/stress-fuzz.mjs` | The fuzzer sends every event, the parameters, the transport, SysEx, a store and restarts, in blocks of 1 to 256 frames at six sample rates. After every block it looks for a trap, a sample that is no number or over full scale, memory that grew, and a hang. The same seed gives the same run. `--level` says how wrong the host may be: 0 a correct host, 1 values out of range and a store that lies, 2 also NaN and the infinities (the default) | a short run: 8 seeds of 1 device-minute, level 2 |
| `tests/stress-flows.sh`, `stress-flows.c` | `sfx-forge.c` compiled with the SDK's own runtime and the undefined-behaviour sanitizer. One thread makes the audio, another works the controls and draws, a third watches for a hang. `hostile` adds parameters that are no number, `wild` such values in events, `one` runs both flows in one thread. It needs Zig and the SDK, as `build.sh` | no |
| `tests/midi-torture/` | It sends notes, controllers, valid and broken SysEx, and all at once, to the port the VIBE FM plugin listens on. `--level` 1 to 3 is the speed. It sends and does not hear: you watch the plugin. With `--in` it asks for the identity after every phase. Windows only | no |
| `tests/emulator-torture.mjs` | It pours that tool's stream into the SDK's emulator page in a Chrome of its own, through a stand-in for Web MIDI, so no MIDI port is needed: `node tests/emulator-torture.mjs --seed 1 --level 1 --seconds 120` (`--selftest` shows that it can see a dead firmware). It watches for errors, asks the identity all through, counts frames and audio, and reads a song back. It is the Playground's host code, not the plugin's native half. |

Limits to know before you trust a pass:

- **The fuzzer runs one flow.** A race between the audio and the controls does not show there.
- **`stress-flows` has no address sanitizer and, on Windows, no thread sanitizer**: Zig 0.13 brings neither. A race
  shows only when it does harm. It was run on Windows only.
- **The MIDI torture tool is the owner's to run.** It was built and its `--list` was run. Nothing was ever sent to
  a port.
- **One input is cut.** With SHIFT held, a DATA turn of more than about 16 million detents never comes back. The
  loop is in the SDK's runtime, before the firmware sees the event. The first two tests cut that turn to a million;
  `--sdk-loop` makes the fuzzer send it as it is.

## The Unity suites

Two rules first.

1. **Only one Unity process may have a project open.** If the editor is open on `unity/Demo`, a headless run on
   that folder fails. Use a shadow project then: a second folder whose `Assets`, `Packages` and `ProjectSettings`
   are junctions to the real ones ([the maintainer's guide](../docs/MAINTAINING.md#unity) has the commands).
2. **`tests/unity-inspectors` must stay apart from `unity/Demo`.** It holds copies of the demo song and presets
   with the same GUIDs.

Set `UNITY` to your Unity 6000.3 editor's executable, then:

```sh
UNITY="/path/to/Unity"     # the Unity 6000.3 editor
OUT="$PWD/out"

# the editor UI (not -nographics: it renders pictures into out/inspector-shots/)
"$UNITY" -batchmode -projectPath "$PWD/tests/unity-inspectors" -runTests -testPlatform EditMode \
  -testResults "$OUT/inspectors.xml" -logFile "$OUT/inspectors.log"
grep -o '<test-run [^>]*' "$OUT/inspectors.xml"

# the showcase's editor checks. Verify writes nothing
PROJECT="$PWD/unity/Demo"
"$UNITY" -batchmode -quit -projectPath "$PROJECT" -executeMethod SfxShowcase.Editor.ShowcaseBuilder.Verify \
  -logFile "$OUT/unity-showcase.log"
grep "SHOWCASE:" "$OUT/unity-showcase.log"

# PlayMode: the showcase's and the first demo's tests
"$UNITY" -batchmode -projectPath "$PROJECT" -runTests -testPlatform PlayMode \
  -testResults "$OUT/unity-showcase-playmode.xml" -logFile "$OUT/unity-showcase-playmode.log"
grep -o '<test-run [^>]*' "$OUT/unity-showcase-playmode.xml"
```

Compile errors show in the log as `error CS`. The 15 skipped PlayMode tests record takes or need a device; they
run only when asked for by name. `ShowcaseBuilder.SelfTest` is not a check: it rebuilds the scene and rewrites
`Showcase.unity`.

## The link, end to end

Not part of any build. It needs the SDK's emulator in a Chrome, a virtual MIDI cable with two ports (loopMIDI) and
Unity. `tests/emulator-link.mjs` is the device's end; `Tests/LinkTakes.cs` in the showcase is the game's. The
steps are in [the showcase's README](../unity/Demo/Assets/Showcase/README.md#recording-the-link).

## Next

- [What Is Tested](What-Is-Tested): what these suites prove and what nobody has done.
- [Building From Source](Building-From-Source): the tools the suites need.
- [Release Checklist](Release-Checklist): the runs a new version needs.
- [Unity Testing Your Audio](Unity-Testing-Your-Audio): testing sound in your own game.

## Reference

- [The maintainer's guide, 2. Build and test](../docs/MAINTAINING.md#2-build-and-test):
  [The firmware and its checks](../docs/MAINTAINING.md#the-firmware-and-its-checks),
  [The package's core, without Unity](../docs/MAINTAINING.md#the-packages-core-without-unity),
  [The two-thread test alone](../docs/MAINTAINING.md#the-two-thread-test-alone),
  [Unity](../docs/MAINTAINING.md#unity)
- [README, Build](../README.md#build): the table of every check
- [build.sh](../build.sh), [tests/check.mjs](../tests/check.mjs), [tests/host.mjs](../tests/host.mjs),
  [tests/song-read.mjs](../tests/song-read.mjs), [tests/dotnet/Program.cs](../tests/dotnet/Program.cs)
- [tests/stress-fuzz.mjs](../tests/stress-fuzz.mjs), [tests/stress-flows.sh](../tests/stress-flows.sh),
  [tests/stress-flows.c](../tests/stress-flows.c), [tests/midi-torture/Program.cs](../tests/midi-torture/Program.cs)
- [The inspectors' test project](../tests/unity-inspectors/README.md),
  [the showcase's README](../unity/Demo/Assets/Showcase/README.md#check-it-test-it-rebuild-it)
- [The Unity manual, 9. Testing, limits, troubleshooting](../docs/UNITY.md#9-testing-limits-troubleshooting)
