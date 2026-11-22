/* SPDX-License-Identifier: MIT
 * sfx_song.h – music for SFX FORGE: a song of 8 patterns over the 16 sounds, and the player that makes it sound.
 * A pattern is a bar of up to 16 sixteenths; its grid cuts each in 1, 2 or 4 steps (16, 32, 64 steps a pattern).
 *
 * The same player runs in the VIBE FM firmware and in the native library a game uses, so a song sounds in the game as
 * it did on the device. What makes it game music:
 *   - layers:   every sound has a threshold; it only plays while the game's INTENSITY is at or above it
 *   - patterns: the game cues a pattern, the player changes to it on the next bar line (or walks a chain by itself)
 * The player also holds the effects every sound can send to: a chorus, a delay and a reverb – and the mixer: every
 * sound's volume, pan and mute, the delay's time and feedback, the reverb's size and damping, a drive on the sum – and
 * automation lanes that move any of these, and any knob of a sound, while a pattern plays. They
 * belong to the song, so a mix made on the device is the mix in the game. Its output is stereo.
 * Portable C, no heap, no libc – as sfx_core.h.
 */
#ifndef SFX_SONG_H
#define SFX_SONG_H
#include "sfx_core.h"

#define SFX_TRACKS   16            /* one per sound */
#define SFX_BAR      16            /* sixteenths in a bar at most: sfx_song.len is 1..SFX_BAR */
#define SFX_STEPS    64            /* cells in a pattern: SFX_BAR sixteenths, each cut in up to 4 */
#define SFX_GRIDS    3             /* a pattern's grid: 0, 1, 2 = sixteenths, thirty-seconds, sixty-fourths */
#define SFX_PATTERNS 8
#define SFX_SONG_VERSION 6
#define SFX_LANES 24               /* automation lanes in a song: each moves one knob of one sound (or the master) */
#define SFX_ARR_MAX 32             /* bars in an arrangement */
#define SFX_M_COUNT    8           /* the master's values (three spare) */
/* the formats before this one: a row of 16 steps for every sound of every pattern, whatever was in it */
#define SFX_SONG_BODY_V4 (SFX_PATTERNS * SFX_TRACKS * (3 + 3 * SFX_BAR))
#define SFX_SONG_BODY_V3 (SFX_PATTERNS * SFX_TRACKS * (3 + 2 * SFX_BAR))
#define SFX_SONG_BODY_V5 (SFX_SONG_BODY_V3 + SFX_PATTERNS * SFX_LANES * SFX_BAR)
#define SFX_SONG_BODY_V2 (SFX_PATTERNS * SFX_TRACKS * (3 + SFX_BAR))
#define SFX_SONG_HEAD_V2 (7 + SFX_TRACKS * 3 + 3 + SFX_M_COUNT)
#define SFX_SONG_HEAD_V4 (SFX_SONG_HEAD_V2 + SFX_TRACKS + 1 + SFX_ARR_MAX)
/* format 6: the head of format 5, then a block a pattern that holds only the rows that are used - so a packed song has
   no fixed size: SFX_SONG_MIN .. SFX_SONG_MAX bytes, each 0..127 (sfx_song_pack says how many) */
#define SFX_SONG_HEAD     147
#define SFX_SONG_MIN      211       /* 147 + 8 * 8 */
#define SFX_SONG_MAX      38355     /* 147 + 8 * (8 + 16 * 202 + 24 * 64) */
#define SFX_SONG_BYTES_V5 7699      /* version 5 (16 steps a pattern, every row written) - still taken */
#define SFX_SONG_BYTES_V4 (SFX_SONG_HEAD_V4 + SFX_SONG_BODY_V4)  /* version 4 (one lane a sound): 6643 bytes – still taken */
#define SFX_SONG_BYTES_V3 (SFX_SONG_HEAD_V2 + SFX_SONG_BODY_V3)  /* version 3 (no automation, no arrangement): 4546 bytes – still taken */
#define SFX_SONG_BYTES_V2 (SFX_SONG_HEAD_V2 + SFX_SONG_BODY_V2)  /* version 2 (no velocity, no length either): 2498 bytes – still taken */
#define SFX_SONG_BYTES_V1 (7 + SFX_TRACKS + SFX_SONG_BODY_V2) /* version 1 (no mixer either): 2455 bytes – still taken */
_Static_assert(SFX_SONG_BYTES_V1 == 2455 && SFX_SONG_BYTES_V2 == 2498 && SFX_SONG_BYTES_V3 == 4546 && SFX_SONG_BYTES_V4 == 6643 && SFX_SONG_BYTES_V5 == 7699, "old sizes");
_Static_assert(SFX_SONG_HEAD == SFX_SONG_HEAD_V2 + 1 + SFX_ARR_MAX + SFX_LANES * 2 && SFX_SONG_BYTES_V5 == SFX_SONG_HEAD + SFX_SONG_BODY_V5, "format 5");
_Static_assert(SFX_SONG_HEAD == 147 && SFX_SONG_MAX == 147 + 8 * (8 + 16 * 202 + 24 * 64), "format 6");
static inline int sfx_div(int grid) { return grid == 2 ? 4 : grid == 1 ? 2 : 1; }   /* steps a sixteenth; any other byte reads as 1 */
/* a step's accent byte in the formats 3 to 5: its velocity (8 levels) and its length */
#define SFX_VEL_DEFAULT 5
#define SFX_ACC(vel, len) ((uint8_t)(((len) & 15) << 3 | ((vel) & 7)))
#define SFX_ACC_VEL(a) ((a) & 7)                   /* 0..7: soft .. full (5 = as it always was) */
#define SFX_ACC_LEN(a) (((a) >> 3) & 15)           /* 0 = by itself (to the sound's end, its next note or the bar line), else steps */
static const float SFX_VEL[8] = { 0.15f, 0.29f, 0.43f, 0.57f, 0.71f, 0.85f, 0.93f, 1.0f };
/* what a sound's automation lane moves: any knob there is. 0 none · 1..25 the sound's own values (SFX_P_* + 1) ·
   then its fader and pan · then the master's five. */
enum { SFX_AT_NONE, SFX_AT_PATCH = 1, SFX_AT_VOL = SFX_AT_PATCH + SFX_P_COUNT, SFX_AT_PAN, SFX_AT_MASTER, SFX_AT_COUNT = SFX_AT_MASTER + 5 };
enum { SFX_M_DELAY_TIME,           /* 0..5: the echo after 1, 2, 3, 4, 6, 8 sixteenths */
       SFX_M_DELAY_FB,             /* 0..127: how much of it comes again (64 = as it was: 0.45) */
       SFX_M_REVERB_SIZE,          /* 0..127: how long the room rings (91 = as it was) */
       SFX_M_REVERB_DAMP,          /* 0..127: how fast its highs fade (40 = as it was) */
       SFX_M_DRIVE };              /* 0..127: saturation on the sum (0 = none) */
#define SFX_DELAY_TIMES 6
enum { SFX_SCALE_CHROMATIC, SFX_SCALE_MAJOR, SFX_SCALE_MINOR, SFX_SCALE_PENTATONIC, SFX_SCALE_COUNT };

/* A pattern is a bar. Its grid cuts every sixteenth of the bar in 1, 2 or 4 steps: 16, 32 or 64 steps a full bar.
   The cells from 16 * sfx_div(grid) on are always empty (sfx_song_init, sfx_song_unpack and sfx_pattern_regrid keep it so). */
typedef struct {
  uint8_t  grid;                          /* 0, 1, 2 = 16, 32, 64 steps a full bar */
  uint16_t on[SFX_STEPS];                 /* per step: bit = sound */
  uint8_t  note[SFX_TRACKS][SFX_STEPS];   /* 0 = the sound as designed (drums, effects), else a MIDI note */
  uint8_t  vel[SFX_TRACKS][SFX_STEPS];    /* 0..7: soft .. full (SFX_VEL_DEFAULT 5 = as it always was) */
  uint8_t  len[SFX_TRACKS][SFX_STEPS];    /* 0 = by itself (to the sound's end, its next note or the bar line), else steps of THIS pattern's grid: 1 .. 16 * div - 1 */
  uint8_t  aut[SFX_LANES][SFX_STEPS];     /* the automation lanes: 0 = no point, else value + 1 (1..127). A point holds to the next, or the bar's end */
} sfx_pattern;

typedef struct {
  sfx_pattern pat[SFX_PATTERNS];
  uint16_t bpm;                           /* 40..240 */
  uint8_t  len;                           /* the bar in sixteenths, 1..16 (a pattern plays len * sfx_div(its grid) steps) */
  uint8_t  swing;                         /* 0..100 */
  uint8_t  scale;                         /* for entering notes (key of C) */
  uint8_t  chain;                         /* 1 = stay on a pattern, n = walk patterns 1..n, one bar each */
  uint8_t  level[SFX_TRACKS];             /* 0..127: the sound plays while intensity >= this (0 = always) */
  uint8_t  vol[SFX_TRACKS];               /* 0..127: the sound's fader (100 = as designed) */
  uint8_t  pan[SFX_TRACKS];               /* 0..127: left .. right (64 = the middle) */
  uint16_t mute;                          /* a bit a sound: silent */
  uint16_t solo;                          /* a bit a sound: only these (not kept with the song) */
  uint8_t  fx[SFX_M_COUNT];               /* the master: SFX_M_* */
  struct { uint8_t track, to; } lane[SFX_LANES];   /* a lane: whose knob (0 = the lane is free, else sound + 1) and which (SFX_AT_*) */
  uint8_t  arr_len, arr[SFX_ARR_MAX];     /* the arrangement: a pattern a bar, then again from the top. 0 = none: the chain */
} sfx_song;

static inline void sfx_song_mix_init(sfx_song *s)
{
  static const uint8_t FX[SFX_M_COUNT] = { 2, 64, 91, 40, 0, 0, 0, 0 };
  for (int t = 0; t < SFX_TRACKS; t++) { s->vol[t] = 100; s->pan[t] = 64; }
  for (int i = 0; i < SFX_M_COUNT; i++) s->fx[i] = FX[i];
  s->mute = s->solo = 0;
}

static inline void sfx__cell_clear(sfx_pattern *p, int i)
{
  p->on[i] = 0;
  for (int t = 0; t < SFX_TRACKS; t++) { p->note[t][i] = 0; p->vel[t][i] = SFX_VEL_DEFAULT; p->len[t][i] = 0; }
  for (int l = 0; l < SFX_LANES; l++) p->aut[l][i] = 0;
}

/* all 64 cells empty; the grid stays */
static inline void sfx_pattern_clear(sfx_pattern *p) { for (int i = 0; i < SFX_STEPS; i++) sfx__cell_clear(p, i); }

/* what lowering the pattern to `grid` would drop: the steps (a sound on a step counts one) and the points that stand
   between the steps of the coarser grid. 0 when that is not lowering. */
static inline int sfx_pattern_regrid_loss(const sfx_pattern *p, int grid)
{
  int od = sfx_div(p->grid), nd = sfx_div(grid), n = 0;
  if (grid < 0 || grid >= SFX_GRIDS || nd >= od) return 0;
  int k = od / nd;
  for (int i = 0; i < SFX_BAR * od; i++) {
    if (i % k == 0) continue;
    for (unsigned m = p->on[i]; m; m &= m - 1) n++;
    for (int l = 0; l < SFX_LANES; l++) if (p->aut[l][i]) n++;
  }
  return n;
}

/* the pattern in another grid, in place. Finer: every step moves to the step that begins at the same moment, a length
   is multiplied - it sounds the same. Coarser: what stands between the steps of the new grid is dropped, a length is
   rounded up. A grid above 2 is ignored. */
static inline void sfx_pattern_regrid(sfx_pattern *p, int grid)
{
  int og = p->grid < SFX_GRIDS ? p->grid : 0;
  if (grid < 0 || grid >= SFX_GRIDS) return;
  int od = sfx_div(og), nd = sfx_div(grid);
  if (nd > od) {
    int k = nd / od;
    for (int i = SFX_BAR * od - 1; i >= 0; i--) {
      int to = i * k;
      if (i) p->on[to] = p->on[i];
      for (int t = 0; t < SFX_TRACKS; t++) { p->note[t][to] = p->note[t][i]; p->vel[t][to] = p->vel[t][i]; p->len[t][to] = (uint8_t)(p->len[t][i] * k); }
      for (int l = 0; l < SFX_LANES; l++) p->aut[l][to] = p->aut[l][i];
      if (i) sfx__cell_clear(p, i);
    }
  }
  else if (nd < od) {
    int k = od / nd, top = SFX_BAR * nd - 1;
    for (int i = 0; i < SFX_BAR * nd; i++) {
      int from = i * k;
      p->on[i] = p->on[from];
      for (int t = 0; t < SFX_TRACKS; t++) {
        int len = (p->len[t][from] + k - 1) / k;
        p->note[t][i] = p->note[t][from]; p->vel[t][i] = p->vel[t][from]; p->len[t][i] = (uint8_t)(len > top ? top : len);
      }
      for (int l = 0; l < SFX_LANES; l++) p->aut[l][i] = p->aut[l][from];
    }
    for (int i = SFX_BAR * nd; i < SFX_STEPS; i++) sfx__cell_clear(p, i);
  }
  p->grid = (uint8_t)grid;
}

static inline void sfx_song_init(sfx_song *s)
{
  for (int p = 0; p < SFX_PATTERNS; p++) { sfx_pattern_clear(&s->pat[p]); s->pat[p].grid = 0; }
  for (int t = 0; t < SFX_TRACKS; t++) s->level[t] = 0;
  for (int l = 0; l < SFX_LANES; l++) s->lane[l].track = s->lane[l].to = 0;
  for (int i = 0; i < SFX_ARR_MAX; i++) s->arr[i] = 0;
  s->arr_len = 0;
  sfx_song_mix_init(s);
  s->bpm = 120; s->len = SFX_BAR; s->swing = 0; s->scale = SFX_SCALE_MINOR; s->chain = 1;
}

/* the next note up (dir > 0) or down in the scale; below the lowest: 0 = "as designed", and up from there: middle C */
static inline int sfx_scale_step(int note, int dir, int scale)
{
  static const uint16_t MASK[SFX_SCALE_COUNT] = { 0xFFF, 0xAB5, 0x5AD, 0x4A9 };   /* bit = semitone above C */
  if (note == 0) return dir > 0 ? 60 : 0;
  for (int i = 0; i < 12; i++) {
    note += dir > 0 ? 1 : -1;
    if (note < 24) return 0;
    if (note > 108) return 108;
    if (MASK[scale % SFX_SCALE_COUNT] & (1u << (note % 12))) break;
  }
  return note;
}

/* ---- a song as bytes 0..127: for storage, for SysEx, for a game's asset ---------------------------------------------
   Format 6:
     0          6
     1 .. 146   the bar, the tempo, swing, scale, chain, the layers, the mixer, the master, the arrangement, the lanes
     147 ..     8 pattern blocks, A to H. A block:
        +0        grid 0, 1, 2          (G = 16, 32, 64 steps; B = 3, 5, 10 bytes of step bits)
        +1 .. +3  the sounds with a row: bit t, as 7 + 7 + 2 bits
        +4 .. +7  the lanes with a row: bit l, as 7 + 7 + 7 + 3 bits
        for each sound with a row, ascending:  B bytes: the steps that are on, 7 bits a byte, step 0 = bit 0 of the first
                                               G bytes notes · G bytes velocities 0..7 · G bytes lengths 0 .. G-1
        for each lane with a row, ascending:   G bytes points: 0 none, else value + 1
   A sound has a row when anything is kept for it in that pattern, a lane when it has a point there. */

/* out holds SFX_SONG_MAX bytes. Returns the bytes written. */
static inline int sfx_song_pack(const sfx_song *s, uint8_t *out)
{
  uint8_t *o = out;
  *o++ = SFX_SONG_VERSION; *o++ = s->len; *o++ = (uint8_t)(s->bpm >> 7); *o++ = (uint8_t)(s->bpm & 0x7F);
  *o++ = s->swing; *o++ = s->scale; *o++ = s->chain;
  for (int t = 0; t < SFX_TRACKS; t++) *o++ = s->level[t] & 0x7F;
  for (int t = 0; t < SFX_TRACKS; t++) *o++ = s->vol[t] & 0x7F;
  for (int t = 0; t < SFX_TRACKS; t++) *o++ = s->pan[t] & 0x7F;
  *o++ = (uint8_t)(s->mute & 0x7F); *o++ = (uint8_t)((s->mute >> 7) & 0x7F); *o++ = (uint8_t)(s->mute >> 14);
  for (int i = 0; i < SFX_M_COUNT; i++) *o++ = s->fx[i] & 0x7F;
  *o++ = s->arr_len <= SFX_ARR_MAX ? s->arr_len : 0;
  for (int i = 0; i < SFX_ARR_MAX; i++) *o++ = s->arr[i] % SFX_PATTERNS;
  for (int l = 0; l < SFX_LANES; l++) {
    int ok = s->lane[l].track <= SFX_TRACKS && s->lane[l].to > 0 && s->lane[l].to < SFX_AT_COUNT;
    *o++ = ok ? s->lane[l].track : 0; *o++ = ok ? s->lane[l].to : 0;
  }
  for (int p = 0; p < SFX_PATTERNS; p++) {
    const sfx_pattern *pt = &s->pat[p];
    int grid = pt->grid < SFX_GRIDS ? pt->grid : 0, g = SFX_BAR * sfx_div(grid), nb = (g + 6) / 7;
    uint32_t rows = 0, lanes = 0;
    for (int i = 0; i < g; i++) {
      rows |= pt->on[i];
      for (int t = 0; t < SFX_TRACKS; t++) if ((pt->note[t][i] & 0x7F) || (pt->vel[t][i] & 7) != SFX_VEL_DEFAULT || pt->len[t][i]) rows |= 1u << t;
      for (int l = 0; l < SFX_LANES; l++) if (pt->aut[l][i] & 0x7F) lanes |= 1u << l;
    }
    *o++ = (uint8_t)grid;
    *o++ = (uint8_t)(rows & 0x7F); *o++ = (uint8_t)((rows >> 7) & 0x7F); *o++ = (uint8_t)((rows >> 14) & 3);
    *o++ = (uint8_t)(lanes & 0x7F); *o++ = (uint8_t)((lanes >> 7) & 0x7F); *o++ = (uint8_t)((lanes >> 14) & 0x7F); *o++ = (uint8_t)((lanes >> 21) & 7);
    for (int t = 0; t < SFX_TRACKS; t++) {
      if (!(rows & (1u << t))) continue;
      for (int b = 0; b < nb; b++) { unsigned bits = 0; for (int i = b * 7; i < b * 7 + 7 && i < g; i++) if (pt->on[i] & (1u << t)) bits |= 1u << (i - b * 7); *o++ = (uint8_t)bits; }
      for (int i = 0; i < g; i++) *o++ = pt->note[t][i] & 0x7F;
      for (int i = 0; i < g; i++) *o++ = pt->vel[t][i] & 7;
      for (int i = 0; i < g; i++) *o++ = (uint8_t)(pt->len[t][i] > g - 1 ? g - 1 : pt->len[t][i]);
    }
    for (int l = 0; l < SFX_LANES; l++) if (lanes & (1u << l)) for (int i = 0; i < g; i++) *o++ = pt->aut[l][i] & 0x7F;
  }
  return (int)(o - out);
}

/* the head's values as formats 5 and 6 have them (and the earlier ones their part of it): 0 = in range */
static inline int sfx__head_ok(const uint8_t *in, int ver)
{
  int bpm = in[2] << 7 | in[3];
  if (in[1] < 1 || in[1] > SFX_BAR || bpm < 40 || bpm > 240 || in[4] > 100 || in[5] >= SFX_SCALE_COUNT || in[6] < 1 || in[6] > SFX_PATTERNS) return -1;
  if (ver >= 2 && in[7 + SFX_TRACKS * 3 + 3 + SFX_M_DELAY_TIME] >= SFX_DELAY_TIMES) return -1;
  if (ver >= 4) {
    const uint8_t *a = in + SFX_SONG_HEAD_V2 + (ver == 4 ? SFX_TRACKS : 0);       /* the arrangement */
    if (a[0] > SFX_ARR_MAX) return -1;
    for (int i = 0; i < SFX_ARR_MAX; i++) if (a[1 + i] >= SFX_PATTERNS) return -1;
    if (ver == 4) { for (int t = 0; t < SFX_TRACKS; t++) if (in[SFX_SONG_HEAD_V2 + t] >= SFX_AT_COUNT) return -1; }
    else for (int l = 0; l < SFX_LANES; l++) if (a[1 + SFX_ARR_MAX + l * 2] > SFX_TRACKS || a[2 + SFX_ARR_MAX + l * 2] >= SFX_AT_COUNT) return -1;
  }
  return 0;
}

/* 0 = taken, -1 = not a song (length, version, a value out of range) – *s is untouched then. Takes the formats before
   this one too: 1 (no mixer), 2 (no velocity, no length), 3 (no automation, no arrangement), 4 (one lane a sound),
   5 (16 steps a pattern, every row written): their patterns come in at grid 16. */
static inline int sfx_song_unpack(sfx_song *s, const uint8_t *in, int n)
{
  if (n < 7) return -1;
  int ver = in[0] == 6 && n >= SFX_SONG_MIN && n <= SFX_SONG_MAX ? 6
          : n == SFX_SONG_BYTES_V5 && in[0] == 5 ? 5 : n == SFX_SONG_BYTES_V4 && in[0] == 4 ? 4 : n == SFX_SONG_BYTES_V3 && in[0] == 3 ? 3
          : n == SFX_SONG_BYTES_V2 && in[0] == 2 ? 2 : n == SFX_SONG_BYTES_V1 && in[0] == 1 ? 1 : 0;
  if (!ver) return -1;
  for (int i = 0; i < n; i++) if (in[i] > 127) return -1;
  if (sfx__head_ok(in, ver)) return -1;
  if (ver == 6) {                                     /* the blocks, before anything is written: every one inside the bytes, every value in range */
    int o = SFX_SONG_HEAD;
    for (int p = 0; p < SFX_PATTERNS; p++) {
      if (o + 8 > n || in[o] >= SFX_GRIDS || in[o + 3] > 3 || in[o + 7] > 7) return -1;
      int g = SFX_BAR * sfx_div(in[o]), nb = (g + 6) / 7, rows = 0, lanes = 0;
      uint32_t rm = (uint32_t)in[o + 1] | (uint32_t)in[o + 2] << 7 | (uint32_t)in[o + 3] << 14;
      uint32_t lm = (uint32_t)in[o + 4] | (uint32_t)in[o + 5] << 7 | (uint32_t)in[o + 6] << 14 | (uint32_t)in[o + 7] << 21;
      for (uint32_t m = rm; m; m &= m - 1) rows++;
      for (uint32_t m = lm; m; m &= m - 1) lanes++;
      int size = 8 + rows * (nb + 3 * g) + lanes * g;
      if (o + size > n) return -1;
      const uint8_t *r = in + o + 8;
      for (int k = 0; k < rows; k++, r += nb + 3 * g) {
        if (r[nb - 1] >> (g - (nb - 1) * 7)) return -1;                            /* a step bit from G on */
        for (int i = 0; i < g; i++) if (r[nb + g + i] > 7 || r[nb + 2 * g + i] > g - 1) return -1;
      }
      o += size;
    }
    if (o != n) return -1;
  }
  const uint8_t *o = in + 7;
  uint8_t was[SFX_TRACKS];                            /* format 4: what each sound's one lane moved */
  s->len = in[1]; s->bpm = (uint16_t)(in[2] << 7 | in[3]); s->swing = in[4]; s->scale = in[5]; s->chain = in[6];
  for (int t = 0; t < SFX_TRACKS; t++) { s->level[t] = *o++; was[t] = 0; }
  for (int l = 0; l < SFX_LANES; l++) s->lane[l].track = s->lane[l].to = 0;
  for (int p = 0; p < SFX_PATTERNS; p++) { sfx_pattern_clear(&s->pat[p]); s->pat[p].grid = 0; }
  s->arr_len = 0;
  sfx_song_mix_init(s);
  if (ver >= 2) {
    for (int t = 0; t < SFX_TRACKS; t++) s->vol[t] = *o++;
    for (int t = 0; t < SFX_TRACKS; t++) s->pan[t] = *o++;
    s->mute = (uint16_t)((unsigned)o[0] | (unsigned)o[1] << 7 | (unsigned)(o[2] & 3) << 14); o += 3;
    for (int i = 0; i < SFX_M_COUNT; i++) s->fx[i] = *o++;
  }
  if (ver == 4) for (int t = 0; t < SFX_TRACKS; t++) was[t] = *o++;
  if (ver >= 4) {
    s->arr_len = *o++;
    for (int i = 0; i < SFX_ARR_MAX; i++) s->arr[i] = *o++;
  }
  if (ver >= 5) for (int l = 0; l < SFX_LANES; l++) { s->lane[l].track = *o++; s->lane[l].to = *o++; if (!s->lane[l].to) s->lane[l].track = 0; }
  if (ver == 6) {
    for (int p = 0; p < SFX_PATTERNS; p++) {
      sfx_pattern *pt = &s->pat[p];
      int g = SFX_BAR * sfx_div(o[0]), nb = (g + 6) / 7;
      uint32_t rm = (uint32_t)o[1] | (uint32_t)o[2] << 7 | (uint32_t)o[3] << 14;
      uint32_t lm = (uint32_t)o[4] | (uint32_t)o[5] << 7 | (uint32_t)o[6] << 14 | (uint32_t)o[7] << 21;
      pt->grid = o[0]; o += 8;
      for (int t = 0; t < SFX_TRACKS; t++) {
        if (!(rm & (1u << t))) continue;
        for (int i = 0; i < g; i++) {
          if (o[i / 7] & (1u << (i % 7))) pt->on[i] |= (uint16_t)(1u << t);
          pt->note[t][i] = o[nb + i]; pt->vel[t][i] = o[nb + g + i]; pt->len[t][i] = o[nb + 2 * g + i];
        }
        o += nb + 3 * g;
      }
      for (int l = 0; l < SFX_LANES; l++) if (lm & (1u << l)) { for (int i = 0; i < g; i++) pt->aut[l][i] = o[i]; o += g; }
    }
    return 0;
  }
  for (int p = 0; p < SFX_PATTERNS; p++) for (int t = 0; t < SFX_TRACKS; t++) {
    unsigned bits = (unsigned)o[0] | (unsigned)o[1] << 7 | (unsigned)(o[2] & 3) << 14; o += 3;
    for (int i = 0; i < SFX_BAR; i++) { if (bits & (1u << i)) s->pat[p].on[i] |= (uint16_t)(1u << t); s->pat[p].note[t][i] = *o++; }
    if (ver >= 3) for (int i = 0; i < SFX_BAR; i++) { uint8_t a = *o++; s->pat[p].vel[t][i] = SFX_ACC_VEL(a); s->pat[p].len[t][i] = SFX_ACC_LEN(a); }
    if (ver == 4) for (int i = 0; i < SFX_BAR; i++) s->pat[p].aut[t][i] = *o++;        /* (sound t's lane becomes lane t) */
  }
  if (ver == 4) for (int t = 0; t < SFX_TRACKS; t++) if (was[t]) { s->lane[t].track = (uint8_t)(t + 1); s->lane[t].to = was[t]; }
  if (ver >= 5) for (int p = 0; p < SFX_PATTERNS; p++) for (int l = 0; l < SFX_LANES; l++) for (int i = 0; i < SFX_BAR; i++) s->pat[p].aut[l][i] = *o++;
  return 0;
}

/* ---- automation: one question, asked wherever a knob's value is used ------------------------------------------------
   A lane moves one knob: `to` (SFX_AT_*) of sound `track - 1`. A song has SFX_LANES of them, so a sound may have
   several – one for each knob that moves. now[l] is the point lane l stands on (0 = none: sfx_pattern.aut, held from
   its step on). The value of knob `target` – for sound `track`, or the master's (track < 0: any lane may move those) –
   or `own`, the knob's own value, where no lane moves it. Nothing is written anywhere: the song and the sounds stay as
   they were made. */
static inline int sfx_auto(const sfx_song *s, const uint8_t *now, int target, int track, int own)
{
  for (int l = 0; l < SFX_LANES; l++)
    if (now[l] && s->lane[l].to == target && s->lane[l].track && (track < 0 || s->lane[l].track == track + 1)) return now[l] - 1;
  return own;
}

/* the sound of a track as its lanes have it now: `out` if they move any of its values, else the sound itself */
static inline const sfx_patch *sfx_auto_patch(const sfx_song *s, const uint8_t *now, int track, const sfx_patch *in, sfx_patch *out)
{
  const sfx_patch *r = in;
  for (int l = 0; l < SFX_LANES; l++) {
    int to = s->lane[l].to;
    if (!now[l] || s->lane[l].track != track + 1 || to < SFX_AT_PATCH || to >= SFX_AT_VOL) continue;
    if (r == in) { *out = *in; r = out; }
    out->p[to - SFX_AT_PATCH] = (uint8_t)(now[l] - 1);
  }
  return r;
}

/* ---- the effects: one chorus, one delay, one reverb for the whole player. Every sound has a send to each. ----------- */
#define SFX_BLOCK      256         /* the player works in pieces of at most this many samples */
#define SFX_CHORUS_LEN 4096
#define SFX_DELAY_LEN  48000       /* up to half a second at 96 kHz */
#define SFX_COMB_LEN   4096
#define SFX_AP_LEN     1024

typedef struct {
  float sr;
  float cho[SFX_CHORUS_LEN]; int cho_w; float cho_ph;
  float dl[2][SFX_DELAY_LEN]; int dl_w;
  float comb[2][4][SFX_COMB_LEN], comb_lp[2][4]; int comb_len[2][4], comb_w[2][4];
  float ap[2][2][SFX_AP_LEN]; int ap_len[2][2], ap_w[2][2];
} sfx_fx;

static inline void sfx_fx_init(sfx_fx *f, float sr)
{
  static const int COMB[4] = { 1116, 1188, 1277, 1356 }, AP[2] = { 556, 441 };       /* at 44.1 kHz; the right side 23 longer */
  f->sr = sr; f->cho_w = 0; f->cho_ph = 0.0f; f->dl_w = 0;
  for (int i = 0; i < SFX_CHORUS_LEN; i++) f->cho[i] = 0.0f;
  for (int c = 0; c < 2; c++) {
    for (int i = 0; i < SFX_DELAY_LEN; i++) f->dl[c][i] = 0.0f;
    for (int k = 0; k < 4; k++) {
      int n = (int)((float)(COMB[k] + c * 23) * sr / 44100.0f);
      f->comb_len[c][k] = n < 8 ? 8 : n > SFX_COMB_LEN ? SFX_COMB_LEN : n; f->comb_w[c][k] = 0; f->comb_lp[c][k] = 0.0f;
      for (int i = 0; i < SFX_COMB_LEN; i++) f->comb[c][k][i] = 0.0f;
    }
    for (int k = 0; k < 2; k++) {
      int n = (int)((float)(AP[k] + c * 23) * sr / 44100.0f);
      f->ap_len[c][k] = n < 8 ? 8 : n > SFX_AP_LEN ? SFX_AP_LEN : n; f->ap_w[c][k] = 0;
      for (int i = 0; i < SFX_AP_LEN; i++) f->ap[c][k][i] = 0.0f;
    }
  }
}

/* the three sends in (mono), their sound ADDED to l and r. delay_seconds: the echo's time; m: the master's values
   (SFX_M_*: feedback, the room's size and damping). */
static inline void sfx_fx_process(sfx_fx *f, const float *in_c, const float *in_d, const float *in_r, float *l, float *r, int n, float delay_seconds, const uint8_t *m)
{
  float fb = (float)(m[SFX_M_DELAY_FB] & 127) * (0.45f / 64.0f), room = 0.70f + (float)(m[SFX_M_REVERB_SIZE] & 127) * (0.20f / 91.0f), damp = (float)(m[SFX_M_REVERB_DAMP] & 127) * (0.25f / 40.0f);
  if (room > 0.97f) room = 0.97f;
  int dlen = (int)(delay_seconds * f->sr);
  while (dlen >= SFX_DELAY_LEN) dlen /= 2;
  if (dlen < 16) dlen = 16;
  float cstep = 0.6f / f->sr, cbase = 0.012f * f->sr, cdepth = 0.004f * f->sr;
  if (cbase + cdepth > (float)(SFX_CHORUS_LEN - 4)) { cbase = (float)(SFX_CHORUS_LEN / 2); cdepth = cbase * 0.3f; }
  for (int i = 0; i < n; i++) {
    /* chorus: two taps that drift against each other */
    f->cho[f->cho_w] = in_c[i];
    for (int c = 0; c < 2; c++) {
      float d = cbase + cdepth * SFX_SIN(f->cho_ph + (float)c * 0.25f), at = (float)f->cho_w - d;
      if (at < 0.0f) at += (float)SFX_CHORUS_LEN;
      int i0 = (int)at; float fr = at - (float)i0;
      float a = f->cho[i0 & (SFX_CHORUS_LEN - 1)], b = f->cho[(i0 + 1) & (SFX_CHORUS_LEN - 1)];
      (c ? r : l)[i] += (a + (b - a) * fr) * 0.8f;
    }
    f->cho_w = (f->cho_w + 1) & (SFX_CHORUS_LEN - 1);
    f->cho_ph += cstep; if (f->cho_ph >= 1.0f) f->cho_ph -= 1.0f;

    /* delay: left, then right, then left … */
    int rd = f->dl_w - dlen; if (rd < 0) rd += SFX_DELAY_LEN;
    float dl = f->dl[0][rd], dr = f->dl[1][rd];
    f->dl[0][f->dl_w] = in_d[i] + dr * fb + 1e-18f;
    f->dl[1][f->dl_w] = dl;
    f->dl_w++; if (f->dl_w >= SFX_DELAY_LEN) f->dl_w = 0;
    l[i] += dl * 0.7f; r[i] += dr * 0.7f;

    /* reverb: four damped combs and two all-passes a side */
    for (int c = 0; c < 2; c++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        float *buf = f->comb[c][k]; int w = f->comb_w[c][k];
        float y = buf[w];
        f->comb_lp[c][k] = y * (1.0f - damp) + f->comb_lp[c][k] * damp + 1e-18f;
        buf[w] = in_r[i] * 0.25f + f->comb_lp[c][k] * room;
        if (++w >= f->comb_len[c][k]) w = 0;
        f->comb_w[c][k] = w;
        sum += y;
      }
      for (int k = 0; k < 2; k++) {
        float *buf = f->ap[c][k]; int w = f->ap_w[c][k];
        float y = buf[w];
        buf[w] = sum + y * 0.5f;
        sum = y - sum * 0.5f;
        if (++w >= f->ap_len[c][k]) w = 0;
        f->ap_w[c][k] = w;
      }
      (c ? r : l)[i] += sum * 0.7f;
    }
  }
}

/* ---- the player -------------------------------------------------------------------------------------------------- */
#define SFX_PLAYER_VOICES 16
#define SFX_KEY_SEQ 200                   /* voices the sequencer started: key 200 + sound */

typedef struct {
  float sr;
  sfx_voice voice[SFX_PLAYER_VOICES];
  int16_t key[SFX_PLAYER_VOICES];
  uint32_t age[SFX_PLAYER_VOICES], ages, seed;
  sfx_patch patch[SFX_TRACKS];
  sfx_song song;                          /* (a host with two threads: the one that edits writes single values - and where it rewrites a whole
                                             pattern or song while this plays (a grid change, a copy, an undo, a song that comes in), a step that
                                             begins in those microseconds may play what is half moved; no index leaves the arrays) */
  sfx_mods mods;
  float intensity;                        /* 0..1 */
  int running, pattern, cue;              /* cue: the pattern to change to on the next bar line, -1 = none */
  uint8_t gate[SFX_TRACKS];               /* sixty-fourths a sequenced note still holds (0: no length given) */
  uint8_t div;                            /* the steps a sixteenth in which `step` is counted: 1, 2, 4 */
  uint8_t aut[SFX_LANES];                 /* where each automation lane stands (sfx_auto) */
  int arr_pos, hold;                      /* the bar of the arrangement; hold: a cued pattern outside it loops until the next cue */
  int loop;                               /* 1: the pattern repeats until a cue (sfx_player_set_loop); 0: arrangement or chain */
  double pos;                             /* steps since the start */
  int step, bar; float frac;              /* where it is: the step in the grid of the pattern that plays (-1 = not yet), how far into it */
  int tail, wet;                          /* samples the effects may still ring after the last voice; a voice sent to them */
  float dry[2][SFX_BLOCK], bus_c[SFX_BLOCK], bus_d[SFX_BLOCK], bus_r[SFX_BLOCK], one[SFX_BLOCK];
  sfx_fx fx;
} sfx_player;

static inline void sfx_player_init(sfx_player *p, float sr)
{
  sfx_wt_init();
  p->sr = sr; p->ages = 0; p->seed = 0x1234567u;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) { p->voice[i].active = 0; p->key[i] = -1; p->age[i] = 0; }
  for (int t = 0; t < SFX_TRACKS; t++) sfx_patch_init(&p->patch[t], 0);
  sfx_song_init(&p->song);
  p->mods.pitch_st = p->mods.cutoff_oct = p->mods.fm = 0.0f; p->mods.level = 1.0f;
  for (int t = 0; t < SFX_TRACKS; t++) p->gate[t] = 0;
  for (int l = 0; l < SFX_LANES; l++) p->aut[l] = 0;
  p->arr_pos = 0; p->hold = 0; p->loop = 0;
  p->intensity = 1.0f; p->running = 0; p->pattern = 0; p->cue = -1; p->pos = 0.0; p->step = -1; p->bar = -1; p->frac = 0.0f; p->tail = 0; p->wet = 0; p->div = 1;
  sfx_fx_init(&p->fx, sr);
}

/* a sound starts. key: what lets go of it again (a pad, a note); midi_note 0 = as designed; patch NULL = the sound's own.
   slot 0..15: one of the song's sounds, through its fader, pan and mute. slot 16..31: a sound beside the song (a game's
   sound effect, with its patch given) – straight to the sum, whatever the mixer says. */
static inline void sfx_player_note_on(sfx_player *p, int slot, int key, float vel, int midi_note, float note_st, const sfx_patch *patch)
{
  int pick = 0;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active && p->key[i] == key && p->voice[i].gate) sfx_voice_release(&p->voice[i]);
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) { if (!p->voice[i].active) { pick = i; break; } if (p->age[i] < p->age[pick]) pick = i; }
  sfx_voice_trigger(&p->voice[pick], patch ? patch : &p->patch[slot & 15], vel, note_st, slot & 31, p->sr, p->seed += 0x9E3779B9u);
  if (midi_note > 0) sfx_voice_set_note(&p->voice[pick], midi_note);
  p->key[pick] = (int16_t)key; p->age[pick] = ++p->ages;
}

static inline void sfx_player_note_off(sfx_player *p, int key)
{
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active && p->key[i] == key) sfx_voice_release(&p->voice[i]);
}

/* a sound changed: for the next time, and for the voices that play it now */
static inline void sfx_player_set_patch(sfx_player *p, int slot, const sfx_patch *patch)
{
  if ((slot & 31) < SFX_TRACKS) p->patch[slot & 15] = *patch;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active && p->voice[i].slot == (slot & 31)) sfx_voice_set_patch(&p->voice[i], patch);
}

/* the sequencer's notes are let go: all of them, or (a bar line) only those without a length of their own */
static inline void sfx__seq_release(sfx_player *p, int all)
{
  for (int i = 0; i < SFX_PLAYER_VOICES; i++)
    if (p->voice[i].active && p->key[i] >= SFX_KEY_SEQ && (all || !p->gate[(p->key[i] - SFX_KEY_SEQ) & 15])) sfx_voice_release(&p->voice[i]);
  if (all) for (int t = 0; t < SFX_TRACKS; t++) p->gate[t] = 0;
}

static inline void sfx_player_start(sfx_player *p, int pattern)
{
  p->running = 1; p->pos = 0.0; p->step = -1; p->bar = -1; p->cue = -1; p->div = 1;
  for (int t = 0; t < SFX_TRACKS; t++) p->gate[t] = 0;
  for (int l = 0; l < SFX_LANES; l++) p->aut[l] = 0;
  if (pattern >= 0) p->pattern = pattern % SFX_PATTERNS;
  p->arr_pos = -1; p->hold = 0;                                  /* an arrangement: from its first bar with this pattern, else from the top after it */
  for (int i = 0; i < p->song.arr_len && i < SFX_ARR_MAX; i++) if (p->song.arr[i] == p->pattern) { p->arr_pos = i; break; }
}

static inline void sfx_player_stop(sfx_player *p) { if (p->running) sfx__seq_release(p, 1); for (int l = 0; l < SFX_LANES; l++) p->aut[l] = 0; p->running = 0; p->step = -1; p->bar = -1; p->div = 1; }

/* change to this pattern on the next bar line (at once while stopped) */
static inline void sfx_player_cue(sfx_player *p, int pattern)
{
  if (pattern < 0 || pattern >= SFX_PATTERNS) return;
  if (p->running) p->cue = pattern; else p->pattern = pattern;
}

/* 1 = the pattern repeats until a cue; 0 = arrangement or chain. Leaving it: on in the arrangement from its
   first bar with the pattern that plays, else from the top - as sfx_player_start does. */
static inline void sfx_player_set_loop(sfx_player *p, int on)
{
  on = on != 0;
  if (p->loop && !on) {
    p->arr_pos = -1; p->hold = 0;
    for (int i = 0; i < p->song.arr_len && i < SFX_ARR_MAX; i++) if (p->song.arr[i] == p->pattern) { p->arr_pos = i; break; }
  }
  p->loop = on;
}

/* the voices from..to of this piece, each through its sound's fader and pan to the dry mix and to its three sends */
static inline void sfx__render(sfx_player *p, int from, int to)
{
  const sfx_song *s = &p->song;
  int n = to - from;
  if (n <= 0) return;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    sfx_voice *v = &p->voice[i];
    if (!v->active) continue;
    float c = sfx__unit(v->patch.p[SFX_P_CHORUS]), d = sfx__unit(v->patch.p[SFX_P_DELAY]), r = sfx__unit(v->patch.p[SFX_P_REVERB]);
    for (int k = 0; k < n; k++) p->one[k] = 0.0f;
    if (c > 0.0f || d > 0.0f || r > 0.0f) p->wet = 1;
    sfx_voice_render(v, &p->mods, p->one, n);
    int t = v->slot & 15, beside = v->slot >= SFX_TRACKS;
    if (!beside && ((s->mute >> t) & 1 || (s->solo && !((s->solo >> t) & 1)))) continue;   /* (it plays on, unheard) */
    int vol = beside ? 100 : sfx_auto(s, p->aut, SFX_AT_VOL, t, s->vol[t] & 127), pan = beside ? 64 : sfx_auto(s, p->aut, SFX_AT_PAN, t, s->pan[t] & 127);
    float g = (float)vol * 0.01f, pn = (float)pan * (1.0f / 127.0f);
    float gl = pan == 64 ? g : g * (pn < 0.5f ? 1.0f : 2.0f - 2.0f * pn), gr = pan == 64 ? g : g * (pn > 0.5f ? 1.0f : 2.0f * pn);
    for (int k = 0; k < n; k++) { float x = p->one[k]; p->dry[0][from + k] += x * gl; p->dry[1][from + k] += x * gr; x *= g; p->bus_c[from + k] += x * c; p->bus_d[from + k] += x * d * 0.7f; p->bus_r[from + k] += x * r; }
  }
}

/* the step at sixteenth st, fraction fr of it: swing stretches the even sixteenth of a pair and squeezes the odd one,
   and the steps inside go with their sixteenth. *frac: how far into that step. */
static inline int sfx__sub(int st, float fr, float swing, int div, int steps, float *frac)
{
  int idx = st; double q = (double)fr, sw = (double)swing;
  if (st & 1) { if (fr < swing) { idx = st - 1; q = (1.0 + q) / (1.0 + sw); } else q = (q - sw) / (1.0 - sw); }
  else if (st + 1 < steps) q = q / (1.0 + sw);
  q *= (double)div; int j = (int)q; if (j > div - 1) j = div - 1; if (j < 0) j = 0;
  *frac = (float)(q - (double)j); if (*frac > 1.0f) *frac = 1.0f;
  return idx * div + j;
}

/* ADDS n stereo samples to l and r; every voice starts on the very sample of its step.
   The clock counts sixteenths; a pattern with a finer grid has 2 or 4 steps in each (a pattern that was only made
   finer plays the same samples as before: its steps begin where they began).
   host_pos < 0: its own clock at the song's tempo. Else the host leads (a DAW): host_pos = its position in steps
   (quarter notes x 4) at the first sample, host_step = steps per sample.
   Returns the sounds that play now, a bit each; bit 31 while only the effects still ring. */
static inline uint32_t sfx_player_render(sfx_player *p, float *l, float *r, int total, double host_pos, double host_step)
{
  const sfx_song *s = &p->song;
  int host = host_pos >= 0.0, steps = s->len >= 1 && s->len <= SFX_BAR ? s->len : SFX_BAR;
  double per = host ? host_step : (double)s->bpm / 60.0 * 4.0 / (double)p->sr, len = (double)steps;
  static const uint8_t SIXTEENTHS[SFX_DELAY_TIMES] = { 1, 2, 3, 4, 6, 8 };
  float swing = (float)s->swing * 0.005f;
  uint8_t fx[SFX_M_COUNT];
  uint32_t on = 0;
  for (int done = 0; done < total; ) {
    int n = total - done < SFX_BLOCK ? total - done : SFX_BLOCK, from = 0;
    for (int k = 0; k < n; k++) p->dry[0][k] = p->dry[1][k] = p->bus_c[k] = p->bus_d[k] = p->bus_r[k] = 0.0f;
    p->wet = 0;
    for (int i = 0; i < SFX_M_COUNT; i++) fx[i] = (uint8_t)sfx_auto(s, p->aut, SFX_AT_MASTER + i, -1, s->fx[i]);   /* the master as the lanes have it now */
    float echo = 0.25f * (float)SIXTEENTHS[fx[SFX_M_DELAY_TIME] % SFX_DELAY_TIMES] * 60.0f / (float)(s->bpm < 40 ? 40 : s->bpm);
    float drive = (float)(fx[SFX_M_DRIVE] & 127) * (1.0f / 127.0f), ring = fx[SFX_M_REVERB_SIZE] > 91 || fx[SFX_M_DELAY_FB] > 64 ? 10.0f : 4.0f;
    if (p->running) {
      if (host) p->pos = host_pos + (double)done * per;
      for (int k = 0; k < n; k++) {
        double a = p->pos + (double)k * per;
        if (a < 0.0) continue;
        long long bar = (long long)(a / len);
        double f = a - len * (double)bar;
        int st = (int)f; float fr = (float)(f - (double)st);
        p->frac = fr;
        int newbar = (int)bar != p->bar, sub = st;
        int div = sfx_div(s->pat[p->pattern % SFX_PATTERNS].grid);
        if (!newbar && div != p->div) {                            /* the grid changed under the playhead: no step fires for that */
          if (p->step >= 0) p->step = p->step * div / p->div;
          p->div = (uint8_t)div;
        }
        if ((st & 1) && fr < swing && (div == 1 || newbar)) continue;   /* swing: the off-beat step waits */
        if (!newbar) {
          if (div > 1) { float f2; sub = sfx__sub(st, fr, swing, div, steps, &f2); p->frac = f2; }
          if (sub == p->step) continue;
        }
        sfx__render(p, from, k); from = k;
        for (int t = 0, dec = 4 / p->div; t < SFX_TRACKS; t++)     /* a note with a length: its steps are over */
          if (p->gate[t]) { if (p->gate[t] <= dec) { p->gate[t] = 0; sfx_player_note_off(p, SFX_KEY_SEQ + t); } else p->gate[t] = (uint8_t)(p->gate[t] - dec); }
        if (newbar) {                                  /* a bar line: the cue, else the loop, else the chain */
          int bars = s->arr_len <= SFX_ARR_MAX ? s->arr_len : 0;
          if (p->cue >= 0) {                                       /* a cue: on in the arrangement from its next bar with that pattern – or, outside it, that pattern until the next cue */
            p->pattern = p->cue; p->cue = -1; p->hold = bars > 0;
            for (int i = 1; i <= bars; i++) { int at = (p->arr_pos + i + bars) % bars; if (s->arr[at] == p->pattern) { p->arr_pos = at; p->hold = 0; break; } }
          }
          else if (p->loop) { }                                    /* LOOP: the pattern stays */
          else if (p->bar >= 0 && bars && !p->hold) { p->arr_pos = (p->arr_pos + 1) % bars; p->pattern = s->arr[p->arr_pos] % SFX_PATTERNS; }
          else if (p->bar >= 0 && !bars && s->chain > 1) p->pattern = (p->pattern + 1) % s->chain;
          for (int l = 0; l < SFX_LANES; l++) p->aut[l] = 0;       /* every bar starts with the knobs where they were made */
          p->bar = (int)bar;
          sfx__seq_release(p, 0);                                  /* what was held is let go at the bar line … */
          div = sfx_div(s->pat[p->pattern % SFX_PATTERNS].grid);   /* … and the pattern that plays now has its own grid */
          p->div = (uint8_t)div; sub = st;
          if (div > 1) { float f2; sub = sfx__sub(st, fr, swing, div, steps, &f2); p->frac = f2; }
        }
        p->step = sub;
        const sfx_pattern *pt = &s->pat[p->pattern % SFX_PATTERNS];
        unsigned m = pt->on[sub], gate = (unsigned)(p->intensity * 127.0f + 0.5f);
        sfx_patch moved;
        unsigned turned = 0;
        for (int l = 0; l < SFX_LANES; l++) {                      /* the lanes: a point here moves its knob … */
          if (!pt->aut[l][sub] || !s->lane[l].track || s->lane[l].track > SFX_TRACKS) continue;
          p->aut[l] = pt->aut[l][sub];
          if (s->lane[l].to < SFX_AT_VOL) turned |= 1u << (s->lane[l].track - 1);
        }
        for (int t = 0; turned && t < SFX_TRACKS; t++) {           /* … also for what sounds now */
          if (!(turned & (1u << t))) continue;
          const sfx_patch *e = sfx_auto_patch(s, p->aut, t, &p->patch[t], &moved);
          for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active && p->voice[i].slot == t) sfx_voice_set_patch(&p->voice[i], e);
        }
        for (int t = 0; t < SFX_TRACKS; t++)                       /* … or when its sound plays again (note_on does that) */
          if ((m & (1u << t)) && gate >= s->level[t]) {
            int len = pt->len[t][sub];
            sfx_player_note_on(p, t, SFX_KEY_SEQ + t, SFX_VEL[pt->vel[t][sub] & 7], pt->note[t][sub], 0.0f, sfx_auto_patch(s, p->aut, t, &p->patch[t], &moved));
            p->gate[t] = (uint8_t)((len > 63 ? 63 : len) * (4 / div));
          }
      }
      if (!host) p->pos += (double)n * per;
    }
    sfx__render(p, from, n);
    on = 0;
    for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active) on |= 1u << (p->voice[i].slot & 15);
    if (p->wet) p->tail = (int)(p->sr * ring); else if (!on && p->tail > 0) p->tail -= n;   /* (dry sounds leave no tail) */
    if (on || p->tail > 0) {
      sfx_fx_process(&p->fx, p->bus_c, p->bus_d, p->bus_r, p->dry[0], p->dry[1], n, echo, fx);
      if (drive > 0.0f) {                                          /* the sum, pushed into a soft ceiling */
        float push = 1.0f + drive * 5.0f, back = 1.0f / (1.0f + drive * 1.5f);
        for (int k = 0; k < n; k++) { p->dry[0][k] = SFX_TANH(p->dry[0][k] * push) * back; p->dry[1][k] = SFX_TANH(p->dry[1][k] * push) * back; }
      }
      for (int k = 0; k < n; k++) { l[done + k] += p->dry[0][k]; r[done + k] += p->dry[1][k]; }
    }
    done += n;
  }
  return on | (p->tail > 0 && !on ? 0x80000000u : 0u);
}

#endif
