/* SPDX-License-Identifier: GPL-3.0-or-later
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
 *
 * WHAT THIS FILE HOLDS, in its order:
 *   1. Sizes and limits           SFX_TRACKS, SFX_STEPS, SFX_LANES ..., the sizes of a packed song in every format
 *   2. A song in memory           sfx_pattern, sfx_song; SFX_AT_* (what a lane moves), SFX_M_* (the master's values)
 *   3. Making and changing        sfx_song_init, sfx_song_mix_init, sfx_pattern_clear, sfx_pattern_regrid,
 *                                 sfx_pattern_regrid_loss, sfx_scale_step
 *   4. A song as bytes            THE BYTE LAYOUT of format 6 and of the formats 1 to 5; sfx_song_pack,
 *                                 sfx_song_unpack
 *   5. Automation                 sfx_auto, sfx_auto_patch: the value of a knob as the lanes have it now
 *   6. The effects                sfx_fx, sfx_fx_init, sfx_fx_process: chorus, delay, reverb
 *   7. The player                 sfx_player; sfx_player_init, _note_on, _note_off, _set_patch, _start, _stop, _cue,
 *                                 _set_loop; sfx_player_render (the sequencer and the mix) with its two helpers
 *                                 sfx__render (the voices of a piece of a block) and sfx__sub (swing and the grid)
 *
 * WHAT TO READ FIRST: the two structs of section 2 – everything else reads or writes them. Then, for the format, the
 * block comment of section 4; for how a song plays, sfx_player_render at the end, which calls all the rest.
 *
 * RULES FOR ALL OF IT: nothing here allocates, blocks, locks, or calls a library; every function works on memory the
 * caller owns. Two things are big: an sfx_song is about 38 KB and an sfx_player about 600 KB (the delay line and the
 * reverb are in it) – a player belongs in static memory or in a buffer of the host, never on a stack.
 * A name with two underscores (sfx__render) is for this file's own use.
 */
#ifndef SFX_SONG_H
#define SFX_SONG_H
#include "sfx_core.h"

/* ==== 1. Sizes and limits ======================================================================================= */

#define SFX_TRACKS       16 /* one per sound */
#define SFX_BAR          16 /* sixteenths in a bar at most: sfx_song.len is 1..SFX_BAR */
#define SFX_STEPS        64 /* cells in a pattern: SFX_BAR sixteenths, each cut in up to 4 */
#define SFX_GRIDS        3  /* a pattern's grid: 0, 1, 2 = sixteenths, thirty-seconds, sixty-fourths */
#define SFX_PATTERNS     8
#define SFX_SONG_VERSION 6
#define SFX_LANES        24 /* automation lanes in a song: each moves one knob of one sound (or the master) */
#define SFX_ARR_MAX      32 /* bars in an arrangement */
#define SFX_M_COUNT      8  /* the master's bytes in a song: five values in use (SFX_M_USED), three spare */

/* The formats before this one: a row of 16 steps for every sound of every pattern, whatever was in it.
   BODY = the patterns' bytes, HEAD = what stands before them (section 4 has the layouts). */
#define SFX_SONG_BODY_V2 (SFX_PATTERNS * SFX_TRACKS * (3 + SFX_BAR))
#define SFX_SONG_BODY_V3 (SFX_PATTERNS * SFX_TRACKS * (3 + 2 * SFX_BAR))
#define SFX_SONG_BODY_V4 (SFX_PATTERNS * SFX_TRACKS * (3 + 3 * SFX_BAR))
#define SFX_SONG_BODY_V5 (SFX_SONG_BODY_V3 + SFX_PATTERNS * SFX_LANES * SFX_BAR)
#define SFX_SONG_HEAD_V2 (7 + SFX_TRACKS * 3 + 3 + SFX_M_COUNT)
#define SFX_SONG_HEAD_V4 (SFX_SONG_HEAD_V2 + SFX_TRACKS + 1 + SFX_ARR_MAX)

/* Format 6: the head of format 5, then a block a pattern that holds only the rows that are used - so a packed song
   has no fixed size: SFX_SONG_MIN .. SFX_SONG_MAX bytes, each 0..127 (sfx_song_pack says how many). */
#define SFX_SONG_HEAD 147
#define SFX_SONG_MIN  211   /* 147 + 8 * 8 */
#define SFX_SONG_MAX  38355 /* 147 + 8 * (8 + 16 * 202 + 24 * 64) */

/* The sizes of the formats before, all still taken by sfx_song_unpack:
     version 5   7699 bytes   16 steps a pattern, every row written
     version 4   6643 bytes   one lane a sound
     version 3   4546 bytes   no automation, no arrangement
     version 2   2498 bytes   no velocity, no length either
     version 1   2455 bytes   no mixer either */
#define SFX_SONG_BYTES_V5 7699
#define SFX_SONG_BYTES_V4 (SFX_SONG_HEAD_V4 + SFX_SONG_BODY_V4)
#define SFX_SONG_BYTES_V3 (SFX_SONG_HEAD_V2 + SFX_SONG_BODY_V3)
#define SFX_SONG_BYTES_V2 (SFX_SONG_HEAD_V2 + SFX_SONG_BODY_V2)
#define SFX_SONG_BYTES_V1 (7 + SFX_TRACKS + SFX_SONG_BODY_V2)

_Static_assert(SFX_SONG_BYTES_V1 == 2455 && SFX_SONG_BYTES_V2 == 2498 && SFX_SONG_BYTES_V3 == 4546 &&
                   SFX_SONG_BYTES_V4 == 6643 && SFX_SONG_BYTES_V5 == 7699,
               "old sizes");
_Static_assert(SFX_SONG_HEAD == SFX_SONG_HEAD_V2 + 1 + SFX_ARR_MAX + SFX_LANES * 2 &&
                   SFX_SONG_BYTES_V5 == SFX_SONG_HEAD + SFX_SONG_BODY_V5,
               "format 5");
_Static_assert(SFX_SONG_HEAD == 147 && SFX_SONG_MAX == 147 + 8 * (8 + 16 * 202 + 24 * 64), "format 6");

/* The steps a sixteenth has in grid 0, 1, 2: 1, 2, 4. Any other byte reads as 1. */
static inline int sfx_div(int grid) {
  return grid == 2 ? 4 : grid == 1 ? 2 : 1;
}

/* A step's accent byte in the formats 3 to 5: its velocity (8 levels) and its length.
   (SFX_ACC makes one: nothing here calls it, since only format 6 is written. It is for a test or a tool that writes
   an old format.) */
#define SFX_VEL_DEFAULT 5
/* clang-format off */
#define SFX_ACC(vel, len) ((uint8_t)(((len) & 15) << 3 | ((vel) & 7)))
/* 0..7: soft .. full (5 = as it always was) */
#define SFX_ACC_VEL(a) ((a) & 7)
/* 0 = by itself (to the sound's end, its next note or the bar line), else steps */
#define SFX_ACC_LEN(a) (((a) >> 3) & 15)
/* clang-format on */

/* the eight velocity levels as loudness */
static const float SFX_VEL[8] = {0.15f, 0.29f, 0.43f, 0.57f, 0.71f, 0.85f, 0.93f, 1.0f};

/* ==== 2. A song in memory ======================================================================================= */

/* The master's values: indices into sfx_song.fx. */
enum {
  SFX_M_DELAY_TIME,  /* 0..5: the echo after 1, 2, 3, 4, 6, 8 sixteenths */
  SFX_M_DELAY_FB,    /* 0..127: how much of it comes again (64 = as it was: 0.45) */
  SFX_M_REVERB_SIZE, /* 0..127: how long the room rings (91 = as it was) */
  SFX_M_REVERB_DAMP, /* 0..127: how fast its highs fade (40 = as it was) */
  SFX_M_DRIVE,       /* 0..127: saturation on the sum (0 = none) */
  SFX_M_USED         /* 5: the values in use. SFX_M_COUNT (8) is the bytes a song stores: three are spare */
};
#define SFX_DELAY_TIMES 6

/* What a lane moves (sfx_song.lane[].to): any knob there is.
     0        none
     1..25    the sound's own values (SFX_P_* + 1)
     26, 27   its fader and pan
     28..32   the master's five (SFX_AT_MASTER + SFX_M_*) */
enum {
  SFX_AT_NONE,
  SFX_AT_PATCH = 1,
  SFX_AT_VOL = SFX_AT_PATCH + SFX_P_COUNT,
  SFX_AT_PAN,
  SFX_AT_MASTER,
  SFX_AT_COUNT = SFX_AT_MASTER + SFX_M_USED
};

enum {
  SFX_SCALE_CHROMATIC,
  SFX_SCALE_MAJOR,
  SFX_SCALE_MINOR,
  SFX_SCALE_PENTATONIC,
  SFX_SCALE_COUNT
};

/* A pattern is a bar. Its grid cuts every sixteenth of the bar in 1, 2 or 4 steps: 16, 32 or 64 steps a full bar.
   The cells from 16 * sfx_div(grid) on are always empty (sfx_song_init, sfx_song_unpack and sfx_pattern_regrid keep
   it so). */
typedef struct {
  /* 0, 1, 2 = 16, 32, 64 steps a full bar */
  uint8_t grid;
  /* per step: bit = sound */
  uint16_t on[SFX_STEPS];
  /* 0 = the sound as designed (drums, effects), else a MIDI note */
  uint8_t note[SFX_TRACKS][SFX_STEPS];
  /* 0..7: soft .. full (SFX_VEL_DEFAULT 5 = as it always was) */
  uint8_t vel[SFX_TRACKS][SFX_STEPS];
  /* 0 = by itself (to the sound's end, its next note or the bar line), else steps of THIS pattern's grid:
     1 .. 16 * div - 1 */
  uint8_t len[SFX_TRACKS][SFX_STEPS];
  /* the automation lanes: 0 = no point, else value + 1 (1..127). A point holds to the next, or the bar's end */
  uint8_t aut[SFX_LANES][SFX_STEPS];
} sfx_pattern;

typedef struct {
  sfx_pattern pat[SFX_PATTERNS];
  uint16_t bpm;              /* 40..240 */
  uint8_t len;               /* the bar in sixteenths, 1..16 (a pattern plays len * sfx_div(its grid) steps) */
  uint8_t swing;             /* 0..100 */
  uint8_t scale;             /* for entering notes (key of C) */
  uint8_t chain;             /* 1 = stay on a pattern, n = walk patterns 1..n, one bar each */
  uint8_t level[SFX_TRACKS]; /* 0..127: the sound plays while intensity >= this (0 = always) */
  uint8_t vol[SFX_TRACKS];   /* 0..127: the sound's fader (100 = as designed) */
  uint8_t pan[SFX_TRACKS];   /* 0..127: left .. right (64 = the middle) */
  uint16_t mute;             /* a bit a sound: silent */
  uint16_t solo;             /* a bit a sound: only these (not kept with the song) */
  uint8_t fx[SFX_M_COUNT];   /* the master: SFX_M_* */
  /* a lane: whose knob (0 = the lane is free, else sound + 1) and which (SFX_AT_*) */
  struct {
    uint8_t track;
    uint8_t to;
  } lane[SFX_LANES];
  /* the arrangement: a pattern a bar, then again from the top. arr_len 0 = none: the chain */
  uint8_t arr_len;
  uint8_t arr[SFX_ARR_MAX];
} sfx_song;

/* ==== 3. Making and changing patterns and songs ================================================================= */

/* The mixer as a new song has it: every fader at 100, every pan in the middle, nothing muted or solo, the master's
   effects as they always were. The patterns and the rest of the song are not touched. */
static inline void sfx_song_mix_init(sfx_song *s) {
  static const uint8_t FX[SFX_M_COUNT] = {2, 64, 91, 40, 0, 0, 0, 0};
  for (int t = 0; t < SFX_TRACKS; t++) {
    s->vol[t] = 100;
    s->pan[t] = 64;
  }
  for (int i = 0; i < SFX_M_COUNT; i++) {
    s->fx[i] = FX[i];
  }
  s->solo = 0;
  s->mute = 0;
}

/* cell `i` of the pattern empty: no sound on it, no note, the default velocity, no length, no point */
static inline void sfx__cell_clear(sfx_pattern *p, int i) {
  p->on[i] = 0;
  for (int t = 0; t < SFX_TRACKS; t++) {
    p->note[t][i] = 0;
    p->vel[t][i] = SFX_VEL_DEFAULT;
    p->len[t][i] = 0;
  }
  for (int l = 0; l < SFX_LANES; l++) {
    p->aut[l][i] = 0;
  }
}

/* all 64 cells empty; the grid stays */
static inline void sfx_pattern_clear(sfx_pattern *p) {
  for (int i = 0; i < SFX_STEPS; i++) {
    sfx__cell_clear(p, i);
  }
}

/* What lowering the pattern to `grid` would drop: the steps (a sound on a step counts one) and the points that stand
   between the steps of the coarser grid. 0 when that is not lowering. Nothing is changed. */
static inline int sfx_pattern_regrid_loss(const sfx_pattern *p, int grid) {
  int old_div = sfx_div(p->grid);
  int new_div = sfx_div(grid);
  int lost = 0;
  if (grid < 0 || grid >= SFX_GRIDS || new_div >= old_div) {
    return 0;
  }
  /* of every `k` steps, the first stays */
  int k = old_div / new_div;
  for (int i = 0; i < SFX_BAR * old_div; i++) {
    if (i % k == 0) {
      continue;
    }
    /* a bit of on[i] is a sound on this step */
    for (unsigned m = p->on[i]; m; m &= m - 1) {
      lost++;
    }
    for (int l = 0; l < SFX_LANES; l++) {
      if (p->aut[l][i]) {
        lost++;
      }
    }
  }
  return lost;
}

/* The pattern in another grid, in place. Finer: every step moves to the step that begins at the same moment, a length
   is multiplied - it sounds the same. Coarser: what stands between the steps of the new grid is dropped, a length is
   rounded up. A grid above 2 is ignored. */
static inline void sfx_pattern_regrid(sfx_pattern *p, int grid) {
  int old_grid = p->grid < SFX_GRIDS ? p->grid : 0;
  if (grid < 0 || grid >= SFX_GRIDS) {
    return;
  }
  int old_div = sfx_div(old_grid);
  int new_div = sfx_div(grid);

  if (new_div > old_div) {
    /* finer: step i moves up to step i * k. From the last step down, so that nothing is overwritten before it moved */
    int k = new_div / old_div;
    for (int i = SFX_BAR * old_div - 1; i >= 0; i--) {
      int to = i * k;
      if (i) {
        p->on[to] = p->on[i];
      }
      for (int t = 0; t < SFX_TRACKS; t++) {
        p->note[t][to] = p->note[t][i];
        p->vel[t][to] = p->vel[t][i];
        p->len[t][to] = (uint8_t)(p->len[t][i] * k);
      }
      for (int l = 0; l < SFX_LANES; l++) {
        p->aut[l][to] = p->aut[l][i];
      }
      if (i) {
        sfx__cell_clear(p, i);
      }
    }
  } else if (new_div < old_div) {
    /* coarser: step i * k moves down to step i; `top` is the longest length the new grid has */
    int k = old_div / new_div;
    int top = SFX_BAR * new_div - 1;
    for (int i = 0; i < SFX_BAR * new_div; i++) {
      int from = i * k;
      p->on[i] = p->on[from];
      for (int t = 0; t < SFX_TRACKS; t++) {
        int len = (p->len[t][from] + k - 1) / k;
        p->note[t][i] = p->note[t][from];
        p->vel[t][i] = p->vel[t][from];
        p->len[t][i] = (uint8_t)(len > top ? top : len);
      }
      for (int l = 0; l < SFX_LANES; l++) {
        p->aut[l][i] = p->aut[l][from];
      }
    }
    /* the cells past the new grid's are empty */
    for (int i = SFX_BAR * new_div; i < SFX_STEPS; i++) {
      sfx__cell_clear(p, i);
    }
  }
  p->grid = (uint8_t)grid;
}

/* An empty song: 8 empty patterns at 16 steps, no layers, no lanes, no arrangement, the mixer of sfx_song_mix_init,
   120 BPM, a bar of 16 sixteenths, no swing, the minor scale, no chain. Writes every field of *s. */
static inline void sfx_song_init(sfx_song *s) {
  for (int p = 0; p < SFX_PATTERNS; p++) {
    sfx_pattern_clear(&s->pat[p]);
    s->pat[p].grid = 0;
  }
  for (int t = 0; t < SFX_TRACKS; t++) {
    s->level[t] = 0;
  }
  for (int l = 0; l < SFX_LANES; l++) {
    s->lane[l].to = 0;
    s->lane[l].track = 0;
  }
  for (int i = 0; i < SFX_ARR_MAX; i++) {
    s->arr[i] = 0;
  }
  s->arr_len = 0;
  sfx_song_mix_init(s);
  s->bpm = 120;
  s->len = SFX_BAR;
  s->swing = 0;
  s->scale = SFX_SCALE_MINOR;
  s->chain = 1;
}

/* The next note up (dir > 0) or down in the scale (SFX_SCALE_*, key of C); below the lowest: 0 = "as designed", and
   up from there: middle C. Notes run from 24 to 108. */
static inline int sfx_scale_step(int note, int dir, int scale) {
  /* bit = semitone above C */
  static const uint16_t MASK[SFX_SCALE_COUNT] = {0xFFF, 0xAB5, 0x5AD, 0x4A9};
  if (note == 0) {
    return dir > 0 ? 60 : 0;
  }
  for (int i = 0; i < 12; i++) {
    note += dir > 0 ? 1 : -1;
    if (note < 24) {
      return 0;
    }
    if (note > 108) {
      return 108;
    }
    if (MASK[scale % SFX_SCALE_COUNT] & (1u << (note % 12))) {
      break;
    }
  }
  return note;
}

/* ==== 4. A song as bytes ===========================================================================================
   For storage, for SysEx, for a game's asset. Every byte is 0..127, so the bytes travel through MIDI as they are.
   A number that needs more than 7 bits is cut in groups of 7 bits, the LOWEST group first – except the tempo.

   FORMAT 6 – what sfx_song_pack writes. SFX_SONG_MIN (211) .. SFX_SONG_MAX (38355) bytes.

   The head, 147 bytes:
     offset  bytes  what                                                  must be
     0       1      the format                                            6
     1       1      the bar in sixteenths (sfx_song.len)                  1..16
     2       2      the tempo in BPM: byte 2 = bpm >> 7, byte 3 = bpm & 127   40..240
     4       1      swing                                                 0..100
     5       1      scale (SFX_SCALE_*)                                   0..3
     6       1      chain                                                 1..8
     7       16     the layers: a threshold a sound (level)               0..127
     23      16     the faders, a sound each (vol)
     39      16     the pans, a sound each (pan)
     55      3      mute, bit t = sound t: bits 0-6, 7-13, 14-15
     58      8      the master (fx, SFX_M_*); byte 58 is the delay time   byte 58: 0..5
     66      1      the arrangement's length in bars (arr_len)            0..32
     67      32     the arrangement: a pattern a bar (arr)                each 0..7
     99      48     the 24 lanes, 2 bytes each: whose knob (0 = free,     0..16,
                    else sound + 1), then which knob (SFX_AT_*)           0..32
   (solo is not kept.)

   Then 8 pattern blocks, A to H, one after the other from offset 147. A block:
     +0        grid 0, 1, 2          (G = 16, 32, 64 steps; B = 3, 5, 10 bytes of step bits)
     +1 .. +3  the sounds with a row: bit t, as 7 + 7 + 2 bits
     +4 .. +7  the lanes with a row: bit l, as 7 + 7 + 7 + 3 bits
     for each sound with a row, ascending:  B bytes: the steps that are on, 7 bits a byte, step 0 = bit 0 of the first
                                            G bytes notes · G bytes velocities 0..7 · G bytes lengths 0 .. G-1
     for each lane with a row, ascending:   G bytes points: 0 none, else value + 1
   A sound has a row when anything is kept for it in that pattern, a lane when it has a point there.
   A sound without a row has no step on, every note 0, every velocity SFX_VEL_DEFAULT (5), every length 0.
   So a block is 8 + rows * (B + 3 * G) + lanes * G bytes: 8 when the pattern is empty, at most 4776.

   A reader must refuse (sfx_song_unpack returns -1, and has written nothing): a byte above 127; a head value outside
   "must be"; a grid above 2; bits above the 2 and the 3 of the two masks' last bytes; a step bit for a step from G
   on; a velocity above 7; a length above G - 1; a block that ends past the bytes; bytes left after block H.

   THE FORMATS BEFORE – read by sfx_song_unpack, never written. Each has one fixed size; their patterns have 16 steps.
   A "row" here is 3 bytes step bits (7 + 7 + 2) and then 16 bytes for each thing the format keeps a step.
     1 (2455 bytes)  bytes 0..6 as format 6, the 16 layers; then for each pattern, for each sound, a row: notes
     2 (2498 bytes)  the head of format 6 up to the master (66 bytes); the rows as format 1
     3 (4546 bytes)  as 2; a row: notes, accents. An accent byte = velocity | length << 3 (SFX_ACC)
     4 (6643 bytes)  the head of 2, then 16 bytes: what each sound's ONE lane moves (SFX_AT_*, 0 = nothing), then the
                     arrangement (1 + 32 bytes); a row: notes, accents, the points of that sound's lane
     5 (7699 bytes)  the head of format 6 (147 bytes); the rows of format 3 for all 8 patterns; then, for each
                     pattern, for each of the 24 lanes, 16 bytes of points */

/* Writes song `s` as format 6 to `out`, which must hold SFX_SONG_MAX bytes. Returns the bytes written.
   Most values out of range are brought into it. Six are written as they are, and sfx_song_unpack refuses them when
   they are out of range: len, bpm, swing, scale, chain and the delay's time (fx[SFX_M_DELAY_TIME], 0..5). */
static inline int sfx_song_pack(const sfx_song *s, uint8_t *out) {
  uint8_t *o = out;

  /* the head: bar, tempo, swing, scale, chain */
  *o++ = SFX_SONG_VERSION;
  *o++ = s->len;
  *o++ = (uint8_t)(s->bpm >> 7);
  *o++ = (uint8_t)(s->bpm & 0x7F);
  *o++ = s->swing;
  *o++ = s->scale;
  *o++ = s->chain;

  /* the layers and the mixer */
  for (int t = 0; t < SFX_TRACKS; t++) {
    *o++ = s->level[t] & 0x7F;
  }
  for (int t = 0; t < SFX_TRACKS; t++) {
    *o++ = s->vol[t] & 0x7F;
  }
  for (int t = 0; t < SFX_TRACKS; t++) {
    *o++ = s->pan[t] & 0x7F;
  }
  *o++ = (uint8_t)(s->mute & 0x7F);
  *o++ = (uint8_t)((s->mute >> 7) & 0x7F);
  *o++ = (uint8_t)(s->mute >> 14);
  for (int i = 0; i < SFX_M_COUNT; i++) {
    *o++ = s->fx[i] & 0x7F;
  }

  /* the arrangement, the lanes (a lane that names no sound or no knob is written as free) */
  *o++ = s->arr_len <= SFX_ARR_MAX ? s->arr_len : 0;
  for (int i = 0; i < SFX_ARR_MAX; i++) {
    *o++ = s->arr[i] % SFX_PATTERNS;
  }
  for (int l = 0; l < SFX_LANES; l++) {
    int ok = s->lane[l].track <= SFX_TRACKS && s->lane[l].to > 0 && s->lane[l].to < SFX_AT_COUNT;
    *o++ = ok ? s->lane[l].track : 0;
    *o++ = ok ? s->lane[l].to : 0;
  }

  /* the eight pattern blocks */
  for (int p = 0; p < SFX_PATTERNS; p++) {
    const sfx_pattern *pat = &s->pat[p];
    int grid = pat->grid < SFX_GRIDS ? pat->grid : 0;
    /* G: the steps of this grid; B: the bytes their bits need */
    int steps = SFX_BAR * sfx_div(grid);
    int bit_bytes = (steps + 6) / 7;

    /* which sounds and which lanes have anything to keep here */
    uint32_t row_mask = 0;
    uint32_t lane_mask = 0;
    for (int i = 0; i < steps; i++) {
      row_mask |= pat->on[i];
      for (int t = 0; t < SFX_TRACKS; t++) {
        if ((pat->note[t][i] & 0x7F) || (pat->vel[t][i] & 7) != SFX_VEL_DEFAULT || pat->len[t][i]) {
          row_mask |= 1u << t;
        }
      }
      for (int l = 0; l < SFX_LANES; l++) {
        if (pat->aut[l][i] & 0x7F) {
          lane_mask |= 1u << l;
        }
      }
    }

    /* the block's 8 bytes: grid, the two masks */
    *o++ = (uint8_t)grid;
    *o++ = (uint8_t)(row_mask & 0x7F);
    *o++ = (uint8_t)((row_mask >> 7) & 0x7F);
    *o++ = (uint8_t)((row_mask >> 14) & 3);
    *o++ = (uint8_t)(lane_mask & 0x7F);
    *o++ = (uint8_t)((lane_mask >> 7) & 0x7F);
    *o++ = (uint8_t)((lane_mask >> 14) & 0x7F);
    *o++ = (uint8_t)((lane_mask >> 21) & 7);

    /* a row for each sound that has one: step bits, notes, velocities, lengths */
    for (int t = 0; t < SFX_TRACKS; t++) {
      if (!(row_mask & (1u << t))) {
        continue;
      }
      for (int b = 0; b < bit_bytes; b++) {
        unsigned bits = 0;
        for (int i = b * 7; i < b * 7 + 7 && i < steps; i++) {
          if (pat->on[i] & (1u << t)) {
            bits |= 1u << (i - b * 7);
          }
        }
        *o++ = (uint8_t)bits;
      }
      for (int i = 0; i < steps; i++) {
        *o++ = pat->note[t][i] & 0x7F;
      }
      for (int i = 0; i < steps; i++) {
        *o++ = pat->vel[t][i] & 7;
      }
      for (int i = 0; i < steps; i++) {
        *o++ = (uint8_t)(pat->len[t][i] > steps - 1 ? steps - 1 : pat->len[t][i]);
      }
    }

    /* a row for each lane that has one: its points */
    for (int l = 0; l < SFX_LANES; l++) {
      if (lane_mask & (1u << l)) {
        for (int i = 0; i < steps; i++) {
          *o++ = pat->aut[l][i] & 0x7F;
        }
      }
    }
  }
  return (int)(o - out);
}

/* The head's values as formats 5 and 6 have them (and the earlier ones their part of it): 0 = in range, -1 = not.
   `in` are a packed song's bytes, long enough for the head of format `ver` (sfx_song_unpack has seen to that). */
static inline int sfx__head_ok(const uint8_t *in, int ver) {
  /* bytes 1 to 6 (the table of section 4): the bar, the tempo in two groups, swing, scale, chain */
  int bpm = in[2] << 7 | in[3];
  // clang-format off
  if (in[1] < 1 || in[1] > SFX_BAR ||     /* the bar */
      bpm < 40 || bpm > 240 ||            /* the tempo */
      in[4] > 100 ||                      /* swing */
      in[5] >= SFX_SCALE_COUNT ||         /* scale */
      in[6] < 1 || in[6] > SFX_PATTERNS   /* chain */) {
    return -1;
  }
  // clang-format on
  /* the mixer: the delay's time is an index */
  if (ver >= 2 && in[7 + SFX_TRACKS * 3 + 3 + SFX_M_DELAY_TIME] >= SFX_DELAY_TIMES) {
    return -1;
  }
  if (ver >= 4) {
    /* the arrangement (format 4 has its 16 lane bytes before it) */
    const uint8_t *arr = in + SFX_SONG_HEAD_V2 + (ver == 4 ? SFX_TRACKS : 0);
    if (arr[0] > SFX_ARR_MAX) {
      return -1;
    }
    for (int i = 0; i < SFX_ARR_MAX; i++) {
      if (arr[1 + i] >= SFX_PATTERNS) {
        return -1;
      }
    }
    /* the lanes: format 4 one knob a sound; from 5 on a sound and a knob for each of the 24 */
    if (ver == 4) {
      for (int t = 0; t < SFX_TRACKS; t++) {
        if (in[SFX_SONG_HEAD_V2 + t] >= SFX_AT_COUNT) {
          return -1;
        }
      }
    } else {
      for (int l = 0; l < SFX_LANES; l++) {
        if (arr[1 + SFX_ARR_MAX + l * 2] > SFX_TRACKS || arr[2 + SFX_ARR_MAX + l * 2] >= SFX_AT_COUNT) {
          return -1;
        }
      }
    }
  }
  return 0;
}

/* Reads the `n` bytes `in` into song `s`.
   0 = taken, -1 = not a song (length, version, a value out of range) – *s is untouched then. Takes the formats before
   this one too: 1 (no mixer), 2 (no velocity, no length), 3 (no automation, no arrangement), 4 (one lane a sound),
   5 (16 steps a pattern, every row written): their patterns come in at grid 16.
   It checks everything first and writes after, and reads no byte past in[n - 1]. s->solo is cleared. */
static inline int sfx_song_unpack(sfx_song *s, const uint8_t *in, int n) {
  /* ---- which format: the first byte, and the size that format has ---- */
  if (n < 7) {
    return -1;
  }
  /* format 6 has no fixed size; each of the others has exactly one. 0: none of them */
  // clang-format off
  int ver = in[0] == 6 && n >= SFX_SONG_MIN && n <= SFX_SONG_MAX ? 6
          : n == SFX_SONG_BYTES_V5 && in[0] == 5                 ? 5
          : n == SFX_SONG_BYTES_V4 && in[0] == 4                 ? 4
          : n == SFX_SONG_BYTES_V3 && in[0] == 3                 ? 3
          : n == SFX_SONG_BYTES_V2 && in[0] == 2                 ? 2
          : n == SFX_SONG_BYTES_V1 && in[0] == 1                 ? 1
          :                                                        0;
  // clang-format on
  if (!ver) {
    return -1;
  }

  /* ---- the checks, before anything is written ---- */
  for (int i = 0; i < n; i++) {
    if (in[i] > 127) {
      return -1;
    }
  }
  if (sfx__head_ok(in, ver)) {
    return -1;
  }
  if (ver == 6) {
    /* the blocks: every one inside the bytes, every value in range */
    int at = SFX_SONG_HEAD;
    for (int p = 0; p < SFX_PATTERNS; p++) {
      if (at + 8 > n || in[at] >= SFX_GRIDS || in[at + 3] > 3 || in[at + 7] > 7) {
        return -1;
      }
      int steps = SFX_BAR * sfx_div(in[at]);
      int bit_bytes = (steps + 6) / 7;
      int rows = 0;
      int lanes = 0;
      uint32_t row_mask = (uint32_t)in[at + 1] | (uint32_t)in[at + 2] << 7 | (uint32_t)in[at + 3] << 14;
      uint32_t lane_mask =
          (uint32_t)in[at + 4] | (uint32_t)in[at + 5] << 7 | (uint32_t)in[at + 6] << 14 | (uint32_t)in[at + 7] << 21;
      for (uint32_t m = row_mask; m; m &= m - 1) {
        rows++;
      }
      for (uint32_t m = lane_mask; m; m &= m - 1) {
        lanes++;
      }
      int size = 8 + rows * (bit_bytes + 3 * steps) + lanes * steps;
      if (at + size > n) {
        return -1;
      }
      /* row by row (the loop steps `row` on to the next one).
         Kept in this form - two things stepped in the loop's head: the plainer one compiled to other instructions
         in the DLL (docs/MAINTAINING.md, "Fragile spots") */
      const uint8_t *row = in + at + 8;
      for (int k = 0; k < rows; k++, row += bit_bytes + 3 * steps) {
        /* a step bit past the grid's last step (bit G or higher) */
        if (row[bit_bytes - 1] >> (steps - (bit_bytes - 1) * 7)) {
          return -1;
        }
        /* a velocity above 7, a length above G - 1 */
        for (int i = 0; i < steps; i++) {
          if (row[bit_bytes + steps + i] > 7 || row[bit_bytes + 2 * steps + i] > steps - 1) {
            return -1;
          }
        }
      }
      at += size;
    }
    if (at != n) {
      return -1;
    }
  }

  /* ---- the head. First an empty song with the mixer of a new one: what an old format lacks stays so. ---- */
  const uint8_t *src = in + 7;
  /* format 4: what each sound's one lane moved */
  uint8_t v4_target[SFX_TRACKS];
  s->len = in[1];
  s->bpm = (uint16_t)(in[2] << 7 | in[3]);
  s->swing = in[4];
  s->scale = in[5];
  s->chain = in[6];
  for (int t = 0; t < SFX_TRACKS; t++) {
    s->level[t] = *src++;
    v4_target[t] = 0;
  }
  for (int l = 0; l < SFX_LANES; l++) {
    s->lane[l].to = 0;
    s->lane[l].track = 0;
  }
  for (int p = 0; p < SFX_PATTERNS; p++) {
    sfx_pattern_clear(&s->pat[p]);
    s->pat[p].grid = 0;
  }
  /* no arrangement: a format 1 to 3 has none, and sfx_song_pack writes all 32 bars whatever arr_len says */
  s->arr_len = 0;
  for (int i = 0; i < SFX_ARR_MAX; i++) {
    s->arr[i] = 0;
  }
  sfx_song_mix_init(s);

  /* from format 2: the mixer */
  if (ver >= 2) {
    for (int t = 0; t < SFX_TRACKS; t++) {
      s->vol[t] = *src++;
    }
    for (int t = 0; t < SFX_TRACKS; t++) {
      s->pan[t] = *src++;
    }
    s->mute = (uint16_t)((unsigned)src[0] | (unsigned)src[1] << 7 | (unsigned)(src[2] & 3) << 14);
    src += 3;
    for (int i = 0; i < SFX_M_COUNT; i++) {
      s->fx[i] = *src++;
    }
  }
  /* format 4 only: its 16 lane bytes */
  if (ver == 4) {
    for (int t = 0; t < SFX_TRACKS; t++) {
      v4_target[t] = *src++;
    }
  }
  /* from format 4: the arrangement */
  if (ver >= 4) {
    s->arr_len = *src++;
    for (int i = 0; i < SFX_ARR_MAX; i++) {
      s->arr[i] = *src++;
    }
  }
  /* from format 5: the 24 lanes (a lane that moves nothing is free) */
  if (ver >= 5) {
    for (int l = 0; l < SFX_LANES; l++) {
      s->lane[l].track = *src++;
      s->lane[l].to = *src++;
      if (!s->lane[l].to) {
        s->lane[l].track = 0;
      }
    }
  }

  /* ---- the patterns of format 6: a block each ---- */
  if (ver == 6) {
    for (int p = 0; p < SFX_PATTERNS; p++) {
      sfx_pattern *pat = &s->pat[p];
      int steps = SFX_BAR * sfx_div(src[0]);
      int bit_bytes = (steps + 6) / 7;
      uint32_t row_mask = (uint32_t)src[1] | (uint32_t)src[2] << 7 | (uint32_t)src[3] << 14;
      uint32_t lane_mask = (uint32_t)src[4] | (uint32_t)src[5] << 7 | (uint32_t)src[6] << 14 | (uint32_t)src[7] << 21;
      pat->grid = src[0];
      src += 8;
      for (int t = 0; t < SFX_TRACKS; t++) {
        if (!(row_mask & (1u << t))) {
          continue;
        }
        for (int i = 0; i < steps; i++) {
          if (src[i / 7] & (1u << (i % 7))) {
            pat->on[i] |= (uint16_t)(1u << t);
          }
          pat->note[t][i] = src[bit_bytes + i];
          pat->vel[t][i] = src[bit_bytes + steps + i];
          pat->len[t][i] = src[bit_bytes + 2 * steps + i];
        }
        src += bit_bytes + 3 * steps;
      }
      for (int l = 0; l < SFX_LANES; l++) {
        if (lane_mask & (1u << l)) {
          for (int i = 0; i < steps; i++) {
            pat->aut[l][i] = src[i];
          }
          src += steps;
        }
      }
    }
    return 0;
  }

  /* ---- the patterns of the formats 1 to 5: a row of 16 steps for every sound of every pattern ---- */
  for (int p = 0; p < SFX_PATTERNS; p++) {
    for (int t = 0; t < SFX_TRACKS; t++) {
      unsigned bits = (unsigned)src[0] | (unsigned)src[1] << 7 | (unsigned)(src[2] & 3) << 14;
      src += 3;
      for (int i = 0; i < SFX_BAR; i++) {
        if (bits & (1u << i)) {
          s->pat[p].on[i] |= (uint16_t)(1u << t);
        }
        s->pat[p].note[t][i] = *src++;
      }
      /* from format 3: an accent byte a step */
      if (ver >= 3) {
        for (int i = 0; i < SFX_BAR; i++) {
          uint8_t a = *src++;
          s->pat[p].vel[t][i] = SFX_ACC_VEL(a);
          s->pat[p].len[t][i] = SFX_ACC_LEN(a);
        }
      }
      /* format 4: the points of this sound's lane (sound t's lane becomes lane t) */
      if (ver == 4) {
        for (int i = 0; i < SFX_BAR; i++) {
          s->pat[p].aut[t][i] = *src++;
        }
      }
    }
  }
  /* format 4: a sound whose lane moved something has lane t now */
  if (ver == 4) {
    for (int t = 0; t < SFX_TRACKS; t++) {
      if (v4_target[t]) {
        s->lane[t].track = (uint8_t)(t + 1);
        s->lane[t].to = v4_target[t];
      }
    }
  }
  /* format 5: after all the rows, the points of the 24 lanes, pattern by pattern */
  if (ver >= 5) {
    for (int p = 0; p < SFX_PATTERNS; p++) {
      for (int l = 0; l < SFX_LANES; l++) {
        for (int i = 0; i < SFX_BAR; i++) {
          s->pat[p].aut[l][i] = *src++;
        }
      }
    }
  }
  return 0;
}

/* ==== 5. Automation: one question, asked wherever a knob's value is used ===========================================
   A lane moves one knob: `to` (SFX_AT_*) of sound `track - 1`. A song has SFX_LANES of them, so a sound may have
   several – one for each knob that moves. now[l] is the point lane l stands on (0 = none: sfx_pattern.aut, held from
   its step on). */

/* The value of knob `target` – for sound `track`, or the master's (track < 0: any lane may move those) – or `own`,
   the knob's own value, where no lane moves it. Nothing is written anywhere: the song and the sounds stay as they
   were made. */
static inline int sfx_auto(const sfx_song *s, const uint8_t *now, int target, int track, int own) {
  for (int l = 0; l < SFX_LANES; l++) {
    if (now[l] && s->lane[l].to == target && s->lane[l].track && (track < 0 || s->lane[l].track == track + 1)) {
      return now[l] - 1;
    }
  }
  return own;
}

/* The sound of a track as its lanes have it now: `out` if they move any of its values, else the sound itself (`in`).
   `out` is the caller's room for the moved copy; it is only written when a lane moves a value. */
static inline const sfx_patch *sfx_auto_patch(const sfx_song *s, const uint8_t *now, int track, const sfx_patch *in,
                                              sfx_patch *out) {
  const sfx_patch *result = in;
  for (int l = 0; l < SFX_LANES; l++) {
    int to = s->lane[l].to;
    if (!now[l] || s->lane[l].track != track + 1 || to < SFX_AT_PATCH || to >= SFX_AT_VOL) {
      continue;
    }
    /* the first lane that moves a value: from here on the copy */
    if (result == in) {
      *out = *in;
      result = out;
    }
    out->p[to - SFX_AT_PATCH] = (uint8_t)(now[l] - 1);
  }
  return result;
}

/* ==== 6. The effects ===============================================================================================
   One chorus, one delay, one reverb for the whole player. Every sound has a send to each. */

#define SFX_BLOCK      256   /* the player works in pieces of at most this many samples */
#define SFX_CHORUS_LEN 4096  /* (a power of two: its index wraps with a mask) */
#define SFX_DELAY_LEN  48000 /* up to half a second at 96 kHz */
#define SFX_COMB_LEN   4096
#define SFX_AP_LEN     1024

/* The effects' memory. The first index of the delay, the combs and the all-passes is the side: 0 left, 1 right. */
typedef struct {
  float sr;

  /* chorus: its line, where it writes, the phase of its slow sine (turns) */
  float cho[SFX_CHORUS_LEN];
  int cho_w;
  float cho_ph;

  /* delay: a line a side, where they write */
  float dl[2][SFX_DELAY_LEN];
  int dl_w;

  /* reverb: four combs a side – their lines, the low-pass in each one's loop, their lengths, where they write */
  float comb[2][4][SFX_COMB_LEN];
  float comb_lp[2][4];
  int comb_len[2][4];
  int comb_w[2][4];

  /* reverb: two all-passes a side – their lines, their lengths, where they write */
  float ap[2][2][SFX_AP_LEN];
  int ap_len[2][2];
  int ap_w[2][2];
} sfx_fx;

/* Silence in all the lines, and the reverb's lengths for sample rate `sr`. It writes about 550 KB: at start-up, not
   in an audio callback. */
static inline void sfx_fx_init(sfx_fx *f, float sr) {
  /* the lengths at 44.1 kHz; the right side 23 longer */
  static const int COMB[4] = {1116, 1188, 1277, 1356};
  static const int AP[2] = {556, 441};
  f->sr = sr;
  f->cho_w = 0;
  f->cho_ph = 0.0f;
  f->dl_w = 0;
  for (int i = 0; i < SFX_CHORUS_LEN; i++) {
    f->cho[i] = 0.0f;
  }
  for (int c = 0; c < 2; c++) {
    for (int i = 0; i < SFX_DELAY_LEN; i++) {
      f->dl[c][i] = 0.0f;
    }
    for (int k = 0; k < 4; k++) {
      int n = (int)((float)(COMB[k] + c * 23) * sr / 44100.0f);
      f->comb_len[c][k] = n < 8 ? 8 : n > SFX_COMB_LEN ? SFX_COMB_LEN : n;
      f->comb_w[c][k] = 0;
      f->comb_lp[c][k] = 0.0f;
      for (int i = 0; i < SFX_COMB_LEN; i++) {
        f->comb[c][k][i] = 0.0f;
      }
    }
    for (int k = 0; k < 2; k++) {
      int n = (int)((float)(AP[k] + c * 23) * sr / 44100.0f);
      f->ap_len[c][k] = n < 8 ? 8 : n > SFX_AP_LEN ? SFX_AP_LEN : n;
      f->ap_w[c][k] = 0;
      for (int i = 0; i < SFX_AP_LEN; i++) {
        f->ap[c][k][i] = 0.0f;
      }
    }
  }
}

/* The three sends in (mono: in_c chorus, in_d delay, in_r reverb; n samples each), their sound ADDED to l and r.
   delay_seconds: the echo's time; m: the master's values (SFX_M_*: feedback, the room's size and damping). */
static inline void sfx_fx_process(sfx_fx *f, const float *in_c, const float *in_d, const float *in_r, float *l,
                                  float *r, int n, float delay_seconds, const uint8_t *m) {
  /* the master's knobs as factors */
  float fb = (float)(m[SFX_M_DELAY_FB] & 127) * (0.45f / 64.0f);
  float room = 0.70f + (float)(m[SFX_M_REVERB_SIZE] & 127) * (0.20f / 91.0f);
  float damp = (float)(m[SFX_M_REVERB_DAMP] & 127) * (0.25f / 40.0f);
  if (room > 0.97f) {
    room = 0.97f;
  }

  /* the echo's time in samples: halved until the line holds it */
  int dlen = (int)(delay_seconds * f->sr);
  while (dlen >= SFX_DELAY_LEN) {
    dlen /= 2;
  }
  if (dlen < 16) {
    dlen = 16;
  }

  /* chorus: 0.6 Hz, 12 ms +/- 4 ms (less where the line is too short for that) */
  float cstep = 0.6f / f->sr;
  float cbase = 0.012f * f->sr;
  float cdepth = 0.004f * f->sr;
  if (cbase + cdepth > (float)(SFX_CHORUS_LEN - 4)) {
    cbase = (float)(SFX_CHORUS_LEN / 2);
    cdepth = cbase * 0.3f;
  }

  for (int i = 0; i < n; i++) {
    /* chorus: two taps that drift against each other */
    f->cho[f->cho_w] = in_c[i];
    for (int c = 0; c < 2; c++) {
      float d = cbase + cdepth * SFX_SIN(f->cho_ph + (float)c * 0.25f);
      float at = (float)f->cho_w - d;
      if (at < 0.0f) {
        at += (float)SFX_CHORUS_LEN;
      }
      int i0 = (int)at;
      float fr = at - (float)i0;
      float a = f->cho[i0 & (SFX_CHORUS_LEN - 1)];
      float b = f->cho[(i0 + 1) & (SFX_CHORUS_LEN - 1)];
      (c ? r : l)[i] += (a + (b - a) * fr) * 0.8f;
    }
    f->cho_w = (f->cho_w + 1) & (SFX_CHORUS_LEN - 1);
    f->cho_ph += cstep;
    if (f->cho_ph >= 1.0f) {
      f->cho_ph -= 1.0f;
    }

    /* delay: left, then right, then left … */
    int rd = f->dl_w - dlen;
    if (rd < 0) {
      rd += SFX_DELAY_LEN;
    }
    float dl = f->dl[0][rd];
    float dr = f->dl[1][rd];
    f->dl[0][f->dl_w] = in_d[i] + dr * fb + 1e-18f;
    f->dl[1][f->dl_w] = dl;
    f->dl_w++;
    if (f->dl_w >= SFX_DELAY_LEN) {
      f->dl_w = 0;
    }
    l[i] += dl * 0.7f;
    r[i] += dr * 0.7f;

    /* reverb: four damped combs and two all-passes a side */
    for (int c = 0; c < 2; c++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        float *buf = f->comb[c][k];
        int w = f->comb_w[c][k];
        float y = buf[w];
        f->comb_lp[c][k] = y * (1.0f - damp) + f->comb_lp[c][k] * damp + 1e-18f;
        buf[w] = in_r[i] * 0.25f + f->comb_lp[c][k] * room;
        if (++w >= f->comb_len[c][k]) {
          w = 0;
        }
        f->comb_w[c][k] = w;
        sum += y;
      }
      for (int k = 0; k < 2; k++) {
        float *buf = f->ap[c][k];
        int w = f->ap_w[c][k];
        float y = buf[w];
        buf[w] = sum + y * 0.5f;
        sum = y - sum * 0.5f;
        if (++w >= f->ap_len[c][k]) {
          w = 0;
        }
        f->ap_w[c][k] = w;
      }
      (c ? r : l)[i] += sum * 0.7f;
    }
  }
}

/* ==== 7. The player ================================================================================================
   16 sounds (patches), 16 voices that play them, the effects, a song and the sequencer that plays it.
   A host: sfx_player_init once; sfx_player_set_patch for each sound; a song into p->song (sfx_song_unpack);
   then sfx_player_render for every block of audio, and between blocks sfx_player_start / _stop / _cue / _set_loop
   for the sequencer and sfx_player_note_on / _note_off for sounds by hand. Nothing here locks: a host whose audio
   runs in a thread of its own must itself keep these calls from running in the middle of sfx_player_render (the
   firmware hands them to its audio flow as messages; see also the note at sfx_player.song). */

#define SFX_PLAYER_VOICES 16
/* voices the sequencer started: key 200 + sound */
#define SFX_KEY_SEQ 200

typedef struct {
  float sr;

  /* the voices: what plays, the key that lets go of each (-1 = none), and when each started (the oldest is taken
     when all are busy) */
  sfx_voice voice[SFX_PLAYER_VOICES];
  int16_t key[SFX_PLAYER_VOICES];
  uint32_t age[SFX_PLAYER_VOICES];
  /* the counter `age` is taken from: one more with every voice that starts */
  uint32_t ages;
  /* the noise seed the last voice got; the next gets another (sfx_player_note_on) */
  uint32_t seed;

  /* the 16 sounds */
  sfx_patch patch[SFX_TRACKS];

  /* The song. (A host with two threads: this is the song of the one that renders. Single values the other may write
     here as they are. A whole pattern or song (a grid change, a copy, an undo, a song that comes in) it rewrites in
     a song of its own and hands over between two calls of sfx_player_render - the firmware: sfx-forge/song_way.h.
     Rewritten here while this plays, a step that begins in those microseconds may play what is half moved; no index
     leaves the arrays.) */
  sfx_song song;

  /* what the game moves */
  sfx_mods mods;
  float intensity; /* 0..1 */

  /* the sequencer */
  int running;              /* 1 while the sequencer runs */
  int pattern;              /* the pattern that plays */
  int cue;                  /* the pattern to change to on the next bar line, -1 = none */
  uint8_t gate[SFX_TRACKS]; /* sixty-fourths a sequenced note still holds (0: no length given) */
  uint8_t div;              /* the steps a sixteenth in which `step` is counted: 1, 2, 4 */
  uint8_t aut[SFX_LANES];   /* where each automation lane stands (sfx_auto) */
  int arr_pos;              /* the bar of the arrangement */
  int hold;                 /* a cued pattern outside the arrangement loops until the next cue */
  /* 1: LOOP - the pattern that plays repeats; a cue changes which one, and that one repeats
     (sfx_player_set_loop). 0: the arrangement, else the chain */
  int loop;
  double pos; /* the clock: sixteenths since the start */

  /* where it is: the step in the grid of the pattern that plays (-1 = not yet), the bar, how far into the step */
  int step;
  int bar;
  float frac;

  /* the mix of one block */
  int tail; /* samples the effects may still ring after the last voice */
  int wet;  /* 1: a voice of this block sent to the effects */
  float dry[2][SFX_BLOCK];
  float bus_c[SFX_BLOCK];
  float bus_d[SFX_BLOCK];
  float bus_r[SFX_BLOCK];
  float one[SFX_BLOCK];
  sfx_fx fx;
} sfx_player;

/* A player at sample rate `sr`: silent, stopped, 16 plain sounds, an empty song, intensity 1. It fills the
   wavetables (sfx_wt_init) and clears about 550 KB of effect memory: call it at start-up, not in an audio callback. */
static inline void sfx_player_init(sfx_player *p, float sr) {
  sfx_wt_init();
  p->sr = sr;
  p->ages = 0;
  p->seed = 0x1234567u;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    p->voice[i].active = 0;
    p->key[i] = -1;
    p->age[i] = 0;
  }
  for (int t = 0; t < SFX_TRACKS; t++) {
    sfx_patch_init(&p->patch[t], 0);
  }
  sfx_song_init(&p->song);
  p->mods.fm = 0.0f;
  p->mods.cutoff_oct = 0.0f;
  p->mods.pitch_st = 0.0f;
  p->mods.level = 1.0f;
  for (int t = 0; t < SFX_TRACKS; t++) {
    p->gate[t] = 0;
  }
  for (int l = 0; l < SFX_LANES; l++) {
    p->aut[l] = 0;
  }
  p->arr_pos = 0;
  p->hold = 0;
  p->loop = 0;
  p->intensity = 1.0f;
  p->running = 0;
  p->pattern = 0;
  p->cue = -1;
  p->pos = 0.0;
  p->step = -1;
  p->bar = -1;
  p->frac = 0.0f;
  p->tail = 0;
  p->wet = 0;
  p->div = 1;
  sfx_fx_init(&p->fx, sr);
}

/* A sound starts. key: what lets go of it again (a pad, a note); midi_note 0 = as designed; patch NULL = the sound's
   own. slot 0..15: one of the song's sounds, through its fader, pan and mute. slot 16..31: a sound beside the song
   (a game's sound effect, with its patch given) – straight to the sum, whatever the mixer says.
   vel 0..1; note_st transposes in semitones. A voice that still holds the same key is let go first. */
static inline void sfx_player_note_on(sfx_player *p, int slot, int key, float vel, int midi_note, float note_st,
                                      const sfx_patch *patch) {
  int pick = 0;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    if (p->voice[i].active && p->key[i] == key && p->voice[i].gate) {
      sfx_voice_release(&p->voice[i]);
    }
  }
  /* the voice to take: the first that is free, else the one that started longest ago */
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    if (!p->voice[i].active) {
      pick = i;
      break;
    }
    if (p->age[i] < p->age[pick]) {
      pick = i;
    }
  }
  /* (every voice gets a seed of its own for its noise: the last argument moves p->seed on and hands it over) */
  sfx_voice_trigger(&p->voice[pick], patch ? patch : &p->patch[slot & 15], vel, note_st, slot & 31, p->sr,
                    p->seed += 0x9E3779B9u);
  if (midi_note > 0) {
    sfx_voice_set_note(&p->voice[pick], midi_note);
  }
  p->key[pick] = (int16_t)key;
  p->age[pick] = ++p->ages;
}

/* the key is let go: every voice started with it begins its release (a one-shot plays on to its end) */
static inline void sfx_player_note_off(sfx_player *p, int key) {
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    if (p->voice[i].active && p->key[i] == key) {
      sfx_voice_release(&p->voice[i]);
    }
  }
}

/* A sound changed: for the next time, and for the voices that play it now. slot as for sfx_player_note_on (a slot
   from 16 on is not kept, it only reaches the voices that play it). */
static inline void sfx_player_set_patch(sfx_player *p, int slot, const sfx_patch *patch) {
  if ((slot & 31) < SFX_TRACKS) {
    p->patch[slot & 15] = *patch;
  }
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    if (p->voice[i].active && p->voice[i].slot == (slot & 31)) {
      sfx_voice_set_patch(&p->voice[i], patch);
    }
  }
}

/* the sequencer's notes are let go: all of them, or (a bar line) only those without a length of their own */
static inline void sfx__seq_release(sfx_player *p, int all) {
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    if (p->voice[i].active && p->key[i] >= SFX_KEY_SEQ && (all || !p->gate[(p->key[i] - SFX_KEY_SEQ) & 15])) {
      sfx_voice_release(&p->voice[i]);
    }
  }
  if (all) {
    for (int t = 0; t < SFX_TRACKS; t++) {
      p->gate[t] = 0;
    }
  }
}

/* The sequencer starts, from the top of a bar. pattern 0..7, or -1: the pattern it is on. */
static inline void sfx_player_start(sfx_player *p, int pattern) {
  p->running = 1;
  p->pos = 0.0;
  p->step = -1;
  p->bar = -1;
  p->cue = -1;
  p->div = 1;
  for (int t = 0; t < SFX_TRACKS; t++) {
    p->gate[t] = 0;
  }
  for (int l = 0; l < SFX_LANES; l++) {
    p->aut[l] = 0;
  }
  if (pattern >= 0) {
    p->pattern = pattern % SFX_PATTERNS;
  }
  /* an arrangement: from its first bar with this pattern, else from the top after it */
  p->arr_pos = -1;
  p->hold = 0;
  for (int i = 0; i < p->song.arr_len && i < SFX_ARR_MAX; i++) {
    if (p->song.arr[i] == p->pattern) {
      p->arr_pos = i;
      break;
    }
  }
}

/* The sequencer stops: its notes are let go (they ring out), the lanes let go of their knobs. */
static inline void sfx_player_stop(sfx_player *p) {
  if (p->running) {
    sfx__seq_release(p, 1);
  }
  for (int l = 0; l < SFX_LANES; l++) {
    p->aut[l] = 0;
  }
  p->running = 0;
  p->step = -1;
  p->bar = -1;
  p->div = 1;
}

/* change to this pattern on the next bar line (at once while stopped); a pattern outside 0..7 is ignored */
static inline void sfx_player_cue(sfx_player *p, int pattern) {
  if (pattern < 0 || pattern >= SFX_PATTERNS) {
    return;
  }
  if (p->running) {
    p->cue = pattern;
  } else {
    p->pattern = pattern;
  }
}

/* 1 = LOOP: the pattern that plays repeats (a cue changes which one, and LOOP stays on); 0 = arrangement or chain.
   Leaving it: on in the arrangement from its first bar with the pattern that plays, else from the top - as
   sfx_player_start does. */
static inline void sfx_player_set_loop(sfx_player *p, int on) {
  on = on != 0;
  if (p->loop && !on) {
    p->arr_pos = -1;
    p->hold = 0;
    for (int i = 0; i < p->song.arr_len && i < SFX_ARR_MAX; i++) {
      if (p->song.arr[i] == p->pattern) {
        p->arr_pos = i;
        break;
      }
    }
  }
  p->loop = on;
}

/* The voices' samples from..to of this block, each through its sound's fader and pan to the dry mix and to its three
   sends (p->dry, p->bus_c, p->bus_d, p->bus_r). Called by sfx_player_render, once for every stretch of a block
   between two steps. */
static inline void sfx__render(sfx_player *p, int from, int to) {
  const sfx_song *s = &p->song;
  int n = to - from;
  if (n <= 0) {
    return;
  }
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    sfx_voice *v = &p->voice[i];
    if (!v->active) {
      continue;
    }
    /* the voice by itself into p->one; its three sends */
    float send_chorus = sfx__unit(v->patch.p[SFX_P_CHORUS]);
    float send_delay = sfx__unit(v->patch.p[SFX_P_DELAY]);
    float send_reverb = sfx__unit(v->patch.p[SFX_P_REVERB]);
    for (int k = 0; k < n; k++) {
      p->one[k] = 0.0f;
    }
    if (send_chorus > 0.0f || send_delay > 0.0f || send_reverb > 0.0f) {
      p->wet = 1;
    }
    sfx_voice_render(v, &p->mods, p->one, n);

    /* the mixer. A sound beside the song (slot 16..31) goes past it; a muted one plays on, unheard */
    int t = v->slot & 15;
    int beside = v->slot >= SFX_TRACKS;
    if (!beside && ((s->mute >> t) & 1 || (s->solo && !((s->solo >> t) & 1)))) {
      continue;
    }
    int vol = beside ? 100 : sfx_auto(s, p->aut, SFX_AT_VOL, t, s->vol[t] & 127);
    int pan = beside ? 64 : sfx_auto(s, p->aut, SFX_AT_PAN, t, s->pan[t] & 127);
    /* the fader as a factor; the pan 0..1. In the middle both sides are full, to a side the other one falls */
    float fader = (float)vol * 0.01f;
    float pan01 = (float)pan * (1.0f / 127.0f);
    float gain_left = pan == 64 ? fader : fader * (pan01 < 0.5f ? 1.0f : 2.0f - 2.0f * pan01);
    float gain_right = pan == 64 ? fader : fader * (pan01 > 0.5f ? 1.0f : 2.0f * pan01);
    for (int k = 0; k < n; k++) {
      float x = p->one[k];
      p->dry[0][from + k] += x * gain_left;
      p->dry[1][from + k] += x * gain_right;
      /* the sends: after the fader, before the pan */
      x *= fader;
      p->bus_c[from + k] += x * send_chorus;
      p->bus_d[from + k] += x * send_delay * 0.7f;
      p->bus_r[from + k] += x * send_reverb;
    }
  }
}

/* The step at sixteenth `st`, fraction `fr` of it, for a pattern with `div` steps a sixteenth in a bar of
   `bar_sixteenths` sixteenths: swing (0..0.5) stretches the even sixteenth of a pair and squeezes the odd one, and the
   steps inside go with their sixteenth. *frac: how far into that step. */
static inline int sfx__sub(int st, float fr, float swing, int div, int bar_sixteenths, float *frac) {
  /* the sixteenth that sounds now, and q: how far into it (0..1) */
  int idx = st;
  double q = (double)fr;
  double sw = (double)swing;
  if (st & 1) {
    if (fr < swing) {
      /* the odd sixteenth has not begun: this is still the stretched even one before it */
      idx = st - 1;
      q = (1.0 + q) / (1.0 + sw);
    } else {
      q = (q - sw) / (1.0 - sw);
    }
  } else if (st + 1 < bar_sixteenths) {
    /* an even sixteenth with an odd one after it: it lasts 1 + swing */
    q = q / (1.0 + sw);
  }
  /* which of the sixteenth's `div` steps, and how far into that */
  q *= (double)div;
  int j = (int)q;
  if (j > div - 1) {
    j = div - 1;
  }
  if (j < 0) {
    j = 0;
  }
  *frac = (float)(q - (double)j);
  if (*frac > 1.0f) {
    *frac = 1.0f;
  }
  return idx * div + j;
}

/* ADDS `total` stereo samples to l and r; every voice starts on the very sample of its step.
   The clock counts sixteenths; a pattern with a finer grid has 2 or 4 steps in each (a pattern that was only made
   finer plays the same samples as before: its steps begin where they began).
   host_pos < 0: its own clock at the song's tempo. Else the host leads (a DAW): host_pos = its position in
   sixteenths (quarter notes x 4) at the first sample, host_step = sixteenths per sample.
   Returns the sounds that play now, a bit each (a sound beside the song, slot 16..31, shows as bit 0..15 too: its
   slot & 15); bit 31 while only the effects still ring.

   This is the audio thread's function: it never allocates, locks or waits, and its time is bounded by `total`.
   How it works: the block is cut in pieces of at most SFX_BLOCK samples. In a piece, the clock is looked at sample
   by sample; where a step begins, the voices are rendered up to that sample (sfx__render), then the step is worked:
   notes whose length is over are let go, a bar line chooses the pattern, the lanes' points move their knobs, the
   step's notes start. After the last step of the piece the voices are rendered to its end, the effects are added,
   and the piece goes to l and r. */
static inline uint32_t sfx_player_render(sfx_player *p, float *l, float *r, int total, double host_pos,
                                         double host_step) {
  const sfx_song *s = &p->song;
  int host = host_pos >= 0.0;
  /* the bar in sixteenths */
  int bar_sixteenths = s->len >= 1 && s->len <= SFX_BAR ? s->len : SFX_BAR;
  /* sixteenths per sample. (Kept as one expression: written in steps it compiled to other instructions in the DLL -
     docs/MAINTAINING.md, "Fragile spots") */
  double per = host ? host_step : (double)s->bpm / 60.0 * 4.0 / (double)p->sr;
  double bar_len = (double)bar_sixteenths;
  static const uint8_t SIXTEENTHS[SFX_DELAY_TIMES] = {1, 2, 3, 4, 6, 8};
  /* how much of a sixteenth the off-beat one waits: 0..0.5 */
  float swing = (float)s->swing * 0.005f;
  uint8_t fx[SFX_M_COUNT];
  uint32_t on = 0;

  for (int done = 0; done < total;) {
    /* ---- a piece of n samples: empty buses ---- */
    int n = total - done < SFX_BLOCK ? total - done : SFX_BLOCK;
    int from = 0;
    for (int k = 0; k < n; k++) {
      p->bus_r[k] = 0.0f;
      p->bus_d[k] = 0.0f;
      p->bus_c[k] = 0.0f;
      p->dry[1][k] = 0.0f;
      p->dry[0][k] = 0.0f;
    }
    p->wet = 0;

    /* the master as the lanes have it now: the echo's time in seconds, the drive, how long the effects ring */
    for (int i = 0; i < SFX_M_COUNT; i++) {
      fx[i] = (uint8_t)sfx_auto(s, p->aut, SFX_AT_MASTER + i, -1, s->fx[i]);
    }
    float echo =
        0.25f * (float)SIXTEENTHS[fx[SFX_M_DELAY_TIME] % SFX_DELAY_TIMES] * 60.0f / (float)(s->bpm < 40 ? 40 : s->bpm);
    float drive = (float)(fx[SFX_M_DRIVE] & 127) * (1.0f / 127.0f);
    float ring = fx[SFX_M_REVERB_SIZE] > 91 || fx[SFX_M_DELAY_FB] > 64 ? 10.0f : 4.0f;

    /* ---- the sequencer: sample by sample, does a step begin here? ---- */
    if (p->running) {
      if (host) {
        p->pos = host_pos + (double)done * per;
      }
      for (int k = 0; k < n; k++) {
        /* where the clock is on this sample: the bar, the sixteenth `st` in it, the fraction `fr` of that */
        double at = p->pos + (double)k * per;
        if (at < 0.0) {
          continue;
        }
        long long bar = (long long)(at / bar_len);
        double in_bar = at - bar_len * (double)bar;
        int st = (int)in_bar;
        float fr = (float)(in_bar - (double)st);
        p->frac = fr;
        int newbar = (int)bar != p->bar;
        int sub = st;
        int div = sfx_div(s->pat[p->pattern % SFX_PATTERNS].grid);

        /* The grid changed under the playhead: it stands on the step of the new grid that is on now, and no step
           fires for that. */
        if (!newbar && div != p->div) {
          if (p->step >= 0) {
            /* now: the step of the new grid that is on at this sample (unused_frac: sfx__sub wants a place for the
               fraction, which is not needed here) */
            int now = st;
            float unused_frac;
            if (div > 1) {
              now = sfx__sub(st, fr, swing, div, bar_sixteenths, &unused_frac);
            } else if ((st & 1) && fr < swing) {
              /* (swing: the off-beat sixteenth has not begun) */
              now = st - 1;
            }
            /* the step of the grid before that is on now: another than the one it stands on = a step begins on
               this very sample */
            int was = st;
            if (p->div > 1) {
              was = sfx__sub(st, fr, swing, p->div, bar_sixteenths, &unused_frac);
            } else if ((st & 1) && fr < swing) {
              was = st - 1;
            }
            /* ... and in the new grid one begins here too: it has not sounded yet, so it stands on the one before
               and this one fires below, as any step. (The comparison measures both grids in sixty-fourths: a step
               is 4 / div of them.) */
            if (was != p->step && now > 0 && now * (4 / div) == was * (4 / p->div)) {
              now--;
            }
            /* a held note counts from where its step began: so many sixty-fourths later (or earlier) now - it ends
               where it would have */
            int by = now * (4 / div) - p->step * (4 / p->div);
            for (int t = 0; t < SFX_TRACKS; t++) {
              if (p->gate[t]) {
                int g = p->gate[t] - by;
                p->gate[t] = (uint8_t)(g < 1 ? 1 : g > 255 ? 255 : g);
              }
            }
            p->step = now;
          }
          p->div = (uint8_t)div;
        }

        /* does a step begin on this sample? No: on to the next sample.
           Swing is allowed for in two places. At 16 steps a bar it is this test. In a finer grid sfx__sub does it,
           just below. On a sample where a new bar is found the test is made here whatever the grid: the pattern of
           that bar, and so its grid, is only chosen further down. */
        if ((st & 1) && fr < swing && (div == 1 || newbar)) {
          /* swing: the off-beat step waits */
          continue;
        }
        if (!newbar) {
          if (div > 1) {
            float step_frac;
            sub = sfx__sub(st, fr, swing, div, bar_sixteenths, &step_frac);
            p->frac = step_frac;
          }
          if (sub == p->step) {
            continue;
          }
        }

        /* yes: the voices up to this sample, then the step */
        sfx__render(p, from, k);
        from = k;

        /* a note with a length: its steps are over */
        int dec = 4 / p->div;
        for (int t = 0; t < SFX_TRACKS; t++) {
          if (p->gate[t]) {
            if (p->gate[t] <= dec) {
              p->gate[t] = 0;
              sfx_player_note_off(p, SFX_KEY_SEQ + t);
            } else {
              p->gate[t] = (uint8_t)(p->gate[t] - dec);
            }
          }
        }

        /* a bar line: the cue, else LOOP, else the arrangement, else the chain */
        if (newbar) {
          int bars = s->arr_len <= SFX_ARR_MAX ? s->arr_len : 0;
          if (p->cue >= 0) {
            /* a cue: on in the arrangement from its next bar with that pattern – or, outside it, that pattern
               until the next cue */
            p->pattern = p->cue;
            p->cue = -1;
            p->hold = bars > 0;
            for (int i = 1; i <= bars; i++) {
              int arr_at = (p->arr_pos + i + bars) % bars;
              if (s->arr[arr_at] == p->pattern) {
                p->arr_pos = arr_at;
                p->hold = 0;
                break;
              }
            }
          } else if (p->loop) {
            /* LOOP: the pattern stays */
          } else if (p->bar >= 0 && bars && !p->hold) {
            /* the arrangement: its next bar */
            p->arr_pos = (p->arr_pos + 1) % bars;
            p->pattern = s->arr[p->arr_pos] % SFX_PATTERNS;
          } else if (p->bar >= 0 && !bars && s->chain > 1) {
            /* the chain: the next pattern of patterns 1..chain */
            p->pattern = (p->pattern + 1) % s->chain;
          }
          /* every bar starts with the knobs where they were made */
          for (int l = 0; l < SFX_LANES; l++) {
            p->aut[l] = 0;
          }
          p->bar = (int)bar;
          /* what was held is let go at the bar line … */
          sfx__seq_release(p, 0);
          /* … and the pattern that plays now has its own grid */
          div = sfx_div(s->pat[p->pattern % SFX_PATTERNS].grid);
          p->div = (uint8_t)div;
          sub = st;
          if (div > 1) {
            float step_frac;
            sub = sfx__sub(st, fr, swing, div, bar_sixteenths, &step_frac);
            p->frac = step_frac;
          }
        }

        /* the step `sub` of the pattern that plays */
        p->step = sub;
        const sfx_pattern *pat = &s->pat[p->pattern % SFX_PATTERNS];
        unsigned on_mask = pat->on[sub];
        unsigned intensity = (unsigned)(p->intensity * 127.0f + 0.5f);
        sfx_patch moved;
        unsigned turned = 0;

        /* the lanes: a point on this step moves its knob */
        for (int l = 0; l < SFX_LANES; l++) {
          if (!pat->aut[l][sub] || !s->lane[l].track || s->lane[l].track > SFX_TRACKS) {
            continue;
          }
          p->aut[l] = pat->aut[l][sub];
          if (s->lane[l].to < SFX_AT_VOL) {
            turned |= 1u << (s->lane[l].track - 1);
          }
        }
        /* a moved knob of a sound reaches the voices that play it now */
        for (int t = 0; turned && t < SFX_TRACKS; t++) {
          if (!(turned & (1u << t))) {
            continue;
          }
          const sfx_patch *e = sfx_auto_patch(s, p->aut, t, &p->patch[t], &moved);
          for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
            if (p->voice[i].active && p->voice[i].slot == t) {
              sfx_voice_set_patch(&p->voice[i], e);
            }
          }
        }
        /* the step's notes: every sound that is on here and whose layer the intensity reaches. They start with
           the moved patch */
        for (int t = 0; t < SFX_TRACKS; t++) {
          if ((on_mask & (1u << t)) && intensity >= s->level[t]) {
            int len = pat->len[t][sub];
            sfx_player_note_on(p, t, SFX_KEY_SEQ + t, SFX_VEL[pat->vel[t][sub] & 7], pat->note[t][sub], 0.0f,
                               sfx_auto_patch(s, p->aut, t, &p->patch[t], &moved));
            /* its length in sixty-fourths (0 = none given) */
            p->gate[t] = (uint8_t)((len > 63 ? 63 : len) * (4 / div));
          }
        }
      }
      if (!host) {
        p->pos += (double)n * per;
      }
    }

    /* ---- the voices to the end of the piece ---- */
    sfx__render(p, from, n);
    on = 0;
    for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
      if (p->voice[i].active) {
        on |= 1u << (p->voice[i].slot & 15);
      }
    }
    /* how long the effects may still ring (dry sounds leave no tail) */
    if (p->wet) {
      p->tail = (int)(p->sr * ring);
    } else if (!on && p->tail > 0) {
      p->tail -= n;
    }

    /* ---- the effects, the drive, and out ---- */
    if (on || p->tail > 0) {
      sfx_fx_process(&p->fx, p->bus_c, p->bus_d, p->bus_r, p->dry[0], p->dry[1], n, echo, fx);
      if (drive > 0.0f) {
        /* the sum, pushed into a soft ceiling */
        float push = 1.0f + drive * 5.0f;
        float back = 1.0f / (1.0f + drive * 1.5f);
        for (int k = 0; k < n; k++) {
          p->dry[0][k] = SFX_TANH(p->dry[0][k] * push) * back;
          p->dry[1][k] = SFX_TANH(p->dry[1][k] * push) * back;
        }
      }
      for (int k = 0; k < n; k++) {
        l[done + k] += p->dry[0][k];
        r[done + k] += p->dry[1][k];
      }
    }
    done += n;
  }
  return on | (p->tail > 0 && !on ? 0x80000000u : 0u);
}

#endif
