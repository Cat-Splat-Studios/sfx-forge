# SFX FORGE

A procedural game-audio workstation for **VIBE FM** (and, later, the M-VAVE FM-1): 16 pads of synthesized sound
effects, a MUTATE button for instant variations, and a live MIDI/SysEx link to a game engine.

Built with the VIBE FM SDK. Status: foundation (v0.1) – the synth, the device UI and the device side of the link work
and are tested; the Unity package is not written yet.

## Layout

| | |
|---|---|
| `core/sfx_core.h` | the engine: patch format, four sound engines, mutate, SysEx packing. Portable C, no heap, no libc – shared by the Vibe and the future FM-1 firmware |
| `sfx-forge/` | the Vibe: pads, pages, display, storage, MIDI |
| `tests/` | a scripted performance (`tour.json`) and its checks (`check.mjs`) |
| `build.sh` | build, `vibe-check`, render the performance to `out/` |

## Build

Needs the SDK in `.tools/vibe-fm-sdk`, Zig 0.13.0 (in `.tools/`, on PATH, or `ZIG=`), Node 18+. On Windows use Git Bash.

```sh
git clone https://github.com/Vibe-FM-SDK/vibe-fm-sdk.git .tools/vibe-fm-sdk
sh build.sh
```

Gives `build/sfx-forge.wasm` (drop it onto <https://play.thefirmwarehub.com/>) and `build/sfx-forge.zip` (drop it onto
the VIBE FM VST window), then runs `vibe-check` and the test performance: `out/audio.wav`, screenshots, `out/midi.json`.

## Playing it

- **Pads** – 16 sounds: row 1 IMPACT, row 2 UI BLIP, row 3 SCI-FI FM, row 4 FOLEY. Pads 11 and 14 hold while pressed.
- **Keys** – the selected sound, chromatically. **DATA** selects a pad, **PLAY** auditions it.
- **K1–K4** – pages TONE · COLOR · FILTER · LINK; **encoders 1–4** are the four values shown (SHIFT: fast).
- **F1–F6** – ENGINE · MUTATE (±15 %) · UNDO · RANDOM · EXPORT (SysEx) · SAVE.
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
| game → device | CC 14–17, ch 2 | shown on the LINK page, not yet mapped |
| game → device | `F0 7D 56 42 02 <slot> <13 patch bytes> F7` | load a patch into a pad |
| game → device | `F0 7D 56 42 03 <ASCII, ≤ 24> F7` | text for the top line |
| game → device | `F0 7D 56 42 04 <slot> F7` | request a dump of a pad |

`<slot>` is 0–15. Telemetry also arrives as the DAW parameters Mod Pitch / Cutoff / FM / Level.

**Patch bytes** (each 0–127): `version(1) engine(0–3) base_freq pitch_env_amt pitch_env_decay fm_ratio fm_depth
noise_mix filter_cutoff filter_res amp_decay bitcrush checksum`, checksum = sum of the first 12 bytes `& 0x7F`.
`amp_decay` 127 = HOLD. `pitch_env_amt` 64 = none. In UI BLIP, `fm_ratio` / `fm_depth` are the arpeggio's step and
speed; in FOLEY the grain rate and amount.

## Licence

MIT. The VIBE FM SDK is Apache-2.0 (Sven Trogus / zvenson); "VIBE FM" is its owner's trademark.
