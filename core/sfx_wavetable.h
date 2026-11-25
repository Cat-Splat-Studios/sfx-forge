/* SPDX-License-Identifier: GPL-3.0-or-later
 * sfx_wavetable.h – the wavetables of SFX FORGE's WAVETABLE engine.
 *
 * Six tables of eight frames each, every frame one cycle of 256 samples. All of them are computed here from sums of
 * harmonics (nothing is sampled or taken from another instrument), once, by sfx_wt_init(). Each frame exists four
 * times with fewer and fewer harmonics, so a high note does not alias.
 *
 * WHAT THIS FILE HOLDS, in its order:
 *   the sizes and the tables' names         SFX_WT_*, SFX_WT_NAME
 *   the memory                              sfx_wt[table][frame][copy][sample]: about 197 KB of floats, static
 *   sfx_wt_frame                            the copy of a frame that fits a pitch (the engine asks for it)
 *   sfx__bump, sfx__wt_harmonic             the recipe: how loud harmonic n is in each table
 *   sfx_wt_init                             fills the memory from the recipe
 *
 * It is included by sfx_core.h, after the maths (it uses SFX_SIN and SFX_EXP2); do not include it by itself.
 * To change a table or add one, read sfx__wt_harmonic.
 */
#ifndef SFX_WAVETABLE_H
#define SFX_WAVETABLE_H

#define SFX_WT_TABLES    6
#define SFX_WT_FRAMES    8
#define SFX_WT_MIPS      4   /* the copies of a frame, with fewer and fewer harmonics */
#define SFX_WT_SIZE      256 /* samples in a cycle */
#define SFX_WT_HARMONICS 48

static const char *const SFX_WT_NAME[SFX_WT_TABLES] = {"SAW-SQ", "PULSE", "PEAK", "VOWEL", "ORGAN", "GLASS"};

/* harmonics kept in each copy */
static const int SFX_WT_LIMIT[SFX_WT_MIPS] = {SFX_WT_HARMONICS, 16, 6, 2};

/* The tables. Every cycle has one sample more than SFX_WT_SIZE: its first again, so that reading between the last
   sample and the next needs no wrap. */
static float sfx_wt[SFX_WT_TABLES][SFX_WT_FRAMES][SFX_WT_MIPS][SFX_WT_SIZE + 1];
static int sfx_wt_ready;

/* The copy of a frame for a note that moves `dt` of a cycle per sample: as many harmonics as fit under half the
   rate. `table` and `frame` are taken modulo their counts. Returns SFX_WT_SIZE + 1 samples. */
static inline const float *sfx_wt_frame(int table, int frame, float dt) {
  int mip = dt <= 0.45f / 48.0f ? 0 : dt <= 0.45f / 16.0f ? 1 : dt <= 0.45f / 6.0f ? 2 : 3;
  return sfx_wt[table % SFX_WT_TABLES][frame % SFX_WT_FRAMES][mip];
}

/* a bell-shaped hill over the harmonics: 1 at `centre`, falling to a half `width` harmonics away */
static inline float sfx__bump(float n, float centre, float width) {
  float d = (n - centre) / width;
  return SFX_EXP2(-d * d);
}

/* The recipe of the tables: the loudness of harmonic `n` (1..48) in a frame (t = 0..1 through the table), and its
   phase in turns (*phase). */
static inline float sfx__wt_harmonic(int table, float t, int n, float *phase) {
  float fn = (float)n;
  *phase = 0.0f;
  switch (table) {
  case 0:
    /* SAW-SQ: a saw that loses its even harmonics: a square */
    return (1.0f / fn) * ((n & 1) ? 1.0f : 1.0f - t);

  case 1:
    /* PULSE: a pulse, from half to narrow */
    return SFX_SIN(fn * (0.5f - 0.44f * t) * 0.5f) / fn;

  case 2:
    /* PEAK: one peak that climbs the harmonics */
    return sfx__bump(fn, 1.0f + t * 15.0f, 1.6f) + (n == 1 ? 0.5f : 0.0f);

  case 3: {
    /* VOWEL: two formants, through A E I O U */
    static const float F1[5] = {7.3f, 3.6f, 2.5f, 4.1f, 3.2f};
    static const float F2[5] = {10.5f, 17.3f, 20.9f, 7.3f, 5.5f};
    /* between vowel k and the next */
    float p = t * 3.999f;
    int k = (int)p;
    float f = p - (float)k;
    float c1 = F1[k] + (F1[k + 1] - F1[k]) * f;
    float c2 = F2[k] + (F2[k + 1] - F2[k]) * f;
    return (sfx__bump(fn, c1, 1.4f) + 0.6f * sfx__bump(fn, c2, 1.9f) + 0.06f) / (1.0f + 0.08f * fn) +
           (n == 1 ? 0.4f : 0.0f);
  }

  case 4: {
    /* ORGAN: drawbars, pulled out one by one */
    static const int BAR[9] = {1, 2, 3, 4, 6, 8, 10, 12, 16};
    float bars = 2.0f + t * 7.0f;
    for (int i = 0; i < 9; i++) {
      if (BAR[i] == n) {
        /* how far bar i is out: 0..1 */
        float in = bars - (float)i;
        return (in >= 1.0f ? 1.0f : in > 0.0f ? in : 0.0f) / (1.0f + 0.3f * (float)i);
      }
    }
    return 0.0f;
  }

  default: {
    /* GLASS: far-apart partials: glass, bells */
    static const int PART[8] = {1, 3, 7, 11, 16, 22, 29, 37};
    for (int i = 0; i < 8; i++) {
      if (PART[i] == n) {
        *phase = (float)((i * 37) % 16) / 16.0f;
        return SFX_EXP2(-(float)i * (1.3f - t));
      }
    }
    return 0.0f;
  }
  }
}

/* Fills the tables. Call it once, before the first wavetable sound (about a million sines: a few milliseconds – so
   not from an audio callback); calling it again does nothing. sfx_player_init calls it. */
static inline void sfx_wt_init(void) {
  if (sfx_wt_ready) {
    return;
  }
  for (int tb = 0; tb < SFX_WT_TABLES; tb++) {
    for (int fr = 0; fr < SFX_WT_FRAMES; fr++) {
      /* the recipe of this frame: every harmonic's loudness and phase */
      float amp[SFX_WT_HARMONICS + 1];
      float ph[SFX_WT_HARMONICS + 1];
      float t = (float)fr / (float)(SFX_WT_FRAMES - 1);
      for (int n = 1; n <= SFX_WT_HARMONICS; n++) {
        amp[n] = sfx__wt_harmonic(tb, t, n, &ph[n]);
      }

      /* its four copies: the sum of the harmonics each keeps */
      float full = 0.0f;
      for (int m = 0; m < SFX_WT_MIPS; m++) {
        float *w = sfx_wt[tb][fr][m];
        float peak = 0.0f;
        for (int s = 0; s < SFX_WT_SIZE; s++) {
          float x = 0.0f;
          float at = (float)s / (float)SFX_WT_SIZE;
          for (int n = 1; n <= SFX_WT_LIMIT[m]; n++) {
            if (amp[n] != 0.0f) {
              x += amp[n] * SFX_SIN((float)n * at + ph[n]);
            }
          }
          w[s] = x;
          if (x > peak) {
            peak = x;
          } else if (-x > peak) {
            peak = -x;
          }
        }
        /* the full copy's peak becomes 0.9; every copy as loud as the full one */
        if (m == 0) {
          full = peak > 0.0001f ? 0.9f / peak : 1.0f;
        }
        for (int s = 0; s < SFX_WT_SIZE; s++) {
          w[s] *= full;
        }
        w[SFX_WT_SIZE] = w[0];
      }
    }
  }
  sfx_wt_ready = 1;
}

#endif
