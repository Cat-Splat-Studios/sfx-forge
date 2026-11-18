/* SPDX-License-Identifier: MIT
 * SFX FORGE – a procedural game-audio workstation for VIBE FM.
 *
 *   16 pads      16 sound effects, each a 13-byte patch (core/sfx_core.h): IMPACT, UI BLIP, SCI-FI FM, FOLEY
 *   keys         the selected sound, chromatically (C4 = as designed)
 *   K1-K4        pages TONE · COLOR · FILTER · LINK, encoders 1-4 = the four values shown
 *   F1-F6        ENGINE · MUTATE · UNDO · RANDOM · EXPORT · SAVE
 *   LINK         the game engine's side: telemetry in (CC 10-17 on channel 2 morph every sounding voice), encoders
 *                out as CC 20-25 on channel 1, patches both ways as SysEx (docs in README.md)
 *
 * Two flows: the voices belong to the audio; everything the controls do reaches them through one queue.
 */
#include "vibe.h"
#include "vibe_dsp.h"
#include "vibe_gfx.h"
#include <stddef.h>
size_t strlen(const char *s);                      /* (the SDK brings these two) */
char *strcpy(char *d, const char *s);

#define SFX_SIN(x)  vd_sin(x)
#define SFX_EXP2(x) vd_exp2(x)
#define SFX_TANH(x) vd_tanh(x)
#include "../core/sfx_core.h"

VIBE_PARALLEL_AUDIO

#define SLOTS  16
#define VOICES 8

/* ---- the protocol (README: "The link") --------------------------------------------------------------------------- */
#define SX_DUMP    0x01            /* out (and in): F0 7D 56 42 01 <slot> <13 patch bytes> F7 */
#define SX_LOAD    0x02            /* in:           F0 7D 56 42 02 <slot> <13 patch bytes> F7 */
#define SX_TEXT    0x03            /* in:           F0 7D 56 42 03 <ASCII, up to 24> F7 */
#define SX_REQUEST 0x04            /* in:           F0 7D 56 42 04 <slot> F7  -> answers with a dump */
#define CC_TELEMETRY 10            /* in, channel 2: CC 10..17 */
#define CC_ENCODERS  20            /* out, channel 1: CC 20..25 */
#define NOTE_PADS    36            /* pads <-> notes 36..51 */

/* ---- parameters the DAW automates: the same four a game moves over MIDI ------------------------------------------ */
enum { P_VOLUME, P_MOD_PITCH, P_MOD_CUTOFF, P_MOD_FM, P_MOD_LEVEL, P_COUNT };
static const vibe_param PARAMS[P_COUNT] = {
  { "volume",     "Volume",     0.0f,  1.0f, 0.8f, "",    0, 0 },
  { "mod_pitch",  "Mod Pitch",  0.0f, 24.0f, 0.0f, "st",  0, 0 },
  { "mod_cutoff", "Mod Cutoff", -4.0f, 4.0f, 0.0f, "oct", 0, 0 },
  { "mod_fm",     "Mod FM",     0.0f,  1.0f, 0.0f, "",    0, 0 },
  { "mod_level",  "Mod Level",  0.0f,  1.0f, 1.0f, "",    0, 0 },
};
static const char *const GROUPS[P_COUNT] = { "OUTPUT", "TELEMETRY", "TELEMETRY", "TELEMETRY", "TELEMETRY" };

/* ---- the sounds it starts with: engine, then FREQ P.AMT P.DEC RATIO DEPTH NOISE CUTOFF RESO DECAY CRUSH ------------ */
static const uint8_t FACTORY[SLOTS][1 + SFX_P_COUNT] = {
  { 0, 28,  96, 55,  20,  60,  70,  85, 10, 100,  0 },   /* explosion */
  { 0, 32, 100, 28,   0,  20,  25,  90,  5,  55,  0 },   /* punch */
  { 0, 24,  90, 35,   0,   0,   5,  70,  0,  70,  0 },   /* thud */
  { 0, 62,  70, 20,  75,  80,  15, 110, 40,  75,  0 },   /* metal hit */
  { 1, 86,  64,  0,  26,  60,   0, 127,  0,  62,  0 },   /* coin */
  { 1, 78,  64,  0,   0,   0,   0, 120,  0,  35,  0 },   /* select */
  { 1, 42,  58, 60,   0,   0,   0,  80, 20,  60, 50 },   /* error */
  { 1, 60,  40, 75,  37,  75,   0, 115, 10,  85, 20 },   /* power-up */
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

/* ---- the control flow's own ---------------------------------------------------------------------------------------- */
static sfx_patch patches[SLOTS], undo[SLOTS];
static uint8_t has_undo[SLOTS];
static int sel, page, help, shift;
static float acc[VE_COUNT];                         /* encoder turns not yet a whole step */
static uint32_t rng = 0x5F3759DFu, trig_count;
static uint8_t telemetry[8], cc_out[6] = { 64, 64, 64, 64, 64, 64 };
static int linked;                                  /* something came in from the other side */
static char link_text[25];
static const char *toast; static int toast_frames;
static uint32_t led_cache[SLOTS];
enum { PAGE_TONE, PAGE_COLOR, PAGE_FILTER, PAGE_LINK, PAGE_COUNT };
static const char *const PAGE_NAME[PAGE_COUNT] = { "TONE", "COLOR", "FILTER", "LINK" };
static const char *const SOFT[6] = { "ENGINE", "MUTATE", "UNDO", "RANDOM", "EXPORT", "SAVE" };

/* ---- from the controls to the audio: one queue, one writer, one reader ---------------------------------------------- */
enum { M_TRIGGER, M_RELEASE, M_UPDATE };
typedef struct { uint8_t type, slot; int16_t key; float vel, note_st; uint32_t seed; sfx_patch patch; } msg;
#define QN 64
static msg q[QN];
static volatile uint32_t qw, qr;

static void send(int type, int slot, int key, float vel, float note_st)
{
  uint32_t w = qw;
  if (w - qr >= QN) return;                          /* full: dropped */
  msg *m = &q[w % QN];
  m->type = (uint8_t)type; m->slot = (uint8_t)slot; m->key = (int16_t)key; m->vel = vel; m->note_st = note_st;
  m->seed = ++trig_count * 2654435761u; m->patch = patches[slot];
  VIBE_RELEASE();
  qw = w + 1;
}

/* ---- the audio flow's own ------------------------------------------------------------------------------------------- */
static sfx_voice V[VOICES];
static int16_t vkey[VOICES];
static uint32_t vage[VOICES], age;
static float SR, mix[VIBE_MAX_BLOCK];
static sfx_mods mods = { 0, 0, 0, 1 };
static float vol_s;
/* … and what it shows the display: each a 32-bit value only the audio writes */
#define SCOPE 164
static volatile float scope_lo[SCOPE], scope_hi[SCOPE];
static volatile uint32_t scope_w, playing;          /* playing: bit per slot */
static float sc_lo, sc_hi; static int sc_n;

static void voice_start(const msg *m)
{
  int pick = 0;
  for (int i = 0; i < VOICES; i++) if (V[i].active && vkey[i] == m->key && V[i].gate) sfx_voice_release(&V[i]);   /* the same pad again */
  for (int i = 0; i < VOICES; i++) { if (!V[i].active) { pick = i; break; } if (vage[i] < vage[pick]) pick = i; }
  sfx_voice_trigger(&V[pick], &m->patch, m->vel, m->note_st, m->slot, SR, m->seed);
  vkey[pick] = m->key; vage[pick] = ++age;
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  (void)in;
  uint32_t w = qw;
  VIBE_ACQUIRE();
  for (uint32_t k = qr; k != w; k++) {
    const msg *m = &q[k % QN];
    if (m->type == M_TRIGGER) voice_start(m);
    else for (int i = 0; i < VOICES; i++) {
      if (!V[i].active) continue;
      if (m->type == M_RELEASE && vkey[i] == m->key) sfx_voice_release(&V[i]);
      if (m->type == M_UPDATE && V[i].slot == m->slot) sfx_voice_set_patch(&V[i], &m->patch);   /* a knob turned while it sounds */
    }
  }
  VIBE_RELEASE(); qr = w;

  float c = vd_coef(0.03f, SR / (float)frames);     /* the telemetry glides: 30 ms, no steps */
  mods.pitch_st   = vibe_param_get(P_MOD_PITCH)  + (mods.pitch_st   - vibe_param_get(P_MOD_PITCH))  * c;
  mods.cutoff_oct = vibe_param_get(P_MOD_CUTOFF) + (mods.cutoff_oct - vibe_param_get(P_MOD_CUTOFF)) * c;
  mods.fm         = vibe_param_get(P_MOD_FM)     + (mods.fm         - vibe_param_get(P_MOD_FM))     * c;
  mods.level      = vibe_param_get(P_MOD_LEVEL)  + (mods.level      - vibe_param_get(P_MOD_LEVEL))  * c;

  for (int n = 0; n < frames; n++) mix[n] = 0.0f;
  uint32_t on = 0;
  for (int i = 0; i < VOICES; i++) if (sfx_voice_render(&V[i], &mods, mix, frames)) on |= 1u << V[i].slot;
  playing = on;

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

/* ---- patches: edit, keep, send ---------------------------------------------------------------------------------------- */
static void say(const char *t) { toast = t; toast_frames = 70; }
static void changed(int slot) { send(M_UPDATE, slot, 0, 0, 0); }
static void keep_undo(int slot) { undo[slot] = patches[slot]; has_undo[slot] = 1; }

static void factory(void)
{
  for (int s = 0; s < SLOTS; s++) {
    patches[s].version = SFX_PATCH_VERSION; patches[s].engine = FACTORY[s][0];
    for (int i = 0; i < SFX_P_COUNT; i++) patches[s].p[i] = FACTORY[s][1 + i];
  }
}

static void bank_save(void)
{
  static uint8_t b[SLOTS * SFX_PATCH_BYTES];
  for (int s = 0; s < SLOTS; s++) sfx_patch_pack(&patches[s], b + s * SFX_PATCH_BYTES);
  vibe_store_write("bank.bin", b, sizeof b);
}

static void bank_load(void)
{
  static uint8_t b[SLOTS * SFX_PATCH_BYTES];
  if (vibe_store_read("bank.bin", b, sizeof b) != (int)sizeof b) return;
  for (int s = 0; s < SLOTS; s++) sfx_patch_unpack(&patches[s], b + s * SFX_PATCH_BYTES);   /* a broken one keeps its factory sound */
}

static void sysex_dump(int slot)
{
  uint8_t b[7 + SFX_PATCH_BYTES] = { 0xF0, 0x7D, 0x56, 0x42, SX_DUMP, (uint8_t)slot };
  sfx_patch_pack(&patches[slot], b + 6);
  b[6 + SFX_PATCH_BYTES] = 0xF7;
  vibe_midi_send_sysex(b, sizeof b);
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
}

/* ---- playing -------------------------------------------------------------------------------------------------------- */
static void pad(int slot, int down, float vel)
{
  if (down) { sel = slot; send(M_TRIGGER, slot, slot, vel > 0.0f ? vel : 0.8f, 0.0f); }
  else send(M_RELEASE, slot, slot, 0, 0);
}

static int param_of(int enc) { return page == PAGE_TONE ? (int[]){ SFX_P_BASE_FREQ, SFX_P_PITCH_ENV_AMT, SFX_P_PITCH_ENV_DEC, SFX_P_AMP_DECAY }[enc]
                                    : page == PAGE_COLOR ? (int[]){ SFX_P_FM_RATIO, SFX_P_FM_DEPTH, SFX_P_NOISE_MIX, SFX_P_BITCRUSH }[enc]
                                    : page == PAGE_FILTER && enc < 2 ? SFX_P_FILTER_CUTOFF + enc : -1; }

static void encoder(int id, float turn)
{
  acc[id] += turn * (shift ? 8.0f : 1.0f);
  int d = (int)acc[id];
  if (!d) return;
  acc[id] -= (float)d;
  if (id == VE_DATA) { sel = (sel + d % SLOTS + SLOTS) % SLOTS; return; }
  if (id == VE_A && page != PAGE_LINK) { vibe_param_set(P_VOLUME, vd_clamp(vibe_param_get(P_VOLUME) + (float)d * 0.02f, 0.0f, 1.0f)); return; }
  if (page == PAGE_LINK) {                           /* the encoders are the game engine's: CC 20..25 */
    int k = id <= VE_4 ? id : id == VE_A ? 4 : id == VE_B ? 5 : -1;
    if (k < 0) return;
    int v = cc_out[k] + d; cc_out[k] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    vibe_midi_send(0xB0, (uint8_t)(CC_ENCODERS + k), cc_out[k]);
    return;
  }
  if (id > VE_4) return;
  sfx_patch *p = &patches[sel];
  int k = param_of(id);
  if (k >= 0) { int v = p->p[k] + d; p->p[k] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); }
  else if (page == PAGE_FILTER && id == VE_3) p->engine = (uint8_t)((p->engine + d % SFX_ENGINE_COUNT + SFX_ENGINE_COUNT) % SFX_ENGINE_COUNT);
  else return;
  changed(sel);
}

static void soft(int f)
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
  if (f != 0) send(M_TRIGGER, sel, sel, 0.8f, 0.0f), send(M_RELEASE, sel, sel, 0, 0);   /* hear what it became */
}

void vibe_on_init(float sample_rate)
{
  SR = sample_rate;
  factory(); bank_load();
  vibe_notes(VIBE_NOTES);
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vf_mode(VF_ON_HOLD);                               /* DATA chooses the pad; ENTER held = FOCUS */
  vf_clicks(1);
  for (int i = 0; i < 6; i++) vibe_label(VB_F1 + i, SOFT[i]);
  for (int i = 0; i < PAGE_COUNT; i++) vibe_label(VB_K1 + i, PAGE_NAME[i]);
  for (int i = 0; i < SLOTS; i++) led_cache[i] = 0xFFFFFFFFu;
}

static const char *const HELP[] = {
  "PADS", "16 sound effects (and select)", "KEYS", "the selected sound, chromatic",
  "DATA", "select a pad", "PLAY", "hear the selected sound",
  "K1-K4", "TONE · COLOR · FILTER · LINK", "ENCODER 1-4", "the four values (SHIFT: fast)",
  "A", "volume (LINK: CC 24)", "F1 ENGINE", "next sound engine",
  "F2 MUTATE", "vary the sound +/- 15 %", "F3 UNDO", "back to before (and forth)",
  "F4 RANDOM", "a new sound", "F5 EXPORT", "the patch as SysEx (to Unity)",
  "F6 SAVE", "keep all 16 sounds", "LINK page", "encoders send CC 20-25",
};

void vibe_on_event(const vibe_event *e)
{
  int id, r = vf_event(e, &id);
  if (r != VF_NO) return;
  switch (e->type) {
  case VIBE_EV_PAD:
    if (e->id >= SLOTS) break;
    pad(e->id, e->down, e->value);
    vibe_midi_send(e->down ? 0x90 : 0x80, (uint8_t)(NOTE_PADS + e->id), e->down ? (uint8_t)vd_clamp(e->value * 127.0f, 1.0f, 127.0f) : 0);
    break;
  case VIBE_EV_NOTE: {
    int outside = e->midi[1] == VIBE_NOTE_MIDI || e->midi[1] == VIBE_NOTE_HOST;
    if (outside && e->id >= NOTE_PADS && e->id < NOTE_PADS + SLOTS) pad(e->id - NOTE_PADS, e->down, e->value);   /* a game or a DAW plays the kit */
    else if (e->down) send(M_TRIGGER, sel, 100 + e->id, e->value > 0.0f ? e->value : 0.8f, (float)e->id - 60.0f);
    else send(M_RELEASE, sel, 100 + e->id, 0, 0);
    break; }
  case VIBE_EV_MIDI:
    if (e->midi[0] == 0xB1 && e->midi[1] >= CC_TELEMETRY && e->midi[1] < CC_TELEMETRY + 8) {   /* telemetry: channel 2 */
      int k = e->midi[1] - CC_TELEMETRY; float v = (float)e->midi[2] / 127.0f;
      telemetry[k] = e->midi[2]; linked = 1;
      if (k == 0) vibe_param_set(P_MOD_PITCH, v * 24.0f);
      if (k == 1) vibe_param_set(P_MOD_CUTOFF, vd_clamp(((float)e->midi[2] - 64.0f) / 16.0f, -4.0f, 4.0f));
      if (k == 2) vibe_param_set(P_MOD_FM, v);
      if (k == 3) vibe_param_set(P_MOD_LEVEL, v);
    }
    break;
  case VIBE_EV_SYSEX: sysex_in(vibe_sysex_in(), (int)e->value); break;
  case VIBE_EV_SAVE: bank_save(); break;
  case VIBE_EV_ENCODER: if (e->id < VE_COUNT) encoder(e->id, e->value); break;
  case VIBE_EV_BUTTON:
    if (e->id == VB_SHIFT) { shift = e->down; break; }
    if (e->id == VB_PLAY) { pad(sel, e->down, 0.8f); break; }
    if (!e->down) break;
    if (e->id == VB_HOME) help = !help;
    else if (e->id == VB_RETURN) { if (help) help = 0; else page = PAGE_TONE; }
    else if (e->id >= VB_F1 && e->id <= VB_F6) soft(e->id - VB_F1);
    else if (e->id >= VB_K1 && e->id < VB_K1 + PAGE_COUNT) page = e->id - VB_K1;
    break;
  default: break;
  }
}

/* ---- the display ------------------------------------------------------------------------------------------------------ */
#define BG     0x0B0F18
#define PANEL  0x151C2B
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

static void column(int i, const char *name, const char *value, float bar, uint32_t rgb)
{
  int x = 12 + i * 116;
  vg_round(x, 150, 108, 126, 6, PANEL);
  vg_text(name, x + 54, 160, VG_SMALL, DIM, VG_CENTER);
  vg_text(value, x + 54, 192, VG_BIG | VG_X2, TEXT, VG_CENTER);
  vg_rect(x + 10, 254, 88, 8, 0x26304A);
  if (bar >= 0.0f) vg_rect(x + 10, 254, (int)(88.0f * vd_clamp(bar, 0.0f, 1.0f)), 8, rgb);
}

void vibe_on_draw(uint16_t *fb)
{
  char buf[24];
  (void)fb;
  vg_clear(BG);
  if (help) { vg_controls("SFX FORGE", HELP, sizeof HELP / sizeof HELP[0] / 2); return; }
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine], on = playing;

  /* the top line: what is selected, what the other side says */
  vg_text("SFX FORGE", 12, 10, VG_BOLD, DIM, VG_LEFT);
  if (toast_frames > 0) { toast_frames--; vg_text(toast, 240, 6, VG_BIG, TEXT, VG_CENTER); }
  else {
    char t[40] = "PAD "; vg_itoa(sel + 1, t + 4); int n = (int)strlen(t);
    t[n++] = ' '; t[n++] = ' '; strcpy(t + n, ENGINE_NAME[p->engine]);
    vg_text(t, 240, 6, VG_BIG, rgb, VG_CENTER);
  }
  vg_circle(462, 17, 5, linked ? 0x6EE7A8 : 0x2A3348);
  vg_text(link_text[0] ? link_text : (linked ? "LINKED" : "NO LINK"), 450, 10, VG_SMALL, linked ? 0x6EE7A8 : DIM, VG_RIGHT);

  /* the sound, as it leaves */
  vg_round(12, 40, 332, 74, 6, PANEL);
  vg_rect(14, 77, 328, 1, 0x26304A);
  uint32_t w = scope_w;
  for (int i = 0; i < SCOPE; i++) {
    float lo = scope_lo[(w + (uint32_t)i) % SCOPE], hi = scope_hi[(w + (uint32_t)i) % SCOPE];
    int y0 = 77 - (int)(hi * 34.0f), y1 = 77 - (int)(lo * 34.0f);
    if (y1 > y0) vg_rect(14 + i * 2, y0, 2, y1 - y0 + 1, rgb);
  }

  /* the 16 pads */
  for (int s = 0; s < SLOTS; s++) {
    int x = 356 + (s % 4) * 29, y = 40 + (s / 4) * 19, lit = (on >> s) & 1;
    uint32_t c = ENGINE_RGB[patches[s].engine];
    vg_round(x, y, 26, 16, 3, lit ? 0xFFFFFF : vg_mix(BG, c, s == sel ? 1.0f : 0.35f));
    if (s == sel && !lit) vg_frame(x, y, 26, 16, 1, 0xFFFFFF);
    uint32_t led = (c & 0xFFFFFF) | (uint32_t)(lit ? 3 : s == sel ? 2 : 1) << 24;
    if (led != led_cache[s]) { led_cache[s] = led; vibe_led_pad(s, lit ? 0xFFFFFF : c, lit ? 1.0f : s == sel ? 0.7f : 0.2f); }
  }

  /* the pages (K1-K4) */
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = 12 + i * 116;
    vg_round(x, 120, 108, 24, 5, i == page ? rgb : PANEL);
    vg_text(PAGE_NAME[i], x + 54, 125, VG_BOLD, i == page ? BG : DIM, VG_CENTER);
  }

  /* the four values over encoders 1-4 */
  if (page == PAGE_LINK) {
    vibe_link_page("LINK");
    for (int i = 0; i < 4; i++) {
      char n[8] = "CC "; vg_itoa(CC_ENCODERS + i, n + 3);
      column(i, n, vg_itoa(cc_out[i], buf), (float)cc_out[i] / 127.0f, 0x6EE7A8);
      vibe_link_encoder(VE_1 + i, n, buf);
    }
    for (int k = 0; k < 8; k++) {                    /* what comes in: CC 10..17 */
      int x = 22 + (k / 2) * 116 + (k % 2) * 46;
      vg_rect(x, 240, 40, 4, 0x26304A);
      vg_rect(x, 240, telemetry[k] * 40 / 127, 4, 0xFFD23F);
    }
  } else {
    vibe_link_page(PAGE_NAME[page]);
    for (int i = 0; i < 4; i++) {
      int k = param_of(i);
      const char *name = k >= 0 ? param_name(k, p->engine) : (page == PAGE_FILTER && i == 2) ? "ENGINE" : "";
      const char *val = k >= 0 ? param_text(k, p, buf) : (page == PAGE_FILTER && i == 2) ? vg_itoa(p->engine + 1, buf) : "";
      column(i, name, val, k >= 0 ? (float)p->p[k] / 127.0f : -1.0f, rgb);
      vibe_link_encoder(VE_1 + i, name, val);
    }
  }

  /* F1-F6 */
  for (int i = 0; i < 6; i++) {
    vg_round(2 + i * 80, 286, 76, 30, 5, 0x1C2436);
    vg_text(SOFT[i], 40 + i * 80, 294, VG_BOLD, i == 2 && !has_undo[sel] ? 0x48526A : TEXT, VG_CENTER);
  }

  /* FOCUS: everything above with DATA + ENTER (and the mouse) */
  vf_begin();
  for (int i = 0; i < PAGE_COUNT; i++) vf_key(VB_K1 + i, 12 + i * 116, 120, 108, 24);
  for (int i = 0; i < 4; i++) vf_encoder(VE_1 + i, 12 + i * 116, 150, 108, 126);
  for (int i = 0; i < 6; i++) vf_key(VB_F1 + i, 2 + i * 80, 286, 76, 30);
  vf_end(0xFFFFFF);
}
