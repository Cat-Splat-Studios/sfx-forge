/* SPDX-License-Identifier: MIT
 * sfx_song.h – music for SFX FORGE: a song of 8 patterns over the 16 sounds, and the player that makes it sound.
 *
 * The same player runs in the VIBE FM firmware and in the native library a game uses, so a song sounds in the game as
 * it did on the device. What makes it game music:
 *   - layers:   every sound has a threshold; it only plays while the game's INTENSITY is at or above it
 *   - patterns: the game cues a pattern, the player changes to it on the next bar line (or walks a chain by itself)
 * The player also holds the effects every sound can send to: a chorus, a delay and a reverb – and the mixer: every
 * sound's volume, pan and mute, the delay's time and feedback, the reverb's size and damping, a drive on the sum. They
 * belong to the song, so a mix made on the device is the mix in the game. Its output is stereo.
 * Portable C, no heap, no libc – as sfx_core.h.
 */
#ifndef SFX_SONG_H
#define SFX_SONG_H
#include "sfx_core.h"

#define SFX_TRACKS   16            /* one per sound */
#define SFX_STEPS    16
#define SFX_PATTERNS 8
#define SFX_SONG_VERSION 3
#define SFX_M_COUNT    8           /* the master's values (three spare) */
#define SFX_SONG_BODY  (SFX_PATTERNS * SFX_TRACKS * (3 + 2 * SFX_STEPS))
#define SFX_SONG_BODY_V2 (SFX_PATTERNS * SFX_TRACKS * (3 + SFX_STEPS))
#define SFX_SONG_HEAD  (7 + SFX_TRACKS * 3 + 3 + SFX_M_COUNT)
#define SFX_SONG_BYTES (SFX_SONG_HEAD + SFX_SONG_BODY)        /* packed: 4546 bytes, each 0..127 */
#define SFX_SONG_BYTES_V2 (SFX_SONG_HEAD + SFX_SONG_BODY_V2)  /* version 2 (no velocity, no length): 2498 bytes – still taken */
#define SFX_SONG_BYTES_V1 (7 + SFX_TRACKS + SFX_SONG_BODY_V2) /* version 1 (no mixer either): 2455 bytes – still taken */
/* a step's accent byte: its velocity (8 levels) and its length */
#define SFX_VEL_DEFAULT 5
#define SFX_ACC(vel, len) ((uint8_t)(((len) & 15) << 3 | ((vel) & 7)))
#define SFX_ACC_VEL(a) ((a) & 7)                   /* 0..7: soft .. full (5 = as it always was) */
#define SFX_ACC_LEN(a) (((a) >> 3) & 15)           /* 0 = by itself (to the sound's end, its next note or the bar line), else steps */
static const float SFX_VEL[8] = { 0.15f, 0.29f, 0.43f, 0.57f, 0.71f, 0.85f, 0.93f, 1.0f };
enum { SFX_M_DELAY_TIME,           /* 0..5: the echo after 1, 2, 3, 4, 6, 8 sixteenths */
       SFX_M_DELAY_FB,             /* 0..127: how much of it comes again (64 = as it was: 0.45) */
       SFX_M_REVERB_SIZE,          /* 0..127: how long the room rings (91 = as it was) */
       SFX_M_REVERB_DAMP,          /* 0..127: how fast its highs fade (40 = as it was) */
       SFX_M_DRIVE };              /* 0..127: saturation on the sum (0 = none) */
#define SFX_DELAY_TIMES 6
enum { SFX_SCALE_CHROMATIC, SFX_SCALE_MAJOR, SFX_SCALE_MINOR, SFX_SCALE_PENTATONIC, SFX_SCALE_COUNT };

typedef struct {
  uint16_t on[SFX_STEPS];                 /* per step: bit = sound */
  uint8_t  note[SFX_TRACKS][SFX_STEPS];   /* 0 = the sound as designed (drums, effects), else a MIDI note */
  uint8_t  acc[SFX_TRACKS][SFX_STEPS];    /* SFX_ACC: velocity and length */
} sfx_pattern;

typedef struct {
  sfx_pattern pat[SFX_PATTERNS];
  uint16_t bpm;                           /* 40..240 */
  uint8_t  len;                           /* steps a pattern, 1..16 */
  uint8_t  swing;                         /* 0..100 */
  uint8_t  scale;                         /* for entering notes (key of C) */
  uint8_t  chain;                         /* 1 = stay on a pattern, n = walk patterns 1..n, one bar each */
  uint8_t  level[SFX_TRACKS];             /* 0..127: the sound plays while intensity >= this (0 = always) */
  uint8_t  vol[SFX_TRACKS];               /* 0..127: the sound's fader (100 = as designed) */
  uint8_t  pan[SFX_TRACKS];               /* 0..127: left .. right (64 = the middle) */
  uint16_t mute;                          /* a bit a sound: silent */
  uint16_t solo;                          /* a bit a sound: only these (not kept with the song) */
  uint8_t  fx[SFX_M_COUNT];               /* the master: SFX_M_* */
} sfx_song;

static inline void sfx_song_mix_init(sfx_song *s)
{
  static const uint8_t FX[SFX_M_COUNT] = { 2, 64, 91, 40, 0, 0, 0, 0 };
  for (int t = 0; t < SFX_TRACKS; t++) { s->vol[t] = 100; s->pan[t] = 64; }
  for (int i = 0; i < SFX_M_COUNT; i++) s->fx[i] = FX[i];
  s->mute = s->solo = 0;
}

static inline void sfx_song_init(sfx_song *s)
{
  for (int p = 0; p < SFX_PATTERNS; p++) for (int i = 0; i < SFX_STEPS; i++) { s->pat[p].on[i] = 0; for (int t = 0; t < SFX_TRACKS; t++) { s->pat[p].note[t][i] = 0; s->pat[p].acc[t][i] = SFX_VEL_DEFAULT; } }
  for (int t = 0; t < SFX_TRACKS; t++) s->level[t] = 0;
  sfx_song_mix_init(s);
  s->bpm = 120; s->len = SFX_STEPS; s->swing = 0; s->scale = SFX_SCALE_MINOR; s->chain = 1;
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

/* ---- a song as bytes 0..127: for storage, for SysEx, for a game's asset --------------------------------------------- */
static inline void sfx_song_pack(const sfx_song *s, uint8_t *out)
{
  uint8_t *o = out;
  *o++ = SFX_SONG_VERSION; *o++ = s->len; *o++ = (uint8_t)(s->bpm >> 7); *o++ = (uint8_t)(s->bpm & 0x7F);
  *o++ = s->swing; *o++ = s->scale; *o++ = s->chain;
  for (int t = 0; t < SFX_TRACKS; t++) *o++ = s->level[t] & 0x7F;
  for (int t = 0; t < SFX_TRACKS; t++) *o++ = s->vol[t] & 0x7F;
  for (int t = 0; t < SFX_TRACKS; t++) *o++ = s->pan[t] & 0x7F;
  *o++ = (uint8_t)(s->mute & 0x7F); *o++ = (uint8_t)((s->mute >> 7) & 0x7F); *o++ = (uint8_t)(s->mute >> 14);
  for (int i = 0; i < SFX_M_COUNT; i++) *o++ = s->fx[i] & 0x7F;
  for (int p = 0; p < SFX_PATTERNS; p++) for (int t = 0; t < SFX_TRACKS; t++) {
    unsigned bits = 0;
    for (int i = 0; i < SFX_STEPS; i++) if (s->pat[p].on[i] & (1u << t)) bits |= 1u << i;
    *o++ = (uint8_t)(bits & 0x7F); *o++ = (uint8_t)((bits >> 7) & 0x7F); *o++ = (uint8_t)(bits >> 14);
    for (int i = 0; i < SFX_STEPS; i++) *o++ = s->pat[p].note[t][i] & 0x7F;
    for (int i = 0; i < SFX_STEPS; i++) *o++ = s->pat[p].acc[t][i] & 0x7F;
  }
}

/* 0 = taken, -1 = not a song (length, version, a value out of range) – *s is untouched then */
static inline int sfx_song_unpack(sfx_song *s, const uint8_t *in, int n)
{
  int v1 = n == SFX_SONG_BYTES_V1 && in[0] == 1, v2 = n == SFX_SONG_BYTES_V2 && in[0] == 2;
  if (!v1 && !v2 && (n != SFX_SONG_BYTES || in[0] != SFX_SONG_VERSION)) return -1;
  for (int i = 0; i < n; i++) if (in[i] > 127) return -1;
  int bpm = in[2] << 7 | in[3];
  if (in[1] < 1 || in[1] > SFX_STEPS || bpm < 40 || bpm > 240 || in[4] > 100 || in[5] >= SFX_SCALE_COUNT || in[6] < 1 || in[6] > SFX_PATTERNS) return -1;
  if (!v1 && in[7 + SFX_TRACKS * 3 + 3 + SFX_M_DELAY_TIME] >= SFX_DELAY_TIMES) return -1;
  const uint8_t *o = in + 7;
  s->len = in[1]; s->bpm = (uint16_t)bpm; s->swing = in[4]; s->scale = in[5]; s->chain = in[6];
  for (int t = 0; t < SFX_TRACKS; t++) s->level[t] = *o++;
  sfx_song_mix_init(s);
  if (!v1) {
    for (int t = 0; t < SFX_TRACKS; t++) s->vol[t] = *o++;
    for (int t = 0; t < SFX_TRACKS; t++) s->pan[t] = *o++;
    s->mute = (uint16_t)((unsigned)o[0] | (unsigned)o[1] << 7 | (unsigned)(o[2] & 3) << 14); o += 3;
    for (int i = 0; i < SFX_M_COUNT; i++) s->fx[i] = *o++;
  }
  for (int p = 0; p < SFX_PATTERNS; p++) for (int i = 0; i < SFX_STEPS; i++) s->pat[p].on[i] = 0;
  for (int p = 0; p < SFX_PATTERNS; p++) for (int t = 0; t < SFX_TRACKS; t++) {
    unsigned bits = (unsigned)o[0] | (unsigned)o[1] << 7 | (unsigned)(o[2] & 3) << 14; o += 3;
    for (int i = 0; i < SFX_STEPS; i++) { if (bits & (1u << i)) s->pat[p].on[i] |= (uint16_t)(1u << t); s->pat[p].note[t][i] = *o++; }
    for (int i = 0; i < SFX_STEPS; i++) s->pat[p].acc[t][i] = v1 || v2 ? SFX_VEL_DEFAULT : *o++;
  }
  return 0;
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
  sfx_song song;                          /* (a host with two threads: the one that edits writes single values only) */
  sfx_mods mods;
  float intensity;                        /* 0..1 */
  int running, pattern, cue;              /* cue: the pattern to change to on the next bar line, -1 = none */
  uint8_t gate[SFX_TRACKS];               /* steps a sequenced note still holds (0: no length given) */
  double pos;                             /* steps since the start */
  int step, bar; float frac;              /* where it is: step -1 = not yet */
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
  p->intensity = 1.0f; p->running = 0; p->pattern = 0; p->cue = -1; p->pos = 0.0; p->step = -1; p->bar = -1; p->frac = 0.0f; p->tail = 0; p->wet = 0;
  sfx_fx_init(&p->fx, sr);
}

/* a sound starts. key: what lets go of it again (a pad, a note); midi_note 0 = as designed; patch NULL = the sound's own */
static inline void sfx_player_note_on(sfx_player *p, int slot, int key, float vel, int midi_note, float note_st, const sfx_patch *patch)
{
  int pick = 0;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active && p->key[i] == key && p->voice[i].gate) sfx_voice_release(&p->voice[i]);
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) { if (!p->voice[i].active) { pick = i; break; } if (p->age[i] < p->age[pick]) pick = i; }
  sfx_voice_trigger(&p->voice[pick], patch ? patch : &p->patch[slot & 15], vel, note_st, slot & 15, p->sr, p->seed += 0x9E3779B9u);
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
  p->patch[slot & 15] = *patch;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active && p->voice[i].slot == (slot & 15)) sfx_voice_set_patch(&p->voice[i], patch);
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
  p->running = 1; p->pos = 0.0; p->step = -1; p->bar = -1; p->cue = -1;
  for (int t = 0; t < SFX_TRACKS; t++) p->gate[t] = 0;
  if (pattern >= 0) p->pattern = pattern % SFX_PATTERNS;
}

static inline void sfx_player_stop(sfx_player *p) { if (p->running) sfx__seq_release(p, 1); p->running = 0; p->step = -1; p->bar = -1; }

/* change to this pattern on the next bar line (at once while stopped) */
static inline void sfx_player_cue(sfx_player *p, int pattern)
{
  if (pattern < 0 || pattern >= SFX_PATTERNS) return;
  if (p->running) p->cue = pattern; else p->pattern = pattern;
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
    int t = v->slot & 15;
    if ((s->mute >> t) & 1 || (s->solo && !((s->solo >> t) & 1))) continue;                /* (it plays on, unheard) */
    float g = (float)(s->vol[t] & 127) * 0.01f, pn = (float)(s->pan[t] & 127) * (1.0f / 127.0f);
    float gl = s->pan[t] == 64 ? g : g * (pn < 0.5f ? 1.0f : 2.0f - 2.0f * pn), gr = s->pan[t] == 64 ? g : g * (pn > 0.5f ? 1.0f : 2.0f * pn);
    for (int k = 0; k < n; k++) { float x = p->one[k]; p->dry[0][from + k] += x * gl; p->dry[1][from + k] += x * gr; x *= g; p->bus_c[from + k] += x * c; p->bus_d[from + k] += x * d * 0.7f; p->bus_r[from + k] += x * r; }
  }
}

/* ADDS n stereo samples to l and r; every voice starts on the very sample of its step.
   host_pos < 0: its own clock at the song's tempo. Else the host leads (a DAW): host_pos = its position in steps
   (quarter notes x 4) at the first sample, host_step = steps per sample.
   Returns the sounds that play now, a bit each; bit 31 while only the effects still ring. */
static inline uint32_t sfx_player_render(sfx_player *p, float *l, float *r, int total, double host_pos, double host_step)
{
  const sfx_song *s = &p->song;
  int host = host_pos >= 0.0, steps = s->len >= 1 && s->len <= SFX_STEPS ? s->len : SFX_STEPS;
  double per = host ? host_step : (double)s->bpm / 60.0 * 4.0 / (double)p->sr, len = (double)steps;
  static const uint8_t SIXTEENTHS[SFX_DELAY_TIMES] = { 1, 2, 3, 4, 6, 8 };
  float swing = (float)s->swing * 0.005f, echo = 0.25f * (float)SIXTEENTHS[s->fx[SFX_M_DELAY_TIME] % SFX_DELAY_TIMES] * 60.0f / (float)(s->bpm < 40 ? 40 : s->bpm);
  float drive = (float)(s->fx[SFX_M_DRIVE] & 127) * (1.0f / 127.0f), ring = s->fx[SFX_M_REVERB_SIZE] > 91 || s->fx[SFX_M_DELAY_FB] > 64 ? 10.0f : 4.0f;
  uint32_t on = 0;
  for (int done = 0; done < total; ) {
    int n = total - done < SFX_BLOCK ? total - done : SFX_BLOCK, from = 0;
    for (int k = 0; k < n; k++) p->dry[0][k] = p->dry[1][k] = p->bus_c[k] = p->bus_d[k] = p->bus_r[k] = 0.0f;
    p->wet = 0;
    if (p->running) {
      if (host) p->pos = host_pos + (double)done * per;
      for (int k = 0; k < n; k++) {
        double a = p->pos + (double)k * per;
        if (a < 0.0) continue;
        long long bar = (long long)(a / len);
        double f = a - len * (double)bar;
        int st = (int)f; float fr = (float)(f - (double)st);
        p->frac = fr;
        if ((st & 1) && fr < swing) continue;                      /* swing: the off-beat step waits */
        if (st == p->step && (int)bar == p->bar) continue;
        sfx__render(p, from, k); from = k;
        for (int t = 0; t < SFX_TRACKS; t++)                       /* a note with a length: its steps are over */
          if (p->gate[t] && --p->gate[t] == 0) sfx_player_note_off(p, SFX_KEY_SEQ + t);
        if ((int)bar != p->bar) {                                  /* a bar line: the cue, else the chain */
          if (p->cue >= 0) { p->pattern = p->cue; p->cue = -1; }
          else if (p->bar >= 0 && s->chain > 1) p->pattern = (p->pattern + 1) % s->chain;
          p->bar = (int)bar;
          sfx__seq_release(p, 0);                                  /* what was held is let go at the bar line … */
        }
        p->step = st;
        const sfx_pattern *pt = &s->pat[p->pattern % SFX_PATTERNS];
        unsigned m = pt->on[st], gate = (unsigned)(p->intensity * 127.0f + 0.5f);
        for (int t = 0; t < SFX_TRACKS; t++)                       /* … or when its sound plays again (note_on does that) */
          if ((m & (1u << t)) && gate >= s->level[t]) {
            sfx_player_note_on(p, t, SFX_KEY_SEQ + t, SFX_VEL[SFX_ACC_VEL(pt->acc[t][st])], pt->note[t][st], 0.0f, 0);
            p->gate[t] = (uint8_t)SFX_ACC_LEN(pt->acc[t][st]);
          }
      }
      if (!host) p->pos += (double)n * per;
    }
    sfx__render(p, from, n);
    on = 0;
    for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (p->voice[i].active) on |= 1u << p->voice[i].slot;
    if (p->wet) p->tail = (int)(p->sr * ring); else if (!on && p->tail > 0) p->tail -= n;   /* (dry sounds leave no tail) */
    if (on || p->tail > 0) {
      sfx_fx_process(&p->fx, p->bus_c, p->bus_d, p->bus_r, p->dry[0], p->dry[1], n, echo, s->fx);
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
