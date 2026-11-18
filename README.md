# SFX FORGE

Sound effects and adaptive music for games, on **VIBE FM** (and, later, the M-VAVE FM-1): 16 synthesized sounds on
16 pads, a MUTATE button for instant variations, a pattern sequencer whose layers follow the game's intensity, and a
live MIDI/SysEx link to a game engine. What you make on the device plays in the game with the same engine.

![SFX FORGE](sfx-forge/icon.png)

Built with the VIBE FM SDK. Status: foundation (v0.1). The synth, the device UI and the device side of the link are
tested in the SDK's tools; the Unity package is tested in Unity 6.3 with the demo project's editor and Play mode tests.
Not tried yet: receiving MIDI against a real port (it needs a loopback port or a device).

## Layout

| | |
|---|---|
| `core/sfx_core.h` | the sound engine: patch format, four engines, mutate, SysEx packing. Portable C, no heap, no libc – shared by the Vibe, the native library and the future FM-1 firmware |
| `core/sfx_song.h` | the music: a song (8 patterns, notes, chain, a layer threshold per sound) and the player that runs it |
| `sfx-forge/` | the Vibe: pads, pages, display, storage, MIDI |
| `native/` | the same engine as a native library (Windows x64 so far), for game engines |
| `unity/com.catsplat.sfxforge/` | the Unity package: the link, presets, a player, telemetry, editor tools |
| `unity/Demo/` | a Unity 6.3 project using the package: a hovercraft whose every sound is synthesized live |
| `tests/` | a scripted performance (`tour.json`) and its checks (`check.mjs`); `dotnet/` tests the Unity package's core without Unity |
| `build.sh` | build, `vibe-check`, render the performance to `out/` |

## Build

Needs the SDK in `.tools/vibe-fm-sdk`, Zig 0.13.0 (in `.tools/`, on PATH, or `ZIG=`), Node 18+. On Windows use Git Bash.

```sh
git clone https://github.com/Vibe-FM-SDK/vibe-fm-sdk.git .tools/vibe-fm-sdk
sh build.sh
```

Gives `build/sfx-forge.wasm` (drop it onto <https://play.thefirmwarehub.com/>) and `build/sfx-forge.zip` (drop it onto
the VIBE FM VST window), then runs `vibe-check` and two test performances: `out/audio.wav`, screenshots,
`out/midi.json`, `out/daw/` (the sequencer following a DAW) and `out/guide/` (the tour).

## Playing it

- **MENU** – the tour: SFX FORGE plays and names what it can do, a caption at a time (about 35 s: the sounds, shaping
  one, MUTATE, notes, the song, its layers, cued patterns, the link). Any touch ends it; everything it changed is put
  back. On the stage: the TOUR button.
- **The display** – on the TONE, COLOR and FILTER pages it shows the selected sound itself: its whole shape from start
  to end, and its wave (the first 10 ms). It redraws as the knobs turn. The other pages show what plays now.
- **Pads** – 16 sounds: row 1 IMPACT, row 2 UI BLIP, row 3 SCI-FI FM, row 4 FOLEY. Pads 11 and 14 hold while pressed.
- **Keys** – the selected sound as an instrument, in tune. **DATA** selects a sound.
- **K1–K6** – pages TONE · COLOR · FILTER · LINK · SEQ · SONG; **encoders 1–4** are the four values shown (SHIFT: fast).
- **F1–F6** – ENGINE · MUTATE (±15 %) · UNDO · RANDOM · EXPORT (SysEx) · SAVE.
- **PLAY / STOP** – the song, with a demo to start from. It follows the DAW's song position while the DAW plays, else
  its own tempo. **REC**: pads and keys you play go into the pattern.
- **SEQ page** – the pads are the 16 steps of the selected sound. A step may carry a note: hold its pad and play a
  key, or turn NOTE (it moves along the scale chosen with encoder B). Encoders: STEP · NOTE · TEMPO · SWING.
- **SONG page** – what makes it game music:
  - **LAYER**: every sound has a threshold and plays only while **INTENSITY** is at or above it. Low intensity: the
    kick alone. High: bass, snare, lead, effects.
  - **PATTERN** A–H (also pads 1–8): a cued pattern takes over on the next bar line. **CHAIN**: walk patterns A…n by
    itself. Pads 9–16 set the intensity. Encoder B: steps in a pattern. F2 COPY makes the next pattern a variation.
  - **SEND** (F5): the 16 sounds and the song to the game.
- **The stage** (browser and plugin) – the same, big: the pads, patterns, intensity, the whole 16 × 16 step grid (dim
  rows are layers that are silent now), the scope. Click to play and to set steps; the wheel sets a step's note.
- **HOME** – what every control does.

## The link (device ⇄ game engine)

Plain MIDI, so it works over USB, the plugin's MIDI ports and Web MIDI. SysEx uses the non-commercial ID `7D`, then `56 42`.

| Direction | Message | Meaning |
|---|---|---|
| device → game | Note On/Off, ch 1, notes 36–51 | a pad was played |
| device → game | CC 20–25, ch 1 | encoders 1–4, A, B on the LINK page |
| device → game | `F0 7D 56 42 01 <slot> <13 patch bytes> F7` | EXPORT: one patch |
| game → device | Note On/Off, notes 36–51 | play a pad |
| game → device | CC 10–13, ch 2 | telemetry: pitch (0…+24 st), cutoff (64 = none, ±4 oct), FM depth, level |
| game → device | CC 14, ch 2 | music: intensity 0…127 (layers come in) |
| game → device | CC 15, ch 2 | music: pattern A…H (value / 16), from the next bar line |
| game → device | CC 16–17, ch 2 | shown on the LINK page, not mapped |
| game → device | `F0 7D 56 42 02 <slot> <13 patch bytes> F7` | load a patch into a pad |
| game → device | `F0 7D 56 42 03 <ASCII, ≤ 24> F7` | text for the top line |
| game → device | `F0 7D 56 42 04 <slot> F7` | request a dump of a pad |
| both ways | `F0 7D 56 42 05 <piece> <pieces> <≤ 200 bytes> F7` | the song, in 13 pieces (2455 bytes, `sfx_song_pack`) |
| game → device | `F0 7D 56 42 06 F7` | request the 16 sounds and the song (as SEND does) |

`<slot>` is 0–15. Telemetry also arrives as the DAW parameters Mod Pitch / Cutoff / FM / Level and Intensity.

**Patch bytes** (each 0–127): `version(1) engine(0–3) base_freq pitch_env_amt pitch_env_decay fm_ratio fm_depth
noise_mix filter_cutoff filter_res amp_decay bitcrush checksum`, checksum = sum of the first 12 bytes `& 0x7F`.
`amp_decay` 127 = HOLD. `pitch_env_amt` 64 = none. In UI BLIP, `fm_ratio` / `fm_depth` are the arpeggio's step and
speed; in FOLEY the grain rate and amount.

## Unity

Add the package from disk: **Window > Package Manager > + > Add package from disk…** and pick
`unity/com.catsplat.sfxforge/package.json` (Unity 2021.3 or newer; developed against Unity 6).

| | |
|---|---|
| **Window > SFX Forge** | choose the MIDI ports, play the device's pads, fetch their patches. EXPORT on the device arrives here and is saved as a preset asset |
| `SfxPreset` | a patch as an asset (Create > SFX Forge > Preset): sliders, Play, Mutate, Bake WAV, send to a pad |
| `SfxPlayer` | plays a preset in the game with the device's own engine, live – its Pitch / Cutoff / FM / Level move the sound as telemetry does on the device. No device needed |
| `SfxSong` | a song and its 16 sounds as an asset: SEND on the device's SONG page (or "Fetch the song" in the window) creates one |
| `SfxMusicPlayer` | plays a song in the game with the device's own player: `intensity` brings its layers in and out, `CuePattern` changes pattern on the next bar line. No device needed |
| `SfxTelemetrySender` | a Rigidbody's speed, an Animator float or your own value → CC 10–17 to the device, and to local `SfxPlayer`s and `SfxMusicPlayer`s |
| `SfxForgeLink` | the link in Play mode: pad and encoder events from the device |
| `SfxEncoderBinding` | a device encoder (LINK page) → an exposed AudioMixer parameter or an event |

**The demo** – open `unity/Demo` with Unity 6.3 and the scene `Assets/SfxDemo/SfxDemo.unity`: W/S thrust, A/D turn,
SPACE fires. The engine hum follows the speed, and so does the music: at rest only the kick, flat out every layer. The
same values go to the device if one is connected. The song is the firmware's own demo song, as it sent it
(`tests/make-demo-song.mjs`). The scene is
generated by `SFX Forge > Rebuild Demo Scene` (`Assets/SfxDemo/Editor/SfxDemoBuilder.cs`). Its tests, headless:

```sh
Unity -batchmode -projectPath unity/Demo -executeMethod SfxDemo.Editor.SfxDemoBuilder.SelfTest
```

```sh
Unity -batchmode -projectPath unity/Demo -runTests -testPlatform PlayMode -testResults out/unity-playmode.xml
```

From code: `SfxLink.Shared` (`SendPatch`, `SendSong`, `SendTelemetry`, `SendIntensity`, `CuePattern`, `SendText`,
`PlayPad`, events) and `SfxNative.Bake`.

MIDI is implemented for Windows (winmm) only; presets, `SfxPlayer` and baking need the native library, built for
Windows x64 (`sh native/build.sh`). **Without the hardware** the device is the Playground in the browser or the plugin,
so Unity needs a virtual MIDI cable to reach it (for example loopMIDI, two ports: one each way).

```sh
dotnet run -c Release --project tests/dotnet -- .
```

runs the package's core without Unity (after `sh build.sh`): the protocol against what the firmware really sent, and
the native library against the Vibe's own rendering.

## Licence

MIT. The VIBE FM SDK is Apache-2.0 (Sven Trogus / zvenson); "VIBE FM" is its owner's trademark.
