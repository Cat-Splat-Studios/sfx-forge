/* SPDX-License-Identifier: MIT
 * SFX FORGE – sound effects and adaptive music for games, on VIBE FM.
 *
 *   16 pads      16 sounds, each a 13-byte patch (core/sfx_core.h): IMPACT, UI BLIP, SCI-FI FM, FOLEY
 *   keys         the selected sound as an instrument, in tune
 *   K1-K6        pages TONE · COLOR · FILTER · LINK · SEQ · SONG, encoders 1-4 = the four values shown
 *   SEQ          a 16-step pattern over the 16 sounds: the pads are the steps of the selected sound, a step may carry a
 *                note (hold its pad and play a key, or the NOTE encoder)
 *   SONG         8 patterns, a chain, and what makes it game music: every sound has a LAYER threshold and plays only
 *                while INTENSITY is at or above it; a cued pattern takes over on the next bar line
 *   LINK         the game engine's side: telemetry in (CC 10-15, channel 2: pitch, cutoff, FM, level, intensity,
 *                pattern), encoders out as CC 20-25, patches and the song as SysEx (README.md)
 *   the stage    the same, big (plugin and browser)
 *
 * Two flows: the player (core/sfx_song.h: voices and sequencer) belongs to the audio. The controls reach it through one
 * queue; the song itself they edit in place, one small value at a time.
 */
#include "vibe.h"
#include "vibe_dsp.h"
#include "vibe_gfx.h"
#include <stddef.h>
size_t strlen(const char *s);                      /* (the SDK brings these two) */
char *strcpy(char *d, const char *s);

#include "../core/sfx_song.h"

VIBE_PARALLEL_AUDIO

#define SLOTS  SFX_TRACKS
#define STEPS  SFX_STEPS

/* ---- the protocol (README: "The link") --------------------------------------------------------------------------- */
#define SX_DUMP     0x01           /* out (and in): F0 7D 56 42 01 <slot> <13 patch bytes> F7 */
#define SX_LOAD     0x02           /* in:           F0 7D 56 42 02 <slot> <13 patch bytes> F7 */
#define SX_TEXT     0x03           /* in:           F0 7D 56 42 03 <ASCII, up to 24> F7 */
#define SX_REQUEST  0x04           /* in:           F0 7D 56 42 04 <slot> F7  -> answers with a dump */
#define SX_SONG     0x05           /* both ways:    F0 7D 56 42 05 <chunk> <chunks> <up to 200 song bytes> F7 */
#define SX_REQ_SONG 0x06           /* in:           F0 7D 56 42 06 F7 -> answers with 16 dumps and the song */
#define SONG_CHUNK  200
#define SONG_CHUNKS ((SFX_SONG_BYTES + SONG_CHUNK - 1) / SONG_CHUNK)
#define CC_TELEMETRY 10            /* in, channel 2: CC 10..17 */
#define CC_ENCODERS  20            /* out, channel 1: CC 20..25 */
#define NOTE_PADS    36            /* pads <-> notes 36..51 */

/* ---- parameters the DAW automates: the same a game moves over MIDI ------------------------------------------------ */
enum { P_VOLUME, P_MOD_PITCH, P_MOD_CUTOFF, P_MOD_FM, P_MOD_LEVEL, P_INTENSITY, P_COUNT };
static const vibe_param PARAMS[P_COUNT] = {
  { "volume",     "Volume",     0.0f,  1.0f, 0.8f, "",    0, 0 },
  { "mod_pitch",  "Mod Pitch",  0.0f, 24.0f, 0.0f, "st",  0, 0 },
  { "mod_cutoff", "Mod Cutoff", -4.0f, 4.0f, 0.0f, "oct", 0, 0 },
  { "mod_fm",     "Mod FM",     0.0f,  1.0f, 0.0f, "",    0, 0 },
  { "mod_level",  "Mod Level",  0.0f,  1.0f, 1.0f, "",    0, 0 },
  { "intensity",  "Intensity",  0.0f,  1.0f, 1.0f, "",    0, 0 },
};
static const char *const GROUPS[P_COUNT] = { "OUTPUT", "TELEMETRY", "TELEMETRY", "TELEMETRY", "TELEMETRY", "MUSIC" };

/* ---- the sounds it starts with: engine, then FREQ P.AMT P.DEC RATIO DEPTH NOISE CUTOFF RESO DECAY CRUSH ------------ */
static const uint8_t FACTORY[SLOTS][1 + SFX_P_COUNT] = {
  { 0, 28,  96, 55,  20,  60,  70,  85, 10, 100,  0 },   /* explosion */
  { 0, 32, 100, 28,   0,  20,  25,  90,  5,  55,  0 },   /* punch */
  { 0, 24,  90, 35,   0,   0,   5,  70,  0,  70,  0 },   /* thud */
  { 0, 62,  70, 20,  75,  80,  15, 110, 40,  75,  0 },   /* metal hit */
  { 1, 86,  64,  0,  26,  60,   0, 127,  0,  62,  0 },   /* coin */
  { 1, 78,  64,  0,   0,   0,   0, 120,  0,  35,  0 },   /* select */
  { 1, 19,  64,  0,   0,   0,   0,  78, 25,  60,  0 },   /* bass */
  { 1, 60,  64,  0,   0,   0,   0, 108, 10,  56, 15 },   /* lead */
  { 2, 70, 110, 45,  50,  70,   0, 120, 20,  58,  0 },   /* laser */
  { 2, 80, 120, 25, 100,  90,  10, 127,  0,  45, 30 },   /* zap */
  { 2, 22,  64,  0,  21,  55,   8,  78, 45, 127,  0 },   /* engine hum – HOLD: made for telemetry */
  { 2, 66,  30, 90,  40,  60,   0, 110, 30, 100,  0 },   /* warp */
  { 3, 30,  80, 25,  90,  40, 127,  62, 15,  45,  0 },   /* footstep */
  { 3, 30,  64,  0,  20,  70, 127,  75, 55, 127,  0 },   /* wind – HOLD */
  { 3, 30,  30, 85,   0,   0, 127,  90, 45,  88,  0 },   /* whoosh */
  { 3, 30,  64,  0, 110, 127, 127, 105, 20, 100, 25 },   /* crackle */
};
static const char *const ENGINE_NAME[SFX_ENGINE_COUNT] = { "IMPACT", "UI BLIP", "SCI-FI FM", "FOLEY" };
static const uint32_t ENGINE_RGB[SFX_ENGINE_COUNT] = { 0xFF5A36, 0xFFD23F, 0x36C5FF, 0x7BE07B };
static const char *const SCALE_NAME[SFX_SCALE_COUNT] = { "CHROMATIC", "MAJOR", "MINOR", "PENTATONIC" };

/* ---- the audio flow's own: the player. Its song is edited by the controls (single values, they alone write them) ---- */
static sfx_player P;
#define SONG P.song
static float SR, mix[VIBE_MAX_BLOCK];
static sfx_mods mods = { 0, 0, 0, 1 };
static float vol_s;
/* what the audio shows the controls: each a 32-bit value only the audio writes */
#define NO_STEP 0xFFFFFFFFu
#define SCOPE 164
static volatile uint32_t seq_step = NO_STEP, seq_pattern, playing;   /* playing: bit per sound */
static volatile float seq_frac;
static volatile float scope_lo[SCOPE], scope_hi[SCOPE];
static volatile uint32_t scope_w;
static float sc_lo, sc_hi; static int sc_n;

/* ---- the control flow's own ---------------------------------------------------------------------------------------- */
static sfx_patch patches[SLOTS], undo[SLOTS];
static uint8_t has_undo[SLOTS];
static int sel, page, help, shift, rec, running;
static int edit_pat, cur_step, held_step = -1;      /* the pattern and the step being edited; the step pad held */
static float acc[VE_COUNT];                         /* encoder turns not yet a whole step */
static uint32_t rng = 0x5F3759DFu;
static uint8_t telemetry[8], cc_out[6] = { 64, 64, 64, 64, 64, 64 };
static int linked;                                  /* something came in from the other side */
static char link_text[25];
static const char *toast; static int toast_frames;
static uint32_t led_cache[SLOTS];
static int stage_pad = -1;                          /* the pad the mouse holds on the stage */
static int export_at = -1;                          /* sending the sounds and the song: the next message, -1 = not */
static uint8_t song_rx[SFX_SONG_BYTES]; static int song_rx_next;
enum { PAGE_TONE, PAGE_COLOR, PAGE_FILTER, PAGE_LINK, PAGE_SEQ, PAGE_SONG, PAGE_COUNT };
static const char *const PAGE_NAME[PAGE_COUNT] = { "TONE", "COLOR", "FILTER", "LINK", "SEQ", "SONG" };
static const char *const SOFT[3][6] = { { "ENGINE", "MUTATE", "UNDO", "RANDOM", "EXPORT", "SAVE" },
                                        { "PLAY", "REC", "CLEAR", "CLR PAT", "DEMO", "SAVE" },
                                        { "PLAY", "COPY", "CLR PAT", "DEMO", "SEND", "SAVE" } };
static int soft_set(void) { return page == PAGE_SEQ ? 1 : page == PAGE_SONG ? 2 : 0; }

/* ---- from the controls to the audio: one queue, one writer, one reader ---------------------------------------------- */
enum { M_TRIGGER, M_RELEASE, M_UPDATE, M_START, M_STOP, M_CUE };
typedef struct { uint8_t type, slot, note, pad_; int16_t key; float vel; sfx_patch patch; } msg;
#define QN 64
static msg q[QN];
static volatile uint32_t qw, qr;

static void send(int type, int slot, int key, float vel, int note)
{
  uint32_t w = qw;
  if (w - qr >= QN) return;                          /* full: dropped */
  msg *m = &q[w % QN];
  m->type = (uint8_t)type; m->slot = (uint8_t)slot; m->key = (int16_t)key; m->vel = vel; m->note = (uint8_t)note;
  m->patch = patches[slot & 15];
  VIBE_RELEASE();
  qw = w + 1;
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  (void)in;
  uint32_t w = qw;
  VIBE_ACQUIRE();
  for (uint32_t k = qr; k != w; k++) {
    const msg *m = &q[k % QN];
    switch (m->type) {
    case M_TRIGGER: sfx_player_note_on(&P, m->slot, m->key, m->vel, m->note, 0.0f, &m->patch); break;
    case M_RELEASE: sfx_player_note_off(&P, m->key); break;
    case M_UPDATE:  sfx_player_set_patch(&P, m->slot, &m->patch); break;   /* a knob turned: also for what sounds now */
    case M_START:   sfx_player_start(&P, m->slot); break;
    case M_STOP:    sfx_player_stop(&P); break;
    case M_CUE:     sfx_player_cue(&P, m->slot); break;
    }
  }
  VIBE_RELEASE(); qr = w;

  float c = vd_coef(0.03f, SR / (float)frames);     /* the telemetry glides: 30 ms, no steps */
  mods.pitch_st   = vibe_param_get(P_MOD_PITCH)  + (mods.pitch_st   - vibe_param_get(P_MOD_PITCH))  * c;
  mods.cutoff_oct = vibe_param_get(P_MOD_CUTOFF) + (mods.cutoff_oct - vibe_param_get(P_MOD_CUTOFF)) * c;
  mods.fm         = vibe_param_get(P_MOD_FM)     + (mods.fm         - vibe_param_get(P_MOD_FM))     * c;
  mods.level      = vibe_param_get(P_MOD_LEVEL)  + (mods.level      - vibe_param_get(P_MOD_LEVEL))  * c;
  P.mods = mods;
  P.intensity = vibe_param_get(P_INTENSITY);

  /* the DAW's song position while it plays, else the player's own clock */
  const vibe_transport_info *tp = vibe_transport();
  int host = tp->has && tp->playing && tp->bpm > 0.0f;
  for (int n = 0; n < frames; n++) mix[n] = 0.0f;
  playing = sfx_player_render(&P, mix, frames, host ? tp->beat * 4.0 : -1.0, host ? (double)tp->bpm / 60.0 * 4.0 / (double)SR : 0.0);
  seq_step = P.running && P.step >= 0 ? (uint32_t)P.step : NO_STEP; seq_frac = P.frac; seq_pattern = (uint32_t)P.pattern;

  float vol = vibe_param_get(P_VOLUME), vc = vd_coef(0.01f, SR);
  for (int n = 0; n < frames; n++) {
    vol_s = vol + (vol_s - vol) * vc;
    float o = vd_tanh(mix[n] * vol_s * 1.25f);
    out_l[n] = o; out_r[n] = o;
    if (o < sc_lo) sc_lo = o;
    if (o > sc_hi) sc_hi = o;
    if (++sc_n >= 48) { uint32_t s = scope_w; scope_lo[s % SCOPE] = sc_lo; scope_hi[s % SCOPE] = sc_hi; scope_w = s + 1; sc_lo = sc_hi = o; sc_n = 0; }
  }
}

/* ---- sounds and the song: edit, keep, send ---------------------------------------------------------------------------- */
static void say(const char *t) { toast = t; toast_frames = 70; }
static void changed(int slot) { send(M_UPDATE, slot, 0, 0, 0); }
static void keep_undo(int slot) { undo[slot] = patches[slot]; has_undo[slot] = 1; }
static sfx_pattern *pat(void) { return &SONG.pat[edit_pat]; }

static void step_set(sfx_pattern *p, int step, int slot, int note) { p->on[step] |= (uint16_t)(1u << slot); p->note[slot][step] = (uint8_t)note; }

/* a song to start from: a beat, a bass, and a lead and effects that only come in when the game heats up */
static void demo_song(void)
{
  static const uint8_t BASS_A[STEPS] = { 33, 0, 0, 33, 0, 0, 36, 0, 31, 0, 0, 31, 0, 0, 40, 0 }, BASS_B[STEPS] = { 29, 0, 0, 29, 0, 0, 33, 0, 31, 0, 0, 31, 0, 35, 0, 38 };
  static const uint8_t LEAD_A[STEPS] = { 69, 0, 72, 0, 76, 0, 0, 74, 72, 0, 69, 0, 67, 0, 69, 0 }, LEAD_B[STEPS] = { 72, 0, 77, 0, 81, 0, 79, 0, 74, 0, 79, 0, 83, 0, 81, 79 };
  sfx_song_init(&SONG);
  for (int b = 0; b < 2; b++) {
    sfx_pattern *p = &SONG.pat[b];
    for (int i = 0; i < STEPS; i++) {
      if (i % 4 == 0 || (b && i == 14)) step_set(p, i, 2, 0);               /* thud: the kick */
      if (i % 8 == 4) step_set(p, i, 1, 0);                                 /* punch: the snare */
      if (i % 4 == 2) step_set(p, i, 12, 0);                                /* footstep: the hat */
      if ((b ? BASS_B : BASS_A)[i]) step_set(p, i, 6, (b ? BASS_B : BASS_A)[i]);
      if ((b ? LEAD_B : LEAD_A)[i]) step_set(p, i, 7, (b ? LEAD_B : LEAD_A)[i]);
    }
    step_set(p, b ? 11 : 7, 4, 0);                                          /* coin */
    step_set(p, 15, 8, 0);                                                  /* laser */
  }
  SONG.chain = 2;
  SONG.level[12] = 20; SONG.level[6] = 35; SONG.level[1] = 50; SONG.level[7] = 80; SONG.level[4] = 105; SONG.level[8] = 105;
}

static void factory(void)
{
  for (int s = 0; s < SLOTS; s++) {
    patches[s].version = SFX_PATCH_VERSION; patches[s].engine = FACTORY[s][0];
    for (int i = 0; i < SFX_P_COUNT; i++) patches[s].p[i] = FACTORY[s][1 + i];
  }
  demo_song();
}

static void bank_save(void)
{
  static uint8_t b[SLOTS * SFX_PATCH_BYTES], s[SFX_SONG_BYTES];
  for (int i = 0; i < SLOTS; i++) sfx_patch_pack(&patches[i], b + i * SFX_PATCH_BYTES);
  vibe_store_write("bank.bin", b, sizeof b);
  sfx_song_pack(&SONG, s);
  vibe_store_write("song.bin", s, sizeof s);
}

static void bank_load(void)
{
  static uint8_t b[SLOTS * SFX_PATCH_BYTES], s[SFX_SONG_BYTES];
  if (vibe_store_read("bank.bin", b, sizeof b) == (int)sizeof b)
    for (int i = 0; i < SLOTS; i++) sfx_patch_unpack(&patches[i], b + i * SFX_PATCH_BYTES);   /* a broken one keeps its factory sound */
  if (vibe_store_read("song.bin", s, sizeof s) == (int)sizeof s) sfx_song_unpack(&SONG, s, sizeof s);
}

static int sysex_dump(int slot)
{
  uint8_t b[7 + SFX_PATCH_BYTES] = { 0xF0, 0x7D, 0x56, 0x42, SX_DUMP, (uint8_t)slot };
  sfx_patch_pack(&patches[slot], b + 6);
  b[6 + SFX_PATCH_BYTES] = 0xF7;
  return vibe_midi_send_sysex(b, sizeof b);
}

/* the 16 sounds, then the song in pieces. The host takes one SysEx a block, so this is called again and again (from
   every draw and every event) until all are out. */
static void export_pump(void)
{
  static uint8_t song[SFX_SONG_BYTES], b[8 + SONG_CHUNK];
  for (int guard = 0; guard < 2 && export_at >= 0; guard++) {
    if (export_at < SLOTS) { if (sysex_dump(export_at)) return; }
    else {
      int k = export_at - SLOTS, n = k == SONG_CHUNKS - 1 ? SFX_SONG_BYTES - k * SONG_CHUNK : SONG_CHUNK;
      if (k == 0) sfx_song_pack(&SONG, song);
      b[0] = 0xF0; b[1] = 0x7D; b[2] = 0x56; b[3] = 0x42; b[4] = SX_SONG; b[5] = (uint8_t)k; b[6] = SONG_CHUNKS;
      for (int i = 0; i < n; i++) b[7 + i] = song[k * SONG_CHUNK + i];
      b[7 + n] = 0xF7;
      if (vibe_midi_send_sysex(b, 8 + n)) return;
    }
    if (++export_at >= SLOTS + SONG_CHUNKS) { export_at = -1; say("SONG SENT"); }
  }
}

static void sysex_in(const uint8_t *b, int n)
{
  if (n < 6 || b[0] != 0xF0 || b[1] != 0x7D || b[2] != 0x56 || b[3] != 0x42 || b[n - 1] != 0xF7) return;
  linked = 1;
  if ((b[4] == SX_LOAD || b[4] == SX_DUMP) && n == 7 + SFX_PATCH_BYTES && b[5] < SLOTS) {
    sfx_patch p;
    if (sfx_patch_unpack(&p, b + 6)) { say("BAD PATCH"); return; }
    keep_undo(b[5]); patches[b[5]] = p; sel = b[5]; changed(sel); say("PATCH LOADED");
  } else if (b[4] == SX_TEXT) {
    int k = 0;
    for (int i = 5; i < n - 1 && k < 24; i++) link_text[k++] = b[i] >= 32 && b[i] < 127 ? (char)b[i] : ' ';
    link_text[k] = 0;
  } else if (b[4] == SX_REQUEST && n == 7 && b[5] < SLOTS) sysex_dump(b[5]);
  else if (b[4] == SX_REQ_SONG) { export_at = 0; export_pump(); }
  else if (b[4] == SX_SONG && n > 8 && b[6] == SONG_CHUNKS) {                 /* a song from the game, piece by piece */
    int k = b[5], len = n - 8;
    if (k == 0) song_rx_next = 0;
    if (k != song_rx_next || k * SONG_CHUNK + len > SFX_SONG_BYTES) { song_rx_next = -1; return; }
    for (int i = 0; i < len; i++) song_rx[k * SONG_CHUNK + i] = b[7 + i];
    if (++song_rx_next == SONG_CHUNKS) { say(sfx_song_unpack(&SONG, song_rx, SFX_SONG_BYTES) ? "BAD SONG" : "SONG LOADED"); song_rx_next = -1; }
  }
}

/* ---- playing -------------------------------------------------------------------------------------------------------- */
static void record(int slot, int note)               /* REC while it runs: onto the nearest step of the pattern that plays */
{
  uint32_t st = seq_step;
  if (!rec || st == NO_STEP) return;
  if (seq_frac > 0.5f) st = (st + 1) % SONG.len;
  step_set(&SONG.pat[seq_pattern % SFX_PATTERNS], (int)(st % STEPS), slot, note);
}

static void pad(int slot, int down, float vel)
{
  if (!down) { send(M_RELEASE, slot, slot, 0, 0); return; }
  sel = slot;
  send(M_TRIGGER, slot, slot, vel > 0.0f ? vel : 0.8f, 0);
  record(slot, 0);
}

static void seq_play(int on)
{
  running = on;
  send(on ? M_START : M_STOP, edit_pat, 0, 0, 0);
  vibe_led_button(VB_PLAY, 0x6EE7A8, on ? 1.0f : 0.0f);
}

static void seq_rec(int on) { rec = on; vibe_led_button(VB_REC, 0xFF4040, on ? 1.0f : 0.0f); }
static void pattern_pick(int p) { edit_pat = (p % SFX_PATTERNS + SFX_PATTERNS) % SFX_PATTERNS; send(M_CUE, edit_pat, 0, 0, 0); }
static void intensity_set(float v) { vibe_param_set(P_INTENSITY, vd_clamp(v, 0.0f, 1.0f)); }
static void step_toggle(int step, int slot) { pat()->on[step] ^= (uint16_t)(1u << slot); cur_step = step; }

static void note_turn(int d)                         /* the cursor step's note, along the scale; it switches the step on */
{
  int n = pat()->note[sel][cur_step];
  for (int i = 0; i < (d < 0 ? -d : d); i++) n = sfx_scale_step(n, d, SONG.scale);
  step_set(pat(), cur_step, sel, n);
}

static void set_page(int p)
{
  page = p; held_step = -1;
  for (int i = 0; i < 6; i++) vibe_label(VB_F1 + i, SOFT[soft_set()][i]);
  for (int i = 0; i < SLOTS; i++) led_cache[i] = 0xFFFFFFFFu;
}

static int param_of(int enc) { return page == PAGE_TONE ? (int[]){ SFX_P_BASE_FREQ, SFX_P_PITCH_ENV_AMT, SFX_P_PITCH_ENV_DEC, SFX_P_AMP_DECAY }[enc]
                                    : page == PAGE_COLOR ? (int[]){ SFX_P_FM_RATIO, SFX_P_FM_DEPTH, SFX_P_NOISE_MIX, SFX_P_BITCRUSH }[enc]
                                    : page == PAGE_FILTER && enc < 2 ? SFX_P_FILTER_CUTOFF + enc : -1; }
static int wrap(int v, int d, int n) { return ((v + d) % n + n) % n; }
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

static void encoder(int id, float turn)
{
  acc[id] += turn * (shift ? 8.0f : 1.0f);
  int d = (int)acc[id];
  if (!d) return;
  acc[id] -= (float)d;
  if (id == VE_DATA) { sel = wrap(sel, d, SLOTS); return; }
  if (id == VE_A && page != PAGE_LINK) { vibe_param_set(P_VOLUME, vd_clamp(vibe_param_get(P_VOLUME) + (float)d * 0.02f, 0.0f, 1.0f)); return; }
  if (page == PAGE_LINK) {                           /* the encoders are the game engine's: CC 20..25 */
    int k = id <= VE_4 ? id : id == VE_A ? 4 : id == VE_B ? 5 : -1;
    if (k < 0) return;
    cc_out[k] = (uint8_t)clampi(cc_out[k] + d, 0, 127);
    vibe_midi_send(0xB0, (uint8_t)(CC_ENCODERS + k), cc_out[k]);
    return;
  }
  if (page == PAGE_SEQ) {
    if (id == VE_1) cur_step = wrap(cur_step, d, STEPS);
    if (id == VE_2) note_turn(d);
    if (id == VE_3) SONG.bpm = (uint16_t)clampi(SONG.bpm + d, 40, 240);
    if (id == VE_4) SONG.swing = (uint8_t)clampi(SONG.swing + d, 0, 100);
    if (id == VE_B) SONG.scale = (uint8_t)wrap(SONG.scale, d, SFX_SCALE_COUNT);
    return;
  }
  if (page == PAGE_SONG) {
    if (id == VE_1) pattern_pick(edit_pat + d);
    if (id == VE_2) SONG.chain = (uint8_t)clampi(SONG.chain + d, 1, SFX_PATTERNS);
    if (id == VE_3) intensity_set(vibe_param_get(P_INTENSITY) + (float)d * 0.02f);
    if (id == VE_4) SONG.level[sel] = (uint8_t)clampi(SONG.level[sel] + d * 2, 0, 127);
    if (id == VE_B) SONG.len = (uint8_t)clampi(SONG.len + d, 1, STEPS);
    return;
  }
  if (id > VE_4) return;
  sfx_patch *p = &patches[sel];
  int k = param_of(id);
  if (k >= 0) p->p[k] = (uint8_t)clampi(p->p[k] + d, 0, 127);
  else if (page == PAGE_FILTER && id == VE_3) p->engine = (uint8_t)wrap(p->engine, d, SFX_ENGINE_COUNT);
  else return;
  changed(sel);
}

static void soft_sound(int f)                        /* F1-F6 on the sound pages */
{
  sfx_patch *p = &patches[sel];
  rng ^= vd_rand();
  switch (f) {
  case 0: keep_undo(sel); p->engine = (uint8_t)((p->engine + 1) % SFX_ENGINE_COUNT); break;
  case 1: keep_undo(sel); sfx_patch_mutate(p, &rng); say("MUTATED"); break;
  case 2: if (!has_undo[sel]) { say("NOTHING TO UNDO"); return; }
          { sfx_patch t = *p; *p = undo[sel]; undo[sel] = t; } say("UNDO"); break;
  case 3: keep_undo(sel); sfx_patch_random(p, &rng); say("RANDOM"); break;
  case 4: sysex_dump(sel); say("EXPORTED"); return;
  case 5: bank_save(); say("SAVED"); return;
  }
  changed(sel);
  if (f != 0) send(M_TRIGGER, sel, sel, 0.8f, 0), send(M_RELEASE, sel, sel, 0, 0);   /* hear what it became */
}

static void pattern_clear(void) { for (int i = 0; i < STEPS; i++) { pat()->on[i] = 0; for (int t = 0; t < SLOTS; t++) pat()->note[t][i] = 0; } say("PATTERN CLEARED"); }
static void demo(void) { demo_song(); edit_pat = 0; send(M_CUE, 0, 0, 0, 0); say("DEMO SONG"); }

static void soft_seq(int f)                          /* F1-F6 on the SEQ page */
{
  switch (f) {
  case 0: seq_play(!running); break;
  case 1: seq_rec(!rec); break;
  case 2: for (int i = 0; i < STEPS; i++) { pat()->on[i] &= (uint16_t)~(1u << sel); pat()->note[sel][i] = 0; } say("SOUND CLEARED"); break;
  case 3: pattern_clear(); break;
  case 4: demo(); break;
  case 5: bank_save(); say("SAVED"); break;
  }
}

static void soft_song(int f)                         /* F1-F6 on the SONG page */
{
  switch (f) {
  case 0: seq_play(!running); break;
  case 1: {                                          /* this pattern into the next one, and on to it: a variation to make */
    int to = (edit_pat + 1) % SFX_PATTERNS;
    for (int i = 0; i < STEPS; i++) { SONG.pat[to].on[i] = pat()->on[i]; for (int t = 0; t < SLOTS; t++) SONG.pat[to].note[t][i] = pat()->note[t][i]; }
    if (SONG.chain < to + 1) SONG.chain = (uint8_t)(to + 1);
    pattern_pick(to); say("COPIED"); break; }
  case 2: pattern_clear(); break;
  case 3: demo(); break;
  case 4: export_at = 0; say("SENDING"); export_pump(); break;
  case 5: bank_save(); say("SAVED"); break;
  }
}

/* ---- the stage: 960 x 540, every element one of the device's controls ------------------------------------------------ */
#define SW 960
#define SH 540
static uint16_t stage_fb[SW * SH];
#define SP_X 24                                     /* the pads */
#define SP_Y 92
#define SP_W 104
#define SP_H 74
#define SP_GAP 8
#define SG_X 500                                    /* the step grid: 16 x 16 */
#define SG_Y 216
#define SG_W 27
#define SG_H 12
#define PB_Y 160                                    /* the patterns A-H, and INTENSITY right of them */
#define PB_W 36
#define IN_X (SG_X + 8 * (PB_W + 4) + 8)
#define IN_W (SG_X + STEPS * SG_W - 2 - IN_X)
#define SB_Y 456                                    /* the buttons */
#define SB_W 100
#define SB_N 7
static const char *const STAGE_BTN[SB_N][2] = { { "PLAY", "PLAY key" }, { "REC", "REC key" }, { "MUTATE", "F2" }, { "UNDO", "F3" }, { "RANDOM", "F4" }, { "EXPORT", "F5" }, { "SAVE", "F6" } };

static void stage_event(const vibe_event *e)
{
  int x = VIBE_STAGE_X(e), y = VIBE_STAGE_Y(e);
  if (e->id == VIBE_STAGE_WHEEL) { note_turn(e->value > 0.0f ? 1 : -1); return; }   /* the note of the step clicked last */
  if (e->id == VIBE_STAGE_UP) { if (stage_pad >= 0) pad(stage_pad, 0, 0), stage_pad = -1; return; }
  if (e->id != VIBE_STAGE_DOWN) return;
  int px = x - SP_X, py = y - SP_Y, gx = x - SG_X, gy = y - SG_Y;
  if (px >= 0 && py >= 0 && px < 4 * (SP_W + SP_GAP) && py < 4 * (SP_H + SP_GAP) && px % (SP_W + SP_GAP) < SP_W && py % (SP_H + SP_GAP) < SP_H) {
    stage_pad = py / (SP_H + SP_GAP) * 4 + px / (SP_W + SP_GAP);
    pad(stage_pad, 1, 0.9f);
  } else if (gx >= 0 && gy >= 0 && gx < STEPS * SG_W && gy < SLOTS * SG_H) {
    sel = gy / SG_H; step_toggle(gx / SG_W, sel);
  } else if (y >= PB_Y && y < PB_Y + 36 && gx >= 0 && gx < 8 * (PB_W + 4)) pattern_pick(gx / (PB_W + 4));
  else if (y >= PB_Y && y < PB_Y + 36 && x >= IN_X && x <= IN_X + IN_W) intensity_set((float)(x - IN_X) / (float)(IN_W - 4));
  else if (y >= SB_Y && y < SB_Y + 44 && x >= 24 && x < 24 + SB_N * (SB_W + 8) && (x - 24) % (SB_W + 8) < SB_W) {
    int b = (x - 24) / (SB_W + 8);
    if (b == 0) seq_play(!running); else if (b == 1) seq_rec(!rec); else soft_sound(b - 1);
  }
}

void vibe_on_init(float sample_rate)
{
  SR = sample_rate;
  sfx_player_init(&P, SR);
  factory(); bank_load();
  for (int i = 0; i < SLOTS; i++) P.patch[i] = patches[i];
  vibe_notes(VIBE_NOTES);
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vibe_stage(stage_fb, SW, SH, 0x0B0F18, 0);
  vf_mode(VF_ON_HOLD);                               /* DATA chooses the sound; ENTER held = FOCUS */
  vf_clicks(1);
  for (int i = 0; i < PAGE_COUNT; i++) vibe_label(VB_K1 + i, PAGE_NAME[i]);
  set_page(PAGE_TONE);
}

static const char *const HELP[] = {
  "PADS", "16 sounds (and select)", "KEYS", "the selected sound, in tune",
  "DATA", "select a sound", "PLAY / STOP", "the song",
  "REC", "pads and keys go into the pattern", "K1-K6", "TONE COLOR FILTER LINK SEQ SONG",
  "ENCODER 1-4", "the four values (SHIFT: fast)", "A", "volume (LINK: CC 24)",
  "F1-F4", "ENGINE MUTATE UNDO RANDOM", "F5 EXPORT", "the sound as SysEx (to a game)",
  "F6 SAVE", "keep sounds and song", "SEQ: PADS", "the 16 steps of the selected sound",
  "SEQ: PAD + KEY", "that step plays the key's note", "SEQ: B", "the scale NOTE moves in",
  "SONG: PADS 1-8", "pattern A-H, from the next bar", "SONG: PADS 9-16", "intensity, low to high",
  "SONG: LAYER", "sound plays from this intensity", "SONG: B", "steps in a pattern",
  "SONG: F5 SEND", "sounds + song to a game", "LINK page", "encoders send CC 20-25",
};

void vibe_on_event(const vibe_event *e)
{
  int id, r = vf_event(e, &id);
  if (export_at >= 0) export_pump();
  if (r != VF_NO) return;
  switch (e->type) {
  case VIBE_EV_PAD:
    if (e->id >= SLOTS) break;
    if (page == PAGE_SEQ) {                          /* the pads are the steps; held, a key gives the step its note */
      if (e->down) { step_toggle(e->id, sel); held_step = e->id; } else if (held_step == e->id) held_step = -1;
      break;
    }
    if (page == PAGE_SONG) { if (e->down) { if (e->id < 8) pattern_pick(e->id); else intensity_set((float)(e->id - 7) / 8.0f); } break; }
    pad(e->id, e->down, e->value);
    vibe_midi_send(e->down ? 0x90 : 0x80, (uint8_t)(NOTE_PADS + e->id), e->down ? (uint8_t)vd_clamp(e->value * 127.0f, 1.0f, 127.0f) : 0);
    break;
  case VIBE_EV_NOTE: {
    int outside = e->midi[1] == VIBE_NOTE_MIDI || e->midi[1] == VIBE_NOTE_HOST;
    if (outside && e->id >= NOTE_PADS && e->id < NOTE_PADS + SLOTS) { pad(e->id - NOTE_PADS, e->down, e->value); break; }   /* a game or a DAW plays the kit */
    if (!e->down) { send(M_RELEASE, sel, 100 + e->id, 0, 0); break; }
    if (page == PAGE_SEQ && held_step >= 0) step_set(pat(), held_step, sel, e->id);
    else record(sel, e->id);
    send(M_TRIGGER, sel, 100 + e->id, e->value > 0.0f ? e->value : 0.8f, e->id);        /* the sound at this pitch */
    break; }
  case VIBE_EV_MIDI:
    if (e->midi[0] == 0xB1 && e->midi[1] >= CC_TELEMETRY && e->midi[1] < CC_TELEMETRY + 8) {   /* telemetry: channel 2 */
      int k = e->midi[1] - CC_TELEMETRY; float v = (float)e->midi[2] / 127.0f;
      telemetry[k] = e->midi[2]; linked = 1;
      if (k == 0) vibe_param_set(P_MOD_PITCH, v * 24.0f);
      if (k == 1) vibe_param_set(P_MOD_CUTOFF, vd_clamp(((float)e->midi[2] - 64.0f) / 16.0f, -4.0f, 4.0f));
      if (k == 2) vibe_param_set(P_MOD_FM, v);
      if (k == 3) vibe_param_set(P_MOD_LEVEL, v);
      if (k == 4) vibe_param_set(P_INTENSITY, v);
      if (k == 5 && (e->midi[2] >> 4) != edit_pat) pattern_pick(e->midi[2] >> 4);        /* 0..127 = pattern A..H */
    }
    break;
  case VIBE_EV_SYSEX: sysex_in(vibe_sysex_in(), (int)e->value); break;
  case VIBE_EV_SAVE: bank_save(); break;
  case VIBE_EV_STAGE: stage_event(e); break;
  case VIBE_EV_ENCODER: if (e->id < VE_COUNT) encoder(e->id, e->value); break;
  case VIBE_EV_BUTTON:
    if (e->id == VB_SHIFT) { shift = e->down; break; }
    if (!e->down) break;
    if (e->id == VB_HOME) help = !help;
    else if (e->id == VB_RETURN) { if (help) help = 0; else set_page(PAGE_TONE); }
    else if (e->id == VB_PLAY) seq_play(!running);
    else if (e->id == VB_STOP) seq_play(0);
    else if (e->id == VB_REC) seq_rec(!rec);
    else if (e->id >= VB_F1 && e->id <= VB_F6) { int f = e->id - VB_F1; if (page == PAGE_SEQ) soft_seq(f); else if (page == PAGE_SONG) soft_song(f); else soft_sound(f); }
    else if (e->id >= VB_K1 && e->id < VB_K1 + PAGE_COUNT) set_page(e->id - VB_K1);
    break;
  default: break;
  }
}

/* ---- the display ------------------------------------------------------------------------------------------------------ */
#define BG     0x0B0F18
#define PANEL  0x151C2B
#define LINE   0x26304A
#define DIM    0x6F7A90
#define TEXT   0xE8ECF4

static const char *param_name(int k, int engine)
{
  static const char *const N[SFX_P_COUNT] = { "FREQ", "PITCH ENV", "ENV TIME", "FM RATIO", "FM DEPTH", "NOISE", "CUTOFF", "RESO", "DECAY", "CRUSH" };
  if (engine == SFX_ENGINE_UI_BLIP && k == SFX_P_FM_RATIO) return "ARP STEP";
  if (engine == SFX_ENGINE_UI_BLIP && k == SFX_P_FM_DEPTH) return "ARP SPEED";
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FM_RATIO) return "GRAIN RATE";
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FM_DEPTH) return "GRAIN AMT";
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FILTER_CUTOFF) return "BAND";
  return N[k];
}

static const char *param_text(int k, const sfx_patch *p, char *buf)
{
  int v = p->p[k];
  if (k == SFX_P_AMP_DECAY && v == SFX_HOLD) return "HOLD";
  if (k == SFX_P_PITCH_ENV_AMT) { if (v > 64) { buf[0] = '+'; vg_itoa(v - 64, buf + 1); return buf; } return vg_itoa(v - 64, buf); }
  if (k == SFX_P_BASE_FREQ || k == SFX_P_FILTER_CUTOFF) {
    float hz = k == SFX_P_BASE_FREQ ? 20.0f * vd_exp2((float)v / 127.0f * 9.5f) : 40.0f * vd_exp2((float)v / 127.0f * 8.6f);
    if (hz >= 1000.0f) { vg_ftoa(hz / 1000.0f, 1, buf); int n = (int)strlen(buf); buf[n] = 'k'; buf[n + 1] = 0; return buf; }
    return vg_itoa((int)hz, buf);
  }
  if (k == SFX_P_BITCRUSH && v == 0) return "OFF";
  return vg_itoa(v, buf);
}

static const char *note_text(int note, char *buf)    /* "A#3", or "--": the sound as designed */
{
  static const char *const N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  if (note <= 0) return "--";
  strcpy(buf, N[note % 12]);
  vg_itoa(note / 12 - 1, buf + strlen(buf));
  return buf;
}

static const char *percent(int v127, char *buf) { return vg_itoa((v127 * 100 + 63) / 127, buf); }
static int layer_on(int slot) { return (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f) >= SONG.level[slot]; }

static void column(int i, const char *name, const char *value, float bar, uint32_t rgb)
{
  int x = 12 + i * 116;
  vg_round(x, 150, 108, 126, 6, PANEL);
  vg_text(name, x + 54, 160, VG_SMALL, DIM, VG_CENTER);
  vg_text(value, x + 54, 192, VG_BIG | VG_X2, TEXT, VG_CENTER);
  vg_rect(x + 10, 254, 88, 8, LINE);
  if (bar >= 0.0f) vg_rect(x + 10, 254, (int)(88.0f * vd_clamp(bar, 0.0f, 1.0f)), 8, rgb);
  vibe_link_encoder(VE_1 + i, name, value);
}

static void scope(int x, int y, int w, int h, uint32_t rgb)
{
  vg_round(x, y, w, h, 6, PANEL);
  vg_rect(x + 2, y + h / 2, w - 4, 1, LINE);
  uint32_t at = scope_w;
  float half = (float)(h / 2 - 3), dx = (float)(w - 4) / (float)SCOPE;
  for (int i = 0; i < SCOPE; i++) {
    float lo = scope_lo[(at + (uint32_t)i) % SCOPE], hi = scope_hi[(at + (uint32_t)i) % SCOPE];
    int y0 = y + h / 2 - (int)(hi * half), y1 = y + h / 2 - (int)(lo * half);
    if (y1 > y0) vg_rect(x + 2 + (int)((float)i * dx), y0, (int)dx + 1, y1 - y0 + 1, rgb);
  }
}

static char *sound_title(char *t)                    /* "PAD 5  UI BLIP" */
{
  strcpy(t, "PAD "); vg_itoa(sel + 1, t + 4);
  int n = (int)strlen(t);
  t[n++] = ' '; t[n++] = ' '; strcpy(t + n, ENGINE_NAME[patches[sel].engine]);
  return t;
}

void vibe_on_draw(uint16_t *fb)
{
  char buf[24], t[40];
  (void)fb;
  if (export_at >= 0) export_pump();
  vg_clear(BG);
  if (help) { vg_controls("SFX FORGE", HELP, sizeof HELP / sizeof HELP[0] / 2); return; }
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine], on = playing, step = seq_step, now = seq_pattern;
  int seq = page == PAGE_SEQ, song = page == PAGE_SONG, heat = (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f);

  /* the top line: what is selected, what the other side says */
  if (seq) { strcpy(t, "B: "); strcpy(t + 3, SCALE_NAME[SONG.scale % SFX_SCALE_COUNT]); }
  else if (song) { strcpy(t, "B: LENGTH "); vg_itoa(SONG.len, t + 10); }
  else strcpy(t, "SFX FORGE");
  vg_text(t, 12, 10, VG_BOLD, DIM, VG_LEFT);
  if (toast_frames > 0) { toast_frames--; vg_text(toast, 240, 6, VG_BIG, TEXT, VG_CENTER); }
  else vg_text(sound_title(t), 240, 6, VG_BIG, rgb, VG_CENTER);
  vg_circle(462, 17, 5, linked ? 0x6EE7A8 : 0x2A3348);
  vg_text(link_text[0] ? link_text : (linked ? "LINKED" : "NO LINK"), 450, 10, VG_SMALL, linked ? 0x6EE7A8 : DIM, VG_RIGHT);

  scope(12, 40, 332, 74, rgb);

  /* the 16 pads as they are on this page: sounds, the selected sound's steps, or patterns and intensity */
  for (int s = 0; s < SLOTS; s++) {
    int x = 356 + (s % 4) * 29, y = 40 + (s / 4) * 19, state, frame = 0;
    uint32_t c, base;                                /* state: 0 off, 1 dim, 2 set, 3 bright */
    if (seq) {
      int set = (pat()->on[s] >> sel) & 1, head = step == (uint32_t)s && now == (uint32_t)edit_pat;
      base = rgb; state = head ? 3 : set ? 2 : s >= SONG.len ? 0 : 1; frame = s == cur_step;
    } else if (song) {
      if (s < 8) { base = 0x36C5FF; state = (uint32_t)s == now && step != NO_STEP ? 3 : s == edit_pat ? 2 : s < SONG.chain ? 1 : 0; frame = s == edit_pat; }
      else { base = 0xFF5A36; state = heat >= (s - 7) * 127 / 8 ? 2 : 1; }
    } else {
      base = ENGINE_RGB[patches[s].engine]; state = (on >> s) & 1 ? 3 : s == sel ? 2 : 1; frame = s == sel;
    }
    c = state == 3 ? 0xFFFFFF : state == 2 ? base : state == 1 ? vg_mix(BG, base, seq || song ? 0.18f : 0.35f) : 0x10141E;
    vg_round(x, y, 26, 16, 3, c);
    if (frame && state != 3) vg_frame(x, y, 26, 16, 1, 0xFFFFFF);
    uint32_t led = (base & 0xFFFFFF) | (uint32_t)(state + 4 * (seq + 2 * song)) << 24;
    if (led != led_cache[s]) { led_cache[s] = led; vibe_led_pad(s, state == 3 ? 0xFFFFFF : base, state == 3 ? 1.0f : state == 2 ? 0.75f : state == 1 ? 0.12f : 0.0f); }
  }

  /* the pages (K1-K6) */
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = 12 + i * 77;
    vg_round(x, 120, 72, 24, 5, i == page ? rgb : PANEL);
    vg_text(PAGE_NAME[i], x + 36, 125, VG_BOLD, i == page ? BG : DIM, VG_CENTER);
  }

  /* the four values over encoders 1-4 */
  vibe_link_page(PAGE_NAME[page]);
  if (page == PAGE_LINK) {
    for (int i = 0; i < 4; i++) {
      char n[8] = "CC "; vg_itoa(CC_ENCODERS + i, n + 3);
      column(i, n, vg_itoa(cc_out[i], buf), (float)cc_out[i] / 127.0f, 0x6EE7A8);
    }
    for (int k = 0; k < 8; k++) {                    /* what comes in: CC 10..17 */
      int x = 22 + (k / 2) * 116 + (k % 2) * 46;
      vg_rect(x, 240, 40, 4, LINE);
      vg_rect(x, 240, telemetry[k] * 40 / 127, 4, 0xFFD23F);
    }
  } else if (seq) {
    int note = pat()->note[sel][cur_step];
    column(0, "STEP", vg_itoa(cur_step + 1, buf), (float)(cur_step + 1) / (float)STEPS, rgb);
    column(1, "NOTE", note_text(note, buf), note ? (float)(note - 24) / 84.0f : 0.0f, rgb);
    column(2, "TEMPO", vg_itoa(SONG.bpm, buf), (float)(SONG.bpm - 40) / 200.0f, rgb);
    column(3, "SWING", vg_itoa(SONG.swing, buf), (float)SONG.swing / 100.0f, rgb);
  } else if (song) {
    char letter[2] = { (char)('A' + edit_pat), 0 };
    column(0, "PATTERN", letter, (float)(edit_pat + 1) / (float)SFX_PATTERNS, 0x36C5FF);
    column(1, "CHAIN", vg_itoa(SONG.chain, buf), (float)SONG.chain / (float)SFX_PATTERNS, 0x36C5FF);
    column(2, "INTENSITY", percent(heat, buf), (float)heat / 127.0f, 0xFF5A36);
    column(3, "LAYER", percent(SONG.level[sel], buf), (float)SONG.level[sel] / 127.0f, layer_on(sel) ? rgb : DIM);
  } else {
    for (int i = 0; i < 4; i++) {
      int k = param_of(i), engine = page == PAGE_FILTER && i == 2;
      column(i, k >= 0 ? param_name(k, p->engine) : engine ? "ENGINE" : "", k >= 0 ? param_text(k, p, buf) : engine ? vg_itoa(p->engine + 1, buf) : "",
             k >= 0 ? (float)p->p[k] / 127.0f : -1.0f, rgb);
    }
  }

  /* F1-F6 */
  for (int i = 0; i < 6; i++) {
    int music = seq || song, lit = music && ((i == 0 && running) || (seq && i == 1 && rec)), dim = !music && i == 2 && !has_undo[sel];
    vg_round(2 + i * 80, 286, 76, 30, 5, lit ? (i == 0 ? 0x6EE7A8 : 0xFF4040) : 0x1C2436);
    vg_text(SOFT[soft_set()][i], 40 + i * 80, 294, VG_BOLD, lit ? BG : dim ? 0x48526A : TEXT, VG_CENTER);
  }

  /* FOCUS: everything above with DATA + ENTER (and the mouse) */
  vf_begin();
  for (int i = 0; i < PAGE_COUNT; i++) vf_key(VB_K1 + i, 12 + i * 77, 120, 72, 24);
  for (int i = 0; i < 4; i++) vf_encoder(VE_1 + i, 12 + i * 116, 150, 108, 126);
  for (int i = 0; i < 6; i++) vf_key(VB_F1 + i, 2 + i * 80, 286, 76, 30);
  vf_end(0xFFFFFF);
}

void vibe_on_stage(uint16_t *fb, int w, int h)
{
  char buf[16], t[48];
  (void)fb; (void)w; (void)h;
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine], on = playing, step = seq_step, now = seq_pattern;
  int heat = (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f), here = now == (uint32_t)edit_pat;
  vg_clear(BG);
  vg_text("SFX FORGE", 24, 20, VG_BIG | VG_X2, TEXT, VG_LEFT);
  vg_text(toast_frames > 0 ? toast : sound_title(t), SW - 24, 20, VG_BIG | VG_X2, toast_frames > 0 ? TEXT : rgb, VG_RIGHT);

  for (int s = 0; s < SLOTS; s++) {                   /* the pads */
    int x = SP_X + (s % 4) * (SP_W + SP_GAP), y = SP_Y + (s / 4) * (SP_H + SP_GAP), lit = (on >> s) & 1;
    uint32_t c = ENGINE_RGB[patches[s].engine];
    vg_round(x, y, SP_W, SP_H, 8, lit ? 0xFFFFFF : vg_mix(BG, c, s == sel ? 0.9f : 0.4f));
    if (s == sel && !lit) vg_frame(x, y, SP_W, SP_H, 2, 0xFFFFFF);
    vg_text(vg_itoa(s + 1, buf), x + 10, y + 8, VG_BIG, lit || s == sel ? BG : TEXT, VG_LEFT);
    vg_text(ENGINE_NAME[patches[s].engine], x + SP_W / 2, y + SP_H - 24, VG_SMALL, lit || s == sel ? BG : vg_mix(BG, TEXT, 0.7f), VG_CENTER);
  }
  vg_text("PADS  -  click to play, as the 16 pads", SP_X, SP_Y + 4 * (SP_H + SP_GAP) + 2, VG_SMALL, DIM, VG_LEFT);

  scope(SG_X, SP_Y, STEPS * SG_W - 2, 60, rgb);

  for (int k = 0; k < SFX_PATTERNS; k++) {            /* the patterns: SONG page, pads 1-8 */
    int x = SG_X + k * (PB_W + 4), playing_it = (uint32_t)k == now && step != NO_STEP;
    char letter[2] = { (char)('A' + k), 0 };
    vg_round(x, PB_Y, PB_W, 36, 6, playing_it ? 0xFFFFFF : k == edit_pat ? 0x36C5FF : k < SONG.chain ? 0x1F3A52 : 0x1C2436);
    vg_text(letter, x + PB_W / 2, PB_Y + 7, VG_BIG, playing_it || k == edit_pat ? BG : TEXT, VG_CENTER);
  }
  vg_round(IN_X, PB_Y, IN_W, 36, 6, 0x1C2436);         /* INTENSITY: SONG page, encoder 3 */
  vg_round(IN_X + 2, PB_Y + 2, 4 + (IN_W - 8) * heat / 127, 32, 5, 0xFF5A36);
  vg_text("INTENSITY", IN_X + IN_W / 2, PB_Y + 10, VG_SMALL, TEXT, VG_CENTER);

  for (int s = 0; s < SLOTS; s++) {                   /* the pattern: a row per sound, dim while its layer is silent */
    uint32_t c = ENGINE_RGB[patches[s].engine];
    int y = SG_Y + s * SG_H, layer = heat >= SONG.level[s];
    vg_text(vg_itoa(s + 1, buf), SG_X - 8, y - 2, VG_SMALL, s == sel ? 0xFFFFFF : DIM, VG_RIGHT);
    for (int i = 0; i < STEPS; i++) {
      int set = (pat()->on[i] >> s) & 1, head = here && step == (uint32_t)i, off = i >= SONG.len;
      uint32_t cell = set ? (head && layer ? 0xFFFFFF : layer ? c : vg_mix(BG, c, 0.35f)) : head ? 0x3A4664 : off ? 0x0F131C : (i & 4) ? 0x1A2233 : 0x202A3E;
      vg_rect(SG_X + i * SG_W, y, SG_W - 2, SG_H - 2, s == sel && !set ? vg_mix(cell, 0xFFFFFF, 0.10f) : cell);
      if (set && pat()->note[s][i]) vg_rect(SG_X + i * SG_W, y, SG_W - 2, 2, 0xFFFFFF);                  /* it carries a note */
      if (s == sel && i == cur_step) vg_frame(SG_X + i * SG_W - 1, y - 1, SG_W, SG_H, 1, 0xFFFFFF);
    }
  }
  strcpy(t, "STEPS  -  click a cell, wheel = its note: "); strcpy(t + strlen(t), note_text(pat()->note[sel][cur_step], buf));
  vg_text(t, SG_X, SG_Y + SLOTS * SG_H + 4, VG_SMALL, DIM, VG_LEFT);
  vg_text("(device: SEQ page)", SG_X + STEPS * SG_W - 2, SG_Y + SLOTS * SG_H + 4, VG_SMALL, DIM, VG_RIGHT);

  for (int b = 0; b < SB_N; b++) {                    /* the buttons, each named after its key */
    int x = 24 + b * (SB_W + 8), lit = (b == 0 && running) || (b == 1 && rec);
    vg_round(x, SB_Y, SB_W, 44, 8, lit ? (b == 0 ? 0x6EE7A8 : 0xFF4040) : 0x1C2436);
    vg_text(STAGE_BTN[b][0], x + SB_W / 2, SB_Y + 11, VG_BIG, lit ? BG : TEXT, VG_CENTER);
    vg_text(STAGE_BTN[b][1], x + SB_W / 2, SB_Y + 50, VG_SMALL, DIM, VG_CENTER);
  }
  vg_text(vg_itoa(SONG.bpm, buf), SW - 24, SB_Y - 2, VG_BIG | VG_X2, TEXT, VG_RIGHT);
  vg_text("BPM  -  SEQ page, encoder 3", SW - 24, SB_Y + 50, VG_SMALL, DIM, VG_RIGHT);
}
