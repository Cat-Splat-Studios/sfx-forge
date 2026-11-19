/* SPDX-License-Identifier: MIT
 * SFX FORGE – sound effects, instruments and adaptive music for games, on VIBE FM.
 *
 *   16 pads      16 sounds, each a 28-byte patch (core/sfx_core.h). Five engines: IMPACT, UI BLIP, SCI-FI FM, FOLEY,
 *                WAVETABLE – with envelopes, an LFO, and sends to a chorus, a delay and a reverb
 *   keys         the selected sound as an instrument, in tune, 16 voices
 *   K1-K6        SOUND (press again: its next page) · ENV · LFO · FX · SEQ (again: SONG) · LINK
 *                encoders 1-4 = the four values shown
 *   F1           PRESET: the next of 32 named sounds onto the selected pad (SHIFT: the one before)
 *   LEFT RIGHT   the sound page before / after; on SEQ and SONG the pattern
 *   SEQ          a 16-step pattern over the 16 sounds: the pads are the steps of the selected sound, a step may carry a
 *                note (hold its pad and play a key, or the NOTE encoder)
 *   SONG         8 patterns, a chain, and what makes it game music: every sound has a LAYER threshold and plays only
 *                while INTENSITY is at or above it; a cued pattern takes over on the next bar line
 *   LINK         the game engine's side: telemetry in (CC 10-15, channel 2: pitch, cutoff, timbre, level, intensity,
 *                pattern), encoders out as CC 20-25, patches and the song as SysEx (README.md)
 *   the stage    the same, big (plugin and browser)
 *   MENU         the tour: it plays and names what SFX FORGE can do, a caption at a time
 * On the sound pages the display shows the selected sound itself – its whole shape and its wave – as the knobs change it.
 *
 * Two flows: the player (core/sfx_song.h: voices, effects and sequencer) belongs to the audio. The controls reach it
 * through one queue; the song itself they edit in place, one small value at a time.
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
#define SX_DUMP     0x01           /* out (and in): F0 7D 56 42 01 <slot> <28 patch bytes> F7 */
#define SX_LOAD     0x02           /* in:           F0 7D 56 42 02 <slot> <28 patch bytes> F7 */
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
  { "mod_fm",     "Mod Timbre", 0.0f,  1.0f, 0.0f, "",    0, 0 },
  { "mod_level",  "Mod Level",  0.0f,  1.0f, 1.0f, "",    0, 0 },
  { "intensity",  "Intensity",  0.0f,  1.0f, 1.0f, "",    0, 0 },
};
static const char *const GROUPS[P_COUNT] = { "OUTPUT", "TELEMETRY", "TELEMETRY", "TELEMETRY", "TELEMETRY", "MUSIC" };

/* ---- the sounds it knows. A row: name, engine, then
        FREQ P.ENV P.TIME  A   B  NOISE CUTOFF RESO DECAY CRUSH | ATTACK SUSTAIN RELEASE F.ENV F.TIME |
        LFO: RATE PITCH FILTER TIMBRE | SPREAD CHORUS DELAY REVERB LEVEL TABLE
   The first 16 are on the pads at the start; F1 PRESET puts any of them on the selected pad. ----------------------- */
typedef struct { const char *name; uint8_t engine, p[SFX_P_COUNT]; } preset;
static const preset PRESETS[] = {
  { "EXPLOSION", 0, { 28,  96, 55,  20,  60,  70,  85, 10, 100,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 30, 100, 0 } },
  { "PUNCH",     0, { 32, 100, 28,   0,  20,  25,  90,  5,  55,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "THUD",      0, { 24,  90, 35,   0,   0,   5,  70,  0,  70,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "METAL HIT", 0, { 62,  70, 20,  75,  80,  15, 110, 40,  75,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 20, 100, 0 } },
  { "COIN",      1, { 86,  64,  0,  26,  60,   0, 127,  0,  62,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "SELECT",    1, { 78,  64,  0,   0,   0,   0, 120,  0,  35,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "BASS",      1, { 19,  64,  0,   0,   0,   0,  78, 25,  60,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "LEAD",      1, { 60,  64,  0,   0,   0,   0, 108, 10,  56, 15,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0, 35,  0, 100, 0 } },
  { "LASER",     2, { 70, 110, 45,  50,  70,   0, 120, 20,  58,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "ZAP",       2, { 80, 120, 25, 100,  90,  10, 127,  0,  45, 30,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "ENGINE HUM",2, { 22,  64,  0,  21,  55,   8,  78, 45,  60,  0,    0, 127, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "WT SWEEP",  4, { 46,  64,  0,  10,  20,   0, 100, 30,  80,  0,    5,  90, 60,  90, 70,   30,  0, 20,  80,   35, 50, 30, 40, 100, 0 } },
  { "FOOTSTEP",  3, { 30,  80, 25,  90,  40, 127,  62, 15,  45,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "WIND",      3, { 30,  64,  0,  20,  70, 127,  75, 55,  60,  0,   40, 127, 60,  64, 40,   20,  0, 40,   0,    0,  0,  0, 30, 100, 0 } },
  { "WHOOSH",    3, { 30,  30, 85,   0,   0, 127,  90, 45,  88,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 20, 100, 0 } },
  { "STRINGS",   4, { 52,  64,  0,   0,   0,   0,  88,  5,  90,  0,   75, 110, 70,  72, 80,   87, 12,  0,   0,   40, 70,  0, 70,  95, 0 } },
  { "BRASS",     4, { 52,  64,  0,  20,  10,   0,  70, 15,  70,  0,   45, 100, 45, 100, 60,   87,  8,  0,   0,   30, 20,  0, 50, 100, 0 } },
  { "CHOIR",     4, { 58,  64,  0,   0,   0,   0,  95,  5,  90,  0,   85, 115, 80,  64, 40,   85, 10,  0,  30,   35, 60,  0, 90, 100, 3 } },
  { "PIZZICATO", 4, { 52,  64,  0,   0,   0,   0,  60, 10,  45,  0,    0,   0, 34, 100, 35,   50,  0,  0,   0,    0,  0,  0, 45, 110, 0 } },
  { "HARP",      4, { 58,  64,  0,  10,   0,   0,  85,  5,  85,  0,    0,   0, 34,  90, 50,   50,  0,  0,   0,    0,  0, 20, 60, 110, 2 } },
  { "TIMPANI",   0, { 30,  76, 40,  20,  25,  12,  62, 20,  95,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 70, 100, 0 } },
  { "BELLS",     4, { 70,  64,  0,  60,   0,   0, 120,  0, 105,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,   20,  0, 30, 70, 100, 5 } },
  { "CELESTA",   2, { 70,  64, 60,  81,  45,   0, 120,  0,  90,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 55, 100, 0 } },
  { "FLUTE",     4, { 64,  64,  0,   0,   0,   8, 100,  0,  80,  0,   50, 105, 45,  64, 40,   87, 10,  0,   0,    0,  0,  0, 55, 100, 2 } },
  { "ORGAN",     4, { 52,  64,  0,  70,   0,   0, 110,  0,  80,  0,    5, 120, 30,  64, 40,   50,  0,  0,   0,    0, 60,  0, 40,  95, 4 } },
  { "WT VOWEL",  4, { 46,  64,  0,  20,   0,   0, 105, 10,  80,  0,   10, 100, 55,  64, 40,   45,  0,  0, 100,   30, 30, 40, 30, 100, 3 } },
  { "WT BASS",   4, { 34,  64,  0,  40,  50,   0,  60, 40,  70,  0,    0,   0, 34, 105, 55,   50,  0,  0,   0,    0,  0,  0,  0, 105, 1 } },
  { "PAD",       4, { 52,  64,  0,  50,   0,   0,  80, 10,  90,  0,   95, 110, 95,  64, 40,   30,  0, 30,  20,   55, 80,  0, 90,  95, 0 } },
  { "ERROR",     1, { 42,  58, 60,   0,   0,   0,  80, 20,  60, 50,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "POWER-UP",  1, { 60,  40, 75,  37,  75,   0, 115, 10,  85, 20,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "WARP",      2, { 66,  30, 90,  40,  60,   0, 110, 30, 100,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0, 30, 30, 100, 0 } },
  { "CRACKLE",   3, { 30,  64,  0, 110, 127, 127, 105, 20, 100, 25,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
};
#define PRESET_COUNT ((int)(sizeof PRESETS / sizeof PRESETS[0]))
static const char *const ENGINE_NAME[SFX_ENGINE_COUNT] = { "IMPACT", "UI BLIP", "SCI-FI FM", "FOLEY", "WAVETABLE" };
static const char *const ENGINE_SHORT[SFX_ENGINE_COUNT] = { "IMPACT", "BLIP", "FM", "FOLEY", "WAVE" };
static const uint32_t ENGINE_RGB[SFX_ENGINE_COUNT] = { 0xFF5A36, 0xFFD23F, 0x36C5FF, 0x7BE07B, 0xC48BFF };
static const char *const SCALE_NAME[SFX_SCALE_COUNT] = { "CHROMATIC", "MAJOR", "MINOR", "PENTATONIC" };

/* ---- the audio flow's own: the player. Its song is edited by the controls (single values, they alone write them) ---- */
static sfx_player P;
#define SONG P.song
static float SR, mixl[VIBE_MAX_BLOCK], mixr[VIBE_MAX_BLOCK];
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
enum { PG_OSC, PG_TIMBRE, PG_FILTER, PG_WAVE, PG_ENV, PG_LFO, PG_FX, PG_SEQ, PG_SONG, PG_LINK, PG_COUNT };
enum { TAB_SOUND, TAB_ENV, TAB_LFO, TAB_FX, TAB_SEQ, TAB_LINK, TAB_COUNT };
static const uint8_t TAB_OF[PG_COUNT] = { TAB_SOUND, TAB_SOUND, TAB_SOUND, TAB_SOUND, TAB_ENV, TAB_LFO, TAB_FX, TAB_SEQ, TAB_SEQ, TAB_LINK };
static const uint8_t TAB_FIRST[TAB_COUNT] = { PG_OSC, PG_ENV, PG_LFO, PG_FX, PG_SEQ, PG_LINK };
static const char *const TAB_NAME[TAB_COUNT] = { "SOUND", "ENV", "LFO", "FX", "SEQ", "LINK" };
#define ENGINE_KNOB (-2)
static const int8_t PAGE_PARAMS[PG_SEQ][4] = {       /* what encoders 1-4 turn on each sound page */
  { ENGINE_KNOB, SFX_P_BASE_FREQ, SFX_P_PITCH_ENV_AMT, SFX_P_PITCH_ENV_DEC },
  { SFX_P_FM_RATIO, SFX_P_FM_DEPTH, SFX_P_NOISE_MIX, SFX_P_BITCRUSH },
  { SFX_P_FILTER_CUTOFF, SFX_P_FILTER_RES, SFX_P_FILTER_ENV, SFX_P_FILTER_ENV_DEC },
  { SFX_P_TABLE, SFX_P_SPREAD, -1, -1 },
  { SFX_P_ATTACK, SFX_P_AMP_DECAY, SFX_P_SUSTAIN, SFX_P_RELEASE },
  { SFX_P_LFO_RATE, SFX_P_LFO_PITCH, SFX_P_LFO_FILTER, SFX_P_LFO_TIMBRE },
  { SFX_P_CHORUS, SFX_P_DELAY, SFX_P_REVERB, SFX_P_LEVEL },
};
static sfx_patch patches[SLOTS], undo[SLOTS];
static uint8_t has_undo[SLOTS];
static int8_t pad_preset[SLOTS], undo_preset[SLOTS]; /* which named sound a pad holds, -1 = its own */
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
static int tour_on, tour_i;                         /* the tour (MENU): running, its next step */
static uint64_t tour_t0;
static const char *tour_title = "", *tour_line = "";
static float tour_progress, tour_heat0, tour_ramp_from, tour_ramp_to, tour_ramp_t0, tour_ramp_len;
static int tour_page0, tour_sel0, tour_kept = -1;   /* what it puts back; the sound it changed */
static sfx_patch tour_keep;
static int export_at = -1;                          /* sending the sounds and the song: the next message, -1 = not */
static uint8_t song_rx[SFX_SONG_BYTES]; static int song_rx_next;
static const char *const SOFT[3][6] = { { "PRESET", "MUTATE", "UNDO", "RANDOM", "EXPORT", "SAVE" },
                                        { "< PAT", "PAT >", "FILL", "CLEAR", "COPY", "SAVE" },
                                        { "PLAY", "COPY", "CLR PAT", "DEMO", "SEND", "SAVE" } };
static int soft_set(void) { return page == PG_SEQ ? 1 : page == PG_SONG ? 2 : 0; }
static int sound_page(void) { return page < PG_SEQ; }

/* ---- from the controls to the audio: one queue, one writer, one reader ---------------------------------------------- */
enum { M_TRIGGER, M_RELEASE, M_UPDATE, M_START, M_STOP, M_CUE, M_QUIET };
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
    case M_QUIET:   for (int i = 0; i < SFX_PLAYER_VOICES; i++) sfx_voice_release(&P.voice[i]); break;   /* every key let go */
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
  for (int n = 0; n < frames; n++) mixl[n] = mixr[n] = 0.0f;
  playing = sfx_player_render(&P, mixl, mixr, frames, host ? tp->beat * 4.0 : -1.0, host ? (double)tp->bpm / 60.0 * 4.0 / (double)SR : 0.0) & 0xFFFFu;
  seq_step = P.running && P.step >= 0 ? (uint32_t)P.step : NO_STEP; seq_frac = P.frac; seq_pattern = (uint32_t)P.pattern;

  float vol = vibe_param_get(P_VOLUME), vc = vd_coef(0.01f, SR);
  for (int n = 0; n < frames; n++) {
    vol_s = vol + (vol_s - vol) * vc;
    float l = vd_tanh(mixl[n] * vol_s * 1.25f), r = vd_tanh(mixr[n] * vol_s * 1.25f), o = (l + r) * 0.5f;
    out_l[n] = l; out_r[n] = r;
    if (o < sc_lo) sc_lo = o;
    if (o > sc_hi) sc_hi = o;
    if (++sc_n >= 48) { uint32_t s = scope_w; scope_lo[s % SCOPE] = sc_lo; scope_hi[s % SCOPE] = sc_hi; scope_w = s + 1; sc_lo = sc_hi = o; sc_n = 0; }
  }
}

/* ---- sounds and the song: edit, keep, send ---------------------------------------------------------------------------- */
static void say(const char *t) { toast = t; toast_frames = 70; }
static void changed(int slot) { send(M_UPDATE, slot, 0, 0, 0); }
static void keep_undo(int slot) { undo[slot] = patches[slot]; undo_preset[slot] = pad_preset[slot]; has_undo[slot] = 1; }
static sfx_pattern *pat(void) { return &SONG.pat[edit_pat]; }
static const char *pad_name(int slot) { return pad_preset[slot] >= 0 && pad_preset[slot] < PRESET_COUNT ? PRESETS[pad_preset[slot]].name : ENGINE_NAME[patches[slot].engine]; }

static void preset_load(int slot, int k)
{
  patches[slot].version = SFX_PATCH_VERSION; patches[slot].engine = PRESETS[k].engine;
  for (int i = 0; i < SFX_P_COUNT; i++) patches[slot].p[i] = PRESETS[k].p[i];
  pad_preset[slot] = (int8_t)k;
}

static void step_set(sfx_pattern *p, int step, int slot, int note) { p->on[step] |= (uint16_t)(1u << slot); p->note[slot][step] = (uint8_t)note; }

/* a song to start from: a beat, a bass, strings – and a lead and effects that only come in when the game heats up */
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
    step_set(p, 0, 15, b ? 53 : 57); step_set(p, 8, 15, 55);                /* strings: two long notes a bar */
    step_set(p, b ? 11 : 7, 4, 0);                                          /* coin */
    step_set(p, 15, 8, 0);                                                  /* laser */
  }
  SONG.chain = 2;
  SONG.level[12] = 20; SONG.level[6] = 35; SONG.level[1] = 50; SONG.level[15] = 65; SONG.level[7] = 80; SONG.level[4] = 105; SONG.level[8] = 105;
}

static void factory(void)
{
  for (int s = 0; s < SLOTS; s++) preset_load(s, s);
  demo_song();
}

static void bank_save(void)
{
  static uint8_t b[SLOTS * SFX_PATCH_BYTES + SLOTS], s[SFX_SONG_BYTES];
  for (int i = 0; i < SLOTS; i++) { sfx_patch_pack(&patches[i], b + i * SFX_PATCH_BYTES); b[SLOTS * SFX_PATCH_BYTES + i] = (uint8_t)(pad_preset[i] + 1); }
  vibe_store_write("bank2.bin", b, sizeof b);
  sfx_song_pack(&SONG, s);
  vibe_store_write("song.bin", s, sizeof s);
}

static void bank_load(void)
{
  static uint8_t b[SLOTS * SFX_PATCH_BYTES + SLOTS], s[SFX_SONG_BYTES];
  if (vibe_store_read("bank2.bin", b, sizeof b) == (int)sizeof b)
    for (int i = 0; i < SLOTS; i++)                  /* a broken one keeps its factory sound */
      if (!sfx_patch_unpack(&patches[i], b + i * SFX_PATCH_BYTES)) { int k = b[SLOTS * SFX_PATCH_BYTES + i]; pad_preset[i] = (int8_t)(k >= 1 && k <= PRESET_COUNT ? k - 1 : -1); }
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
    keep_undo(b[5]); patches[b[5]] = p; pad_preset[b[5]] = -1; sel = b[5]; changed(sel); say("PATCH LOADED");
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
static void step_toggle(int step, int slot)
{
  pat()->on[step] ^= (uint16_t)(1u << slot); cur_step = step;
  if (!running && (pat()->on[step] >> slot) & 1) {   /* switched on: hear it, at its note (while the song stands still) */
    send(M_TRIGGER, slot, slot, 0.8f, pat()->note[slot][step]); send(M_RELEASE, slot, slot, 0, 0);
  }
}

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
  vibe_label(VB_K1 + TAB_SEQ, p == PG_SONG ? "SONG" : "SEQ");
  for (int i = 0; i < SLOTS; i++) led_cache[i] = 0xFFFFFFFFu;
}

/* a K key: its page – or, on it already, its next one (SOUND has three, a fourth for a wavetable; SEQ has SONG) */
static void tab_key(int tab)
{
  if (TAB_OF[page] != tab) { set_page(TAB_FIRST[tab]); return; }
  if (tab == TAB_SOUND) { int last = patches[sel].engine == SFX_ENGINE_WAVETABLE ? PG_WAVE : PG_FILTER; set_page(page >= last ? PG_OSC : page + 1); }
  else if (tab == TAB_SEQ) set_page(page == PG_SEQ ? PG_SONG : PG_SEQ);
}

static int wrap(int v, int d, int n) { return ((v + d) % n + n) % n; }
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

/* LEFT / RIGHT: the sound page before or after (WAVE only for a wavetable) – on SEQ and SONG the pattern */
static void arrow(int d)
{
  if (page == PG_SEQ || page == PG_SONG) { pattern_pick(edit_pat + d); return; }
  if (page == PG_LINK) return;
  int to = page;
  do to = wrap(to, d, PG_SEQ); while (to == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE);
  set_page(to);
}

static void encoder(int id, float turn)
{
  acc[id] += turn * (shift ? 8.0f : 1.0f);
  int d = (int)acc[id];
  if (!d) return;
  acc[id] -= (float)d;
  if (id == VE_DATA) { sel = wrap(sel, d, SLOTS); return; }
  if (id == VE_A && page != PG_LINK) { vibe_param_set(P_VOLUME, vd_clamp(vibe_param_get(P_VOLUME) + (float)d * 0.02f, 0.0f, 1.0f)); return; }
  if (page == PG_LINK) {                             /* the encoders are the game engine's: CC 20..25 */
    int k = id <= VE_4 ? id : id == VE_A ? 4 : id == VE_B ? 5 : -1;
    if (k < 0) return;
    cc_out[k] = (uint8_t)clampi(cc_out[k] + d, 0, 127);
    vibe_midi_send(0xB0, (uint8_t)(CC_ENCODERS + k), cc_out[k]);
    return;
  }
  if (page == PG_SEQ) {
    if (id == VE_1) cur_step = wrap(cur_step, d, STEPS);
    if (id == VE_2) note_turn(d);
    if (id == VE_3) SONG.bpm = (uint16_t)clampi(SONG.bpm + d, 40, 240);
    if (id == VE_4) SONG.swing = (uint8_t)clampi(SONG.swing + d, 0, 100);
    if (id == VE_B) SONG.scale = (uint8_t)wrap(SONG.scale, d, SFX_SCALE_COUNT);
    return;
  }
  if (page == PG_SONG) {
    if (id == VE_1) pattern_pick(edit_pat + d);
    if (id == VE_2) SONG.chain = (uint8_t)clampi(SONG.chain + d, 1, SFX_PATTERNS);
    if (id == VE_3) intensity_set(vibe_param_get(P_INTENSITY) + (float)d * 0.02f);
    if (id == VE_4) SONG.level[sel] = (uint8_t)clampi(SONG.level[sel] + d * 2, 0, 127);
    if (id == VE_B) SONG.len = (uint8_t)clampi(SONG.len + d, 1, STEPS);
    return;
  }
  if (id > VE_4) return;
  sfx_patch *p = &patches[sel];
  int k = PAGE_PARAMS[page][id];
  if (k == ENGINE_KNOB) { p->engine = (uint8_t)wrap(p->engine, d, SFX_ENGINE_COUNT); pad_preset[sel] = -1; }
  else if (k == SFX_P_TABLE) p->p[k] = (uint8_t)wrap(p->p[k] % SFX_WT_TABLES, d, SFX_WT_TABLES);
  else if (k >= 0) p->p[k] = (uint8_t)clampi(p->p[k] + d, 0, 127);
  else return;
  changed(sel);
}

static void audition(void)                           /* hear the selected sound: a moment of it if it would hold */
{
  send(M_TRIGGER, sel, sel, 0.8f, 0); send(M_RELEASE, sel, sel, 0, 0);
}

static void soft_sound(int f)                        /* F1-F6 on the sound pages */
{
  sfx_patch *p = &patches[sel];
  rng ^= vd_rand();
  switch (f) {
  case 0: { int k = wrap(pad_preset[sel] < 0 ? (shift ? 0 : -1) : pad_preset[sel], shift ? -1 : 1, PRESET_COUNT);
            keep_undo(sel); preset_load(sel, k); say(PRESETS[k].name);
            if (page == PG_WAVE && p->engine != SFX_ENGINE_WAVETABLE) set_page(PG_OSC);
            break; }
  case 1: keep_undo(sel); sfx_patch_mutate(p, &rng); say("MUTATED"); break;
  case 2: if (!has_undo[sel]) { say("NOTHING TO UNDO"); return; }
          { sfx_patch t = *p; int8_t n = pad_preset[sel]; *p = undo[sel]; pad_preset[sel] = undo_preset[sel]; undo[sel] = t; undo_preset[sel] = n; } say("UNDO"); break;
  case 3: keep_undo(sel); sfx_patch_random(p, &rng); pad_preset[sel] = -1; say("RANDOM"); break;
  case 4: sysex_dump(sel); say("EXPORTED"); return;
  case 5: bank_save(); say("SAVED"); return;
  }
  changed(sel);
  audition();
}

static void pattern_clear(void) { for (int i = 0; i < STEPS; i++) { pat()->on[i] = 0; for (int t = 0; t < SLOTS; t++) pat()->note[t][i] = 0; } say("PATTERN CLEARED"); }
static void demo(void) { demo_song(); edit_pat = 0; send(M_CUE, 0, 0, 0, 0); say("DEMO SONG"); }

static void pattern_copy(void)                       /* this pattern into the next one, and on to it: a variation to make */
{
  int to = (edit_pat + 1) % SFX_PATTERNS;
  for (int i = 0; i < STEPS; i++) { SONG.pat[to].on[i] = pat()->on[i]; for (int t = 0; t < SLOTS; t++) SONG.pat[to].note[t][i] = pat()->note[t][i]; }
  if (SONG.chain < to + 1) SONG.chain = (uint8_t)(to + 1);
  pattern_pick(to); say("COPIED TO NEXT");
}

static void fill(void)                               /* the selected sound on every 4th step, then every 2nd, every one, none */
{
  static const uint8_t EVERY[3] = { 4, 2, 1 };
  static const char *const SAID[4] = { "EVERY 4TH STEP", "EVERY 2ND STEP", "EVERY STEP", "SOUND CLEARED" };
  int next = 0;
  for (int k = 0; k < 3; k++) {
    int same = 1;
    for (int i = 0; i < STEPS; i++) if (((pat()->on[i] >> sel) & 1) != (i % EVERY[k] == 0 && i < SONG.len)) same = 0;
    if (same) next = k + 1;
  }
  for (int i = 0; i < STEPS; i++) {
    pat()->on[i] &= (uint16_t)~(1u << sel);
    if (next < 3 && i % EVERY[next] == 0 && i < SONG.len) pat()->on[i] |= (uint16_t)(1u << sel);
  }
  say(SAID[next]);
}

static void soft_seq(int f)                          /* F1-F6 on the SEQ page */
{
  switch (f) {
  case 0: pattern_pick(edit_pat - 1); break;
  case 1: pattern_pick(edit_pat + 1); break;
  case 2: fill(); break;
  case 3: for (int i = 0; i < STEPS; i++) { pat()->on[i] &= (uint16_t)~(1u << sel); pat()->note[sel][i] = 0; } say("SOUND CLEARED"); break;
  case 4: pattern_copy(); break;
  case 5: bank_save(); say("SAVED"); break;
  }
}

static void soft_song(int f)                         /* F1-F6 on the SONG page */
{
  switch (f) {
  case 0: seq_play(!running); break;
  case 1: pattern_copy(); break;
  case 2: pattern_clear(); break;
  case 3: demo(); break;
  case 4: export_at = 0; say("SENDING"); export_pump(); break;
  case 5: bank_save(); say("SAVED"); break;
  }
}

/* ---- the tour: SFX FORGE shows by itself what it can do. Everything it touches is put back at the end. --------------- */
enum { T_SAY, T_PAGE, T_PAD, T_SELECT, T_PARAM, T_MUTATE, T_RESTORE, T_NOTE, T_ON, T_OFF, T_PLAY, T_STOP, T_HEAT, T_RAMP, T_CUE, T_END };
typedef struct { float t; uint8_t act, a, b; const char *title, *line; } tour_step;
#define S 5.4f                                      /* (the later parts, after the instruments) */
static const tour_step TOUR[] = {
  {  0.0f, T_SAY, 0, 0, "16 SOUNDS, 5 ENGINES", "every sound is synthesized - no samples" },
  {  0.0f, T_PAGE, PG_OSC, 0, 0, 0 }, { 0.0f, T_HEAT, 8, 0, 0, 0 },
  {  0.2f, T_PAD, 0, 0, 0, 0 }, { 0.65f, T_PAD, 4, 0, 0, 0 }, { 1.1f, T_PAD, 8, 0, 0, 0 }, { 1.55f, T_PAD, 12, 0, 0, 0 },
  {  2.0f, T_PAD, 3, 0, 0, 0 }, { 2.45f, T_PAD, 5, 0, 0, 0 }, { 2.9f, T_PAD, 9, 0, 0, 0 }, { 3.35f, T_PAD, 14, 0, 0, 0 },
  {  4.2f, T_SAY, 0, 0, "SHAPE IT", "four knobs a page - watch the wave change" },
  {  4.2f, T_SELECT, 8, 0, 0, 0 },
  {  4.4f, T_PARAM, SFX_P_PITCH_ENV_DEC, 75, 0, 0 }, { 4.4f, T_PAD, 8, 0, 0, 0 },
  {  5.0f, T_PARAM, SFX_P_BASE_FREQ, 48, 0, 0 },     { 5.0f, T_PAD, 8, 0, 0, 0 },
  {  5.6f, T_PARAM, SFX_P_PITCH_ENV_AMT, 24, 0, 0 }, { 5.6f, T_PAD, 8, 0, 0, 0 },
  {  6.2f, T_PAGE, PG_TIMBRE, 0, 0, 0 }, { 6.2f, T_PARAM, SFX_P_FM_DEPTH, 112, 0, 0 }, { 6.2f, T_PAD, 8, 0, 0, 0 },
  {  6.8f, T_PARAM, SFX_P_BITCRUSH, 64, 0, 0 },      { 6.8f, T_PAD, 8, 0, 0, 0 },
  {  7.6f, T_RESTORE, 0, 0, 0, 0 },
  {  7.8f, T_SAY, 0, 0, "MUTATE", "one key - a new variation every time" },
  {  7.8f, T_PAGE, PG_OSC, 0, 0, 0 },
  {  8.0f, T_MUTATE, 0, 0, 0, 0 }, { 8.7f, T_MUTATE, 0, 0, 0, 0 }, { 9.4f, T_MUTATE, 0, 0, 0, 0 }, { 10.1f, T_MUTATE, 0, 0, 0, 0 },
  { 10.9f, T_RESTORE, 0, 0, 0, 0 },
  { 11.2f, T_SAY, 0, 0, "PLAY IT", "the keys play any sound as an instrument" },
  { 11.2f, T_SELECT, 7, 0, 0, 0 },
  { 11.4f, T_NOTE, 69, 0, 0, 0 }, { 11.65f, T_NOTE, 72, 0, 0, 0 }, { 11.9f, T_NOTE, 76, 0, 0, 0 }, { 12.15f, T_NOTE, 81, 0, 0, 0 },
  { 12.4f, T_NOTE, 79, 0, 0, 0 }, { 12.65f, T_NOTE, 76, 0, 0, 0 }, { 12.9f, T_NOTE, 72, 0, 0, 0 }, { 13.15f, T_NOTE, 69, 0, 0, 0 },
  { 13.8f, T_SAY, 0, 0, "INSTRUMENTS", "strings, choir, bells - with chorus, delay, reverb" },
  { 13.8f, T_SELECT, 15, 0, 0, 0 }, { 13.8f, T_PAGE, PG_ENV, 0, 0, 0 },
  { 14.0f, T_ON, 57, 0, 0, 0 }, { 14.0f, T_ON, 60, 0, 0, 0 }, { 14.0f, T_ON, 64, 0, 0, 0 },
  { 15.7f, T_OFF, 57, 0, 0, 0 }, { 15.7f, T_OFF, 60, 0, 0, 0 }, { 15.7f, T_OFF, 64, 0, 0, 0 },
  { 16.2f, T_SAY, 0, 0, "WAVETABLES", "a wave that morphs - by hand, LFO or the game" },
  { 16.2f, T_SELECT, 11, 0, 0, 0 }, { 16.2f, T_PAGE, PG_TIMBRE, 0, 0, 0 }, { 16.2f, T_PARAM, SFX_P_LFO_TIMBRE, 0, 0, 0 },
  { 16.4f, T_ON, 45, 0, 0, 0 },
  { 16.8f, T_PARAM, SFX_P_FM_RATIO, 40, 0, 0 }, { 17.2f, T_PARAM, SFX_P_FM_RATIO, 70, 0, 0 }, { 17.6f, T_PARAM, SFX_P_FM_RATIO, 100, 0, 0 },
  { 18.0f, T_PARAM, SFX_P_FM_RATIO, 127, 0, 0 }, { 18.4f, T_OFF, 45, 0, 0, 0 }, { 18.9f, T_RESTORE, 0, 0, 0, 0 },
  { S + 13.8f, T_SAY, 0, 0, "A SONG", "patterns over the 16 sounds - here at intensity 0" },
  { S + 13.8f, T_PAGE, PG_SONG, 0, 0, 0 }, { S + 13.8f, T_HEAT, 0, 0, 0, 0 }, { S + 13.9f, T_PLAY, 0, 0, 0, 0 },
  { S + 17.8f, T_SAY, 0, 0, "LAYERS FOLLOW THE GAME", "its intensity brings the sounds in" },
  { S + 17.8f, T_RAMP, 8, 24, 0, 0 },
  { S + 25.8f, T_SAY, 0, 0, "A GAME CUES PATTERNS", "they change on the bar line" },
  { S + 25.8f, T_PAGE, PG_SEQ, 0, 0, 0 }, { S + 26.0f, T_CUE, 1, 0, 0, 0 }, { S + 28.0f, T_CUE, 0, 0, 0, 0 },
  { S + 29.8f, T_SAY, 0, 0, "LINKED TO THE GAME", "Unity plays these sounds with the same engine" },
  { S + 29.8f, T_PAGE, PG_LINK, 0, 0, 0 }, { S + 29.8f, T_RAMP, 2, 12, 0, 0 },
  { S + 33.0f, T_STOP, 0, 0, 0, 0 }, { S + 33.0f, T_PAGE, PG_OSC, 0, 0, 0 },
  { S + 33.0f, T_SAY, 0, 0, "YOUR TURN", "HOME shows what every control does" },
  { S + 35.5f, T_END, 0, 0, 0, 0 },
};
#define TOUR_SECONDS (S + 35.5f)

static float tour_time(void) { return (float)(vibe_frames() - tour_t0) / SR; }
static void tour_restore(void) { if (tour_kept >= 0) { patches[tour_kept] = tour_keep; changed(tour_kept); } }   /* (kept until the next sound or the end) */

static void tour_end(void)
{
  if (!tour_on) return;
  tour_on = 0;
  tour_restore(); tour_kept = -1;
  send(M_QUIET, 0, 0, 0, 0);
  if (running) seq_play(0);
  pattern_pick(0);
  intensity_set(tour_heat0);
  sel = tour_sel0;
  set_page(tour_page0);
}

static void tour_start(void)
{
  if (tour_on) { tour_end(); return; }
  tour_heat0 = vibe_param_get(P_INTENSITY); tour_page0 = page; tour_sel0 = sel;
  if (running) seq_play(0);
  seq_rec(0); help = 0;
  tour_on = 1; tour_i = 0; tour_t0 = vibe_frames(); tour_ramp_len = 0.0f; tour_title = tour_line = "";
}

static void tour_pump(void)                          /* from the display: the tour is something to watch */
{
  if (!tour_on) return;
  float t = tour_time();
  tour_progress = t / TOUR_SECONDS;
  if (tour_ramp_len > 0.0f) {
    float k = vd_clamp((t - tour_ramp_t0) / tour_ramp_len, 0.0f, 1.0f);
    intensity_set(tour_ramp_from + (tour_ramp_to - tour_ramp_from) * k);
    if (k >= 1.0f) tour_ramp_len = 0.0f;
  }
  while (tour_on && TOUR[tour_i].t <= t) {
    const tour_step *st = &TOUR[tour_i++];
    switch (st->act) {
    case T_SAY:     tour_title = st->title; tour_line = st->line; break;
    case T_PAGE:    set_page(st->a); break;
    case T_PAD:     pad(st->a, 1, 0.9f); pad(st->a, 0, 0); break;
    case T_SELECT:  tour_restore(); sel = st->a; tour_kept = sel; tour_keep = patches[sel]; break;
    case T_PARAM:   patches[sel].p[st->a] = st->b; changed(sel); break;
    case T_MUTATE:  rng ^= vd_rand(); sfx_patch_mutate(&patches[sel], &rng); changed(sel); pad(sel, 1, 0.9f); pad(sel, 0, 0); break;
    case T_RESTORE: tour_restore(); break;
    case T_NOTE:    send(M_TRIGGER, sel, 100 + st->a, 0.9f, st->a); send(M_RELEASE, sel, 100 + st->a, 0, 0); break;
    case T_ON:      send(M_TRIGGER, sel, 100 + st->a, 0.8f, st->a); break;
    case T_OFF:     send(M_RELEASE, sel, 100 + st->a, 0, 0); break;
    case T_PLAY:    edit_pat = 0; seq_play(1); break;
    case T_STOP:    seq_play(0); break;
    case T_HEAT:    tour_ramp_len = 0.0f; intensity_set((float)st->a / 8.0f); break;
    case T_RAMP:    tour_ramp_from = vibe_param_get(P_INTENSITY); tour_ramp_to = (float)st->a / 8.0f; tour_ramp_t0 = t; tour_ramp_len = (float)st->b / 4.0f; break;
    case T_CUE:     pattern_pick(st->a); break;
    case T_END:     tour_end(); break;
    }
  }
}
#undef S

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
#define SB_W 92
#define SB_N 8
static const char *const STAGE_BTN[SB_N][2] = { { "PLAY", "PLAY key" }, { "REC", "REC key" }, { "MUTATE", "F2" }, { "UNDO", "F3" }, { "RANDOM", "F4" }, { "PRESET", "F1" }, { "SAVE", "F6" }, { "TOUR", "MENU key" } };

static void stage_event(const vibe_event *e)
{
  int x = VIBE_STAGE_X(e), y = VIBE_STAGE_Y(e);
  if (e->id == VIBE_STAGE_WHEEL) { note_turn(e->value > 0.0f ? 1 : -1); return; }   /* the note of the step clicked last */
  if (e->id == VIBE_STAGE_UP) { if (stage_pad >= 0) pad(stage_pad, 0, 0), stage_pad = -1; return; }
  if (e->id != VIBE_STAGE_DOWN) return;
  if (tour_on) { tour_end(); return; }
  int px = x - SP_X, py = y - SP_Y, gx = x - SG_X, gy = y - SG_Y;
  if (px >= 0 && py >= 0 && px < 4 * (SP_W + SP_GAP) && py < 4 * (SP_H + SP_GAP) && px % (SP_W + SP_GAP) < SP_W && py % (SP_H + SP_GAP) < SP_H) {
    stage_pad = py / (SP_H + SP_GAP) * 4 + px / (SP_W + SP_GAP);
    pad(stage_pad, 1, 0.9f);
  } else if (gx >= 0 && gy >= 0 && gx < STEPS * SG_W && gy < SLOTS * SG_H) {
    sel = gy / SG_H; step_toggle(gx / SG_W, sel);
  } else if (y >= PB_Y && y < PB_Y + 36 && gx >= 0 && gx < 8 * (PB_W + 4)) pattern_pick(gx / (PB_W + 4));
  else if (y >= PB_Y && y < PB_Y + 36 && x >= IN_X && x <= IN_X + IN_W) intensity_set((float)(x - IN_X) / (float)(IN_W - 4));
  else if (y >= SB_Y && y < SB_Y + 44 && x >= 24 && x < 24 + SB_N * (SB_W + 8) && (x - 24) % (SB_W + 8) < SB_W) {
    static const int8_t SOUND_KEY[SB_N] = { -1, -1, 1, 2, 3, 0, 5, -1 };
    int b = (x - 24) / (SB_W + 8);
    if (b == 0) seq_play(!running); else if (b == 1) seq_rec(!rec); else if (b == 7) tour_start(); else soft_sound(SOUND_KEY[b]);
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
  for (int i = 0; i < TAB_COUNT; i++) vibe_label(VB_K1 + i, TAB_NAME[i]);
  set_page(PG_OSC);
}

/* HOME: the controls of the page open now – up to 11 rows, so each has the whole width */
static const char *const HELP_SOUND[] = {
  "PADS", "play a sound, and select it",
  "KEYS", "the selected sound as an instrument",
  "DATA", "select a sound",
  "ENCODER 1-4", "the four values shown (SHIFT: fast)",
  "K1 SOUND", "again: OSC, TONE, FILTER (WAVE)",
  "K2 - K6", "ENV · LFO · FX · SEQ · LINK",
  "LEFT RIGHT", "the page before / after",
  "F1 PRESET", "next named sound (SHIFT: the one before)",
  "F2 F3 F4", "MUTATE · UNDO · RANDOM",
  "F5 F6", "EXPORT to a game (SysEx) · SAVE",
  "MENU", "the tour.  PLAY STOP: the song",
};
static const char *const HELP_SEQ[] = {
  "PADS", "the 16 steps of the selected sound",
  "SHIFT + PAD", "hear and select that pad's sound",
  "PAD + KEY", "hold a step: it plays the key's note",
  "DATA", "select a sound (a row)",
  "ENCODER 1 2", "the step in the frame, and its NOTE",
  "ENCODER 3 4", "TEMPO · SWING.  B: the scale of NOTE",
  "F1 F2", "pattern before / after (LEFT RIGHT too)",
  "F3 FILL", "every 4th step, 2nd, every one, none",
  "F4 F5", "CLEAR the sound · COPY to next pattern",
  "PLAY STOP REC", "the song; REC records pads and keys",
  "K5", "SONG: patterns, chain, layers",
};
static const char *const HELP_SONG[] = {
  "PADS 1-8", "pattern A-H, from the next bar line",
  "PADS 9-16", "INTENSITY, low to high",
  "DATA", "select a sound",
  "ENCODER 1", "PATTERN (LEFT RIGHT too)",
  "ENCODER 2", "CHAIN: A to here play in a row",
  "ENCODER 3", "INTENSITY (a game sends it: CC 14)",
  "ENCODER 4", "LAYER: the sound plays from here on",
  "B", "steps in a pattern (1-16)",
  "F1 F2 F3", "PLAY · COPY to next · CLEAR pattern",
  "F4 F5 F6", "DEMO song · SEND to a game · SAVE",
  "K5", "back to SEQ",
};
static const char *const HELP_LINK[] = {
  "ENCODERS", "1-4, A, B send CC 20-25 to the game",
  "PADS", "play, and send notes 36-51",
  "IN: CC 10-13", "channel 2: pitch, cutoff, timbre, level",
  "IN: CC 14 15", "channel 2: intensity · pattern",
  "IN: NOTES", "36-51 play the pads",
  "F5 EXPORT", "the selected sound as SysEx",
  "SONG: F5", "SEND: all sounds and the song",
  "MENU", "the tour",
};

void vibe_on_event(const vibe_event *e)
{
  int id, r = vf_event(e, &id);
  if (export_at >= 0) export_pump();
  if (r != VF_NO) return;
  if (tour_on && e->down && (e->type == VIBE_EV_PAD || e->type == VIBE_EV_NOTE || (e->type == VIBE_EV_BUTTON && e->id != VB_SHIFT))) { tour_end(); return; }   /* any touch ends the tour */
  switch (e->type) {
  case VIBE_EV_PAD:
    if (e->id >= SLOTS) break;
    if (page == PG_SEQ) {                            /* the pads are the steps; held, a key gives the step its note */
      if (shift) { pad(e->id, e->down, e->value); break; }                    /* SHIFT: the pad's sound – hear it, select it */
      if (e->down) { step_toggle(e->id, sel); held_step = e->id; } else if (held_step == e->id) held_step = -1;
      break;
    }
    if (page == PG_SONG) { if (e->down) { if (e->id < 8) pattern_pick(e->id); else intensity_set((float)(e->id - 7) / 8.0f); } break; }
    pad(e->id, e->down, e->value);
    if (e->down && page == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE) set_page(PG_OSC);
    vibe_midi_send(e->down ? 0x90 : 0x80, (uint8_t)(NOTE_PADS + e->id), e->down ? (uint8_t)vd_clamp(e->value * 127.0f, 1.0f, 127.0f) : 0);
    break;
  case VIBE_EV_NOTE: {
    int outside = e->midi[1] == VIBE_NOTE_MIDI || e->midi[1] == VIBE_NOTE_HOST;
    if (outside && e->id >= NOTE_PADS && e->id < NOTE_PADS + SLOTS) { pad(e->id - NOTE_PADS, e->down, e->value); break; }   /* a game or a DAW plays the kit */
    if (!e->down) { send(M_RELEASE, sel, 100 + (e->id % 96), 0, 0); break; }
    if (page == PG_SEQ && held_step >= 0) step_set(pat(), held_step, sel, e->id);
    else record(sel, e->id);
    send(M_TRIGGER, sel, 100 + (e->id % 96), e->value > 0.0f ? e->value : 0.8f, e->id);  /* the sound at this pitch */
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
    else if (e->id == VB_RETURN) { if (help) help = 0; else set_page(PG_OSC); }
    else if (e->id == VB_PLAY) seq_play(!running);
    else if (e->id == VB_STOP) seq_play(0);
    else if (e->id == VB_REC) seq_rec(!rec);
    else if (e->id == VB_MENU) tour_start();
    else if (e->id == VB_LEFT || e->id == VB_RIGHT) arrow(e->id == VB_LEFT ? -1 : 1);
    else if (e->id >= VB_F1 && e->id <= VB_F6) { int f = e->id - VB_F1; if (page == PG_SEQ) soft_seq(f); else if (page == PG_SONG) soft_song(f); else soft_sound(f); }
    else if (e->id >= VB_K1 && e->id < VB_K1 + TAB_COUNT) tab_key(e->id - VB_K1);
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
  static const char *const N[SFX_P_COUNT] = { "FREQ", "PITCH ENV", "ENV TIME", "FM RATIO", "FM DEPTH", "NOISE", "CUTOFF", "RESO", "DECAY", "CRUSH",
    "ATTACK", "SUSTAIN", "RELEASE", "FILTER ENV", "F.ENV TIME", "LFO RATE", "> PITCH", "> FILTER", "> TIMBRE", "SPREAD", "CHORUS", "DELAY", "REVERB", "LEVEL", "TABLE" };
  if (engine == SFX_ENGINE_UI_BLIP && k == SFX_P_FM_RATIO) return "ARP STEP";
  if (engine == SFX_ENGINE_UI_BLIP && k == SFX_P_FM_DEPTH) return "ARP SPEED";
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FM_RATIO) return "GRAIN RATE";
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FM_DEPTH) return "GRAIN AMT";
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FILTER_CUTOFF) return "BAND";
  if (engine == SFX_ENGINE_WAVETABLE && k == SFX_P_FM_RATIO) return "POSITION";
  if (engine == SFX_ENGINE_WAVETABLE && k == SFX_P_FM_DEPTH) return "WARP";
  return N[k];
}

static const char *with(char *buf, const char *unit) { strcpy(buf + strlen(buf), unit); return buf; }
static const char *time_text(float s, char *buf)     /* "120ms", "1.4s" */
{
  if (s < 0.9995f) return with(vg_itoa((int)(s * 1000.0f + 0.5f), buf), "ms");
  return with(vg_ftoa(s, 1, buf), "s");
}

static const char *param_text(int k, const sfx_patch *p, char *buf)
{
  int v = p->p[k];
  float u = (float)v / 127.0f;
  switch (k) {
  case SFX_P_TABLE: return SFX_WT_NAME[v % SFX_WT_TABLES];
  case SFX_P_SUSTAIN: if (v == 0) return "OFF"; break;
  case SFX_P_BITCRUSH: if (v == 0) return "OFF"; break;
  case SFX_P_PITCH_ENV_AMT: case SFX_P_FILTER_ENV:
    if (v > 64) { buf[0] = '+'; vg_itoa(v - 64, buf + 1); return buf; }
    return vg_itoa(v - 64, buf);
  case SFX_P_BASE_FREQ: case SFX_P_FILTER_CUTOFF: {
    float hz = k == SFX_P_BASE_FREQ ? 20.0f * vd_exp2(u * 9.5f) : 40.0f * vd_exp2(u * 8.6f);
    if (hz >= 1000.0f) return with(vg_ftoa(hz / 1000.0f, 1, buf), "kHz");
    return with(vg_itoa((int)hz, buf), "Hz"); }
  case SFX_P_ATTACK:         return time_text(0.0015f * vd_exp2(u * 11.4f), buf);
  case SFX_P_AMP_DECAY:      return time_text(0.010f * vd_exp2(u * 8.6f), buf);
  case SFX_P_RELEASE:        return time_text(0.005f * vd_exp2(u * 9.6f), buf);
  case SFX_P_PITCH_ENV_DEC: case SFX_P_FILTER_ENV_DEC: return time_text(0.005f * vd_exp2(u * 9.0f), buf);
  case SFX_P_LFO_RATE: { float hz = 0.1f * vd_exp2(u * 8.2f); return with(hz < 10.0f ? vg_ftoa(hz, 1, buf) : vg_itoa((int)hz, buf), "Hz"); }
  case SFX_P_LEVEL: case SFX_P_FM_RATIO: case SFX_P_FM_DEPTH: return vg_itoa(v, buf);
  default: break;
  }
  return with(vg_itoa((v * 100 + 63) / 127, buf), "%");
}

static const char *note_text(int note, char *buf)    /* "A#3", or "--": the sound as designed */
{
  static const char *const N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  if (note <= 0) return "--";
  strcpy(buf, N[note % 12]);
  vg_itoa(note / 12 - 1, buf + strlen(buf));
  return buf;
}

static const char *percent(int v127, char *buf) { return with(vg_itoa((v127 * 100 + 63) / 127, buf), "%"); }
static int layer_on(int slot) { return (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f) >= SONG.level[slot]; }

/* one of the four values over encoders 1-4. centre: its bar grows from the middle (64 = none) */
#define EY 222
static void cell(int i, const char *name, const char *value, float bar, int centre, uint32_t rgb)
{
  static const char *const NUM[4] = { "1", "2", "3", "4" };
  int x = 12 + i * 116;
  vg_round(x, EY, 108, 58, 6, PANEL);
  vg_text(NUM[i], x + 7, EY + 4, VG_SMALL, 0x48526A, VG_LEFT);
  vg_text(name, x + 54, EY + 4, VG_SMALL, DIM, VG_CENTER);
  vg_text(value, x + 54, EY + 20, VG_BIG, TEXT, VG_CENTER);
  vg_rect(x + 10, EY + 48, 88, 5, LINE);
  if (bar >= 0.0f) {
    int at = (int)(88.0f * vd_clamp(bar, 0.0f, 1.0f));
    if (!centre) vg_rect(x + 10, EY + 48, at, 5, rgb);
    else { vg_rect(x + 10 + (at < 44 ? at : 44), EY + 48, at < 44 ? 44 - at : at - 44, 5, rgb); vg_rect(x + 53, EY + 46, 2, 9, TEXT); }
  }
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

/* ---- the selected sound itself: rendered here whenever it changes, so every knob shows what it does ------------------- */
#define PV_SR   24000.0f           /* enough for a picture, and half the work */
#define PV_MAX  35968              /* 1.5 s, in blocks of 64 */
#define PV_COLS 256
static float pv_buf[PV_MAX], pv_lo[PV_COLS], pv_hi[PV_COLS], pv_peak;
static sfx_patch pv_patch;
static int pv_n;

static void preview_render(const sfx_patch *p)
{
  static sfx_voice v;
  sfx_mods none = { 0, 0, 0, 1 };
  int same = pv_n && pv_patch.engine == p->engine;
  for (int i = 0; same && i < SFX_P_COUNT; i++) same = pv_patch.p[i] == p->p[i];
  if (same) return;
  pv_patch = *p;
  int hold = sfx_patch_sustained(p), n = 0, last = 0;
  for (int i = 0; i < PV_MAX; i++) pv_buf[i] = 0.0f;
  sfx_voice_trigger(&v, p, 1.0f, 0.0f, 0, PV_SR, 12345u);         /* (always the same noise: a steady picture) */
  while (n < PV_MAX && v.active) {
    if (hold && n >= (int)(0.8f * PV_SR)) sfx_voice_release(&v);  /* a sustained sound: held 0.8 s, then let go */
    sfx_voice_render(&v, &none, pv_buf + n, 64);
    n += 64;
  }
  pv_peak = 0.001f;
  for (int i = 0; i < n; i++) { float a = vd_absf(pv_buf[i]); if (a > pv_peak) pv_peak = a; }
  for (int i = 0; i < n; i++) if (vd_absf(pv_buf[i]) > pv_peak * 0.03f) last = i;   /* the picture ends where the sound has faded */
  pv_n = last + 240 < 1200 ? 1200 : last + 240 > n ? n : last + 240;
  for (int c = 0; c < PV_COLS; c++) {
    int a = c * pv_n / PV_COLS, b = (c + 1) * pv_n / PV_COLS;
    float lo = 0.0f, hi = 0.0f;
    for (int i = a; i < b; i++) { if (pv_buf[i] < lo) lo = pv_buf[i]; if (pv_buf[i] > hi) hi = pv_buf[i]; }
    pv_lo[c] = lo; pv_hi[c] = hi;
  }
}

static void path(const float *xy, int n, uint32_t rgb) { for (int i = 0; i + 1 < n; i++) vg_line(xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], 2.0f, rgb); }

/* left: the whole sound, start to end. Right: what the page's knobs shape – the wave (10 ms of it, where it is loudest),
   the envelope, the filter or the LFO, drawn from their values. */
static void preview(int x, int y, int w, int h, const sfx_patch *p, uint32_t rgb, int pane)
{
  char buf[16];
  preview_render(p);
  int sw = w * 3 / 5, ww = w - sw - 8, mid = y + h / 2, from = 48;
  float gain = (float)(h / 2 - 4) / pv_peak;
  for (int i = 48; i < pv_n - 240; i += 16) if (vd_absf(pv_buf[i]) > pv_peak * 0.7f) { from = i; break; }   /* (a slow attack: not its silent start) */
  vg_round(x, y, w, h, 6, PANEL);
  vg_rect(x + 2, mid, sw - 4, 1, LINE);
  vg_rect(x + sw, y + 6, 1, h - 12, LINE);
  for (int i = 0; i < sw - 6; i++) {
    int c = i * PV_COLS / (sw - 6), y0 = mid - (int)(pv_hi[c] * gain), y1 = mid - (int)(pv_lo[c] * gain);
    vg_rect(x + 3 + i, y0, 1, y1 - y0 + 1, rgb);
  }
  vg_text(with(vg_ftoa((float)pv_n / PV_SR, 2, buf), "s"), x + sw - 6, y + h - 18, VG_SMALL, DIM, VG_RIGHT);       /* (where a sound has faded) */

  float x0 = (float)(x + sw + 6), pw = (float)(ww - 4), top = (float)(y + 10), bot = (float)(y + h - 24);
  const char *label = "WAVE  10 ms";
  if (pane == PG_ENV) {
    float a = (float)p->p[SFX_P_ATTACK] / 127.0f, d = (float)p->p[SFX_P_AMP_DECAY] / 127.0f, s = (float)p->p[SFX_P_SUSTAIN] / 127.0f, r = (float)p->p[SFX_P_RELEASE] / 127.0f;
    int hold = p->p[SFX_P_SUSTAIN] > 0;
    float seg = pw * 0.24f, xa = x0 + 3.0f + a * seg, xd = xa + 5.0f + d * seg, xs = hold ? xd + pw * 0.16f : xd, xr = hold ? xs + 5.0f + r * seg : xd, ys = hold ? bot - (bot - top) * s : bot;
    float xy[10] = { x0, bot, xa, top, xd, ys, xs, ys, xr, bot };
    path(xy, 5, 0xFFFFFF);
    if (hold) vg_rect((int)xs, (int)top, 1, (int)(bot - top), LINE);                 /* (the pad is let go here) */
    label = hold ? "ENVELOPE  held" : "ENVELOPE  one-shot";
  } else if (pane == PG_FILTER) {
    float cx = x0 + 12.0f + (float)p->p[SFX_P_FILTER_CUTOFF] / 127.0f * (pw - 36.0f), base = top + (bot - top) * 0.45f, peak = base - (float)p->p[SFX_P_FILTER_RES] / 127.0f * (bot - top) * 0.42f;
    if (p->engine == SFX_ENGINE_FOLEY_NOISE) { float xy[6] = { cx - 12.0f, bot, cx, peak, cx + 22.0f, bot }; path(xy, 3, 0xFFFFFF); label = "BAND"; }
    else { float xy[8] = { x0, base, cx - 10.0f > x0 ? cx - 10.0f : x0, base, cx, peak, cx + 22.0f, bot }; path(xy, 4, 0xFFFFFF); label = "FILTER"; }
  } else if (pane == PG_LFO) {
    int depth = p->p[SFX_P_LFO_PITCH] > p->p[SFX_P_LFO_FILTER] ? p->p[SFX_P_LFO_PITCH] : p->p[SFX_P_LFO_FILTER];
    if (p->p[SFX_P_LFO_TIMBRE] > depth) depth = p->p[SFX_P_LFO_TIMBRE];
    float cyc = 1.0f + (float)p->p[SFX_P_LFO_RATE] / 127.0f * 5.0f, amp = (bot - top) * 0.5f * (float)depth / 127.0f, cy = (top + bot) * 0.5f, py = cy;
    vg_rect((int)x0, (int)cy, (int)pw, 1, LINE);
    for (int i = 1; i < (int)pw; i++) {
      float ny = cy - vd_sin((float)i / pw * cyc) * amp;
      vg_line(x0 + (float)i - 1.0f, py, x0 + (float)i, ny, 2.0f, depth ? 0xFFFFFF : DIM);
      py = ny;
    }
    label = depth ? "LFO" : "LFO  off";
  } else {
    int prev = mid - (int)(pv_buf[from] * gain);
    vg_rect(x + sw + 2, mid, ww + 4, 1, LINE);
    for (int i = 0; i < ww; i++) {
      int now = mid - (int)(pv_buf[from + i * 240 / ww] * gain), a = now < prev ? now : prev, b = now < prev ? prev : now;
      vg_rect(x + sw + 4 + i, a, 1, b - a + 1, 0xFFFFFF);
      prev = now;
    }
  }
  vg_text(label, x + w - 6, y + h - 18, VG_SMALL, DIM, VG_RIGHT);
}

static void tour_caption(void)                       /* where F1-F6 are, while the tour runs */
{
  vg_round(2, 286, 476, 30, 5, 0x1C2436);
  vg_text(tour_line, 240, 293, VG_MED, TEXT, VG_CENTER);
  vg_rect(2, 282, (int)(476.0f * vd_clamp(tour_progress, 0.0f, 1.0f)), 2, 0x36C5FF);
}

static char *sound_title(char *t)                    /* "PAD 5  COIN" (the stage) */
{
  strcpy(t, "PAD "); vg_itoa(sel + 1, t + 4);
  int n = (int)strlen(t);
  t[n++] = ' '; t[n++] = ' '; strcpy(t + n, pad_name(sel));
  return t;
}

/* the top line: the selected sound – and, right, the song: playing or not, its pattern and tempo, the link */
static void header(uint32_t rgb, uint32_t step, uint32_t now)
{
  char buf[16], t[16];
  vg_round(8, 5, 30, 24, 5, rgb);
  vg_text(vg_itoa(sel + 1, buf), 23, 10, VG_BOLD, BG, VG_CENTER);
  if (tour_on) vg_text(tour_title, 46, 6, VG_BIG, 0x36C5FF, VG_LEFT);
  else if (toast_frames > 0) { toast_frames--; vg_text(toast, 46, 6, VG_BIG, TEXT, VG_LEFT); }
  else {
    int w = vg_text(pad_name(sel), 46, 6, VG_BIG, TEXT, VG_LEFT);
    if (pad_preset[sel] >= 0) vg_text(ENGINE_SHORT[patches[sel].engine], 54 + w, 12, VG_SMALL, rgb, VG_LEFT);
    vg_text("MENU: TOUR", 366, 10, VG_SMALL, 0x48526A, VG_RIGHT);
  }
  t[0] = (char)('A' + (step != NO_STEP ? (int)now : edit_pat)); t[1] = t[2] = ' '; vg_itoa(SONG.bpm, t + 3);
  vg_circle(467, 17, 4, linked ? 0x6EE7A8 : 0x2A3348);
  int ix = 454 - vg_text(t, 454, 10, VG_BOLD, step != NO_STEP ? TEXT : DIM, VG_RIGHT) - 17;
  if (rec) vg_circle((float)ix + 5.0f, 17.0f, 5.0f, 0xFF4040);
  else if (step != NO_STEP) vg_tri((float)ix, 11.0f, (float)ix, 23.0f, (float)ix + 10.0f, 17.0f, 0x6EE7A8);
  else vg_rect(ix, 12, 10, 10, 0x48526A);
}

/* ---- SEQ: the selected sound's 16 steps, big, with their notes – and under them the whole pattern, a row a sound ------ */
#define LX 16                                       /* 16 columns: steps (SEQ) and sounds (SONG) */
#define LW 28
static void draw_seq(uint32_t rgb, uint32_t step, uint32_t now, int heat)
{
  char buf[16], t[40];
  int here = now == (uint32_t)edit_pat && step != NO_STEP;
  strcpy(t, "PATTERN  "); t[8] = (char)('A' + edit_pat);
  int w = vg_text(t, 14, 58, VG_BOLD, 0x36C5FF, VG_LEFT);
  w += vg_text(pad_name(sel), 26 + w, 58, VG_BOLD, rgb, VG_LEFT);
  if (!layer_on(sel)) vg_text("layer off", 38 + w, 58, VG_SMALL, DIM, VG_LEFT);
  strcpy(t, "B: "); strcpy(t + 3, SCALE_NAME[SONG.scale % SFX_SCALE_COUNT]);
  vg_text(t, 466, 58, VG_SMALL, DIM, VG_RIGHT);
  for (int i = 0; i < STEPS; i++) {
    int x = LX + i * LW, set = (pat()->on[i] >> sel) & 1, head = here && step == (uint32_t)i, off = i >= SONG.len, note = pat()->note[sel][i];
    vg_round(x, 76, 26, 32, 4, head ? (set ? 0xFFFFFF : 0x3A4664) : set ? rgb : off ? 0x10141E : (i / 4) & 1 ? 0x1A2233 : 0x242F47);
    if (set && note) vg_text(note_text(note, buf), x + 13, 85, VG_SMALL, BG, VG_CENTER);
    else if (set) vg_circle((float)x + 13.0f, 92.0f, 3.0f, BG);
    else if (i % 4 == 0 && !off) vg_text(vg_itoa(i + 1, buf), x + 13, 85, VG_SMALL, 0x55607A, VG_CENTER);
    if (i == cur_step) vg_frame(x - 2, 74, 30, 36, 2, 0xFFFFFF);
  }
  for (int s = 0; s < SLOTS; s++) {
    uint32_t c = ENGINE_RGB[patches[s].engine];
    int y = 116 + s * 6, layer = heat >= SONG.level[s];
    if (s == sel) vg_tri(7.0f, (float)y - 1.0f, 7.0f, (float)y + 6.0f, 13.0f, (float)y + 2.5f, 0xFFFFFF);
    for (int i = 0; i < STEPS; i++) {
      int set = (pat()->on[i] >> s) & 1, head = here && step == (uint32_t)i, off = i >= SONG.len;
      vg_rect(LX + i * LW, y, 26, 5, set ? (head && layer ? 0xFFFFFF : layer ? c : vg_mix(BG, c, 0.35f)) : head ? 0x34415F : off ? 0x0F131C : s == sel ? 0x2C3854 : (i / 4) & 1 ? 0x151B28 : 0x1B2334);
    }
  }
}

/* ---- SONG: the patterns and their chain – and the layers: a sound plays once INTENSITY reaches its mark ------------- */
static void draw_song(uint32_t step, uint32_t now, int heat)
{
  char buf[16];
  uint16_t used = 0;
  for (int k = 0; k < SFX_PATTERNS; k++) {
    int x = 12 + k * 57, playing_it = (uint32_t)k == now && step != NO_STEP, mine = k == edit_pat;
    uint32_t bg = playing_it ? 0xFFFFFF : mine ? 0x36C5FF : k < SONG.chain ? 0x1F3A52 : PANEL, fg = playing_it || mine ? BG : k < SONG.chain ? TEXT : DIM;
    char letter[2] = { (char)('A' + k), 0 };
    vg_round(x, 58, 53, 38, 5, bg);
    vg_text(letter, x + 6, 60, VG_BOLD, fg, VG_LEFT);
    for (int i = 0; i < STEPS; i++) {                /* what is in it: how many sounds on each step */
      int n = 0;
      for (uint16_t m = SONG.pat[k].on[i]; m; m &= (uint16_t)(m - 1)) n++;
      used |= SONG.pat[k].on[i];
      if (n) vg_rect(x + 3 + i * 3, 58 + 35 - (n > 8 ? 18 : 2 + n * 2), 2, n > 8 ? 18 : 2 + n * 2, vg_mix(bg, fg, 0.75f));
    }
  }
  vg_rect(12, 99, SONG.chain * 57 - 4, 2, 0x36C5FF);  /* the chain: these play one after another */
  vg_text("LAYERS", 14, 104, VG_BOLD, DIM, VG_LEFT);
  vg_text("a sound plays once INTENSITY reaches its mark", 466, 104, VG_SMALL, 0x55607A, VG_RIGHT);
  int iy = 204 - heat * 78 / 127;                    /* the intensity: a level that rises … */
  for (int s = 0; s < SLOTS; s++) vg_rect(LX + s * LW, 122, 26, 82, 0x121824);
  vg_rect_a(LX - 4, iy, STEPS * LW + 6, 204 - iy, 0xFF5A36, 0.16f);
  for (int s = 0; s < SLOTS; s++) {                  /* … over each sound's mark: from there on it plays */
    uint32_t c = ENGINE_RGB[patches[s].engine];
    int x = LX + s * LW, in = (used >> s) & 1, active = in && heat >= SONG.level[s], ly = 204 - SONG.level[s] * 78 / 127;
    if (in) vg_round(x, ly - 5, 26, 6, 2, active ? c : vg_mix(BG, c, 0.40f));
    if (in && (playing >> s) & 1) vg_round(x, ly - 5, 26, 6, 2, 0xFFFFFF);
    vg_text(vg_itoa(s + 1, buf), x + 13, 205, VG_SMALL, s == sel ? 0xFFFFFF : active ? c : 0x3A4358, VG_CENTER);
    if (s == sel) vg_frame(x - 2, 120, 30, 86, 2, 0xFFFFFF);
  }
  vg_rect(LX - 4, iy - 1, STEPS * LW + 6, 2, 0xFF5A36);
  vg_tri(4.0f, (float)iy - 5.0f, 4.0f, (float)iy + 5.0f, 12.0f, (float)iy, 0xFF5A36);
}

/* ---- LINK: what the game sends, what goes to the game ---------------------------------------------------------------- */
static void draw_link(void)
{
  static const char *const IN[6] = { "PITCH", "CUTOFF", "TIMBRE", "LEVEL", "INTENSITY", "PATTERN" };
  static const char *const OUT[3][2] = { { "ENCODERS 1-4, A, B", "CC 20-25" }, { "PADS", "NOTES 36-51" }, { "EXPORT, SEND", "SYSEX" } };
  char buf[16];
  vg_round(12, 58, 226, 160, 6, PANEL);
  vg_text("FROM THE GAME", 22, 64, VG_BOLD, 0xFFD23F, VG_LEFT);
  vg_text("CC 10-15, CH 2", 228, 64, VG_SMALL, DIM, VG_RIGHT);
  for (int k = 0; k < 6; k++) {
    int y = 88 + k * 21;
    vg_text(IN[k], 22, y, VG_SMALL, TEXT, VG_LEFT);
    vg_rect(96, y + 5, 100, 5, LINE);
    vg_rect(96, y + 5, telemetry[k] * 100 / 127, 5, 0xFFD23F);
    vg_text(vg_itoa(telemetry[k], buf), 228, y, VG_SMALL, DIM, VG_RIGHT);
  }
  vg_round(246, 58, 222, 160, 6, PANEL);
  vg_text("TO THE GAME", 256, 64, VG_BOLD, 0x6EE7A8, VG_LEFT);
  for (int k = 0; k < 3; k++) {
    vg_text(OUT[k][0], 256, 88 + k * 21, VG_SMALL, TEXT, VG_LEFT);
    vg_text(OUT[k][1], 458, 88 + k * 21, VG_SMALL, DIM, VG_RIGHT);
  }
  vg_rect(256, 156, 202, 1, LINE);
  vg_circle(262.0f, 174.0f, 5.0f, linked ? 0x6EE7A8 : 0x2A3348);
  vg_text(linked ? "LINKED" : "WAITING FOR A GAME", 274, 167, VG_BOLD, linked ? 0x6EE7A8 : DIM, VG_LEFT);
  if (link_text[0]) vg_text(link_text, 256, 190, VG_MED, TEXT, VG_LEFT);
}

void vibe_on_draw(uint16_t *fb)
{
  static const char *const SUB[4] = { "OSC", "TONE", "FILTER", "WAVE" };
  static const char *const HINT[PG_SEQ] = { 0, 0, 0, 0, "SUSTAIN off: a one-shot. Above: held while pressed",
                                            "A slow wave that moves pitch, filter and timbre", "Sends to chorus, delay and reverb - and the level" };
  char buf[24];
  (void)fb;
  if (export_at >= 0) export_pump();
  tour_pump();
  vg_clear(BG);
  if (help) {
    if (page == PG_SEQ) vg_controls("SFX FORGE: SEQ", HELP_SEQ, sizeof HELP_SEQ / sizeof HELP_SEQ[0] / 2);
    else if (page == PG_SONG) vg_controls("SFX FORGE: SONG", HELP_SONG, sizeof HELP_SONG / sizeof HELP_SONG[0] / 2);
    else if (page == PG_LINK) vg_controls("SFX FORGE: LINK", HELP_LINK, sizeof HELP_LINK / sizeof HELP_LINK[0] / 2);
    else vg_controls("SFX FORGE: SOUND", HELP_SOUND, sizeof HELP_SOUND / sizeof HELP_SOUND[0] / 2);
    return;
  }
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine], on = playing, step = seq_step, now = seq_pattern;
  int seq = page == PG_SEQ, song = page == PG_SONG, heat = (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f), wt = p->engine == SFX_ENGINE_WAVETABLE;

  header(rgb, step, now);

  for (int i = 0; i < TAB_COUNT; i++) {                /* the pages (K1-K6) */
    int x = 12 + i * 77, here = TAB_OF[page] == i;
    vg_round(x, 34, 72, 20, 5, here ? rgb : PANEL);
    vg_text(i == TAB_SEQ && song ? "SONG" : TAB_NAME[i], x + 36, 37, VG_BOLD, here ? BG : DIM, VG_CENTER);
  }

  /* the 16 pads as they are on this page: sounds, the selected sound's steps, or patterns and intensity */
  for (int s = 0; s < SLOTS; s++) {
    int state, frame = 0;
    uint32_t base;                                   /* state: 0 off, 1 dim, 2 set, 3 bright */
    if (seq) {
      int set = (pat()->on[s] >> sel) & 1, head = step == (uint32_t)s && now == (uint32_t)edit_pat;
      base = rgb; state = head ? 3 : set ? 2 : s >= SONG.len ? 0 : 1;
    } else if (song) {
      if (s < 8) { base = 0x36C5FF; state = (uint32_t)s == now && step != NO_STEP ? 3 : s == edit_pat ? 2 : s < SONG.chain ? 1 : 0; }
      else { base = 0xFF5A36; state = heat >= (s - 7) * 127 / 8 ? 2 : 1; }
    } else {
      base = ENGINE_RGB[patches[s].engine]; state = (on >> s) & 1 ? 3 : s == sel ? 2 : 1; frame = s == sel;
    }
    if (sound_page()) {
      int x = 356 + (s % 4) * 29, y = 58 + (s / 4) * 34;
      vg_round(x, y, 26, 30, 4, state == 3 ? 0xFFFFFF : state == 2 ? base : vg_mix(BG, base, 0.30f));
      vg_text(vg_itoa(s + 1, buf), x + 13, y + 8, VG_SMALL, state >= 2 ? BG : vg_mix(BG, base, 0.85f), VG_CENTER);
      if (frame && state != 3) vg_frame(x - 1, y - 1, 28, 32, 1, 0xFFFFFF);
    }
    uint32_t led = (base & 0xFFFFFF) | (uint32_t)(state + 4 * (seq + 2 * song)) << 24;
    if (led != led_cache[s]) { led_cache[s] = led; vibe_led_pad(s, state == 3 ? 0xFFFFFF : base, state == 3 ? 1.0f : state == 2 ? 0.75f : state == 1 ? 0.12f : 0.0f); }
  }

  /* the page itself, and the four values over encoders 1-4 */
  vibe_link_page(page <= PG_WAVE ? SUB[page] : song ? "SONG" : TAB_NAME[TAB_OF[page]]);
  if (page == PG_LINK) {
    draw_link();
    for (int i = 0; i < 4; i++) {
      char n[8] = "CC "; vg_itoa(CC_ENCODERS + i, n + 3);
      cell(i, n, vg_itoa(cc_out[i], buf), (float)cc_out[i] / 127.0f, 0, 0x6EE7A8);
    }
  } else if (seq) {
    int note = pat()->note[sel][cur_step];
    draw_seq(rgb, step, now, heat);
    cell(0, "STEP", vg_itoa(cur_step + 1, buf), (float)(cur_step + 1) / (float)STEPS, 0, rgb);
    cell(1, "NOTE", note_text(note, buf), note ? (float)(note - 24) / 84.0f : 0.0f, 0, rgb);
    cell(2, "TEMPO", vg_itoa(SONG.bpm, buf), (float)(SONG.bpm - 40) / 200.0f, 0, rgb);
    cell(3, "SWING", percent(SONG.swing * 127 / 100, buf), (float)SONG.swing / 100.0f, 0, rgb);
  } else if (song) {
    char chain[4] = "A-A";
    draw_song(step, now, heat);
    chain[2] = (char)('A' + SONG.chain - 1); if (SONG.chain == 1) chain[1] = 0;
    buf[0] = (char)('A' + edit_pat); buf[1] = 0;
    cell(0, "PATTERN", buf, (float)(edit_pat + 1) / (float)SFX_PATTERNS, 0, 0x36C5FF);
    cell(1, "CHAIN", chain, (float)SONG.chain / (float)SFX_PATTERNS, 0, 0x36C5FF);
    cell(2, "INTENSITY", percent(heat, buf), (float)heat / 127.0f, 0, 0xFF5A36);
    cell(3, "LAYER", percent(SONG.level[sel], buf), (float)SONG.level[sel] / 127.0f, 0, layer_on(sel) ? rgb : DIM);
  } else {
    preview(12, 58, 332, 136, p, rgb, page);
    vg_text(ENGINE_NAME[p->engine], 411, 198, VG_SMALL, rgb, VG_CENTER);
    if (page <= PG_WAVE) for (int i = 0; i < 4; i++) {     /* SOUND's pages: K1 again walks them */
      vg_round(12 + i * 84, 199, 80, 18, 5, i == page ? vg_mix(BG, rgb, 0.45f) : PANEL);
      vg_text(SUB[i], 52 + i * 84, 201, VG_SMALL, i == page ? TEXT : i == 3 && !wt ? 0x333C52 : DIM, VG_CENTER);
    } else vg_text(HINT[page], 14, 200, VG_SMALL, DIM, VG_LEFT);
    for (int i = 0; i < 4; i++) {
      int k = PAGE_PARAMS[page][i];
      if (k == ENGINE_KNOB) cell(i, "ENGINE", ENGINE_SHORT[p->engine], (float)(p->engine + 1) / (float)SFX_ENGINE_COUNT, 0, rgb);
      else if (k == SFX_P_TABLE) cell(i, "TABLE", param_text(k, p, buf), (float)(p->p[k] % SFX_WT_TABLES + 1) / (float)SFX_WT_TABLES, 0, rgb);
      else if (k >= 0) cell(i, param_name(k, p->engine), param_text(k, p, buf), (float)p->p[k] / 127.0f, k == SFX_P_PITCH_ENV_AMT || k == SFX_P_FILTER_ENV, rgb);
      else cell(i, "", "", -1.0f, 0, rgb);
    }
  }

  /* F1-F6 */
  if (tour_on) tour_caption();
  else for (int i = 0; i < 6; i++) {
    int lit = song && i == 0 && running, dim = sound_page() && i == 2 && !has_undo[sel];
    vg_round(2 + i * 80, 286, 76, 30, 5, lit ? 0x6EE7A8 : 0x1C2436);
    vg_text(SOFT[soft_set()][i], 40 + i * 80, 294, VG_BOLD, lit ? BG : dim ? 0x48526A : TEXT, VG_CENTER);
  }

  /* FOCUS: everything above with DATA + ENTER (and the mouse) */
  vf_begin();
  for (int i = 0; i < TAB_COUNT; i++) vf_key(VB_K1 + i, 12 + i * 77, 34, 72, 20);
  for (int i = 0; i < 4; i++) vf_encoder(VE_1 + i, 12 + i * 116, EY, 108, 58);
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
  if (tour_on) {
    vg_text(tour_title, SW - 24, 14, VG_BIG | VG_X2, TEXT, VG_RIGHT);
    vg_text(tour_line, SW - 24, 62, VG_BIG, 0x36C5FF, VG_RIGHT);
  } else vg_text(toast_frames > 0 ? toast : sound_title(t), SW - 24, 20, VG_BIG | VG_X2, toast_frames > 0 ? TEXT : rgb, VG_RIGHT);

  for (int s = 0; s < SLOTS; s++) {                   /* the pads */
    int x = SP_X + (s % 4) * (SP_W + SP_GAP), y = SP_Y + (s / 4) * (SP_H + SP_GAP), lit = (on >> s) & 1;
    uint32_t c = ENGINE_RGB[patches[s].engine];
    vg_round(x, y, SP_W, SP_H, 8, lit ? 0xFFFFFF : vg_mix(BG, c, s == sel ? 0.9f : 0.4f));
    if (s == sel && !lit) vg_frame(x, y, SP_W, SP_H, 2, 0xFFFFFF);
    vg_text(vg_itoa(s + 1, buf), x + 10, y + 8, VG_BIG, lit || s == sel ? BG : TEXT, VG_LEFT);
    vg_text(pad_name(s), x + SP_W / 2, y + SP_H - 24, VG_SMALL, lit || s == sel ? BG : vg_mix(BG, TEXT, 0.7f), VG_CENTER);
  }
  vg_text("PADS  -  click to play, as the 16 pads", SP_X, SP_Y + 4 * (SP_H + SP_GAP) + 2, VG_SMALL, DIM, VG_LEFT);

  preview(SG_X, SP_Y, 262, 60, p, rgb, PG_OSC);                /* the selected sound, and beside it what plays now */
  scope(SG_X + 268, SP_Y, STEPS * SG_W - 2 - 268, 60, rgb);

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
    int x = 24 + b * (SB_W + 8), lit = (b == 0 && running) || (b == 1 && rec) || (b == 7 && tour_on);
    vg_round(x, SB_Y, SB_W, 44, 8, lit ? (b == 0 ? 0x6EE7A8 : b == 1 ? 0xFF4040 : 0x36C5FF) : 0x1C2436);
    vg_text(STAGE_BTN[b][0], x + SB_W / 2, SB_Y + 11, VG_BIG, lit ? BG : TEXT, VG_CENTER);
    vg_text(STAGE_BTN[b][1], x + SB_W / 2, SB_Y + 50, VG_SMALL, DIM, VG_CENTER);
  }
  vg_text(vg_itoa(SONG.bpm, buf), SW - 24, SB_Y - 2, VG_BIG | VG_X2, TEXT, VG_RIGHT);
  vg_text("BPM (SEQ page)", SW - 24, SB_Y + 50, VG_SMALL, DIM, VG_RIGHT);
}
