# Firmware Internals

This page shows how the firmware works from the inside: how a key press becomes sound, how a frame of the display
is drawn, and which of the 19 parts holds what. Read [Architecture Overview](Architecture-Overview) first for the
two flows.

The firmware is one compilation unit: the compiler is given the one file `sfx-forge/sfx-forge.c`. It includes 19
parts named `fw_*.h`, once, in a fixed order, plus `song_way.h`. Each part uses what the parts before it define.

The keys this page names (K1 to K6, F1 to F6, HOME, MENU, SHIFT) and the words FOCUS, stage and tour are in the
[Glossary](Glossary#the-device); `P`, `SONG`, `WAY` and the queue are on
[Architecture Overview](Architecture-Overview#the-firmwares-two-flows).

## How a key press travels

Take one press: pad 5 on the SEQ page while the song stands still, REC off and SHIFT not held. It switches step 5
of the selected sound on, and you hear the sound once.

```mermaid
sequenceDiagram
  participant H as Host
  participant V as vibe_on_event
  participant E as on_event
  participant S as step_toggle
  participant Q as the queue
  participant W as WAY
  participant A as vibe_on_audio
  H->>V: a pad, down
  V->>E: on_event
  E->>S: the page is SEQ, so the pad is a step
  S->>S: undo_open, then set the bit in SONG
  S->>Q: send M_TRIGGER, then send M_RELEASE
  V->>W: song_hand copies what differs in SONG
  H->>A: the next block of samples
  A->>W: song_way_take: P.song is the new song
  A->>Q: read the messages, sfx_player_note_on
  A->>A: sfx_player_render makes the block
```

The same in words:

1. The host calls `vibe_on_event` (end of `sfx-forge.c`), which calls `on_event` (`fw_events.h`) and then
   `song_hand()`.
2. `on_event` decides nothing itself. It gives the event to FOCUS first (the SDK's frame for controllers without
   the keys), sends on what waits (`out_pump`), takes in what the audio did (`seq_poll`), and then has one `case`
   for each kind of event: a pad, a note, MIDI, SysEx, an encoder, the stage, a button.
3. The pad case asks which page is open. On SEQ it calls `step_toggle` (`fw_sequencer.h`).
4. `step_toggle` calls `undo_open` so that F2 can take the edit back, flips the step's bit in `SONG`, and, because
   the song stands still, calls `send()` twice so you hear the step.
5. `song_hand()` copies the changed pattern into `WAY`. At the start of the next block the audio takes it and reads
   the queue.

To find what any key does, start in `on_event`: it names the function for every pad, key and message.

| Event | Goes to |
|---|---|
| a pad | by page: `step_toggle` (SEQ), `pattern_pick` or `intensity_set` (SONG), `auto_toggle` (AUTO), `perform_pad` (PERFORM), mute or solo (MIX), else `pad` (it plays its sound) |
| a note | notes 36 to 51 from outside are the pads; else a key plays the selected sound at that pitch |
| MIDI | the game's telemetry: CC 10 to 15 on channel 2 |
| SysEx | `sysex_in` (`fw_link.h`) |
| an encoder | `encoder` (`fw_pages.h`) |
| the stage (the mouse) | `stage_event` (`fw_stage_input.h`) |
| F1 to F6 | by page: `soft_sound`, `soft_seq`, `soft_song`, `soft_auto`, `soft_mix`, `soft_perform` (`fw_softkeys.h`) |
| K1 to K6 | `tab_key` (`fw_pages.h`), which calls `set_page` |

## The pages and the six K keys

A page is what the display shows and what the encoders and F1 to F6 do. Each page hangs on one K key; pressing the
key again goes to its next page.

```mermaid
flowchart LR
  K1["K1 SOUND"] --> OSC --> TIMBRE["TIMBRE<br/>shown as TONE"] --> FILTER --> WAVE["WAVE<br/>wavetable sounds only"]
  K2["K2 ENV"] --> ENV
  K3["K3 LFO"] --> LFO
  K4["K4 FX"] --> FX --> MIX --> MASTER
  K5["K5 SEQ"] --> SEQ --> SONG --> AUTO --> PERFORM
  K6["K6 LINK"] --> LINK
```

The order of the `PG_` enum in `fw_state.h` is used by the code: `page < PG_SEQ` is a sound page, and `PG_SEQ` to
`PG_MASTER` are the song's pages. See [Firmware Add A Page Or Key](Firmware-Add-A-Page-Or-Key).

## How a frame is drawn

The host calls `vibe_on_draw`, which calls `on_draw` (`fw_draw.h`) and then `song_hand()`. `on_draw` has seven
numbered parts.

![The SEQ page of the display while the song plays, with the parts of on_draw marked: 3 the top line, 5 the page, 6 the soft keys; a list under it names all seven](img/in-frame-parts.png)

| Part | What it does |
|---|---|
| 1 | what must go on whether or not anything is seen: `out_pump` (the link), `tour_pump` (the tour), `seq_poll` (the sequencer) |
| 2 | HOME: the page's controls instead of the page, from a `HELP_...` table. Nothing else is drawn |
| 3 | the top line (`header`), and under it the page's name, the pattern and six marks for K1 to K6 |
| 4 | the 16 pads' lights. A light is sent to the host only when it changes (`led_cache`) |
| 5 | the page itself: one function for each page (`scene`, `draw_seq`, `draw_song`, `draw_auto`, `draw_mix`, `draw_master`, `draw_link`, `draw_perform`) and the four values over encoders 1 to 4 |
| 6 | F1 to F6, or the tour's caption |
| 7 | FOCUS: where the SDK's frame may go |

`on_draw` is also the control flow's clock: part 1 runs every frame. But the firmware must play when the host does
not draw (the plugin with its window closed), so nothing the audio needs may hang on `on_draw`.

The display is 480 x 320 pixels. The stage, 960 x 540, is the whole instrument for the mouse: `on_stage`
(`fw_stage_draw.h`) draws it and `stage_event` (`fw_stage_input.h`) handles clicks at the same numbers.

## The parts

In include order. That order is also the order of the code in the built `.wasm`.

| Part | What it holds |
|---|---|
| `fw_protocol.h` | the numbers of the link: SysEx messages, the identity, CCs, notes |
| `fw_sounds.h` | the parameters a DAW automates; the 39 named sounds (`PRESETS`); the engines' names and colours |
| `fw_state.h` | every variable the parts share, and the smallest helpers |
| `fw_audio.h` | the queue from the controls to the audio (`send`), and `vibe_on_audio` |
| `fw_edit.h` | `say` (a word on the display), `changed`, the undo, `confirm` (keys that ask twice) |
| `fw_factory.h` | the sounds and the song a new device holds (`factory`, `demo_song`), and `step_set` |
| `fw_store.h` | what is kept on the device: `bank_save`, `bank_load` |
| `fw_link.h` | SysEx out and in: `out_pump`, `sysex_in`, `song_piece` |
| `fw_sequencer.h` | playing and recording, LOOP and SONG, patterns, steps, automation lanes |
| `fw_pages.h` | `set_page`, `tab_key`, `arrow`, and what the encoders turn on each page (`encoder`) |
| `fw_softkeys.h` | F1 to F6 on every page |
| `fw_tour.h` | the tour (MENU): its script and what runs it |
| `fw_stage_input.h` | the stage's layout and what the mouse does on it |
| `fw_events.h` | `vibe_on_init`, the HOME texts, `factory_reset`, `on_event` |
| `fw_draw_parts.h` | the colours, a value as text, and the pieces a page is drawn from (`dial`, `hud`, `row`) |
| `fw_draw_sound.h` | the picture of the selected sound |
| `fw_draw_pages.h` | the top line and the pages SEQ, SONG, AUTO, MIX, MASTER, LINK, PERFORM |
| `fw_draw.h` | `on_draw`: a whole frame of the display, the pads' lights, FOCUS |
| `fw_stage_draw.h` | `on_stage`: a whole frame of the stage |
| `song_way.h` | the song from the control flow to the audio flow. It stands alone, so a test can set two threads on it |

## The state groups

All shared state is in `fw_state.h`, in six groups.

| Group | Examples | Written by |
|---|---|---|
| 1. the audio flow's own | `P` (the player), `seq_pos`, `play_k`, the scope, the click | the audio (but `metro`, `rec_on`, `beat_len`: the controls) |
| 2. the song as the controls have it | `SONG`, `WAY` | the controls |
| 3. the pages | the `PG_` enum, `TAB_OF`, `PAGE_PARAMS` | constants |
| 4. the two kits | `kits`, `kit`, the macro `patches` | the controls |
| 5. the control flow's own | `sel`, `page`, `shift`, `rec`, the undo, the tour, the link's transfers, `perf_mode` | the controls |
| 6. F1 to F6 | `SOFT`, `soft_caption`, `relabel` | constants and helpers |

Two traps:

- **`patches` is a macro for `kits[kit]`**: the kit that is chosen. So are `pad_preset` and `sound_undo`. Code that
  must reach the other kit sets `kit` first and puts it back, as `factory_reset` and `sysex_in` do.
- **The order of the definitions is the order of the data in the `.wasm`.** A new variable goes at the end of its
  group.

## Next

- [Firmware Add A Page Or Key](Firmware-Add-A-Page-Or-Key), [Firmware Add An Engine](Firmware-Add-An-Engine),
  [Firmware Add A Setting](Firmware-Add-A-Setting): the three recipes.
- [Link Protocol](Link-Protocol): what `sysex_in` and `out_pump` speak.
- [Running The Tests](Running-The-Tests): how to check a change.

## Reference

- [sfx-forge.c](../sfx-forge/sfx-forge.c): the header comment (the map of the parts) and the three entry points
- [fw_events.h](../sfx-forge/fw_events.h) (`on_event`), [fw_draw.h](../sfx-forge/fw_draw.h) (`on_draw`),
  [fw_state.h](../sfx-forge/fw_state.h), [fw_audio.h](../sfx-forge/fw_audio.h),
  [fw_sequencer.h](../sfx-forge/fw_sequencer.h), [fw_pages.h](../sfx-forge/fw_pages.h)
- [The maintainer's guide, 3. Reading order](../docs/MAINTAINING.md#3-reading-order) and
  [4. The two flows](../docs/MAINTAINING.md#4-the-two-flows)
- [The manual, 20. Every control, page by page](../docs/MANUAL.md#20-every-control-page-by-page)
- The picture is made by [wiki/figures/in_shots.mjs](../wiki/figures/in_shots.mjs) and
  [wiki/figures/in_figures.py](../wiki/figures/in_figures.py) from
  [a screenshot of the SEQ page](img/in-shot-seq.png)
