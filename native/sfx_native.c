/* SPDX-License-Identifier: MIT
 * sfx_native.c – the SFX FORGE engine as a native library, for game engines (Unity: P/Invoke).
 * The caller owns all memory: a voice is sfxn_voice_size() zeroed bytes. Nothing here allocates or keeps state.
 * A patch is the 13 bytes of core/sfx_core.h. Every function may be called from an audio thread.
 */
#include "../core/sfx_core.h"

#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

API int sfxn_abi(void) { return 1; }
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
