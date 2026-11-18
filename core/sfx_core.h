/* SPDX-License-Identifier: MIT
 * sfx_core.h – the SFX FORGE engine: a game-audio synth in portable C.
 *
 * No heap, no libc. The same files build for
 *   - the VIBE FM SDK (WebAssembly): sfx-forge/
 *   - a native library for game engines: native/ (the Unity package plays patches with it)
 *   - an M-VAVE FM-1 firmware, later.
 *
 * A port may bring faster maths: define SFX_SIN(turns), SFX_EXP2(x) and SFX_TANH(x) before including.
 *
 * A patch is 28 bytes, every one 0..127, so it travels through MIDI SysEx as it is. Five engines share one chain:
 * source -> noise -> filter (with its own envelope) -> drive -> bitcrush -> loudness (attack, decay, sustain, release),
 * with an LFO on pitch, filter and timbre. Three of the bytes are sends to the player's chorus, delay and reverb.
 */
#ifndef SFX_CORE_H
#define SFX_CORE_H
#include <stdint.h>

/* ---- maths without libc: the same approximations on every target, so a patch sounds the same everywhere --------- */
#ifndef SFX_SIN
static inline float sfx_sin(float t)                       /* t in turns */
{
  t -= (float)(int)t; if (t < 0.0f) t += 1.0f;
  float x = t * 6.28318530718f - 3.14159265359f;           /* -pi..pi; sin(x + pi) = -sin(x) */
  float y = 1.27323954f * x - 0.405284735f * x * (x < 0.0f ? -x : x);
  y = 0.225f * (y * (y < 0.0f ? -y : y) - y) + y;
  return -y;
}
static inline float sfx_exp2(float x)
{
  if (x < -126.0f) return 0.0f;
  if (x > 126.0f) x = 126.0f;
  int i = (int)x; if (x < 0.0f && x != (float)i) i--;
  float f = x - (float)i;
  float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
  union { uint32_t i; float f; } b; b.i = (uint32_t)(i + 127) << 23;
  return p * b.f;
}
static inline float sfx_tanh(float x) { if (x > 3.0f) return 1.0f; if (x < -3.0f) return -1.0f; float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }
#define SFX_SIN(x)  sfx_sin(x)
#define SFX_EXP2(x) sfx_exp2(x)
#define SFX_TANH(x) sfx_tanh(x)
#endif

#include "sfx_wavetable.h"

#define SFX_PATCH_VERSION 2

typedef enum { SFX_ENGINE_IMPACT, SFX_ENGINE_UI_BLIP, SFX_ENGINE_SCIFI_FM, SFX_ENGINE_FOLEY_NOISE, SFX_ENGINE_WAVETABLE, SFX_ENGINE_COUNT } sfx_engine;

/* the parameters, in wire order (bytes 2.. of a packed patch) */
enum {
  SFX_P_BASE_FREQ,      /* 20 Hz .. 14 kHz, logarithmic (a sound played as a note ignores it) */
  SFX_P_PITCH_ENV_AMT,  /* 64 = none, below falls towards the note, above rises: +/- 4 octaves */
  SFX_P_PITCH_ENV_DEC,  /* 5 ms .. 2.5 s */
  SFX_P_FM_RATIO,       /* "A" – IMPACT, SCIFI: modulator ratio · UI: arp interval · FOLEY: grain rate · WAVETABLE: position */
  SFX_P_FM_DEPTH,       /* "B" – IMPACT, SCIFI: FM index · UI: arp speed · FOLEY: grain depth · WAVETABLE: warp */
  SFX_P_NOISE_MIX,
  SFX_P_FILTER_CUTOFF,  /* 40 Hz .. 15 kHz */
  SFX_P_FILTER_RES,
  SFX_P_AMP_DECAY,      /* 10 ms .. 3.9 s: to silence, or to the sustain level */
  SFX_P_BITCRUSH,       /* 0 = clean */
  SFX_P_ATTACK,         /* 1.5 ms .. 4 s */
  SFX_P_SUSTAIN,        /* 0 = a one-shot: it plays to its end whatever the key does. Above: held while the key is */
  SFX_P_RELEASE,        /* 5 ms .. 3.9 s, for a sustained sound */
  SFX_P_FILTER_ENV,     /* 64 = none: the filter opens (above) or closes (below) at the start, +/- 5 octaves */
  SFX_P_FILTER_ENV_DEC, /* 5 ms .. 2.5 s */
  SFX_P_LFO_RATE,       /* 0.1 .. 29 Hz */
  SFX_P_LFO_PITCH,      /* vibrato, up to an octave */
  SFX_P_LFO_FILTER,     /* up to 3 octaves */
  SFX_P_LFO_TIMBRE,     /* FM index, or the wavetable's position */
  SFX_P_SPREAD,         /* WAVETABLE: 0 = one oscillator, above: three, detuned */
  SFX_P_CHORUS,         /* sends */
  SFX_P_DELAY,
  SFX_P_REVERB,
  SFX_P_LEVEL,          /* 100 = as designed */
  SFX_P_TABLE,          /* WAVETABLE: which table, 0..5 */
  SFX_P_COUNT
};
#define SFX_PATCH_BYTES (3 + SFX_P_COUNT)   /* version, engine, the parameters, checksum: 28 */

typedef struct {
  uint8_t version;
  uint8_t engine;                 /* sfx_engine */
  uint8_t p[SFX_P_COUNT];         /* each 0..127 */
} sfx_patch;

/* what the game (or a DAW) moves while a sound plays – "telemetry". Neutral: all 0, level 1. */
typedef struct {
  float pitch_st;                 /* semitones added */
  float cutoff_oct;               /* octaves added to the filter */
  float fm;                       /* timbre added (FM index, wavetable position), 0..1 of the full range */
  float level;                    /* 0..1 */
} sfx_mods;

typedef struct {
  sfx_patch patch;
  int   active, gate, slot, stage;        /* stage: 0 attack, 1 decay / sustain, 2 release */
  float sr, vel, note_st;         /* note_st: transposition in semitones */
  float abs_oct;                  /* >= 0: played as a note – this pitch (octaves above 20 Hz) instead of the patch's FREQ */
  float phase, mphase, fb;        /* carrier, modulator (turns), FM feedback memory */
  float uph[2];                   /* WAVETABLE: the two detuned oscillators */
  const float *wt0, *wt1;         /* WAVETABLE: the two frames around the position, and how far between them */
  float wtf, wbend; int unison;
  float penv, pcoef, fenv, fcoef; /* pitch and filter envelopes 1 -> 0, their factors per control step */
  float aenv, ainc, acoef, rcoef, sus;    /* loudness: attack step, decay and release factors per sample, sustain level */
  float lfo, lfo_dt;              /* the LFO's phase (turns) and its step per control step */
  float ic1, ic2;                 /* filter state */
  float a1, a2, a3, k;            /* filter coefficients (TPT state-variable) */
  float dt, mdt, idx;             /* per-sample phase steps, FM index in turns */
  float lvl, gain;
  float arp_t, arp_dt; int arp_step;      /* UI: the arpeggio's clock */
  float gr_t, gr_dt, gr_target, gr_amp;   /* FOLEY: grains */
  float cr_hold, cr_cnt, cr_val, cr_q;    /* bitcrush: samples held, counter, held value, quantisation */
  uint32_t rng;
  int   ctl;                      /* samples until the next control step */
} sfx_voice;

#define SFX_CTL 16                /* control rate: every 16 samples */
#define SFX__TAU 6.28318530718f

static inline float sfx__clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float sfx__unit(uint8_t v) { return (float)(v > 127 ? 127 : v) * (1.0f / 127.0f); }
static inline uint32_t sfx__rand(uint32_t *s) { uint32_t x = *s ? *s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }
static inline float sfx__noise(uint32_t *s) { return (float)(int32_t)sfx__rand(s) * (1.0f / 2147483648.0f); }

/* a plain patch of an engine: a short one-shot, nothing modulated, no effects */
static inline void sfx_patch_init(sfx_patch *p, int engine)
{
  static const uint8_t D[SFX_P_COUNT] = { 60, 64, 40, 40, 40, 0, 110, 10, 60, 0,   0, 0, 34, 64, 40,   50, 0, 0, 0,   0, 0, 0, 0, 100, 0 };
  p->version = SFX_PATCH_VERSION; p->engine = (uint8_t)(engine % SFX_ENGINE_COUNT);
  for (int i = 0; i < SFX_P_COUNT; i++) p->p[i] = D[i];
}

static inline int sfx_patch_sustained(const sfx_patch *p) { return p->p[SFX_P_SUSTAIN] > 0; }

/* ---- the patch on the wire ------------------------------------------------------------------------------------ */
static inline uint8_t sfx_patch_checksum(const uint8_t *b) { unsigned s = 0; for (int i = 0; i < SFX_PATCH_BYTES - 1; i++) s += b[i]; return (uint8_t)(s & 0x7F); }

static inline void sfx_patch_pack(const sfx_patch *p, uint8_t out[SFX_PATCH_BYTES])
{
  out[0] = SFX_PATCH_VERSION; out[1] = (uint8_t)(p->engine % SFX_ENGINE_COUNT);
  for (int i = 0; i < SFX_P_COUNT; i++) out[2 + i] = (uint8_t)(p->p[i] & 0x7F);
  out[SFX_PATCH_BYTES - 1] = sfx_patch_checksum(out);
}

/* 0 = taken, -1 = not a valid patch (wrong version, a byte out of range, checksum) – *p is untouched then */
static inline int sfx_patch_unpack(sfx_patch *p, const uint8_t in[SFX_PATCH_BYTES])
{
  if (in[0] != SFX_PATCH_VERSION || in[1] >= SFX_ENGINE_COUNT) return -1;
  for (int i = 0; i < SFX_PATCH_BYTES; i++) if (in[i] > 127) return -1;
  if (in[SFX_PATCH_BYTES - 1] != sfx_patch_checksum(in)) return -1;
  p->version = in[0]; p->engine = in[1];
  for (int i = 0; i < SFX_P_COUNT; i++) p->p[i] = in[2 + i];
  return 0;
}

/* ---- MUTATE: every parameter moves by at most +/- 15 % of its range. What kind of sound it is stays: the engine,
   the table, its level, and whether it is a one-shot or a sustained sound. ------------------------------------------ */
static inline void sfx_patch_mutate(sfx_patch *p, uint32_t *rng)
{
  for (int i = 0; i < SFX_P_COUNT; i++) {
    if (i == SFX_P_LEVEL || i == SFX_P_TABLE || (i == SFX_P_SUSTAIN && p->p[i] == 0)) continue;
    int v = (int)p->p[i] + (int)(sfx__rand(rng) % 39u) - 19;          /* 19 / 127 = 15 % */
    int low = i == SFX_P_SUSTAIN ? 1 : 0;
    p->p[i] = (uint8_t)(v < low ? low : (v > 127 ? 127 : v));
  }
}

/* a new one-shot from nothing, inside ranges that stay audible */
static inline void sfx_patch_random(sfx_patch *p, uint32_t *rng)
{
  static const uint8_t lo[SFX_P_COUNT] = { 30, 20, 20, 0, 0, 0, 50, 0, 30, 0,   0, 0, 20, 44, 20,   20, 0, 0, 0,   0, 0, 0, 0, 100, 0 };
  static const uint8_t hi[SFX_P_COUNT] = { 100, 110, 100, 127, 127, 127, 127, 100, 105, 60,   12, 0, 60, 100, 90,   90, 20, 40, 50,   60, 40, 40, 50, 100, SFX_WT_TABLES - 1 };
  p->version = SFX_PATCH_VERSION;
  p->engine = (uint8_t)(sfx__rand(rng) % SFX_ENGINE_COUNT);
  for (int i = 0; i < SFX_P_COUNT; i++) p->p[i] = (uint8_t)(lo[i] + sfx__rand(rng) % (uint32_t)(hi[i] - lo[i] + 1));
  if (sfx__rand(rng) & 1) p->p[SFX_P_BITCRUSH] = 0;
  if (sfx__rand(rng) & 1) p->p[SFX_P_LFO_PITCH] = 0;
}

/* ---- a voice ---------------------------------------------------------------------------------------------------- */
/* the parts of a patch that cost an exp2 and only change with the patch itself */
static inline void sfx_voice_set_patch(sfx_voice *v, const sfx_patch *p)
{
  v->patch = *p;
  float pt = 0.005f * SFX_EXP2(sfx__unit(p->p[SFX_P_PITCH_ENV_DEC]) * 9.0f);        /* seconds */
  v->pcoef = SFX_EXP2(-1.442695f * (float)SFX_CTL / (pt * v->sr));
  float ft = 0.005f * SFX_EXP2(sfx__unit(p->p[SFX_P_FILTER_ENV_DEC]) * 9.0f);
  v->fcoef = SFX_EXP2(-1.442695f * (float)SFX_CTL / (ft * v->sr));
  v->ainc = 1.0f / (0.0015f * SFX_EXP2(sfx__unit(p->p[SFX_P_ATTACK]) * 11.4f) * v->sr);
  float at = 0.010f * SFX_EXP2(sfx__unit(p->p[SFX_P_AMP_DECAY]) * 8.6f);
  v->acoef = SFX_EXP2(-1.442695f / (at * v->sr));
  float rt = 0.005f * SFX_EXP2(sfx__unit(p->p[SFX_P_RELEASE]) * 9.6f);
  v->rcoef = SFX_EXP2(-1.442695f / (rt * v->sr));
  v->sus = sfx__unit(p->p[SFX_P_SUSTAIN]);
  v->lfo_dt = 0.1f * SFX_EXP2(sfx__unit(p->p[SFX_P_LFO_RATE]) * 8.2f) * (float)SFX_CTL / v->sr;
  float c = sfx__unit(p->p[SFX_P_BITCRUSH]);
  v->cr_hold = p->p[SFX_P_BITCRUSH] ? (1.0f + c * c * 40.0f) * (v->sr / 48000.0f) : 0.0f;
  v->cr_q = SFX_EXP2(15.0f - c * 12.0f);                                            /* 16 bit down to 4 */
  float g = (float)p->p[SFX_P_LEVEL] * 0.01f;
  v->gain = g * g;
  v->unison = p->engine == SFX_ENGINE_WAVETABLE && p->p[SFX_P_SPREAD] > 0;
}

static inline void sfx_voice_trigger(sfx_voice *v, const sfx_patch *p, float vel, float note_st, int slot, float sr, uint32_t seed)
{
  v->sr = sr; v->vel = vel; v->note_st = note_st; v->slot = slot; v->abs_oct = -1.0f;
  sfx_voice_set_patch(v, p);
  v->active = 1; v->gate = 1; v->stage = 0;
  v->phase = v->mphase = v->fb = 0.0f;
  v->uph[0] = 0.37f; v->uph[1] = 0.71f;
  v->wt0 = v->wt1 = sfx_wt[0][0][0]; v->wtf = 0.0f; v->wbend = 0.5f;
  v->penv = 1.0f; v->fenv = 1.0f; v->aenv = 0.0f; v->lfo = 0.0f;
  v->ic1 = v->ic2 = 0.0f;
  v->arp_t = 0.0f; v->arp_step = 0;
  v->gr_t = 1.0f; v->gr_target = 1.0f; v->gr_amp = 1.0f;
  v->cr_cnt = 0.0f; v->cr_val = 0.0f;
  v->rng = seed ? seed : 1u;
  v->ctl = 0;
}

/* after a trigger: play the sound as a MIDI note (69 = 440 Hz), so every sound of a song is in tune with the others */
static inline void sfx_voice_set_note(sfx_voice *v, int midi_note) { v->abs_oct = ((float)midi_note - 69.0f) * (1.0f / 12.0f) + 4.4594316f; }

static inline void sfx_voice_release(sfx_voice *v) { v->gate = 0; }

static inline void sfx__control(sfx_voice *v, const sfx_mods *m)
{
  const uint8_t *p = v->patch.p;
  int eng = v->patch.engine;
  float sr = v->sr;
  float lfo = SFX_SIN(v->lfo);
  v->lfo += v->lfo_dt; if (v->lfo >= 1.0f) v->lfo -= 1.0f;
  float lp = sfx__unit(p[SFX_P_LFO_PITCH]), lt = sfx__unit(p[SFX_P_LFO_TIMBRE]);
  float amt = ((float)p[SFX_P_PITCH_ENV_AMT] - 64.0f) * (4.0f / 64.0f);            /* octaves */
  float oct = (v->abs_oct >= 0.0f ? v->abs_oct : sfx__unit(p[SFX_P_BASE_FREQ]) * 9.5f) + (v->note_st + m->pitch_st) * (1.0f / 12.0f) + lfo * lp * lp;
  float sweep = amt * v->penv;
  float cut_oct = sfx__unit(p[SFX_P_FILTER_CUTOFF]) * 8.6f + m->cutoff_oct
                + ((float)p[SFX_P_FILTER_ENV] - 64.0f) * (5.0f / 64.0f) * v->fenv + lfo * sfx__unit(p[SFX_P_LFO_FILTER]) * 3.0f;
  float timbre = m->fm + lfo * lt * 0.5f;                                           /* what moves the sound's colour */

  if (eng == SFX_ENGINE_UI_BLIP) {                                                  /* an arpeggio upwards: 4 steps, then it stays */
    int interval = (p[SFX_P_FM_RATIO] * 24 + 63) / 127;
    v->arp_dt = (float)SFX_CTL * (2.0f * SFX_EXP2(sfx__unit(p[SFX_P_FM_DEPTH]) * 5.0f)) / sr;   /* 2 .. 64 steps a second */
    v->arp_t += v->arp_dt;
    if (v->arp_t >= 1.0f) { v->arp_t -= 1.0f; if (v->arp_step < 3) v->arp_step++; }
    oct += (float)(v->arp_step * interval) * (1.0f / 12.0f);
    v->idx = 0.0f; v->mdt = 0.0f;
  } else if (eng == SFX_ENGINE_FOLEY_NOISE) {                                       /* the sweep moves the band, not a pitch */
    cut_oct += sweep;
    v->gr_dt = (2.0f * SFX_EXP2(sfx__unit(p[SFX_P_FM_RATIO]) * 7.0f)) / sr;         /* 2 .. 256 grains a second */
    v->idx = 0.0f; v->mdt = 0.0f;
  } else if (eng == SFX_ENGINE_WAVETABLE) {
    float pos = sfx__clamp(sfx__unit(p[SFX_P_FM_RATIO]) + timbre, 0.0f, 1.0f) * (float)(SFX_WT_FRAMES - 1);
    int f0 = (int)pos; if (f0 > SFX_WT_FRAMES - 2) f0 = SFX_WT_FRAMES - 2;
    float dt = sfx__clamp(20.0f * SFX_EXP2(oct + sweep) / sr, 0.0f, 0.45f);
    int table = p[SFX_P_TABLE] % SFX_WT_TABLES;
    v->wt0 = sfx_wt_frame(table, f0, dt); v->wt1 = sfx_wt_frame(table, f0 + 1, dt); v->wtf = pos - (float)f0;
    v->wbend = 0.5f - 0.45f * sfx__unit(p[SFX_P_FM_DEPTH]);                         /* warp: the cycle's first half squeezed */
    v->idx = 0.0f; v->mdt = 0.0f;
  } else {
    float d = sfx__clamp(sfx__unit(p[SFX_P_FM_DEPTH]) + timbre, 0.0f, 2.0f);
    float idx = d * d * (eng == SFX_ENGINE_SCIFI_FM ? 24.0f : 8.0f);                /* radians */
    if (eng == SFX_ENGINE_SCIFI_FM) idx *= 0.25f + 0.75f * v->penv;                 /* the index falls with the sweep: a laser */
    v->idx = idx * (1.0f / SFX__TAU);
  }
  float f = 20.0f * SFX_EXP2(oct + (eng == SFX_ENGINE_FOLEY_NOISE ? 0.0f : sweep));
  v->dt = sfx__clamp(f / sr, 0.0f, 0.45f);
  if (eng == SFX_ENGINE_IMPACT || eng == SFX_ENGINE_SCIFI_FM)
    v->mdt = sfx__clamp(v->dt * 0.25f * SFX_EXP2(sfx__unit(p[SFX_P_FM_RATIO]) * 6.0f), 0.0f, 0.45f);

  float fc = sfx__clamp(40.0f * SFX_EXP2(cut_oct), 30.0f, sr * 0.45f);
  float w = 0.5f * fc / sr;                                                         /* tan(pi fc / sr) as sin / cos */
  float g = SFX_SIN(w) / SFX_SIN(w + 0.25f);
  float r = sfx__unit(p[SFX_P_FILTER_RES]);
  v->k = 1.0f / (0.5f + r * r * 15.0f);
  v->a1 = 1.0f / (1.0f + g * (g + v->k)); v->a2 = g * v->a1; v->a3 = g * v->a2;

  v->lvl = sfx__clamp(m->level, 0.0f, 1.0f) * v->vel * v->gain;
  v->penv *= v->pcoef; v->fenv *= v->fcoef;
}

static inline float sfx__blep(float t, float dt)
{
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

/* one cycle of the wavetable at this phase: between two frames, and between two samples of each */
static inline float sfx__wt_read(const sfx_voice *v, float ph)
{
  float b = v->wbend, w = ph < b ? ph * 0.5f / b : 0.5f + (ph - b) * 0.5f / (1.0f - b);
  float at = w * (float)SFX_WT_SIZE; int i = (int)at; float fr = at - (float)i;
  if (i >= SFX_WT_SIZE) { i = SFX_WT_SIZE - 1; fr = 1.0f; }
  float a = v->wt0[i] + (v->wt0[i + 1] - v->wt0[i]) * fr, c = v->wt1[i] + (v->wt1[i + 1] - v->wt1[i]) * fr;
  return a + (c - a) * v->wtf;
}

/* adds `n` samples of this voice to out[]. Returns 1 while it sounds. */
static inline int sfx_voice_render(sfx_voice *v, const sfx_mods *m, float *out, int n)
{
  if (!v->active) return 0;
  const int eng = v->patch.engine;
  const float nm = sfx__unit(v->patch.p[SFX_P_NOISE_MIX]);
  const float gdepth = sfx__unit(v->patch.p[SFX_P_FM_DEPTH]);
  const float spread = sfx__unit(v->patch.p[SFX_P_SPREAD]) * 0.0175f;               /* up to 30 cents either way */
  const float drive = eng == SFX_ENGINE_IMPACT ? 2.2f : eng == SFX_ENGINE_WAVETABLE ? 1.0f : 1.3f;

  for (int i = 0; i < n; i++) {
    if (v->ctl <= 0) { sfx__control(v, m); v->ctl = SFX_CTL; }
    v->ctl--;

    /* the source */
    float x;
    if (eng == SFX_ENGINE_UI_BLIP) {
      float t2 = v->phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
      x = (v->phase < 0.5f ? 1.0f : -1.0f) + sfx__blep(v->phase, v->dt) - sfx__blep(t2, v->dt);
      x *= 0.6f;
    } else if (eng == SFX_ENGINE_FOLEY_NOISE) {
      x = SFX_SIN(v->phase) * 0.5f;                                                 /* a tone under the noise, when NOISE is turned down */
    } else if (eng == SFX_ENGINE_WAVETABLE) {
      x = sfx__wt_read(v, v->phase);
      if (v->unison) {
        x = (x + sfx__wt_read(v, v->uph[0]) + sfx__wt_read(v, v->uph[1])) * 0.5f;
        v->uph[0] += v->dt * (1.0f + spread); if (v->uph[0] >= 1.0f) v->uph[0] -= 1.0f;
        v->uph[1] += v->dt * (1.0f - spread); if (v->uph[1] >= 1.0f) v->uph[1] -= 1.0f;
      }
      x *= 0.8f;
    } else {
      float mod = SFX_SIN(v->mphase + v->fb * 0.15f);
      if (eng == SFX_ENGINE_SCIFI_FM) v->fb = mod;
      x = SFX_SIN(v->phase + mod * v->idx);
      v->mphase += v->mdt; if (v->mphase >= 1.0f) v->mphase -= 1.0f;
    }
    v->phase += v->dt; if (v->phase >= 1.0f) v->phase -= 1.0f;
    x += (sfx__noise(&v->rng) * 1.4f - x) * nm;

    /* the filter: low-pass, FOLEY a band-pass */
    float v3 = x - v->ic2, v1 = v->a1 * v->ic1 + v->a2 * v3, v2 = v->ic2 + v->a2 * v->ic1 + v->a3 * v3;
    v->ic1 = 2.0f * v1 - v->ic1 + 1e-18f; v->ic2 = 2.0f * v2 - v->ic2 + 1e-18f;
    x = eng == SFX_ENGINE_FOLEY_NOISE ? v1 * (0.6f + v->k) * 2.0f : v2;

    if (eng == SFX_ENGINE_FOLEY_NOISE) {                                            /* grains: a new random loudness at the grain rate */
      v->gr_t += v->gr_dt;
      if (v->gr_t >= 1.0f) { v->gr_t -= 1.0f; float r = sfx__noise(&v->rng); v->gr_target = r * r; }
      v->gr_amp += (v->gr_target - v->gr_amp) * 0.01f;
      x *= 1.0f - gdepth + gdepth * v->gr_amp * 2.5f;
    }
    x = SFX_TANH(x * drive);

    if (v->cr_hold > 0.0f) {                                                        /* fewer samples, fewer bits */
      v->cr_cnt += 1.0f;
      if (v->cr_cnt >= v->cr_hold) { v->cr_cnt -= v->cr_hold; float q = x * v->cr_q; v->cr_val = (float)(int)(q + (q >= 0.0f ? 0.5f : -0.5f)) / v->cr_q; }
      x = v->cr_val;
    }

    /* loudness: attack, then decay – to silence (a one-shot), or to the sustain level until the key is let go */
    if (v->sus > 0.0f && !v->gate) v->stage = 2;
    if (v->stage == 0) { v->aenv += v->ainc; if (v->aenv >= 1.0f) { v->aenv = 1.0f; v->stage = 1; } }
    else if (v->stage == 1) v->aenv = v->sus + (v->aenv - v->sus) * v->acoef;
    else v->aenv *= v->rcoef;
    out[i] += x * v->aenv * v->lvl * 0.6f;
    if (v->stage && v->aenv < 0.0002f) { v->active = 0; return 0; }
  }
  return 1;
}

#endif
