/* SPDX-License-Identifier: MIT
 * sfx_native.c – the SFX FORGE engine as a native library, for game engines (Unity: P/Invoke).
 * The caller owns all memory: a voice is sfxn_voice_size() zeroed bytes, a player sfxn_player_size() bytes. Nothing here
 * allocates. A patch is the 28 bytes of core/sfx_core.h. Rendering functions may be called from an audio thread.
 */
#include "../core/sfx_song.h"

#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

API int sfxn_abi(void) { return 3; }
API int sfxn_patch_bytes(void) { return SFX_PATCH_BYTES; }
API int sfxn_song_bytes(void) { return SFX_SONG_BYTES; }

/* ---- one voice, dry and mono: the smallest way to play a patch ------------------------------------------------------- */
API int sfxn_voice_size(void) { return (int)sizeof(sfx_voice); }

/* 0 = it plays, -1 = not a valid patch */
API int sfxn_trigger(void *voice, const uint8_t *patch, float vel, float note_st, float sr, uint32_t seed)
{
  sfx_patch p;
  if (sfx_patch_unpack(&p, patch)) return -1;
  sfx_wt_init();
  sfx_voice_trigger((sfx_voice *)voice, &p, vel, note_st, 0, sr, seed);
  return 0;
}

/* new values for a voice that sounds (a slider moved) */
API int sfxn_update(void *voice, const uint8_t *patch)
{
  sfx_patch p; sfx_voice *v = (sfx_voice *)voice;
  if (!v->active || sfx_patch_unpack(&p, patch)) return -1;
  sfx_voice_set_patch(v, &p);
  return 0;
}

API void sfxn_release(void *voice) { sfx_voice_release((sfx_voice *)voice); }
API int  sfxn_active(const void *voice) { return ((const sfx_voice *)voice)->active; }

/* ADDS n mono samples to out. mods4 = pitch (semitones), cutoff (octaves), timbre (0..1), level (0..1); NULL = none.
   Returns 1 while the voice sounds. */
API int sfxn_render(void *voice, const float *mods4, float *out, int n)
{
  sfx_mods m = { 0.0f, 0.0f, 0.0f, 1.0f };
  if (mods4) { m.pitch_st = mods4[0]; m.cutoff_oct = mods4[1]; m.fm = mods4[2]; m.level = mods4[3]; }
  return sfx_voice_render((sfx_voice *)voice, &m, out, n);
}

/* ---- the player: 16 sounds, 16 voices, the effects, a song and its sequencer. Stereo. --------------------------------- */
API int  sfxn_player_size(void) { return (int)sizeof(sfx_player); }
API void sfxn_player_init(void *player, float sr) { sfx_player_init((sfx_player *)player, sr); }

API int sfxn_player_set_patch(void *player, int slot, const uint8_t *patch)
{
  sfx_patch p;
  if (slot < 0 || slot >= SFX_TRACKS || sfx_patch_unpack(&p, patch)) return -1;
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

/* a sound by hand, beside the song. key: any number below 200 that names this note for sfxn_player_note_off.
   midi_note 0 = as designed (a sound effect), else at that pitch; note_st transposes either. */
API void sfxn_player_note_on(void *player, int slot, int key, float vel, int midi_note, float note_st)
{
  if (slot >= 0 && slot < SFX_TRACKS && key >= 0 && key < SFX_KEY_SEQ) sfx_player_note_on((sfx_player *)player, slot, key, vel, midi_note, note_st, 0);
}
API void sfxn_player_note_off(void *player, int key) { if (key >= 0 && key < SFX_KEY_SEQ) sfx_player_note_off((sfx_player *)player, key); }

/* ADDS n stereo samples to l and r, on its own clock. Returns the sounds that play, a bit each; bit 31 while only the
   effects still ring; 0 = silence. */
API unsigned sfxn_player_render(void *player, float *l, float *r, int n) { return sfx_player_render((sfx_player *)player, l, r, n, -1.0, 0.0); }

/* out4: running, step (-1 = none yet), pattern, bar */
API void sfxn_player_state(const void *player, int *out4)
{
  const sfx_player *p = (const sfx_player *)player;
  out4[0] = p->running; out4[1] = p->step; out4[2] = p->pattern; out4[3] = p->bar;
}

/* the whole sound with its effects into l and r (at most max samples each), for a sample asset. `player` is scratch:
   sfxn_player_size() bytes, initialised here. midi_note 0 = as designed. A sustained sound is held for hold_seconds,
   then let go. Returns the samples written, -1 = not a valid patch. */
API int sfxn_bake(void *player, const uint8_t *patch, float sr, float vel, int midi_note, float note_st, float hold_seconds, float *l, float *r, int max)
{
  sfx_player *p = (sfx_player *)player; sfx_patch pt;
  if (sfx_patch_unpack(&pt, patch)) return -1;
  sfx_player_init(p, sr);
  sfx_player_set_patch(p, 0, &pt);
  for (int i = 0; i < max; i++) l[i] = r[i] = 0.0f;
  sfx_player_note_on(p, 0, 0, vel, midi_note, note_st, 0);
  int n = 0, hold = (int)(hold_seconds * sr), wet = pt.p[SFX_P_CHORUS] || pt.p[SFX_P_DELAY] || pt.p[SFX_P_REVERB], last = 0;
  float floor = 0.0005f;
  for (;;) {
    int k = max - n < 64 ? max - n : 64;
    if (k <= 0) break;
    if (n >= hold) sfx_player_note_off(p, 0);
    uint32_t on = sfx_player_render(p, l + n, r + n, k, -1.0, 0.0);
    for (int i = n; i < n + k; i++) if (l[i] > floor || l[i] < -floor || r[i] > floor || r[i] < -floor) last = i;
    n += k;
    if (!(on & 0xFFFF) && (!wet || n - last > (int)(sr * 0.25f))) break;      /* the voice is done and the effects have faded */
  }
  if (n > last + 256) n = last + 256;
  for (int i = 0, f = n < 256 ? n : 256; i < f; i++) { float g = (float)i / (float)f; l[n - 1 - i] *= g; r[n - 1 - i] *= g; }   /* the end: to zero */
  return n;
}
