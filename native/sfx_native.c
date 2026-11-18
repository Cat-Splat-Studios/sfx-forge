/* SPDX-License-Identifier: MIT
 * sfx_native.c – the SFX FORGE engine as a native library, for game engines (Unity: P/Invoke).
 * The caller owns all memory: a voice is sfxn_voice_size() zeroed bytes. Nothing here allocates or keeps state.
 * A patch is the 13 bytes of core/sfx_core.h. Every function may be called from an audio thread.
 */
#include "../core/sfx_song.h"

#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

API int sfxn_abi(void) { return 2; }
API int sfxn_voice_size(void) { return (int)sizeof(sfx_voice); }

/* 0 = it plays, -1 = not a valid patch */
API int sfxn_trigger(void *voice, const uint8_t *patch13, float vel, float note_st, float sr, uint32_t seed)
{
  sfx_patch p;
  if (sfx_patch_unpack(&p, patch13)) return -1;
  sfx_voice_trigger((sfx_voice *)voice, &p, vel, note_st, 0, sr, seed);
  return 0;
}

/* new values for a voice that sounds (a slider moved) */
API int sfxn_update(void *voice, const uint8_t *patch13)
{
  sfx_patch p; sfx_voice *v = (sfx_voice *)voice;
  if (!v->active || sfx_patch_unpack(&p, patch13)) return -1;
  sfx_voice_set_patch(v, &p);
  return 0;
}

API void sfxn_release(void *voice) { sfx_voice_release((sfx_voice *)voice); }
API int  sfxn_active(const void *voice) { return ((const sfx_voice *)voice)->active; }

/* ADDS n mono samples to out. mods4 = pitch (semitones), cutoff (octaves), fm (0..1), level (0..1); NULL = none.
   Returns 1 while the voice sounds. */
API int sfxn_render(void *voice, const float *mods4, float *out, int n)
{
  sfx_mods m = { 0.0f, 0.0f, 0.0f, 1.0f };
  if (mods4) { m.pitch_st = mods4[0]; m.cutoff_oct = mods4[1]; m.fm = mods4[2]; m.level = mods4[3]; }
  return sfx_voice_render((sfx_voice *)voice, &m, out, n);
}

/* the whole sound into out (mono, at most max samples), for a sample asset. A HOLD patch is held for hold_seconds,
   then let go. Returns the samples written, -1 = not a valid patch. */
API int sfxn_bake(const uint8_t *patch13, float sr, float vel, float note_st, float hold_seconds, uint32_t seed, float *out, int max)
{
  sfx_patch p; sfx_voice v; sfx_mods m = { 0.0f, 0.0f, 0.0f, 1.0f };
  if (sfx_patch_unpack(&p, patch13)) return -1;
  for (int i = 0; i < max; i++) out[i] = 0.0f;
  sfx_voice_trigger(&v, &p, vel, note_st, 0, sr, seed);
  int n = 0, hold = (int)(hold_seconds * sr);
  while (n < max && v.active) {
    int k = max - n < 64 ? max - n : 64;
    if (n >= hold) sfx_voice_release(&v);
    sfx_voice_render(&v, &m, out + n, k);
    n += k;
  }
  if (n < max && n > 0) for (int i = 0, f = n < 64 ? n : 64; i < f; i++) out[n - 1 - i] *= (float)i / (float)f;   /* the last 64: to zero */
  return n;
}

/* ---- music: the whole player (16 sounds, a song, its sequencer) in sfxn_player_size() bytes of the caller's ----------- */
API int  sfxn_song_bytes(void) { return SFX_SONG_BYTES; }
API int  sfxn_player_size(void) { return (int)sizeof(sfx_player); }
API void sfxn_player_init(void *player, float sr) { sfx_player_init((sfx_player *)player, sr); }

API int sfxn_player_set_patch(void *player, int slot, const uint8_t *patch13)
{
  sfx_patch p;
  if (slot < 0 || slot >= SFX_TRACKS || sfx_patch_unpack(&p, patch13)) return -1;
  sfx_player_set_patch((sfx_player *)player, slot, &p);
  return 0;
}

/* the packed song (sfx_song_pack: what the device sends and stores). -1 = not a song, the player keeps what it had */
API int  sfxn_player_set_song(void *player, const uint8_t *song, int n) { return sfx_song_unpack(&((sfx_player *)player)->song, song, n); }
API void sfxn_player_start(void *player, int pattern) { sfx_player_start((sfx_player *)player, pattern); }   /* -1: the pattern it is on */
API void sfxn_player_stop(void *player) { sfx_player_stop((sfx_player *)player); }
API void sfxn_player_cue(void *player, int pattern) { sfx_player_cue((sfx_player *)player, pattern); }

/* what the game moves while it plays: mods4 as sfxn_render (NULL = leave), intensity 0..1 */
API void sfxn_player_set_live(void *player, const float *mods4, float intensity)
{
  sfx_player *p = (sfx_player *)player;
  if (mods4) { p->mods.pitch_st = mods4[0]; p->mods.cutoff_oct = mods4[1]; p->mods.fm = mods4[2]; p->mods.level = mods4[3]; }
  p->intensity = intensity < 0.0f ? 0.0f : intensity > 1.0f ? 1.0f : intensity;
}

/* a sound by hand, beside the song: midi_note 0 = as designed (a sound effect), else at that pitch */
API void sfxn_player_note_on(void *player, int slot, float vel, int midi_note)
{
  if (slot >= 0 && slot < SFX_TRACKS) sfx_player_note_on((sfx_player *)player, slot, midi_note > 0 ? 100 + (midi_note & 127) : slot, vel, midi_note, 0.0f, 0);
}
API void sfxn_player_note_off(void *player, int slot, int midi_note) { sfx_player_note_off((sfx_player *)player, midi_note > 0 ? 100 + (midi_note & 127) : slot); }

/* ADDS n mono samples to out, on its own clock. Returns the sounds that play, a bit each. */
API unsigned sfxn_player_render(void *player, float *out, int n) { return sfx_player_render((sfx_player *)player, out, n, -1.0, 0.0); }

/* out4: running, step (-1 = none yet), pattern, bar */
API void sfxn_player_state(const void *player, int *out4)
{
  const sfx_player *p = (const sfx_player *)player;
  out4[0] = p->running; out4[1] = p->step; out4[2] = p->pattern; out4[3] = p->bar;
}
