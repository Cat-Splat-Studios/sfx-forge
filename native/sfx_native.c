/* SPDX-License-Identifier: MIT
 * sfx_native.c – the SFX FORGE engine as a native library, for game engines (Unity: P/Invoke).
 * The caller owns all memory: a voice is sfxn_voice_size() zeroed bytes, a player sfxn_player_size() bytes. Nothing
 * here allocates. A patch is the 28 bytes of core/sfx_core.h. Rendering functions may be called from an audio thread.
 *
 * WHAT THIS FILE HOLDS, in its order:
 *   1. What the library is        sfxn_abi, sfxn_patch_bytes, sfxn_song_bytes
 *   2. One voice, dry and mono    sfxn_voice_size, sfxn_trigger, sfxn_update, sfxn_release, sfxn_active, sfxn_render
 *   3. The player                 sfxn_player_size, _init, _set_patch, _set_song, _start, _stop, _cue, _set_loop,
 *                                 _set_track, _set_master, _set_live, _note_on, _note_off, _render, _state, _grid
 *   4. Tools for a packed song    sfxn_song_convert, sfxn_song_regrid_loss, sfxn_song_regrid
 *   5. A sound as samples         sfxn_bake
 *
 * Every function here is a thin door to core/sfx_core.h and core/sfx_song.h: it turns the caller's bytes and pointers
 * into the core's structs, refuses what is out of range, and calls the core. The sound itself is made there.
 * The C# side of every function is unity/com.catsplat.sfxforge/Runtime/Core/SfxNative.cs: a function added, removed
 * or changed here must change there too, and sfxn_abi's number with it.
 * Built by native/build.sh.
 */
#include "../core/sfx_song.h"

#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

/* ==== 1. What the library is ====================================================================================== */

/* the number of this interface: the C# side refuses a library with another */
API int sfxn_abi(void) {
  return 5;
}

/* the bytes of a packed patch: 28 */
API int sfxn_patch_bytes(void) {
  return SFX_PATCH_BYTES;
}

/* the most a packed song has: it has no fixed size from format 6 on */
API int sfxn_song_bytes(void) {
  return SFX_SONG_MAX;
}

/* ==== 2. One voice, dry and mono: the smallest way to play a patch ================================================ */

/* the bytes a voice needs; the caller gives them zeroed */
API int sfxn_voice_size(void) {
  return (int)sizeof(sfx_voice);
}

/* Starts the voice with the packed `patch` (28 bytes). vel 0..1, note_st: semitones up or down, sr: sample rate,
   seed: for its noise (the same seed gives the same samples).
   0 = it plays, -1 = not a valid patch.
   Not for an audio thread the first time: it fills the wavetables once (a few milliseconds). */
API int sfxn_trigger(void *voice, const uint8_t *patch, float vel, float note_st, float sr, uint32_t seed) {
  sfx_patch p;
  if (sfx_patch_unpack(&p, patch)) {
    return -1;
  }
  sfx_wt_init();
  sfx_voice_trigger((sfx_voice *)voice, &p, vel, note_st, 0, sr, seed);
  return 0;
}

/* New values for a voice that sounds (a slider moved). 0 = taken, -1 = the voice is silent or the patch not valid. */
API int sfxn_update(void *voice, const uint8_t *patch) {
  sfx_patch p;
  sfx_voice *v = (sfx_voice *)voice;
  if (!v->active || sfx_patch_unpack(&p, patch)) {
    return -1;
  }
  sfx_voice_set_patch(v, &p);
  return 0;
}

/* the key is let go: a sustained sound begins its release */
API void sfxn_release(void *voice) {
  sfx_voice_release((sfx_voice *)voice);
}

/* 1 while the voice sounds */
API int sfxn_active(const void *voice) {
  return ((const sfx_voice *)voice)->active;
}

/* ADDS n mono samples to out. mods4 = pitch (semitones), cutoff (octaves), timbre (0..1), level (0..1); NULL = none.
   Returns 1 while the voice sounds. */
API int sfxn_render(void *voice, const float *mods4, float *out, int n) {
  sfx_mods m = {0.0f, 0.0f, 0.0f, 1.0f};
  if (mods4) {
    m.pitch_st = mods4[0];
    m.cutoff_oct = mods4[1];
    m.fm = mods4[2];
    m.level = mods4[3];
  }
  return sfx_voice_render((sfx_voice *)voice, &m, out, n);
}

/* ==== 3. The player: 16 sounds, 16 voices, the effects, a song and its sequencer. Stereo. ========================= */

/* the bytes a player needs (about 600 KB) */
API int sfxn_player_size(void) {
  return (int)sizeof(sfx_player);
}

/* A player at sample rate sr: silent, stopped, an empty song. Not for an audio thread (it clears all its memory). */
API void sfxn_player_init(void *player, float sr) {
  sfx_player_init((sfx_player *)player, sr);
}

/* Sound `slot` (0..15) becomes the packed `patch`. 0 = taken, -1 = no such slot or not a valid patch. */
API int sfxn_player_set_patch(void *player, int slot, const uint8_t *patch) {
  sfx_patch p;
  if (slot < 0 || slot >= SFX_TRACKS || sfx_patch_unpack(&p, patch)) {
    return -1;
  }
  sfx_player_set_patch((sfx_player *)player, slot, &p);
  return 0;
}

/* the packed song (sfx_song_pack: what the device sends and stores). -1 = not a song, the player keeps what it had */
API int sfxn_player_set_song(void *player, const uint8_t *song, int n) {
  return sfx_song_unpack(&((sfx_player *)player)->song, song, n);
}

/* the sequencer starts with this pattern (0..7); -1: the pattern it is on */
API void sfxn_player_start(void *player, int pattern) {
  sfx_player_start((sfx_player *)player, pattern);
}

API void sfxn_player_stop(void *player) {
  sfx_player_stop((sfx_player *)player);
}

/* change to this pattern on the next bar line (at once while stopped) */
API void sfxn_player_cue(void *player, int pattern) {
  sfx_player_cue((sfx_player *)player, pattern);
}

/* 1: LOOP - the pattern that plays repeats; a cue chooses another, and that one repeats.
   0: the arrangement or the chain (as it starts) */
API void sfxn_player_set_loop(void *player, int on) {
  sfx_player_set_loop((sfx_player *)player, on);
}

/* The mixer, while it plays. A sound's fader (0..127, 100 = as designed), pan (0..127, 64 = the middle) and mute
   (0 / 1); -1 leaves a value as it is. */
API void sfxn_player_set_track(void *player, int slot, int vol, int pan, int mute) {
  sfx_song *s = &((sfx_player *)player)->song;
  if (slot < 0 || slot >= SFX_TRACKS) {
    return;
  }
  if (vol >= 0) {
    s->vol[slot] = (uint8_t)(vol > 127 ? 127 : vol);
  }
  if (pan >= 0) {
    s->pan[slot] = (uint8_t)(pan > 127 ? 127 : pan);
  }
  if (mute >= 0) {
    s->mute = (uint16_t)(mute ? s->mute | (1u << slot) : s->mute & ~(1u << slot));
  }
}

/* The master: index SFX_M_* (0 delay time 0..5, 1 feedback, 2 reverb size, 3 damping, 4 drive). A value above its
   range is brought to its top; a negative value or an index outside 0..7 is ignored. (5, 6 and 7 are the song's three
   spare bytes: they are written, and nothing reads them.) */
API void sfxn_player_set_master(void *player, int index, int value) {
  sfx_song *s = &((sfx_player *)player)->song;
  if (index < 0 || index >= SFX_M_COUNT || value < 0) {
    return;
  }
  /* the delay's time is one of six, every other value 0..127.
     (Kept as one expression: the plainer form compiled to other instructions - docs/MAINTAINING.md, "Fragile
     spots") */
  s->fx[index] = (uint8_t)(index == SFX_M_DELAY_TIME ? (value >= SFX_DELAY_TIMES ? SFX_DELAY_TIMES - 1 : value)
                                                     : (value > 127 ? 127 : value));
}

/* what the game moves while it plays: mods4 as sfxn_render (NULL = leave), intensity 0..1 */
API void sfxn_player_set_live(void *player, const float *mods4, float intensity) {
  sfx_player *p = (sfx_player *)player;
  if (mods4) {
    p->mods.pitch_st = mods4[0];
    p->mods.cutoff_oct = mods4[1];
    p->mods.fm = mods4[2];
    p->mods.level = mods4[3];
  }
  p->intensity = intensity < 0.0f ? 0.0f : intensity > 1.0f ? 1.0f : intensity;
}

/* One of the song's 16 sounds played by hand (through its fader, pan and mute), whether the sequencer runs or not.
   key: any number below 200 that names this note for sfxn_player_note_off.
   midi_note 0 = as designed (a sound effect), else at that pitch; note_st transposes either.
   A slot or a key out of range is ignored. */
API void sfxn_player_note_on(void *player, int slot, int key, float vel, int midi_note, float note_st) {
  if (slot >= 0 && slot < SFX_TRACKS && key >= 0 && key < SFX_KEY_SEQ) {
    sfx_player_note_on((sfx_player *)player, slot, key, vel, midi_note, note_st, 0);
  }
}

API void sfxn_player_note_off(void *player, int key) {
  if (key >= 0 && key < SFX_KEY_SEQ) {
    sfx_player_note_off((sfx_player *)player, key);
  }
}

/* ADDS n stereo samples to l and r, on its own clock. Returns the sounds that play, a bit each; bit 31 while only the
   effects still ring; 0 = silence. */
API unsigned sfxn_player_render(void *player, float *l, float *r, int n) {
  return sfx_player_render((sfx_player *)player, l, r, n, -1.0, 0.0);
}

/* out4: running, the sixteenth of the bar (-1 = none yet), pattern, bar */
API void sfxn_player_state(const void *player, int *out4) {
  const sfx_player *p = (const sfx_player *)player;
  out4[0] = p->running;
  out4[1] = p->step < 0 ? -1 : p->step / (p->div ? p->div : 1);
  out4[2] = p->pattern;
  out4[3] = p->bar;
}

/* out2: the step in the grid of the pattern that plays (-1 = none yet), the steps a sixteenth of that grid (1, 2, 4) */
API void sfxn_player_grid(const void *player, int *out2) {
  const sfx_player *p = (const sfx_player *)player;
  out2[0] = p->step;
  out2[1] = p->div ? p->div : 1;
}

/* ==== 4. Tools for a packed song ==================================================================================
   Not for an audio thread: they hold a whole song on the stack (about 38 KB). */

/* The song packed into out (max bytes) by way of a copy on the stack: for a buffer that may be too short for the
   longest song. -1 = it does not fit. (noinline: so that only this path pays for the second 38 KB of stack.) */
__attribute__((noinline)) static int sfxn__pack_small(const sfx_song *s, uint8_t *out, int max) {
  uint8_t b[SFX_SONG_MAX];
  int len = sfx_song_pack(s, b);
  if (len > max) {
    return -1;
  }
  for (int i = 0; i < len; i++) {
    out[i] = b[i];
  }
  return len;
}

/* The song packed into out (max bytes): straight into it where it holds the longest song there is
   (sfxn_song_bytes()), else by way of a copy - so the caller's stack carries one song (38 KB), and a second only for
   a short buffer. Returns the bytes written, -1 = it does not fit. */
static int sfxn__pack(const sfx_song *s, uint8_t *out, int max) {
  return max >= SFX_SONG_MAX ? sfx_song_pack(s, out) : sfxn__pack_small(s, out, max);
}

/* any format in, format 6 out: the bytes written; -1 = not a song, or max is too small (sfxn_song_bytes() always
   holds it) */
API int sfxn_song_convert(const uint8_t *in, int n, uint8_t *out, int max) {
  sfx_song s;
  if (sfx_song_unpack(&s, in, n)) {
    return -1;
  }
  return sfxn__pack(&s, out, max);
}

/* what putting one pattern (0..7) in grid 0..2 (16, 32, 64 steps) would drop: steps and points; 0 when nothing;
   -1 = not a song */
API int sfxn_song_regrid_loss(const uint8_t *in, int n, int pattern, int grid) {
  sfx_song s;
  if (pattern < 0 || pattern >= SFX_PATTERNS || grid < 0 || grid >= SFX_GRIDS || sfx_song_unpack(&s, in, n)) {
    return -1;
  }
  return sfx_pattern_regrid_loss(&s.pat[pattern], grid);
}

/* the song with one pattern (0..7) in grid 0..2, as format 6: the bytes written, -1 as sfxn_song_convert */
API int sfxn_song_regrid(const uint8_t *in, int n, int pattern, int grid, uint8_t *out, int max) {
  sfx_song s;
  if (pattern < 0 || pattern >= SFX_PATTERNS || grid < 0 || grid >= SFX_GRIDS || sfx_song_unpack(&s, in, n)) {
    return -1;
  }
  sfx_pattern_regrid(&s.pat[pattern], grid);
  return sfxn__pack(&s, out, max);
}

/* ==== 5. A sound as samples ======================================================================================= */

/* The whole sound with its effects into l and r (at most max samples each), for a sample asset. `player` is scratch:
   sfxn_player_size() bytes, initialised here. midi_note 0 = as designed. A sustained sound is held for hold_seconds,
   then let go. Returns the samples written, -1 = not a valid patch. Not for an audio thread. */
API int sfxn_bake(void *player, const uint8_t *patch, float sr, float vel, int midi_note, float note_st,
                  float hold_seconds, float *l, float *r, int max) {
  sfx_player *p = (sfx_player *)player;
  sfx_patch pt;
  if (sfx_patch_unpack(&pt, patch)) {
    return -1;
  }

  /* a fresh player with this one sound, and the note */
  sfx_player_init(p, sr);
  sfx_player_set_patch(p, 0, &pt);
  for (int i = 0; i < max; i++) {
    r[i] = 0.0f;
    l[i] = 0.0f;
  }
  sfx_player_note_on(p, 0, 0, vel, midi_note, note_st, 0);

  /* n: the samples written; hold: the sample at which the key is let go; wet: the sound sends to an effect;
     last: the last sample that was louder than `heard_above` */
  int n = 0;
  int hold = (int)(hold_seconds * sr);
  int wet = pt.p[SFX_P_CHORUS] || pt.p[SFX_P_DELAY] || pt.p[SFX_P_REVERB];
  int last = 0;
  float heard_above = 0.0005f;

  /* 64 samples at a time, until the buffer is full or the sound is over */
  for (;;) {
    int k = max - n < 64 ? max - n : 64;
    if (k <= 0) {
      break;
    }
    if (n >= hold) {
      sfx_player_note_off(p, 0);
    }
    uint32_t on = sfx_player_render(p, l + n, r + n, k, -1.0, 0.0);
    for (int i = n; i < n + k; i++) {
      if (l[i] > heard_above || l[i] < -heard_above || r[i] > heard_above || r[i] < -heard_above) {
        last = i;
      }
    }
    n += k;
    /* the voice is done and the effects have faded (a quarter of a second below the floor) */
    if (!(on & 0xFFFF) && (!wet || n - last > (int)(sr * 0.25f))) {
      break;
    }
  }

  /* cut 256 samples after the last that was heard, and the end faded to zero over 256 samples */
  if (n > last + 256) {
    n = last + 256;
  }
  int fade = n < 256 ? n : 256;
  for (int i = 0; i < fade; i++) {
    float g = (float)i / (float)fade;
    l[n - 1 - i] *= g;
    r[n - 1 - i] *= g;
  }
  return n;
}
