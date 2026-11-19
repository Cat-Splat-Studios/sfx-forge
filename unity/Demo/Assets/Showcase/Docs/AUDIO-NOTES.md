# BRIO: FIRST HARMONIC – audio notes

How the showcase's sound is put together (`Scripts/Audio`, assembly `SfxShowcase.Audio`), what the scene builder
must wire, and which sound answers which game event. Every sound is SFX FORGE at runtime; there is no audio file.

## 1. The files, in reading order

| File | What it is |
|---|---|
| `SoundLibrary.cs` | **The whole sound set on one screen.** One named, commented `SfxPatch` per effect, and a table giving each its route, the note it is tuned to and its volume. Start here. |
| `GameSound.cs` | The names of the 19 sound effects. |
| `SoundRoute.cs` | The three ways a sound is played: `Baked`, `LiveOneShot`, `LiveHeld`. |
| `SoundDefinition.cs` / `SoundBinding.cs` | A library entry (code) / the same with its `SfxPreset` asset (serialized in the scene). |
| `Pitch.cs` | MIDI notes of E minor pentatonic and `PentatonicSemitones(step)`. |
| `SoundEffects.cs` | **Use 1 – one-shots.** Listens to `GameEvents`; plays baked clips and live pooled voices. |
| `LiveVoicePool.cs` | A small pool of `SfxPlayer` voices for live one-shots. |
| `ContinuousSounds.cs` | **Use 2 – sounds bent while they play.** Roll hum, wind, Wind-Up rev, driven from `IPlayerState` every frame. |
| `HeldSound.cs` | Hold / let go / retrigger for one sustained `SfxPlayer`. |
| `MusicDirector.cs` | **Use 3 – adaptive music.** Intensity from speed, a pattern per section, hurt / checkpoint / goal moments. |
| `SectionCue.cs`, `MusicNote.cs`, `MusicKit.cs` | The music's data: a section's pattern and floor, one hand-played note, the song's instrument indices. |
| `AudioInstaller.cs`, `PresetFactory.cs` | Builds and wires the `Audio` object. The entry point the scene builder calls. |

Audio only listens and reads: it subscribes to `GameEvents` in `OnEnable`, unsubscribes in `OnDisable`, reads
`PlayerBase` / `GameSessionBase`, and never raises an event or changes the game.

## 2. What the builder must do

One call, after the player and the session exist:

```csharp
AudioInstaller.Install(player, session, song, presetFactory);
```

The builder does this in `Editor/ShowcaseAudio.cs`, the only editor file that names the audio code.

- `player` and `session` are the `PlayerBase` and the `GameSessionBase` of the scene; nothing else of the game is needed.
- `song` is `Assets/SfxDemo/DemoSong.asset` (only read).
- `presetFactory(name, patch, holdSeconds)` is called once for each of the 19 sounds, with the names
  `Jump, Land, FootstepLeft, FootstepRight, TuckStart, RollHum, WindUpRev, Burst, Chime, ChimeScatter, Woofer, Hurt,
  ClipperPop, TuningFork, Goal, Wind, Skid, Respawn, UiConfirm`. It must return an `SfxPreset` whose `patch` and
  `holdSeconds` are set from the arguments (the builder writes `Generated/Presets/<name>.asset`). Passing `null`
  creates the presets in memory instead.
- The installer creates everything below and assigns every field; the builder adds nothing by hand. The scene's
  `AudioListener` is the camera's (level step).
- The `Audio` object is built inactive and activated at the end, so the same call also works at runtime.

### The object tree and its wiring

| GameObject | Components | Fields assigned |
|---|---|---|
| `Audio` | – | root, at the origin |
| `Audio/Music` | `AudioSource`, `SfxMusicPlayer`, `MusicDirector` | `SfxMusicPlayer.song` = the song, `playOnEnable` = true, `startPattern` = 0. `MusicDirector.Wire(musicPlayer, context.Player, context.Session)` → `music`, `player`, `session`. |
| `Audio/Effects` | `AudioSource`, `LiveVoicePool`, `SoundEffects` | `LiveVoicePool.Configure(voices)` → `voices` = the three `Live Voice n` players. `SoundEffects.Configure(bindings, oneShots, footsteps, pool)` → `sounds` (19 bindings: sound, preset, route, MIDI note, volume), `oneShotSource` = this object's `AudioSource`, `footstepSource` = `Footsteps`' source, `liveVoices` = the pool. |
| `Audio/Effects/Footsteps` | `AudioSource` | – |
| `Audio/Effects/Live Voice 1..3` | `AudioSource`, `SfxPlayer` | `preset` = none (the pool assigns it per trigger), `volume` = 0.8, `playOnEnable` = false |
| `Audio/Continuous` | `ContinuousSounds` | `Wire(context.Player, rollHum, wind, windUp)` → `player`, `rollHumPlayer`, `windPlayer`, `windUpPlayer` |
| `Audio/Continuous/Roll Hum` | `AudioSource`, `SfxPlayer` | `preset` = RollHum, `volume` = 0.5, `playOnEnable` = false |
| `Audio/Continuous/Wind` | `AudioSource`, `SfxPlayer` | `preset` = Wind, `volume` = 0.6, `playOnEnable` = false |
| `Audio/Continuous/Wind-Up Rev` | `AudioSource`, `SfxPlayer` | `preset` = WindUpRev, `volume` = 0.6, `playOnEnable` = false |

Every `AudioSource` is 2D (`spatialBlend` 0), `playOnAwake` false, no clip. `SfxPlayer` and `SfxMusicPlayer` add
their sound to the `AudioSource` beside them and keep it running themselves.

Doing it by hand in the Inspector is the same list: add the components of a row, drag the references of the last
column. All tunable numbers (thresholds, ranges, times, the section table, the two phrases) are serialized fields
with tooltips on `SoundEffects`, `ContinuousSounds`, `MusicDirector` and `LiveVoicePool`.

## 3. Game event → patch → how it is played

"Baked" = rendered once in `SoundEffects.Awake` with `SfxPreset.Bake` (at the output sample rate, as the MIDI note
in the last column) and played with `AudioSource.PlayOneShot`. "Live" = synthesized by an `SfxPlayer`.

| # | Game event / state | Patch (`SoundLibrary`) | Engine, starts from | How it is played | Variation from the game | Note |
|---|---|---|---|---|---|---|
| 1 | `Jumped` | `Jump` | UI BLIP, POWER-UP | Baked | – | E5 |
| 2 | `Landed(impactSpeed)` | `Land` | IMPACT, THUD | Baked | Silent below 4 m/s; volume 25 %..100 % from 4 to 25 m/s | – |
| 3 | `Footstep(speed01)` | `FootstepLeft` / `FootstepRight` | FOLEY, FOOTSTEP | Baked, two clips, own `AudioSource` | Alternates left / right; source pitch 1.0..1.3 with speed | – |
| 4 | `TuckStarted` | `TuckStart` | SCI-FI FM, LASER | Baked | – | B4 |
| 5 | state: `IsRolling && IsGrounded` | `RollHum` | SCI-FI FM, ENGINE HUM | Live, held (`ContinuousSounds`) | Held as the note E2 (`Play(1, 0, 40)`); `pitchSemitones` 0..12, `cutoffOctaves` −1..+1.5, `level` 0.35..1 with `Speed01` | E2 |
| 6 | `WindUpCharged(charge01)`, state `IsCharging` / `Charge01` | `WindUpRev` | SCI-FI FM, ENGINE HUM / WARP | Live, held; retriggered on every tap | `pitchSemitones` 0..12, `fm` 0..0.6, `cutoffOctaves` 0..1 with charge (falls again as it leaks); released on `BurstReleased` or when `IsCharging` goes false | E3 |
| 7 | `BurstReleased(charge01)` | `Burst` | FOLEY, WHOOSH + PUNCH | Baked | Volume 50 %..100 % with charge | – |
| 8 | `ChimeCollected(combo)` | `Chime` | UI BLIP, COIN | Live one-shot (`LiveVoicePool`) | One step up E minor pentatonic per combo count, capped at 10 steps (two octaves); at most one ping per 45 ms | E5 |
| 9 | `ChimesScattered(count)` | `ChimeScatter` | UI BLIP, COIN + CRUSH | Live one-shots: a run of 5–8 pings, 55 ms apart | More pings with more Chimes; each ping a pentatonic step lower and quieter | E5 |
| 10 | `WooferBounced` | `Woofer` | SCI-FI FM, WARP / POWER-UP | Baked | – | E3 |
| 11 | `Hurt` | `Hurt` | UI BLIP, ERROR / ZAP | Baked | – (the music flinches, below) | D3 |
| 12 | `ClipperDefeated` | `ClipperPop` | IMPACT, METAL HIT + PUNCH | Baked | – | B4 |
| 13 | `CheckpointReached(index)` | `TuningFork` | IMPACT, METAL HIT | Baked | – (the music answers, below) | E5 |
| 14 | `GoalReached` | `Goal` | UI BLIP, POWER-UP | Baked | – (the fanfare follows, below) | E4 |
| 15 | state: `Speed01` above 0.6 | `Wind` | FOLEY, WIND | Live, held (`ContinuousSounds`) | `level` 0..1 and `cutoffOctaves` −1.5..+1.5 from 60 % to 100 % of top speed, faded over 0.4 s | – |
| 16 | `Skidded` | `Skid` | FOLEY, CRACKLE | Baked | – | – |
| 17 | `Respawned(section)` | `Respawn` | SCI-FI FM, WARP | Baked | – | E4 |
| 18 | `UiConfirmed` | `UiConfirm` | UI BLIP, SELECT | Baked | – | B5 |
| 19 | `SectionEntered(section)` | – | – | Music only | – | – |

There is no event for the end of a roll: the roll hum ends when the state says the roll ended. The held
sounds are started with their MIDI note like every other pitched sound (`HeldSound.Hold`); the wind is noise
and is started with note 0, at the patch's own FREQ.

### Music (`MusicDirector`)

| Game event / state | What the music does | SFX FORGE call |
|---|---|---|
| every frame | Intensity = `Speed01`, never below the section's floor; rises in 0.6 s, falls in 2.5 s. Fixed at 0.15 on the title card. | `SfxMusicPlayer.intensity` |
| `SectionEntered(section)` | The section's pattern from the next bar line, and held there: A, B, C, D, E, C, D, – (floors 0.15, 0.25, 0.40, 0.40, 0.20, 0.45, 0.70, 0). | `CuePattern`, again whenever `Bar` changes |
| `FinalLaunch` | The turnaround, pattern F, held until the goal stops the song. | `CuePattern(5)`, again whenever `Bar` changes |
| `Hurt` | Intensity to 0 at once; volume to 50 % over 50 ms; both recover over 2 s. | `intensity`, `volume` |
| `Respawned(section)` | That section's pattern again; intensity starts from its floor. | `CuePattern` |
| `CheckpointReached` | BELLS play E5, B5, E6 a sixteenth apart on top of the song, starting on the song's next sixteenth (`Step`); intensity at least 0.6 for one bar. | `PlaySound(13, velocity, note)` |
| `GoalReached` | The song stops. The fanfare's instruments are brought forward and the room made bigger, then: STRINGS hold E3, BRASS plays E4 G4 B4 E5 an eighth apart, BELLS E6 and TIMPANI on the last note. | `Stop`, `SetTrackVolume`, `SetMaster(ReverbSize)`, `PlaySound`, `ReleaseSound` |
| `GameStarted` | From the title card: pattern A is cued. After a goal: the fanfare is cut, the song's own mix is restored and the song starts again at pattern A. | `CuePattern`, `Reload`, `Play` |

## 4. Decisions worth knowing

- **Speed.** `Speed01` is the player's speed of travel ÷ the speed cap: along the ground, or sideways in the
  air. A Woofer's throw straight up or a fall does not raise the intensity or start the wind.
- **Holding a section's pattern.** The demo song has a 16-bar arrangement (A B A B C D C D E E A B C D C F) that
  contains every pattern. With an arrangement, `CuePattern(n)` means "jump to the arrangement's next bar with
  pattern n, then walk on", so one cue lasts one bar (two for E). `MusicDirector.HoldPattern` cues the
  section's pattern again each time `SfxMusicPlayer.Bar` changes; a cue pending at every bar line keeps the
  pattern. On the title card nothing is held and the whole arrangement plays. A song without an arrangement
  (`SfxSong.ArrangementBars == 0`) holds a cued pattern by itself.
- **The package does not smooth `SfxMusicPlayer.volume`** (it is applied per audio block; the four live values
  of an `SfxPlayer` glide over 30 ms). A dip in one step is a click, so the hurt dip is ramped over 50 ms.
- **Notes played by hand are not quantised.** `PlaySound` sounds at once. The checkpoint's phrase waits for the
  song's next sixteenth (`Step` changes) and then spaces its notes by the song's tempo.
- **The Chime's arpeggio.** UI BLIP's arpeggio always climbs three steps and stays on the last. With a fifth as
  the step the Chime is E, B, F♯, C♯ at falling levels (about 100 %, 40 %, 17 %, 7 %); the last two are outside
  the pentatonic and are heard as sparkle. Not judged by ear in this pass: engineA 64 (octaves) would stay on E.

- **Tuning.** Every pitched effect is played as a MIDI note (`Bake(midiNote: …)` / `SfxPlayer.Play(…, midiNote)`), which
  overrides the patch's FREQ, so it is exactly in tune with the song. Pure-noise FOLEY patches have no pitch
  (transposing them does nothing), which is why the right footstep is a patch variation and footsteps rise with
  the `AudioSource`'s pitch.
- **Why the scatter is live, not baked** (a change from the design table): each ping of the spill has its own
  pitch, which a single baked clip cannot have. It uses the same pool as the Chime.
- **Why a voice pool.** One `SfxPlayer` holds one patch: it overlaps up to 16 notes of it, but a different patch
  reshapes what still sounds. The pool gives a sound the voice already playing it, else a silent voice, else the
  oldest.
- **"At most four Chime voices"** is kept by a minimum interval between pings (45 ms against a decay of about
  0.12 s), not by counting voices – `SfxPlayer` does not expose its voices.
- **`SetTrackMute` is not used**: the design asks for no mute. `SetTrackVolume` and `SetMaster` are shown in the
  fanfare.
- **Without the native library** (anything but Windows x64): `SoundEffects` logs one warning and bakes nothing;
  every play call finds no clip or a player that ignores it, so the game runs silent without errors. The package's
  `SfxPlayer` and `SfxMusicPlayer` each log their own single warning as well.

## 5. Tuning a sound

1. Open `SoundLibrary.cs`, change a value of the patch (the header comment explains each one), rebuild the scene.
2. Or, in play mode, select `Generated/Presets/<name>.asset` and move a slider: live sounds (Chime, roll, wind,
   rev) follow at once; baked sounds are re-rendered on the next start.
3. Routes, notes and volumes are the table at the bottom of `SoundLibrary.cs`; behaviour numbers are on the three
   components under `Audio`.
