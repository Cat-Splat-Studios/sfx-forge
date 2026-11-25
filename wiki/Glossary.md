# Glossary

Every word this wiki uses in a special sense, in one or two lines, with the page that teaches it. The words are
grouped by subject; use your browser's search to find one by name.

Jump to: [A sound](#a-sound) · [Playing a sound](#playing-a-sound) · [The device](#the-device) · [Music](#music) · [Unity](#unity) ·
[The link](#the-link) · [Code and files](#code-and-files)

## A sound

| Word | Meaning | Taught on |
|---|---|---|
| **patch** | one sound: an engine and 25 values, each 0 to 127. Packed it is 28 bytes | [Unity Presets And Patches](Unity-Presets-And-Patches), [Patch Format](Patch-Format) |
| **engine** | what makes a sound's raw signal. There are five: IMPACT, UI BLIP, SCI-FI FM, FOLEY, WAVETABLE | [Device The Five Engines](Device-The-Five-Engines) |
| **A and B** | the two knobs that belong to the engine; the display names them by what they do in the chosen engine | [Device The Five Engines](Device-The-Five-Engines) |
| **wavetable** | a row of eight single waves that the WAVETABLE engine moves through. There are six tables, computed at start, not recorded | [Sound Engine](Sound-Engine) |
| **envelope** | how a sound's loudness rises and falls: attack, decay, sustain, release (the ENV page) | [Device Make A Sound](Device-Make-A-Sound) |
| **LFO** | a slow wave that moves the pitch, the filter or the timbre of a sound | [Sound Engine](Sound-Engine) |
| **signal chain** | the fixed order in which a voice makes its sound: source, noise, filter, drive, bitcrush, loudness | [Sound Engine](Sound-Engine#the-signal-path-of-one-voice) |
| **FM, carrier, modulator, FM index** | frequency modulation: one sine, the modulator, moves the phase of another, the carrier, which is the one you hear. The index is how far it moves it: more index, more overtones | [Sound Engine](Sound-Engine#the-five-engines) |
| **grain** | in FOLEY: the noise's loudness aims at a new random value many times a second. Each stretch between two is a grain | [Sound Engine](Sound-Engine#the-five-engines) |
| **drive, bitcrush** | drive pushes the signal into a soft clip, so it distorts. Bitcrush plays it with fewer samples and fewer bits | [Sound Engine](Sound-Engine#the-signal-path-of-one-voice) |
| **control step** | every 16 samples the engine works out pitch, timbre and filter anew (`sfx__control`). Between two steps they stay | [Sound Engine](Sound-Engine#the-signal-path-of-one-voice) |
| **one-shot** | a sound whose SUSTAIN is 0 (the device shows OFF): it ends by itself | [Device Make A Sound](Device-Make-A-Sound#one-shot-or-held-sustain-decides), [Unity Presets And Patches](Unity-Presets-And-Patches#one-shot-or-held) |
| **held** | a sound whose SUSTAIN is above 0: it sounds until it is released | [Device Make A Sound](Device-Make-A-Sound#one-shot-or-held-sustain-decides), [Unity Held Sounds](Unity-Held-Sounds) |
| **voice** | one note that is sounding. A player has 16; when all are busy, a new note takes one over | [Sound Engine](Sound-Engine) |
| **send** | how much of a sound goes to the chorus, the delay and the reverb (the FX page) | [Device Mixer And Effects](Device-Mixer-And-Effects) |
| **MUTATE** | move a sound's values a little, at random, for a variation | [Device Make A Sound](Device-Make-A-Sound) |
| **variant** | a mutated copy of a sound | [Unity Variations](Unity-Variations) |
| **preset** | on the device: a named factory sound that F1 PRESET offers. In Unity: a patch saved as an asset (`SfxPreset`) | [Device Make A Sound](Device-Make-A-Sound), [Unity Presets And Patches](Unity-Presets-And-Patches) |

## Playing a sound

Words for what a game says when it plays a sound. The Unity pages use them in every call.

| Word | Meaning | Taught on |
|---|---|---|
| **trigger** | one start of a sound: one call of `Play` | [Unity Live Voices](Unity-Live-Voices) |
| **velocity** | how hard a note is played, 0 to 1 in Unity (1 is full). A softer note is quieter | [Unity Live Voices](Unity-Live-Voices#the-three-ways-to-call-play) |
| **semitone, cent** | steps of pitch. Twelve semitones are an octave, which doubles the frequency; a cent is a hundredth of a semitone | [Unity Live Voices](Unity-Live-Voices#the-three-ways-to-call-play), [Unity Variations](Unity-Variations#the-sets-values) |
| **MIDI note** | a pitch as a number: 69 is the A at 440 Hz, and one more is one semitone higher. Playing a sound "as a MIDI note" puts it in tune with the song | [Unity One Shots](Unity-One-Shots#what-bake-takes) |
| **FREQ** | a patch's own pitch (`baseFreq`). A sound plays there unless it is given a MIDI note | [Unity Presets And Patches](Unity-Presets-And-Patches#the-25-values-page-by-page) |
| **hold** | how long a baked clip keeps a held sound before letting it go: a preset's `holdSeconds` (HOLD), 1.5 s at first | [Unity One Shots](Unity-One-Shots#good-to-know) |
| **audio block** | the run of samples Unity asks for in one go. A player starts a sound at the next block, not at the call | [Unity Live Voices](Unity-Live-Voices#what-happens-when) |
| **2D, 3D** | Unity's words: a 3D sound has a place in the scene and gets quieter with distance; a 2D sound is just added to the mix. The players are 2D; a baked clip can be 3D | [Unity Choosing A Route](Unity-Choosing-A-Route) |

## The device

| Word | Meaning | Taught on |
|---|---|---|
| **the device** | SFX FORGE as a player meets it: the firmware running on a VIBE FM, in its plugin, or in a browser | [Device Quick Start](Device-Quick-Start) |
| **firmware** | the program that SFX FORGE is on the VIBE FM: `sfx-forge.wasm`, version 1.7.1 | [Building From Source](Building-From-Source) |
| **host** | whatever runs the firmware: the hardware, the plugin, the emulator, or a test tool | [Architecture Overview](Architecture-Overview) |
| **Playground** | a web page that runs a firmware file in the browser | [Device Quick Start](Device-Quick-Start) |
| **emulator** | the VIBE FM SDK's picture of the device in a browser. The link was tested against it | [What Is Tested](What-Is-Tested) |
| **pad** | one of the 16 square keys. It plays a sound of the kit; on some pages the pads are steps, patterns, points or mutes | [Device Controls And Pages](Device-Controls-And-Pages) |
| **kit** | 16 sounds on the 16 pads. There are two: **SFX** (the game's sound effects) and **MUSIC** (the instruments the song plays) | [Device Controls And Pages](Device-Controls-And-Pages) |
| **slot** | a sound's place in a kit, 0 to 15. In a message, 16 to 31 means the MUSIC kit | [Link Protocol](Link-Protocol) |
| **page** | one screen of the device with its own values and soft keys. There are fourteen, reached with K1 to K6 | [Device Controls And Pages](Device-Controls-And-Pages) |
| **soft key** | F1 to F6 under the display. What they do changes with the page | [Device Controls And Pages](Device-Controls-And-Pages) |
| **K1 to K6** | the six keys that choose a group of pages: SOUND, ENV, LFO, FX, SEQ, LINK. Pressed again, a key goes to the next page of its group | [Firmware Internals](Firmware-Internals#the-pages-and-the-six-k-keys) |
| **HOME** | a key: the display lists the open page's controls instead of the page | [Firmware Internals](Firmware-Internals#how-a-frame-is-drawn) |
| **encoder** | one of the four knobs for the values the display shows; A and B are two more | [Device Controls And Pages](Device-Controls-And-Pages) |
| **display** | the device's screen, 480 × 320 | [Firmware Internals](Firmware-Internals) |
| **top line** | the first line of the display: the selected sound's pad number, name and engine, and at the right what plays. For about two seconds it shows instead what a key just did | [Device Controls And Pages](Device-Controls-And-Pages) |
| **second line** | the line under it: the page's name, and on SEQ and PERFORM the pattern you see and its grid | [Device Controls And Pages](Device-Controls-And-Pages) |
| **transport** | the keys REC, STOP and PLAY | [Device Recording](Device-Recording) |
| **SHIFT** | a key you hold while you press or turn something else, for its second meaning. Held, it also makes the encoders fast | [Device Controls And Pages](Device-Controls-And-Pages) |
| **FOCUS** | ENTER held: the VIBE's own frame that DATA moves from control to control. It is not SFX FORGE's | [Device Controls And Pages](Device-Controls-And-Pages) |
| **toast** | what the top line says for a moment, like `MUTATED` | [Device Cheat Sheet](Device-Cheat-Sheet#what-the-top-line-says) |
| **stage** | a bigger picture of the whole instrument for the mouse, in the Playground and the plugin | [Device The Stage](Device-The-Stage) |
| **tour** | MENU: the device plays by itself and names what it does | [Device Quick Start](Device-Quick-Start) |
| **store** | the device's small file store, written by F6 SAVE | [Device Saving And Reset](Device-Saving-And-Reset) |

## Music

| Word | Meaning | Taught on |
|---|---|---|
| **song** | eight patterns over the 16 sounds of the MUSIC kit, with an arrangement, layers, a mix and automation | [Device Song And Arrangement](Device-Song-And-Arrangement) |
| **pattern** | one bar of music for each of the 16 sounds. A song has eight, A to H | [Device Make A Beat](Device-Make-A-Beat) |
| **bar** | a pattern's length: 1 to 16 sixteenths, one number for the whole song | [Device Time Signatures](Device-Time-Signatures) |
| **bar line** | the moment a bar ends and the next begins. A pattern you choose while the song plays starts there | [Device Song And Arrangement](Device-Song-And-Arrangement#choosing-a-pattern) |
| **step** | one cell of a pattern. It may be on for a sound, with a note, a velocity and a length | [Device Make A Beat](Device-Make-A-Beat) |
| **note** | the pitch a step plays its sound at. A step without a note plays the sound as it was designed | [Device Make A Beat](Device-Make-A-Beat#everything-a-step-can-do) |
| **velocity** | how hard a step plays, 1 to 8 on the device. A pad you hit while REC plays is recorded as hard as you hit it | [Device Make A Beat](Device-Make-A-Beat#everything-a-step-can-do) |
| **scale** | the notes the NOTE knob of SEQ moves through: CHROMATIC, MAJOR, MINOR or PENTATONIC | [Device Make A Beat](Device-Make-A-Beat#everything-a-step-can-do) |
| **playhead** | the step that plays now: a lit tile on SEQ, a lit column on PERFORM | [Device Perform](Device-Perform) |
| **grid** | how finely a pattern cuts the bar: 1/16, 1/32 or 1/64, which is 16, 32 or 64 steps for a full bar. Each pattern has its own | [Device Finer Grids](Device-Finer-Grids) |
| **beat** | how the bar is counted: an eighth, three sixteenths, a quarter or a dotted quarter. It is the device's own setting: it is not in the song and a game never gets it | [Device Time Signatures](Device-Time-Signatures) |
| **time signature** | the bar's name on the display, made from the bar and the beat: `BAR 3/4`, `BAR 6/8`, `BAR 7/8` | [Device Time Signatures](Device-Time-Signatures) |
| **tempo, swing** | the song's speed, and its one change of timing, which moves sixteenths | [Device Make A Beat](Device-Make-A-Beat) |
| **click** | the metronome. It falls on the beat | [Device Recording](Device-Recording) |
| **LOOP / SONG** | what PLAY plays: one pattern round and round, or the arrangement | [Device Recording](Device-Recording) |
| **REC, take** | with REC lit, what you play is written into the pattern. A take is what is recorded from the first hit until STOP, PLAY or REC; it is one undo step | [Device Recording](Device-Recording) |
| **FILL** | a key that writes steps for the selected sound: on every beat first, finer with each press | [Device Perform](Device-Perform) |
| **chain** | play patterns A to a chosen pattern in a row, a bar each. Used when there is no arrangement | [Device Song And Arrangement](Device-Song-And-Arrangement) |
| **arrangement** | up to 32 bars, a pattern each, played in order and then again from the top | [Device Song And Arrangement](Device-Song-And-Arrangement) |
| **layer** | a sound's LAYER mark: the sound plays only while the intensity is at or above it | [Device Layers And Intensity](Device-Layers-And-Intensity) |
| **intensity** | one number, 0 to 1, that the game sets. It brings the layers in and out | [Unity Adaptive Music](Unity-Adaptive-Music) |
| **cue** | choose a pattern: it takes over on the next bar line while the song plays | [Unity Cues And Patterns](Unity-Cues-And-Patterns) |
| **bar line** | the moment one bar ends and the next begins | [Unity Cues And Patterns](Unity-Cues-And-Patterns#four-words) |
| **LAYER mark** | the same as layer: the number, 0 to 1, a sound of the song carries | [Unity Adaptive Music](Unity-Adaptive-Music#the-rule) |
| **floor** | in a game's music script: the least intensity it ever writes, so some of the band always plays. It is the script's own number, not the package's | [Unity Adaptive Music](Unity-Adaptive-Music#a-game-value-becomes-intensity) |
| **lane** | an automation lane: it moves one knob of one sound, or of the master, while a pattern plays. A song has 24 | [Device Automation](Device-Automation) |
| **point, target** | a point is a lane's value on a step; the target is the knob the lane moves | [Device Automation](Device-Automation) |
| **mix** | a fader, a pan and a mute for each instrument of the song | [Device Mixer And Effects](Device-Mixer-And-Effects) |
| **mute, solo** | a muted sound is not heard in the song. While any sound is solo, only the soloed ones are heard | [Device Mixer And Effects](Device-Mixer-And-Effects) |
| **master** | the song's one delay and one reverb, on the MASTER page | [Device Mixer And Effects](Device-Mixer-And-Effects) |
| **pad mode** | on PERFORM: what the 16 pads are: STEPS, SOUNDS, MUTES or PATTERN | [Device Perform](Device-Perform) |
| **duck** | turn the music down for a moment so that something else is heard | [Unity Music Mix And Notes](Unity-Music-Mix-And-Notes#ducking) |
| **stinger** | a few notes the game plays by hand on the song's instruments | [Unity Music Mix And Notes](Unity-Music-Mix-And-Notes#notes-by-hand) |

## Unity

| Word | Meaning | Taught on |
|---|---|---|
| **the package** | SFX Forge for Unity: the engine as a native library, components, assets and editor tools | [Unity Install](Unity-Install) |
| **native library** | the device's engine compiled for the game: `sfxforge.dll`, Windows x64 only | [Unity Porting The Native Library](Unity-Porting-The-Native-Library) |
| **route** | one of the ways a patch becomes sound in a game: baked clip, live voice, held voice, music | [Unity Choosing A Route](Unity-Choosing-A-Route) |
| **bake** | render a sound once into samples: an `AudioClip` in memory or a `.wav` file | [Unity One Shots](Unity-One-Shots), [Unity Baking And WAV](Unity-Baking-And-WAV) |
| **live voice** | an `SfxPlayer` that synthesizes the sound each time it plays | [Unity Live Voices](Unity-Live-Voices) |
| **live values** | the four numbers that bend a playing sound: pitch, cutoff, timbre, level | [Unity Held Sounds](Unity-Held-Sounds) |
| **variation set** | an asset that plays one sound as several variants in a shuffled order | [Unity Variations](Unity-Variations) |
| **song asset** | an `SfxSong`: the 16 sounds and the packed song. `SfxMusicPlayer` plays it | [Unity Music First Song](Unity-Music-First-Song) |
| **Inspector** | Unity's panel for the selected asset. The package draws a sound and a song there as the device draws them | [Unity Song Inspector](Unity-Song-Inspector) |
| **tile, key** | in an Inspector of the package: a tile shows one value and is dragged up or down; a key is a button under the picture, like PLAY. They are the device's knobs and soft keys | [Unity Presets And Patches](Unity-Presets-And-Patches#the-presets-inspector) |
| **tools** | NEW, VARY, LEVEL and SHARE in a sound's Inspector | [Unity Sound Design Tools](Unity-Sound-Design-Tools) |
| **showcase** | the example game, *Brio: First Harmonic*; a sample of the package | [Unity Showcase Walkthrough](Unity-Showcase-Walkthrough) |
| **event seam** | the showcase's one place where the game says what happened and the audio listens | [Unity Showcase Walkthrough](Unity-Showcase-Walkthrough#the-event-seam) |
| **audio thread** | the thread on which Unity asks for samples (`OnAudioFilterRead`). The players synthesize there | [Unity Architecture](Unity-Architecture) |
| **P/Invoke** | how C# calls a C function of a native library: a `[DllImport]` declaration. Two files of the package have them: `SfxNative.cs` (the engine) and `WinMidi.cs` (MIDI) | [Unity Architecture](Unity-Architecture#the-layers) |
| **assembly definition** | an `.asmdef` file: it makes the scripts of its folder one assembly and names the assemblies they may use | [Unity Architecture](Unity-Architecture#the-two-assemblies) |
| **`.meta` file** | Unity's file beside every script and asset. It holds the GUID by which scenes and other assets name that file | [Unity Extending](Unity-Extending#the-same-as-a-new-component-of-the-package) |
| **EditMode, PlayMode** | Unity's two kinds of test. An EditMode test runs in the editor without starting the game; a PlayMode test runs the game | [Running The Tests](Running-The-Tests#the-unity-suites) |
| **shadow project** | a second project folder whose `Assets`, `Packages` and `ProjectSettings` are junctions (Windows links between folders) to the real ones. A headless Unity can run on it while the editor has the real project open | [Running The Tests](Running-The-Tests#the-unity-suites) |
| **ABI number** | the number `sfxn_abi()` of the native library returns. `SfxNative` refuses a library whose number is not its own `Abi` | [Unity Porting The Native Library](Unity-Porting-The-Native-Library#how-c-finds-and-checks-the-library) |

## The link

| Word | Meaning | Taught on |
|---|---|---|
| **link** | the MIDI connection between the device and a game. It carries sounds, the song and controller values, never audio | [Unity Device Link Setup](Unity-Device-Link-Setup) |
| **MIDI** | the standard that carries notes and controller values between instruments and computers | [Link Protocol](Link-Protocol) |
| **CC** | a MIDI control change: a controller number and a value 0 to 127 | [Unity Telemetry And Encoders](Unity-Telemetry-And-Encoders#the-control-changes) |
| **SysEx** | a MIDI system-exclusive message: a run of bytes between `F0` and `F7`. Sounds and songs travel in them | [Link Protocol](Link-Protocol#the-sysex-messages) |
| **EXPORT, SEND** | keys of the device: EXPORT sends the selected sound, SEND the song with its 16 sounds | [Unity Send And Receive](Unity-Send-And-Receive) |
| **telemetry** | values the game sends while a sound plays: pitch, cutoff, timbre, level (CC 10 to 13) | [Unity Telemetry And Encoders](Unity-Telemetry-And-Encoders) |
| **identity** | the 18 bytes a device answers when asked what it is: hardware, firmware version, formats, sizes | [Link Protocol](Link-Protocol#ask-first-the-identity) |
| **profile** | a device's limits as numbers: formats, sounds, patterns, steps, lanes, bars, voices | [Unity API Link](Unity-API-Link) |
| **piece** | one SysEx message of a song in transfer: up to 240 bytes of it | [Link Protocol](Link-Protocol#a-song-in-pieces) |
| **virtual MIDI cable** | a program that gives the computer MIDI ports joined to each other, such as loopMIDI | [Unity Device Link Setup](Unity-Device-Link-Setup) |

## Code and files

| Word | Meaning | Taught on |
|---|---|---|
| **core** | the synth and the song player in portable C (`core/`). The firmware and the native library both compile it | [Architecture Overview](Architecture-Overview) |
| **player** | in the core: 16 voices, the effects and the sequencer. In Unity, `SfxPlayer` and `SfxMusicPlayer` each hold one | [Sound Engine](Sound-Engine) |
| **packed song** | a song as bytes 0 to 127, for the store, SysEx and a game's asset | [Song Format](Song-Format) |
| **format** | the first byte of a packed patch (2) or song (6): which layout wrote it | [Patch Format](Patch-Format), [Song Format](Song-Format) |
| **flow** | one of the two paths the host runs the firmware in: audio and controls | [Architecture Overview](Architecture-Overview#the-firmwares-two-flows) |
| **block** | the run of samples the host asks the firmware for in one call of `vibe_on_audio` | [Architecture Overview](Architecture-Overview#the-firmwares-two-flows) |
| **the queue** | a ring of 64 messages: the one way the control flow tells the audio flow what to play (`send`) | [Architecture Overview](Architecture-Overview#the-firmwares-two-flows) |
| **`P`, `SONG`, `WAY`** | three variables of the firmware: the player, which the audio flow owns; the song being edited, which the control flow owns; and the hand-over between the two | [Architecture Overview](Architecture-Overview#the-firmwares-two-flows) |
| **part** | one of the 19 files `fw_*.h` that `sfx-forge.c` includes | [Firmware Internals](Firmware-Internals#the-parts) |
| **the song's way** | how an edited song gets from the control flow to the audio flow, whole | [Firmware Internals](Firmware-Internals) |
| **scene** | in `tests/`: a script of key presses that the SDK's test tool plays to the firmware | [Running The Tests](Running-The-Tests) |
| **check** | a script that reads what the firmware played, sent or stored and prints `PASSED` | [Running The Tests](Running-The-Tests) |
| **SDK** | the VIBE FM SDK: what a firmware for the VIBE FM is built with. A clone of it is in `.tools/vibe-fm-sdk` and is never edited | [Building From Source](Building-From-Source#what-must-be-installed) |
| **`wasmtest`, `vibe-check`** | two tools of the SDK. `wasmtest` plays a scene into a built firmware and writes the sound, the MIDI and pictures. `vibe-check` checks a firmware against its manifest | [Running The Tests](Running-The-Tests#how-a-firmware-check-is-written) |
| **Zig** | the C compiler used here, version 0.13.0: for the firmware, the DLL and the two-thread test | [Building From Source](Building-From-Source#what-must-be-installed) |
| **two-thread test** | `tests/stress-song-way.sh`: two real threads hand a song over through `song_way.h`. Also called the stress test | [Running The Tests](Running-The-Tests#the-suites-at-a-glance) |
| **reference documents** | the README, the manual, the Unity manual and the maintainer's guide. This wiki links to them for the full detail | [Editing This Wiki](Editing-This-Wiki) |

## Next

- [Start Here](Start-Here): a reading list by role.
- [FAQ](FAQ): short answers.
- [Device Cheat Sheet](Device-Cheat-Sheet): every key of the device.

## Reference

- [The maintainer's guide, 8. Glossary](../docs/MAINTAINING.md#8-glossary): the same words with their names in the
  code.
- [The manual, 4. Two kits](../docs/MANUAL.md#4-two-kits) and
  [6. How a sound is built](../docs/MANUAL.md#6-how-a-sound-is-built)
- [The Unity manual, 2. What is in the package](../docs/UNITY.md#2-what-is-in-the-package)
