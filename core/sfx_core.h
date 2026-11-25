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
 * source -> noise -> filter (with its own envelope) -> grains (FOLEY only) -> drive -> bitcrush -> loudness (attack,
 * decay, sustain, release), with an LFO on pitch, filter and timbre. Three of the bytes are sends to the player's
 * chorus, delay and reverb.
 *
 * WHAT THIS FILE HOLDS, in its order:
 *   1. Maths without libc        sfx_sin, sfx_exp2, sfx_tanh and the SFX_SIN / SFX_EXP2 / SFX_TANH macros
 *   2. The patch                 sfx_engine, the SFX_P_* parameters, sfx_patch, SFX_PATCH_BYTES
 *   3. A voice's state           sfx_mods (what a game moves while a sound plays), sfx_voice
 *   4. Small helpers             sfx__clamp, sfx__unit, sfx__rand, sfx__noise
 *   5. Making patches            sfx_patch_init, sfx_patch_sustained
 *   6. The patch on the wire     sfx_patch_checksum, sfx_patch_pack, sfx_patch_unpack (the 28 bytes)
 *   7. MUTATE and RANDOM         sfx_patch_mutate, sfx_patch_random
 *   8. Playing a voice           sfx_voice_set_patch, sfx_voice_trigger, sfx_voice_set_note, sfx_voice_release
 *   9. The control step          sfx__control: every 16 samples, pitch, timbre and filter are worked out anew
 *  10. The sample loop           sfx__blep, sfx__wt_read, sfx_voice_render
 *
 * WHAT TO READ FIRST: the SFX_P_* list (section 2) says what a patch is; sfx_voice_render (section 10) is the signal
 * chain, top to bottom; sfx__control (section 9) is where the knobs become frequencies and coefficients.
 * The wavetables are in sfx_wavetable.h, songs and the player with its effects in sfx_song.h.
 *
 * RULES FOR ALL OF IT: nothing here allocates, blocks, locks, or calls a library. Every function works on memory the
 * caller owns and may run on an audio thread. A name with two underscores (sfx__clamp) is for this file's own use.
 */
#ifndef SFX_CORE_H
#define SFX_CORE_H
#include <stdint.h>

/* ==== 1. Maths without libc ========================================================================================
   The same approximations on every target, so a patch sounds the same everywhere. */
#ifndef SFX_SIN

/* The sine of an angle given in TURNS (1.0 = a full circle), any value. A parabola, bent once more: about 0.1 % off. */
static inline float sfx_sin(float t) {
  /* the fraction of the turn, 0..1 */
  t -= (float)(int)t;
  if (t < 0.0f) {
    t += 1.0f;
  }
  /* -pi..pi; sin(x + pi) = -sin(x) */
  float x = t * 6.28318530718f - 3.14159265359f;
  float y = 1.27323954f * x - 0.405284735f * x * (x < 0.0f ? -x : x);
  y = 0.225f * (y * (y < 0.0f ? -y : y) - y) + y;
  return -y;
}

/* 2 to the power of x. Below -126 it is 0, above 126 it stays at 2^126. */
static inline float sfx_exp2(float x) {
  if (x < -126.0f) {
    return 0.0f;
  }
  if (x > 126.0f) {
    x = 126.0f;
  }
  /* the whole part, rounded down (also for a negative x) */
  int i = (int)x;
  if (x < 0.0f && x != (float)i) {
    i--;
  }
  /* 2^fraction as a polynomial ... */
  float f = x - (float)i;
  float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
  /* ... times 2^whole, written straight into a float's exponent bits */
  union {
    uint32_t i;
    float f;
  } bits;
  bits.i = (uint32_t)(i + 127) << 23;
  return p * bits.f;
}

/* A soft ceiling: close to tanh(x), exactly -1 or 1 beyond -3 and 3. */
static inline float sfx_tanh(float x) {
  if (x > 3.0f) {
    return 1.0f;
  }
  if (x < -3.0f) {
    return -1.0f;
  }
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

#define SFX_SIN(x)  sfx_sin(x)
#define SFX_EXP2(x) sfx_exp2(x)
#define SFX_TANH(x) sfx_tanh(x)
#endif

#include "sfx_wavetable.h"

/* ==== 2. The patch ============================================================================================== */

#define SFX_PATCH_VERSION 2

/* The five engines: what the source of the chain is, and what the knobs "A" and "B" do there. */
typedef enum {
  SFX_ENGINE_IMPACT,      /* a sine with FM, driven hard: hits, thuds, explosions */
  SFX_ENGINE_UI_BLIP,     /* a square wave that steps up an arpeggio: menu sounds, coins */
  SFX_ENGINE_SCIFI_FM,    /* FM with feedback, its index falls with the pitch sweep: lasers */
  SFX_ENGINE_FOLEY_NOISE, /* noise through a band-pass, in grains: wind, steps, fire */
  SFX_ENGINE_WAVETABLE,   /* a wavetable (sfx_wavetable.h), one oscillator or three detuned: notes, pads */
  SFX_ENGINE_COUNT
} sfx_engine;

/* The parameters, in wire order (bytes 2.. of a packed patch). Each is 0..127.
   Two of them mean something else in every engine – the panel calls them "A" and "B":
     SFX_P_FM_RATIO "A"   IMPACT, SCIFI: modulator ratio · UI: arp interval · FOLEY: grain rate · WAVETABLE: position
     SFX_P_FM_DEPTH "B"   IMPACT, SCIFI: FM index · UI: arp speed · FOLEY: grain depth · WAVETABLE: warp */
enum {
  SFX_P_BASE_FREQ, /* 20 Hz .. 14 kHz, logarithmic (a sound played as a note ignores it) */
  /* 64 = none. Above: the pitch starts up to 4 octaves above the note and falls to it; below: it starts under the
     note and rises to it */
  SFX_P_PITCH_ENV_AMT,
  SFX_P_PITCH_ENV_DEC,  /* 5 ms .. 2.5 s */
  SFX_P_FM_RATIO,       /* "A" – see above */
  SFX_P_FM_DEPTH,       /* "B" – see above */
  SFX_P_NOISE_MIX,      /* 0 = the source alone, 127 = noise alone */
  SFX_P_FILTER_CUTOFF,  /* 40 Hz .. 15 kHz */
  SFX_P_FILTER_RES,     /* resonance */
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
  SFX_P_CHORUS,         /* the three sends: to the player's chorus, */
  SFX_P_DELAY,          /* its delay */
  SFX_P_REVERB,         /* and its reverb */
  SFX_P_LEVEL,          /* 100 = as designed */
  SFX_P_TABLE,          /* WAVETABLE: which table, 0..5 */
  SFX_P_COUNT
};

/* version, engine, the parameters, checksum: 28 */
#define SFX_PATCH_BYTES (3 + SFX_P_COUNT)

typedef struct {
  uint8_t version;
  uint8_t engine;         /* sfx_engine */
  uint8_t p[SFX_P_COUNT]; /* each 0..127 */
} sfx_patch;

/* ==== 3. A voice's state ======================================================================================== */

/* What the game (or a DAW) moves while a sound plays – "telemetry". Neutral: all 0, level 1. */
typedef struct {
  float pitch_st;   /* semitones added */
  float cutoff_oct; /* octaves added to the filter */
  float fm;         /* timbre added (FM index, wavetable position), 0..1 of the full range */
  float level;      /* 0..1 */
} sfx_mods;

/* One sound that plays. The caller owns it; sfx_voice_trigger fills every field, so it needs no initialising. */
typedef struct {
  sfx_patch patch;

  /* what it is doing */
  int active; /* 1 while it sounds */
  int gate;   /* 1 while its key is held */
  int slot;   /* which of the player's sounds it plays (sfx_song.h) */
  int stage;  /* 0 attack, 1 decay / sustain, 2 release */

  /* how it was started */
  float sr;      /* sample rate */
  float vel;     /* velocity 0..1 */
  float note_st; /* transposition in semitones */
  float abs_oct; /* >= 0: played as a note – this pitch (octaves above 20 Hz) instead of the patch's FREQ */

  /* the oscillators */
  float phase;      /* the carrier, in turns */
  float mphase;     /* the modulator, in turns */
  float fb;         /* FM feedback memory */
  float uph[2];     /* WAVETABLE: the two detuned oscillators */
  const float *wt0; /* WAVETABLE: the two frames around the position ... */
  const float *wt1; /* (the second of the two) */
  float wtf;        /* ... and how far between them, 0..1 */
  float wbend;      /* WAVETABLE: where the cycle's middle is played (warp); 0.5 = unbent */
  int unison;       /* WAVETABLE: 1 = three oscillators */

  /* the envelopes */
  /* pitch envelope 1 -> 0, its factor per control step */
  float penv;
  float pcoef;
  /* filter envelope 1 -> 0, its factor per control step */
  float fenv;
  float fcoef;
  float aenv;  /* loudness now, 0..1 */
  float ainc;  /* loudness: the attack's step per sample */
  float acoef; /* loudness: the decay's factor per sample */
  float rcoef; /* loudness: the release's factor per sample */
  float sus;   /* loudness: the sustain level */

  /* the LFO: its phase (turns) and its step per control step */
  float lfo;
  float lfo_dt;

  /* the filter (TPT state-variable): its state, its coefficients */
  float ic1;
  float ic2;
  float a1;
  float a2;
  float a3;
  float k;

  /* what the control step hands to the sample loop */
  float dt;   /* the carrier's phase step per sample */
  float mdt;  /* the modulator's phase step per sample */
  float idx;  /* FM index in turns */
  float lvl;  /* loudness of this voice now: telemetry level x velocity x gain */
  float gain; /* the patch's LEVEL as a factor */

  /* UI: the arpeggio's clock */
  float arp_t;
  float arp_dt;
  int arp_step;

  /* FOLEY: grains – the clock, its step, the loudness aimed at, the loudness now */
  float gr_t;
  float gr_dt;
  float gr_target;
  float gr_amp;

  /* bitcrush: samples held, counter, held value, quantisation */
  float cr_hold;
  float cr_cnt;
  float cr_val;
  float cr_q;

  uint32_t rng; /* this voice's noise */
  int ctl;      /* samples until the next control step */
} sfx_voice;

/* control rate: every 16 samples */
#define SFX_CTL  16
#define SFX__TAU 6.28318530718f

/* ==== 4. Small helpers ========================================================================================== */

/* v, kept inside low..high */
static inline float sfx__clamp(float v, float low, float high) {
  return v < low ? low : (v > high ? high : v);
}

/* a patch byte 0..127 as 0..1 */
static inline float sfx__unit(uint8_t v) {
  return (float)(v > 127 ? 127 : v) * (1.0f / 127.0f);
}

/* The next random number of the row *state (xorshift32); a state of 0 is given a start value. */
static inline uint32_t sfx__rand(uint32_t *state) {
  uint32_t x = *state ? *state : 0x9E3779B9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

/* white noise, -1..1 */
static inline float sfx__noise(uint32_t *state) {
  return (float)(int32_t)sfx__rand(state) * (1.0f / 2147483648.0f);
}

/* ==== 5. Making patches ========================================================================================= */

/* A plain patch of an engine: a short one-shot, nothing modulated, no effects. `engine` is taken modulo the count. */
static inline void sfx_patch_init(sfx_patch *p, int engine) {
  /* clang-format off */
  static const uint8_t DEFAULTS[SFX_P_COUNT] = {
    /* FREQ  P.AMT  P.DEC  A    B    NOISE  CUTOFF  RES  DECAY  CRUSH */
       60,   64,    40,    40,  40,  0,     110,    10,  60,    0,
    /* ATTACK  SUSTAIN  RELEASE  F.ENV  F.DEC */
       0,      0,       34,      64,    40,
    /* LFO: RATE  PITCH  FILTER  TIMBRE */
            50,   0,     0,      0,
    /* SPREAD  CHORUS  DELAY  REVERB  LEVEL  TABLE */
       0,      0,      0,     0,      100,   0,
  };
  /* clang-format on */
  p->version = SFX_PATCH_VERSION;
  p->engine = (uint8_t)(engine % SFX_ENGINE_COUNT);
  for (int i = 0; i < SFX_P_COUNT; i++) {
    p->p[i] = DEFAULTS[i];
  }
}

/* 1 = the sound is held while its key is (SUSTAIN above 0), 0 = a one-shot */
static inline int sfx_patch_sustained(const sfx_patch *p) {
  return p->p[SFX_P_SUSTAIN] > 0;
}

/* ==== 6. The patch on the wire =====================================================================================
   SFX_PATCH_BYTES (28) bytes, each 0..127:
     byte 0        SFX_PATCH_VERSION (2)
     byte 1        the engine, 0..4 (sfx_engine)
     byte 2..26    the 25 parameters, in the order of the SFX_P_* list
     byte 27       the checksum: the sum of bytes 0..26, its low 7 bits */

/* the checksum of a packed patch `b` (its first 27 bytes are read) */
static inline uint8_t sfx_patch_checksum(const uint8_t *b) {
  unsigned sum = 0;
  for (int i = 0; i < SFX_PATCH_BYTES - 1; i++) {
    sum += b[i];
  }
  return (uint8_t)(sum & 0x7F);
}

/* Writes patch `p` as its 28 bytes to `out`. Values out of range are brought into it. */
static inline void sfx_patch_pack(const sfx_patch *p, uint8_t out[SFX_PATCH_BYTES]) {
  out[0] = SFX_PATCH_VERSION;
  out[1] = (uint8_t)(p->engine % SFX_ENGINE_COUNT);
  for (int i = 0; i < SFX_P_COUNT; i++) {
    out[2 + i] = (uint8_t)(p->p[i] & 0x7F);
  }
  out[SFX_PATCH_BYTES - 1] = sfx_patch_checksum(out);
}

/* Reads the 28 bytes `in` into `p`.
   0 = taken, -1 = not a valid patch (wrong version, a byte out of range, checksum) – *p is untouched then. */
static inline int sfx_patch_unpack(sfx_patch *p, const uint8_t in[SFX_PATCH_BYTES]) {
  if (in[0] != SFX_PATCH_VERSION || in[1] >= SFX_ENGINE_COUNT) {
    return -1;
  }
  for (int i = 0; i < SFX_PATCH_BYTES; i++) {
    if (in[i] > 127) {
      return -1;
    }
  }
  if (in[SFX_PATCH_BYTES - 1] != sfx_patch_checksum(in)) {
    return -1;
  }
  p->version = in[0];
  p->engine = in[1];
  for (int i = 0; i < SFX_P_COUNT; i++) {
    p->p[i] = in[2 + i];
  }
  return 0;
}

/* ==== 7. MUTATE and RANDOM ====================================================================================== */

/* MUTATE: every parameter moves by at most +/- 15 % of its range. What kind of sound it is stays: the engine,
   the table, its level, and whether it is a one-shot or a sustained sound. `rng` is the caller's random state
   (sfx__rand). */
static inline void sfx_patch_mutate(sfx_patch *p, uint32_t *rng) {
  for (int i = 0; i < SFX_P_COUNT; i++) {
    if (i == SFX_P_LEVEL || i == SFX_P_TABLE || (i == SFX_P_SUSTAIN && p->p[i] == 0)) {
      continue;
    }
    /* 19 / 127 = 15 % */
    int v = (int)p->p[i] + (int)(sfx__rand(rng) % 39u) - 19;
    /* a sustained sound stays sustained: its SUSTAIN never reaches 0 */
    int low = i == SFX_P_SUSTAIN ? 1 : 0;
    p->p[i] = (uint8_t)(v < low ? low : (v > 127 ? 127 : v));
  }
}

/* RANDOM: a new one-shot from nothing, inside ranges that stay audible. `rng` as for sfx_patch_mutate. */
static inline void sfx_patch_random(sfx_patch *p, uint32_t *rng) {
  /* clang-format off */
  static const uint8_t LOWEST[SFX_P_COUNT] = {
    /* FREQ  P.AMT  P.DEC  A    B    NOISE  CUTOFF  RES  DECAY  CRUSH */
       30,   20,    20,    0,   0,   0,     50,     0,   30,    0,
    /* ATTACK  SUSTAIN  RELEASE  F.ENV  F.DEC */
       0,      0,       20,      44,    20,
    /* LFO: RATE  PITCH  FILTER  TIMBRE */
            20,   0,     0,      0,
    /* SPREAD  CHORUS  DELAY  REVERB  LEVEL  TABLE */
       0,      0,      0,     0,      100,   0,
  };
  static const uint8_t HIGHEST[SFX_P_COUNT] = {
    /* FREQ  P.AMT  P.DEC  A    B    NOISE  CUTOFF  RES  DECAY  CRUSH */
       100,  110,   100,   127, 127, 127,   127,    100, 105,   60,
    /* ATTACK  SUSTAIN  RELEASE  F.ENV  F.DEC */
       12,     0,       60,      100,   90,
    /* LFO: RATE  PITCH  FILTER  TIMBRE */
            90,   20,    40,     50,
    /* SPREAD  CHORUS  DELAY  REVERB  LEVEL  TABLE */
       60,     40,     40,    50,     100,   SFX_WT_TABLES - 1,
  };
  /* clang-format on */
  p->version = SFX_PATCH_VERSION;
  p->engine = (uint8_t)(sfx__rand(rng) % SFX_ENGINE_COUNT);
  for (int i = 0; i < SFX_P_COUNT; i++) {
    p->p[i] = (uint8_t)(LOWEST[i] + sfx__rand(rng) % (uint32_t)(HIGHEST[i] - LOWEST[i] + 1));
  }
  /* half of them clean, half of them without vibrato */
  if (sfx__rand(rng) & 1) {
    p->p[SFX_P_BITCRUSH] = 0;
  }
  if (sfx__rand(rng) & 1) {
    p->p[SFX_P_LFO_PITCH] = 0;
  }
}

/* ==== 8. Playing a voice ===========================================================================================
   The life of a voice: sfx_voice_trigger starts it, sfx_voice_render adds its samples block after block and says when
   it has ended; sfx_voice_release lets its key go; sfx_voice_set_patch gives a sounding voice new values. */

/* Gives voice `v` the patch `p`, and works out the parts of it that cost an exp2 and only change with the patch
   itself: the envelopes' factors, the LFO's step, the bitcrusher, the gain. v->sr must be set (a trigger does that). */
static inline void sfx_voice_set_patch(sfx_voice *v, const sfx_patch *p) {
  v->patch = *p;

  /* the pitch and filter envelopes: a time in seconds, then the factor per control step that falls to 1/e in it */
  float pitch_time = 0.005f * SFX_EXP2(sfx__unit(p->p[SFX_P_PITCH_ENV_DEC]) * 9.0f);
  v->pcoef = SFX_EXP2(-1.442695f * (float)SFX_CTL / (pitch_time * v->sr));
  float filter_time = 0.005f * SFX_EXP2(sfx__unit(p->p[SFX_P_FILTER_ENV_DEC]) * 9.0f);
  v->fcoef = SFX_EXP2(-1.442695f * (float)SFX_CTL / (filter_time * v->sr));

  /* loudness: the attack is a straight line (a step per sample), decay and release are factors per sample */
  v->ainc = 1.0f / (0.0015f * SFX_EXP2(sfx__unit(p->p[SFX_P_ATTACK]) * 11.4f) * v->sr);
  float decay_time = 0.010f * SFX_EXP2(sfx__unit(p->p[SFX_P_AMP_DECAY]) * 8.6f);
  v->acoef = SFX_EXP2(-1.442695f / (decay_time * v->sr));
  float release_time = 0.005f * SFX_EXP2(sfx__unit(p->p[SFX_P_RELEASE]) * 9.6f);
  v->rcoef = SFX_EXP2(-1.442695f / (release_time * v->sr));
  v->sus = sfx__unit(p->p[SFX_P_SUSTAIN]);

  v->lfo_dt = 0.1f * SFX_EXP2(sfx__unit(p->p[SFX_P_LFO_RATE]) * 8.2f) * (float)SFX_CTL / v->sr;

  /* bitcrush: how many samples one value is held, and to how many steps it is rounded (16 bit down to 4) */
  float crush = sfx__unit(p->p[SFX_P_BITCRUSH]);
  v->cr_hold = p->p[SFX_P_BITCRUSH] ? (1.0f + crush * crush * 40.0f) * (v->sr / 48000.0f) : 0.0f;
  v->cr_q = SFX_EXP2(15.0f - crush * 12.0f);

  float level = (float)p->p[SFX_P_LEVEL] * 0.01f;
  v->gain = level * level;
  v->unison = p->engine == SFX_ENGINE_WAVETABLE && p->p[SFX_P_SPREAD] > 0;
}

/* Starts voice `v` with patch `p` from silence.
     vel       velocity 0..1
     note_st   transposition in semitones
     slot      which of the player's sounds it is (0 for a voice by itself)
     sr        sample rate
     seed      for its noise; 0 is taken as 1. The same seed gives the same samples.
   A WAVETABLE patch needs sfx_wt_init() to have run once before. */
static inline void sfx_voice_trigger(sfx_voice *v, const sfx_patch *p, float vel, float note_st, int slot, float sr,
                                     uint32_t seed) {
  v->sr = sr;
  v->vel = vel;
  v->note_st = note_st;
  v->slot = slot;
  v->abs_oct = -1.0f;
  sfx_voice_set_patch(v, p);

  v->active = 1;
  v->gate = 1;
  v->stage = 0;

  /* the oscillators */
  v->fb = 0.0f;
  v->mphase = 0.0f;
  v->phase = 0.0f;
  v->uph[0] = 0.37f;
  v->uph[1] = 0.71f;
  v->wt1 = sfx_wt[0][0][0];
  v->wt0 = sfx_wt[0][0][0];
  v->wtf = 0.0f;
  v->wbend = 0.5f;

  /* the envelopes, the LFO, the filter */
  v->penv = 1.0f;
  v->fenv = 1.0f;
  v->aenv = 0.0f;
  v->lfo = 0.0f;
  v->ic2 = 0.0f;
  v->ic1 = 0.0f;

  /* the engines' clocks, the bitcrusher */
  v->arp_t = 0.0f;
  v->arp_step = 0;
  v->gr_t = 1.0f;
  v->gr_target = 1.0f;
  v->gr_amp = 1.0f;
  v->cr_cnt = 0.0f;
  v->cr_val = 0.0f;

  v->rng = seed ? seed : 1u;
  /* 0: the first sample begins with a control step */
  v->ctl = 0;
}

/* After a trigger: play the sound as a MIDI note (69 = 440 Hz), so every sound of a song is in tune with the others.
   (4.4594316 = log2(440 / 20): the octaves from 20 Hz up to the A.) */
static inline void sfx_voice_set_note(sfx_voice *v, int midi_note) {
  v->abs_oct = ((float)midi_note - 69.0f) * (1.0f / 12.0f) + 4.4594316f;
}

/* The key is let go: a sustained sound begins its release, a one-shot plays on to its end. */
static inline void sfx_voice_release(sfx_voice *v) {
  v->gate = 0;
}

/* ==== 9. The control step ==========================================================================================
   Every SFX_CTL samples: the LFO and the envelopes move on, and from the patch, the telemetry `m` and those, the
   values of the next 16 samples are worked out – the oscillators' phase steps (v->dt, v->mdt), the FM index (v->idx),
   the wavetable's frames, the filter's coefficients, the loudness (v->lvl). Called by sfx_voice_render only. */
static inline void sfx__control(sfx_voice *v, const sfx_mods *m) {
  const uint8_t *p = v->patch.p;
  int eng = v->patch.engine;
  float sr = v->sr;

  /* the LFO: its value now, then on by a step */
  float lfo = SFX_SIN(v->lfo);
  v->lfo += v->lfo_dt;
  if (v->lfo >= 1.0f) {
    v->lfo -= 1.0f;
  }
  float lfo_pitch = sfx__unit(p[SFX_P_LFO_PITCH]);
  float lfo_timbre = sfx__unit(p[SFX_P_LFO_TIMBRE]);

  /* the pitch in octaves above 20 Hz: the note or the patch's FREQ, transposed, with vibrato */
  float sweep_range = ((float)p[SFX_P_PITCH_ENV_AMT] - 64.0f) * (4.0f / 64.0f);
  float oct = (v->abs_oct >= 0.0f ? v->abs_oct : sfx__unit(p[SFX_P_BASE_FREQ]) * 9.5f) +
              (v->note_st + m->pitch_st) * (1.0f / 12.0f) + lfo * lfo_pitch * lfo_pitch;
  /* the pitch envelope's part now, in octaves */
  float sweep = sweep_range * v->penv;

  /* the filter's cutoff in octaves above 40 Hz: the knob, telemetry, its envelope, the LFO */
  float cut_oct = sfx__unit(p[SFX_P_FILTER_CUTOFF]) * 8.6f + m->cutoff_oct +
                  ((float)p[SFX_P_FILTER_ENV] - 64.0f) * (5.0f / 64.0f) * v->fenv +
                  lfo * sfx__unit(p[SFX_P_LFO_FILTER]) * 3.0f;

  /* what moves the sound's colour */
  float timbre = m->fm + lfo * lfo_timbre * 0.5f;

  /* what "A" and "B" do in this engine */
  if (eng == SFX_ENGINE_UI_BLIP) {
    /* an arpeggio upwards: 4 steps, then it stays. A = the interval in semitones, B = 2 .. 64 steps a second */
    int interval = (p[SFX_P_FM_RATIO] * 24 + 63) / 127;
    v->arp_dt = (float)SFX_CTL * (2.0f * SFX_EXP2(sfx__unit(p[SFX_P_FM_DEPTH]) * 5.0f)) / sr;
    v->arp_t += v->arp_dt;
    if (v->arp_t >= 1.0f) {
      v->arp_t -= 1.0f;
      if (v->arp_step < 3) {
        v->arp_step++;
      }
    }
    oct += (float)(v->arp_step * interval) * (1.0f / 12.0f);
    v->idx = 0.0f;
    v->mdt = 0.0f;
  } else if (eng == SFX_ENGINE_FOLEY_NOISE) {
    /* the sweep moves the band, not a pitch. A = 2 .. 256 grains a second (B, the grains' depth: sfx_voice_render) */
    cut_oct += sweep;
    v->gr_dt = (2.0f * SFX_EXP2(sfx__unit(p[SFX_P_FM_RATIO]) * 7.0f)) / sr;
    v->idx = 0.0f;
    v->mdt = 0.0f;
  } else if (eng == SFX_ENGINE_WAVETABLE) {
    /* A = the position in the table: the two frames around it, in the copy with as many harmonics as this pitch
       can carry */
    float pos = sfx__clamp(sfx__unit(p[SFX_P_FM_RATIO]) + timbre, 0.0f, 1.0f) * (float)(SFX_WT_FRAMES - 1);
    int f0 = (int)pos;
    if (f0 > SFX_WT_FRAMES - 2) {
      f0 = SFX_WT_FRAMES - 2;
    }
    float dt = sfx__clamp(20.0f * SFX_EXP2(oct + sweep) / sr, 0.0f, 0.45f);
    int table = p[SFX_P_TABLE] % SFX_WT_TABLES;
    v->wt0 = sfx_wt_frame(table, f0, dt);
    v->wt1 = sfx_wt_frame(table, f0 + 1, dt);
    v->wtf = pos - (float)f0;
    /* B = warp: the cycle's first half squeezed */
    v->wbend = 0.5f - 0.45f * sfx__unit(p[SFX_P_FM_DEPTH]);
    v->idx = 0.0f;
    v->mdt = 0.0f;
  } else {
    /* IMPACT and SCIFI: B = the FM index, in radians here.
       (This is the branch of every engine not named above: a new engine needs a branch of its own, or it lands
       here without a word.) */
    float d = sfx__clamp(sfx__unit(p[SFX_P_FM_DEPTH]) + timbre, 0.0f, 2.0f);
    float idx = d * d * (eng == SFX_ENGINE_SCIFI_FM ? 24.0f : 8.0f);
    if (eng == SFX_ENGINE_SCIFI_FM) {
      /* the index falls with the sweep: a laser */
      idx *= 0.25f + 0.75f * v->penv;
    }
    v->idx = idx * (1.0f / SFX__TAU);
  }

  /* the carrier's frequency as a phase step, at most 0.45 of the sample rate; A = the modulator's ratio to it */
  float f = 20.0f * SFX_EXP2(oct + (eng == SFX_ENGINE_FOLEY_NOISE ? 0.0f : sweep));
  v->dt = sfx__clamp(f / sr, 0.0f, 0.45f);
  if (eng == SFX_ENGINE_IMPACT || eng == SFX_ENGINE_SCIFI_FM) {
    v->mdt = sfx__clamp(v->dt * 0.25f * SFX_EXP2(sfx__unit(p[SFX_P_FM_RATIO]) * 6.0f), 0.0f, 0.45f);
  }

  /* the filter's coefficients. g = tan(pi fc / sr), as sin / cos (w is that angle in turns) */
  float fc = sfx__clamp(40.0f * SFX_EXP2(cut_oct), 30.0f, sr * 0.45f);
  float w = 0.5f * fc / sr;
  float g = SFX_SIN(w) / SFX_SIN(w + 0.25f);
  float r = sfx__unit(p[SFX_P_FILTER_RES]);
  v->k = 1.0f / (0.5f + r * r * 15.0f);
  v->a1 = 1.0f / (1.0f + g * (g + v->k));
  v->a2 = g * v->a1;
  v->a3 = g * v->a2;

  /* the loudness, and the envelopes on by a step */
  v->lvl = sfx__clamp(m->level, 0.0f, 1.0f) * v->vel * v->gain;
  v->penv *= v->pcoef;
  v->fenv *= v->fcoef;
}

/* ==== 10. The sample loop ======================================================================================= */

/* What smooths the jump of a square wave so that it does not alias (polyBLEP): the correction at phase `t` (turns)
   for a wave that moves `dt` of a cycle per sample; 0 away from the jump. */
static inline float sfx__blep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

/* One cycle of the wavetable at phase `ph` (0..1): between two frames, and between two samples of each. */
static inline float sfx__wt_read(const sfx_voice *v, float ph) {
  /* warp: the phase up to v->wbend plays the cycle's first half, the rest its second */
  float bend = v->wbend;
  float warped = ph < bend ? ph * 0.5f / bend : 0.5f + (ph - bend) * 0.5f / (1.0f - bend);

  /* the sample before that place, and how far to the next */
  float at = warped * (float)SFX_WT_SIZE;
  int i = (int)at;
  float fr = at - (float)i;
  if (i >= SFX_WT_SIZE) {
    i = SFX_WT_SIZE - 1;
    fr = 1.0f;
  }

  float a = v->wt0[i] + (v->wt0[i + 1] - v->wt0[i]) * fr;
  float c = v->wt1[i] + (v->wt1[i + 1] - v->wt1[i]) * fr;
  return a + (c - a) * v->wtf;
}

/* ADDS `n` samples of voice `v` to out[] (mono). `m` is the telemetry (never NULL; neutral: 0, 0, 0, 1).
   Returns 1 while it sounds, 0 when it has ended (or was not active) – the rest of out[] is left as it was then.
   This is the signal chain: source -> noise -> filter -> grains (FOLEY) -> drive -> bitcrush -> loudness. */
static inline int sfx_voice_render(sfx_voice *v, const sfx_mods *m, float *out, int n) {
  if (!v->active) {
    return 0;
  }
  const int eng = v->patch.engine;
  const float noise_mix = sfx__unit(v->patch.p[SFX_P_NOISE_MIX]);
  const float grain_depth = sfx__unit(v->patch.p[SFX_P_FM_DEPTH]);
  /* up to 30 cents either way */
  const float spread = sfx__unit(v->patch.p[SFX_P_SPREAD]) * 0.0175f;
  const float drive = eng == SFX_ENGINE_IMPACT ? 2.2f : eng == SFX_ENGINE_WAVETABLE ? 1.0f : 1.3f;

  for (int i = 0; i < n; i++) {
    /* every SFX_CTL samples: pitch, timbre and filter anew */
    if (v->ctl <= 0) {
      sfx__control(v, m);
      v->ctl = SFX_CTL;
    }
    v->ctl--;

    /* the source */
    float x;
    if (eng == SFX_ENGINE_UI_BLIP) {
      /* a square wave: its two jumps a cycle are smoothed */
      float t2 = v->phase + 0.5f;
      if (t2 >= 1.0f) {
        t2 -= 1.0f;
      }
      x = (v->phase < 0.5f ? 1.0f : -1.0f) + sfx__blep(v->phase, v->dt) - sfx__blep(t2, v->dt);
      x *= 0.6f;
    } else if (eng == SFX_ENGINE_FOLEY_NOISE) {
      /* a tone under the noise, when NOISE is turned down */
      x = SFX_SIN(v->phase) * 0.5f;
    } else if (eng == SFX_ENGINE_WAVETABLE) {
      x = sfx__wt_read(v, v->phase);
      if (v->unison) {
        x = (x + sfx__wt_read(v, v->uph[0]) + sfx__wt_read(v, v->uph[1])) * 0.5f;
        v->uph[0] += v->dt * (1.0f + spread);
        if (v->uph[0] >= 1.0f) {
          v->uph[0] -= 1.0f;
        }
        v->uph[1] += v->dt * (1.0f - spread);
        if (v->uph[1] >= 1.0f) {
          v->uph[1] -= 1.0f;
        }
      }
      x *= 0.8f;
    } else {
      /* IMPACT and SCIFI: a sine, its phase moved by another sine (SCIFI: which also moves itself).
         (Again the branch of every engine not named above: a new engine needs its own.) */
      float mod = SFX_SIN(v->mphase + v->fb * 0.15f);
      if (eng == SFX_ENGINE_SCIFI_FM) {
        v->fb = mod;
      }
      x = SFX_SIN(v->phase + mod * v->idx);
      v->mphase += v->mdt;
      if (v->mphase >= 1.0f) {
        v->mphase -= 1.0f;
      }
    }
    v->phase += v->dt;
    if (v->phase >= 1.0f) {
      v->phase -= 1.0f;
    }

    /* noise: mixed in, up to noise alone */
    x += (sfx__noise(&v->rng) * 1.4f - x) * noise_mix;

    /* the filter: low-pass, FOLEY a band-pass (the 1e-18 keeps its state from becoming a denormal) */
    float v3 = x - v->ic2;
    float v1 = v->a1 * v->ic1 + v->a2 * v3;
    float v2 = v->ic2 + v->a2 * v->ic1 + v->a3 * v3;
    v->ic1 = 2.0f * v1 - v->ic1 + 1e-18f;
    v->ic2 = 2.0f * v2 - v->ic2 + 1e-18f;
    x = eng == SFX_ENGINE_FOLEY_NOISE ? v1 * (0.6f + v->k) * 2.0f : v2;

    /* grains: a new random loudness at the grain rate */
    if (eng == SFX_ENGINE_FOLEY_NOISE) {
      v->gr_t += v->gr_dt;
      if (v->gr_t >= 1.0f) {
        v->gr_t -= 1.0f;
        float r = sfx__noise(&v->rng);
        v->gr_target = r * r;
      }
      v->gr_amp += (v->gr_target - v->gr_amp) * 0.01f;
      x *= 1.0f - grain_depth + grain_depth * v->gr_amp * 2.5f;
    }

    /* drive */
    x = SFX_TANH(x * drive);

    /* bitcrush: fewer samples, fewer bits */
    if (v->cr_hold > 0.0f) {
      v->cr_cnt += 1.0f;
      if (v->cr_cnt >= v->cr_hold) {
        v->cr_cnt -= v->cr_hold;
        float q = x * v->cr_q;
        v->cr_val = (float)(int)(q + (q >= 0.0f ? 0.5f : -0.5f)) / v->cr_q;
      }
      x = v->cr_val;
    }

    /* loudness: attack, then decay – to silence (a one-shot), or to the sustain level until the key is let go */
    if (v->sus > 0.0f && !v->gate) {
      v->stage = 2;
    }
    if (v->stage == 0) {
      v->aenv += v->ainc;
      if (v->aenv >= 1.0f) {
        v->aenv = 1.0f;
        v->stage = 1;
      }
    } else if (v->stage == 1) {
      v->aenv = v->sus + (v->aenv - v->sus) * v->acoef;
    } else {
      v->aenv *= v->rcoef;
    }
    out[i] += x * v->aenv * v->lvl * 0.6f;

    /* past the attack and faded away: the voice has ended */
    if (v->stage && v->aenv < 0.0002f) {
      v->active = 0;
      return 0;
    }
  }
  return 1;
}

#endif
