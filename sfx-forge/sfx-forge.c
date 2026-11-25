/* SPDX-License-Identifier: GPL-3.0-or-later
 * SFX FORGE – sound effects, instruments and adaptive music for games, on VIBE FM.
 *
 * WHAT THE INSTRUMENT IS
 *
 *   two kits     SFX (the game's sound effects) and MUSIC (the instruments the song plays): 16 sounds each. SHIFT +
 *                ENTER (a short press) changes kit
 *   16 pads      the kit's 16 sounds, each a 28-byte patch (core/sfx_core.h). Five engines: IMPACT, UI BLIP, SCI-FI FM,
 *                FOLEY, WAVETABLE – with envelopes, an LFO, and sends to a chorus, a delay and a reverb
 *   keys         the selected sound as an instrument, in tune, 16 voices
 *   K1-K6        SOUND (press again: its next page) · ENV · LFO · FX (again: MIX, MASTER) · SEQ (again: SONG,
 *                AUTO, PERFORM) · LINK
 *   PERFORM      the whole pattern as a grid, a row a sound; F1-F4 make the pads STEPS, SOUNDS, MUTES or PATTERN
 *   AUTO         automation: a lane for every sound, moving any knob - one of the sound's, its fader, the master's
 *   SONG         also the arrangement: up to 32 bars, a pattern each
 *   MIX          a fader, a pan, mute and solo for every sound; a drive on the sum
 *   MASTER       the delay's time and feedback, the reverb's size and damping
 *                encoders 1-4 = the four values shown
 *   F1           PRESET: the next of the kit's named sounds (16 for SFX, 23 for MUSIC) onto the selected pad
 *                (SHIFT: the one before)
 *   LEFT RIGHT   the sound page before / after; on SEQ, SONG, AUTO and PERFORM the pattern
 *   PLAY         plays what the top line of the song's pages says: LOOP – the pattern repeats, a chosen one takes over
 *                on the bar line – or SONG – the arrangement, else the chain. F1 there changes it; REC lit switches to
 *                LOOP
 *   SEQ          a pattern over the 16 sounds: a bar of 16, 32 or 64 steps (its grid: SHIFT + F5 / F6). The pads are 16
 *                steps of the selected sound (SHIFT + LEFT / RIGHT: the 16 before / after), a step may carry a note
 *                (hold its pad and play a key, or the NOTE encoder), a velocity and a length
 *   SONG         8 patterns, a chain, and what makes it game music: every sound has a LAYER threshold and plays only
 *                while INTENSITY is at or above it; a cued pattern takes over on the next bar line
 *   LINK         the game engine's side: telemetry in (CC 10-15, channel 2: pitch, cutoff, timbre, level, intensity,
 *                pattern), encoders out as CC 20-25, patches and the song as SysEx (README.md)
 *   the stage    the whole instrument for the mouse (plugin and browser): pads, the sound's pages with values to
 *                drag, the song as 16 rows of steps with their notes, intensity and layer as sliders
 *   MENU         the tour: it plays and names what SFX FORGE can do, a caption at a time. SHIFT + MENU twice: factory
 *                reset
 *
 * On the sound pages the display shows the selected sound itself – its whole shape and its wave – as the knobs change
 * it.
 *
 * THE TWO FLOWS
 *
 * The host runs this firmware in two flows at once (VIBE_PARALLEL_AUDIO):
 *   the audio flow     vibe_on_audio (fw_audio.h). The player (core/sfx_song.h: voices, effects and sequencer) belongs
 *                      to it, its song included.
 *   the control flow   vibe_on_event, vibe_on_draw, vibe_on_stage (at the end of this file): keys, pads, MIDI, the
 *                      display, the store. Everything else in the firmware runs here.
 * The controls reach the audio through one queue (fw_audio.h: send). The song they edit is their own; after every event
 * and every draw what differs goes on its way to the player, which takes it whole between two blocks (song_way.h) - so
 * a grid change, a COPY, an UNDO or a song that comes in while it plays is heard from one block on, never half made.
 * What the audio tells the controls (the step that plays, what sounds, the scope) is a handful of 32-bit values that
 * only the audio writes (fw_state.h).
 *
 * THE PARTS
 *
 * The VIBE FM SDK compiles every .c file of this folder as a unit of its own, and the firmware is one unit: this file.
 * Its parts are the fw_*.h files, included below, once, in this order. They are not headers for anything else: each
 * uses what the parts before it define (each says what, at its top), and all of them share the statics of fw_state.h.
 * The order is also the order of the functions and tables in the built .wasm, so moving a function between parts
 * changes the binary (not what it does).
 *
 *   fw_protocol.h     the numbers of the link: SysEx messages, the identity, CCs, notes
 *   fw_sounds.h       the parameters a DAW automates; the 39 named sounds (PRESETS); the engines' names and colours
 *   fw_state.h        every variable the parts share - the audio's, the controls' - and the smallest helpers
 *   fw_audio.h        the queue from the controls to the audio, and vibe_on_audio
 *   fw_edit.h         what the display says for a moment (say), one step back for the song (F2), keys that ask twice
 *   fw_factory.h      the sounds and the song a new device holds
 *   fw_store.h        what is kept on the device: kits.bin, song6.bin, meter.bin (and song.bin of the firmwares before)
 *   fw_link.h         SysEx out (dumps, the song in pieces, the identity) and in (patches, a song, requests)
 *   fw_sequencer.h    playing and recording, LOOP and SONG, patterns, steps, automation lanes
 *   fw_pages.h        the pages and what the encoders turn on each of them
 *   fw_softkeys.h     F1-F6 on every page: presets, fill, clear, copy, the grid, the arrangement, the mixer
 *   fw_tour.h         the tour (MENU): its script and what runs it
 *   fw_stage_input.h  the stage's layout (960 x 540) and what the mouse does on it
 *   fw_events.h       vibe_on_init; the HOME texts; factory reset; on_event: every pad, key, note and MIDI message
 *   fw_draw_parts.h   the display's colours, a value as text, and the pieces a page is drawn from (dial, hud, row)
 *   fw_draw_sound.h   the picture of the selected sound
 *   fw_draw_pages.h   the top line and the pages SEQ, SONG, AUTO, MIX, MASTER, LINK, PERFORM
 *   fw_draw.h         on_draw: a whole frame of the display, the pads' lights, FOCUS
 *   fw_stage_draw.h   on_stage: a whole frame of the stage
 *   song_way.h        the song from the control flow to the audio flow (it stands alone: tests/stress-song-way.c)
 *
 * WHERE TO START READING
 *
 *   what a key does           fw_events.h, on_event - it names the function for every pad, key and message
 *   what a page shows         fw_draw.h, on_draw - it calls one draw_... for each page
 *   how a sound is made       core/sfx_core.h (the firmware only holds the patches and hands them to the player)
 *   how the song is played    core/sfx_song.h, sfx_player_render; from here: fw_audio.h, vibe_on_audio
 *   a message of the link     fw_protocol.h for its bytes, fw_link.h for what is done with it
 */
#include "vibe.h"
#include "vibe_dsp.h"
#include "vibe_gfx.h"
#include <stddef.h>

/* (the SDK brings these two) */
size_t strlen(const char *s);
char *strcpy(char *d, const char *s);

#include "../core/sfx_song.h"
#include "song_way.h"

VIBE_PARALLEL_AUDIO

/* The firmware's version. firmware.json says the same: tests/check-identity.mjs reads these three lines from THIS file
   and compares, so they stay here (the identity that carries them is in fw_protocol.h and fw_link.h). */
#define FW_MAJOR 1
#define FW_MINOR 7
#define FW_PATCH 1

#include "fw_protocol.h"
#include "fw_sounds.h"
#include "fw_state.h"
#include "fw_audio.h"
#include "fw_edit.h"
#include "fw_factory.h"
#include "fw_store.h"
#include "fw_link.h"
#include "fw_sequencer.h"
#include "fw_pages.h"
#include "fw_softkeys.h"
#include "fw_tour.h"
#include "fw_stage_input.h"
#include "fw_events.h"
#include "fw_draw_parts.h"
#include "fw_draw_sound.h"
#include "fw_draw_pages.h"
#include "fw_draw.h"
#include "fw_stage_draw.h"

/* ---- what the host calls in the control flow ------------------------------------------------------------------- */
/* After each of them the song as it is now goes on its way to the player: whatever was done to it - one step or all
   eight patterns - the audio takes whole, with its next block. Before that, after an event and a draw, the host is
   told what changed in the mix (mix_report, fw_pages.h). (vibe_on_init is in fw_events.h, vibe_on_audio in
   fw_audio.h.) */
void vibe_on_event(const vibe_event *e) {
  on_event(e);
  mix_report();
  song_hand();
}

void vibe_on_draw(uint16_t *fb) {
  on_draw(fb);
  mix_report();
  song_hand();
}

void vibe_on_stage(uint16_t *fb, int w, int h) {
  on_stage(fb, w, h);
  song_hand();
}
