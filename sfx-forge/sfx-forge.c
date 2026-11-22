/* SPDX-License-Identifier: MIT
 * SFX FORGE – sound effects, instruments and adaptive music for games, on VIBE FM.
 *
 *   two kits     SFX (the game's sound effects) and MUSIC (the instruments the song plays): 16 sounds each. SHIFT +
 *                ENTER (a short press) changes kit
 *   16 pads      the kit's 16 sounds, each a 28-byte patch (core/sfx_core.h). Five engines: IMPACT, UI BLIP, SCI-FI FM, FOLEY,
 *                WAVETABLE – with envelopes, an LFO, and sends to a chorus, a delay and a reverb
 *   keys         the selected sound as an instrument, in tune, 16 voices
 *   K1-K6        SOUND (press again: its next page) · ENV · LFO · FX (again: MIX, MASTER) · SEQ (again: SONG, AUTO) · LINK
 *   AUTO         automation: a lane for every sound, moving any knob - one of the sound's, its fader, the master's
 *   SONG         also the arrangement: up to 32 bars, a pattern each
 *   MIX          a fader, a pan, mute and solo for every sound; a drive on the sum
 *   MASTER       the delay's time and feedback, the reverb's size and damping
 *                encoders 1-4 = the four values shown
 *   F1           PRESET: the next of 32 named sounds onto the selected pad (SHIFT: the one before)
 *   LEFT RIGHT   the sound page before / after; on SEQ, SONG and AUTO the pattern
 *   PLAY         plays what the top line of the song's pages says: LOOP – the pattern repeats, a chosen one takes over on
 *                the bar line – or SONG – the arrangement, else the chain. F1 there changes it; REC lit switches to LOOP
 *   SEQ          a pattern over the 16 sounds: a bar of 16, 32 or 64 steps (its grid: SHIFT + F5 / F6). The pads are 16
 *                steps of the selected sound (SHIFT + LEFT / RIGHT: the 16 before / after), a step may carry a note
 *                (hold its pad and play a key, or the NOTE encoder), a velocity and a length
 *   SONG         8 patterns, a chain, and what makes it game music: every sound has a LAYER threshold and plays only
 *                while INTENSITY is at or above it; a cued pattern takes over on the next bar line
 *   LINK         the game engine's side: telemetry in (CC 10-15, channel 2: pitch, cutoff, timbre, level, intensity,
 *                pattern), encoders out as CC 20-25, patches and the song as SysEx (README.md)
 *   the stage    the whole instrument for the mouse (plugin and browser): pads, the sound's pages with values to
 *                drag, the song as 16 rows of steps with their notes, intensity and layer as sliders
 *   MENU         the tour: it plays and names what SFX FORGE can do, a caption at a time. SHIFT + MENU twice: factory reset
 * On the sound pages the display shows the selected sound itself – its whole shape and its wave – as the knobs change it.
 *
 * Two flows: the player (core/sfx_song.h: voices, effects and sequencer) belongs to the audio, its song included. The
 * controls reach it through one queue. The song they edit is their own; after every event and every draw what differs
 * goes on its way to the player, which takes it whole between two blocks (song_way.h) - so a grid change, a COPY, an
 * UNDO or a song that comes in while it plays is heard from one block on, never half made.
 */
#include "vibe.h"
#include "vibe_dsp.h"
#include "vibe_gfx.h"
#include <stddef.h>
size_t strlen(const char *s);                      /* (the SDK brings these two) */
char *strcpy(char *d, const char *s);

#include "../core/sfx_song.h"
#include "song_way.h"

VIBE_PARALLEL_AUDIO

#define SLOTS  SFX_TRACKS
#define PADS   16                  /* the pads, and the steps they show at a time */

/* ---- the protocol (README: "The link") --------------------------------------------------------------------------- */
#define SX_DUMP     0x01           /* out (and in): F0 7D 56 42 01 <slot> <28 patch bytes> F7 (in: not our own, come straight back) */
#define SX_LOAD     0x02           /* in:           F0 7D 56 42 02 <slot> <28 patch bytes> F7. Slot 0-15: the pad of the kit that is
                                      selected. Slot 16-31 (1.3.1): pad 1-16 of the MUSIC kit, whatever is selected - a song's sounds */
#define SX_TEXT     0x03           /* in:           F0 7D 56 42 03 <ASCII, up to 24> F7 */
#define SX_REQUEST  0x04           /* in:           F0 7D 56 42 04 <slot> F7  -> answers with a dump */
#define SX_SONG     0x05           /* both ways (1.6.0): F0 7D 56 42 05 <piece lo> <piece hi> <format> <pieces lo> <pieces hi> <1..240 song bytes> F7,
                                      in any order. In, also the shape of before: F0 7D 56 42 05 <piece> 39 <up to 200 bytes> F7 (a format-5 song) */
#define SX_REQ_SONG 0x06           /* in:           F0 7D 56 42 06 F7 -> answers with 16 dumps and the song */
#define SX_REQ_IDENT 0x07          /* in (1.4.0):   F0 7D 56 42 07 F7 (these 6 bytes, no others) -> answers with the identity */
#define SX_IDENT    0x08           /* out:          F0 7D 56 42 08 <18 bytes, each 0..127> F7. One that comes in is left alone */
/* The identity: what this device is and what it holds, so that a game can ask before it sends. Fields are only ever
   added at the end: a reader takes the first 18 and ignores what follows. A firmware before 1.4.0 never answers. */
#define FW_MAJOR 1                 /* the firmware's version: firmware.json says the same (tests/check-identity.mjs) */
#define FW_MINOR 6
#define FW_PATCH 0
#define ID_HARDWARE 1              /* 0 unknown, 1 VIBE FM (the SDK's host: emulator, plugin, VIBE Box - not told apart from here) */
#define ID_PROFILE  1              /* what SFX FORGE 1.x offers on VIBE FM */
#define ID_FLAGS    0x0F           /* bit0 pads, bit1 encoders for the game (LINK), bit2 takes a song's sounds (LOAD 16-31), bit3 takes telemetry */
#define SONG_OLD_PIECE  200        /* the song message up to 1.5.0: 39 pieces of 200 bytes (the last 99), a 7-bit piece number */
#define SONG_OLD_PIECES 39
#define SONG_PIECE      240        /* from 1.6.0: pieces of 240 bytes (the last what is left), 14-bit piece number and count */
#define SONG_PIECES_MAX 160
_Static_assert(SONG_PIECES_MAX * SONG_PIECE >= SFX_SONG_MAX && SONG_OLD_PIECES * SONG_OLD_PIECE >= SFX_SONG_BYTES_V5, "a song fits its pieces");
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
  /* the SFX kit: what a game makes noise with */
  { "EXPLOSION", 0, { 28,  96, 55,  20,  60,  70,  85, 10, 100,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 30, 100, 0 } },
  { "PUNCH",     0, { 32, 100, 28,   0,  20,  25,  90,  5,  55,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "THUD",      0, { 24,  90, 35,   0,   0,   5,  70,  0,  70,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "METAL HIT", 0, { 62,  70, 20,  75,  80,  15, 110, 40,  75,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 20, 100, 0 } },
  { "COIN",      1, { 86,  64,  0,  26,  60,   0, 127,  0,  62,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "SELECT",    1, { 78,  64,  0,   0,   0,   0, 120,  0,  35,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "ERROR",     1, { 42,  58, 60,   0,   0,   0,  80, 20,  60, 50,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "POWER-UP",  1, { 60,  40, 75,  37,  75,   0, 115, 10,  85, 20,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "LASER",     2, { 70, 110, 45,  50,  70,   0, 120, 20,  58,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "ZAP",       2, { 80, 120, 25, 100,  90,  10, 127,  0,  45, 30,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "ENGINE HUM",2, { 22,  64,  0,  21,  55,   8,  78, 45,  60,  0,    0, 127, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "WARP",      2, { 66,  30, 90,  40,  60,   0, 110, 30, 100,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0, 30, 30, 100, 0 } },
  { "FOOTSTEP",  3, { 30,  80, 25,  90,  40, 127,  62, 15,  45,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "WIND",      3, { 30,  64,  0,  20,  70, 127,  75, 55,  60,  0,   40, 127, 60,  64, 40,   20,  0, 40,   0,    0,  0,  0, 30, 100, 0 } },
  { "WHOOSH",    3, { 30,  30, 85,   0,   0, 127,  90, 45,  88,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 20, 100, 0 } },
  { "CRACKLE",   3, { 30,  64,  0, 110, 127, 127, 105, 20, 100, 25,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  /* the MUSIC kit: a band. Drums (a sine that falls for the kick, noise over a tone for the snare, a high band of noise
     for the hats), FM for the bass and the keys – its index falls with ENV TIME, which is the pluck –, wavetables for
     what is held. The first 16 are on the pads at the start. */
  { "TIMPANI",   0, { 30,  76, 40,  20,  25,  12,  62, 20,  95,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 70, 100, 0 } },
  { "SNARE",     0, { 44,  84, 18,  30,  30,  85, 102,  5,  46,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 14, 100, 0 } },
  { "KICK",      0, { 20,  98, 30,   0,   0,   3,  76,  0,  60,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 105, 0 } },
  { "COWBELL",   0, { 72,  64,  0,  75,  60,   0, 110, 30,  40,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0,  90, 0 } },
  { "PIZZICATO", 4, { 52,  64,  0,   0,   0,   0,  60, 10,  45,  0,    0,   0, 34, 100, 35,   50,  0,  0,   0,    0,  0,  0, 45, 110, 0 } },
  { "BRASS",     4, { 52,  64,  0,  20,  10,   0,  70, 15,  70,  0,   45, 100, 45, 100, 60,   87,  8,  0,   0,   30, 20,  0, 50, 100, 0 } },
  { "FM BASS",   2, { 22,  64, 35,  42,  50,   0,  96, 10,  62,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 108, 0 } },
  { "FM KEYS",   2, { 58,  64, 55,  42,  38,   0,  98,  0,  84,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0, 35, 18, 28, 100, 0 } },
  { "HARP",      4, { 58,  64,  0,  10,   0,   0,  85,  5,  85,  0,    0,   0, 34,  90, 50,   50,  0,  0,   0,    0,  0, 20, 60, 110, 2 } },
  { "CELESTA",   2, { 70,  64, 60,  81,  45,   0, 120,  0,  90,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0, 55, 100, 0 } },
  { "ORGAN",     4, { 52,  64,  0,  70,   0,   0, 110,  0,  80,  0,    5, 120, 30,  64, 40,   50,  0,  0,   0,    0, 60,  0, 40,  95, 4 } },
  { "WT SWEEP",  4, { 46,  64,  0,  10,  20,   0, 100, 30,  80,  0,    5,  90, 60,  90, 70,   30,  0, 20,  80,   35, 50, 30, 40, 100, 0 } },
  { "HI-HAT",    3, { 30,  64,  0,   0,   0, 127, 114, 25,  18,  0,    0,   0, 20,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0,  85, 0 } },
  { "BELLS",     4, { 70,  64,  0,  60,   0,   0, 120,  0, 105,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,   20,  0, 30, 70, 100, 5 } },
  { "CHOIR",     4, { 58,  64,  0,   0,   0,   0,  95,  5,  90,  0,   85, 115, 80,  64, 40,   85, 10,  0,  30,   35, 60,  0, 90, 100, 3 } },
  { "STRINGS",   4, { 52,  64,  0,   0,   0,   0,  88,  5,  90,  0,   75, 110, 70,  72, 80,   87, 12,  0,   0,   40, 70,  0, 70,  95, 0 } },
  { "OPEN HAT",  3, { 30,  64,  0,   0,   0, 127, 112, 25,  52,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  8,  80, 0 } },
  { "FLUTE",     4, { 64,  64,  0,   0,   0,   8, 100,  0,  80,  0,   50, 105, 45,  64, 40,   87, 10,  0,   0,    0,  0,  0, 55, 100, 2 } },
  { "WT VOWEL",  4, { 46,  64,  0,  20,   0,   0, 105, 10,  80,  0,   10, 100, 55,  64, 40,   45,  0,  0, 100,   30, 30, 40, 30, 100, 3 } },
  { "WT BASS",   4, { 34,  64,  0,  40,  50,   0,  60, 40,  70,  0,    0,   0, 34, 105, 55,   50,  0,  0,   0,    0,  0,  0,  0, 105, 1 } },
  { "PAD",       4, { 52,  64,  0,  50,   0,   0,  80, 10,  90,  0,   95, 110, 95,  64, 40,   30,  0, 30,  20,   55, 80,  0, 90,  95, 0 } },
  { "SQ BASS",   1, { 19,  64,  0,   0,   0,   0,  78, 25,  60,  0,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0,  0,  0, 100, 0 } },
  { "SQ LEAD",   1, { 60,  64,  0,   0,   0,   0, 108, 10,  56, 15,    0,   0, 34,  64, 40,   50,  0,  0,   0,    0,  0, 35,  0, 100, 0 } },
};
#define SFX_PRESETS 16             /* the first 16: the SFX kit's. The rest: the MUSIC kit's */
#define PRESET_COUNT ((int)(sizeof PRESETS / sizeof PRESETS[0]))
static const char *const ENGINE_NAME[SFX_ENGINE_COUNT] = { "IMPACT", "UI BLIP", "SCI-FI FM", "FOLEY", "WAVETABLE" };
static const char *const ENGINE_SHORT[SFX_ENGINE_COUNT] = { "IMPACT", "BLIP", "FM", "FOLEY", "WAVE" };
static const uint32_t ENGINE_RGB[SFX_ENGINE_COUNT] = { 0xFF5A36, 0xFFD23F, 0x36C5FF, 0x7BE07B, 0xC48BFF };
static const char *const SCALE_NAME[SFX_SCALE_COUNT] = { "CHROMATIC", "MAJOR", "MINOR", "PENTATONIC" };

/* ---- the audio flow's own: the player, with the song it plays (P.song: the controls never touch it) ---------------- */
static sfx_player P;
/* ---- the song, as the controls have it: every edit is made here, and this is what is shown, kept and sent. The audio
   never looks at it. WAY takes it to the player (song_way.h): song_hand when it may have changed, song_way_take at the
   start of a block. Two songs more than the one that plays: 2 x 38 KB. ------------------------------------------------- */
static sfx_song song_ed;
#define SONG song_ed
static song_way WAY;
static void song_hand(void) { song_way_hand(&WAY, &SONG); }  /* (should the audio be taking the one before right now, and again: with the next event or draw) */
static float SR, mixl[VIBE_MAX_BLOCK], mixr[VIBE_MAX_BLOCK];
static sfx_mods mods = { 0, 0, 0, 1 };
static float vol_s;
/* what the audio shows the controls: each a 32-bit value only the audio writes */
#define NO_STEP 0xFFFFFFFFu
#define SCOPE 164
static volatile uint32_t seq_arr;                    /* the bar of the arrangement that plays (NO_STEP: none) */
static volatile uint32_t seq_step = NO_STEP, seq_pattern, play_k[2];   /* play_k: what sounds, a bit a sound, for each kit */
/* where the song is, in one word (so that step, grid and fraction are read together): div << 28 | step << 20 | fraction
   of the step in 20 bits. step counts in the grid of the pattern that plays, div is its steps a sixteenth. */
static volatile uint32_t seq_pos = NO_STEP;
static int pos_step(uint32_t w) { return (int)(w >> 20 & 0xFF); }
static int pos_div(uint32_t w) { int d = (int)(w >> 28); return d == 2 || d == 4 ? d : 1; }
static float pos_frac(uint32_t w) { return (float)(w & 0xFFFFFu) * (1.0f / 1048575.0f); }
static volatile float scope_lo[SCOPE], scope_hi[SCOPE];
static volatile uint32_t scope_w;
static float sc_lo, sc_hi; static int sc_n;
/* the metronome: a click on every beat while REC is on and the song runs – higher on the bar line. The firmware's own:
   it is not part of the song, and a game never hears it. No count-in: REC loops the pattern, the first pass is one. */
static volatile int metro = 1, rec_on;              /* (set by the controls: SHIFT + REC, REC) */
static float click_env, click_ph, click_f; static int click_step = -1;

/* ---- the control flow's own ---------------------------------------------------------------------------------------- */
enum { PG_OSC, PG_TIMBRE, PG_FILTER, PG_WAVE, PG_ENV, PG_LFO, PG_FX, PG_SEQ, PG_SONG, PG_AUTO, PG_MIX, PG_MASTER, PG_LINK, PG_COUNT };
enum { TAB_SOUND, TAB_ENV, TAB_LFO, TAB_FX, TAB_SEQ, TAB_LINK, TAB_COUNT };
static const uint8_t TAB_OF[PG_COUNT] = { TAB_SOUND, TAB_SOUND, TAB_SOUND, TAB_SOUND, TAB_ENV, TAB_LFO, TAB_FX, TAB_SEQ, TAB_SEQ, TAB_SEQ, TAB_FX, TAB_FX, TAB_LINK };
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
/* two kits of 16 sounds: MUSIC – the instruments the song plays, through the mixer – and SFX – the game's sound effects,
   beside the song. The pads, the keys and the sound pages work on the kit that is chosen; the song pages mean MUSIC. */
enum { KIT_MUSIC, KIT_SFX };
static const char *const KIT_NAME[2] = { "MUSIC", "SFX" };
static sfx_patch kits[2][SLOTS], undo_k[2][SLOTS];
static uint8_t has_undo_k[2][SLOTS];
static int8_t pad_preset_k[2][SLOTS], undo_preset_k[2][SLOTS];   /* which named sound a pad holds, -1 = its own */
static int kit = KIT_SFX;
#define patches     kits[kit]
#define undo        undo_k[kit]
#define has_undo    has_undo_k[kit]
#define pad_preset  pad_preset_k[kit]
#define undo_preset undo_preset_k[kit]
static int sel, page, help, shift, rec, running;
static int loop_mode;                               /* what PLAY plays: 1 LOOP (the pattern repeats), 0 SONG (the arrangement, else the chain) */
static int queued = -1;                             /* the pattern chosen while it runs that has not started yet */
static sfx_song undo_song;                          /* F2 on SEQ, SONG, AUTO: the song as it was one step ago … */
static int undo_has, undo_whole, undo_redo, undo_run;   /* … whole: tempo and mix too, else its notes; run: the edits (1), the take (2) or the bars (3) that are one step */
static int undo_pat = -1;                           /* COPY went on to its copy: the pattern to go back to with it */
static const char *undo_what = "";
static int f4_held, erase_at, erase_pat;            /* SEQ, REC while it plays: F4 held erases the selected sound as the bar passes; the step erased last (-1: none yet), and in which pattern */
static int from_outside;                            /* a game's or a DAW's note is being played: nothing is said about it */
static int tour_mode0;
static uint32_t blink;                              /* draws counted: what is queued blinks */
#define BLINK ((blink >> 3) & 1)
static int edit_pat, cur_step;                      /* the pattern and the step being edited (a step of that pattern's grid) */
static int held_pad = -1, held_step = -1;           /* the pad held on SEQ, and the step it stands for */
static int follow_win = -1;                         /* the 16 steps a take showed last (-1: none) */
static int cur_pat = -1, cur_div = 1;               /* what cur_step was counted in: the pattern, its steps a sixteenth */
static const char *const GRID_NAME[SFX_GRIDS] = { "1/16", "1/32", "1/64" };
static int wrap(int v, int d, int n) { return ((v + d) % n + n) % n; }
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int bar_len(void) { return SONG.len >= 1 && SONG.len <= SFX_BAR ? SONG.len : SFX_BAR; }   /* the bar in sixteenths */
static int pat_grid(int k) { int g = SONG.pat[k % SFX_PATTERNS].grid; return g < SFX_GRIDS ? g : 0; }
static int pat_div(int k) { return sfx_div(SONG.pat[k % SFX_PATTERNS].grid); }
static int gsteps(void) { return SFX_BAR * pat_div(edit_pat); }      /* the cells of the pattern shown: 16, 32, 64 */
static int touch_i = -1; static uint64_t touch_t;    /* the encoder (1-4) turned last, and when */
static float acc[VE_COUNT];                         /* encoder turns not yet a whole step */
static uint32_t rng = 0x5F3759DFu;
static uint8_t telemetry[8], cc_out[6] = { 64, 64, 64, 64, 64, 64 };
static int linked;                                  /* something came in from the other side */
static char link_text[25];
static const char *toast; static uint64_t toast_t; static int toast_live;   /* what the display says for a moment, and since when */
static uint32_t led_cache[SLOTS];
static int stage_pad = -1;                          /* the pad the mouse holds on the stage */
static int tour_on, tour_i;                         /* the tour (MENU): running, its next step */
static uint64_t tour_t0;
static const char *tour_title = "", *tour_line = "";
static float tour_progress, tour_heat0, tour_ramp_from, tour_ramp_to, tour_ramp_t0, tour_ramp_len;
static int tour_kit0;
static int tour_page0, tour_sel0, tour_kept = -1;   /* what it puts back; the sound it changed */
static sfx_patch tour_keep;
static int export_at = -1;                          /* sending the sounds and the song: the next message, -1 = not */
/* a song coming in: its pieces, which of them are here (a bit a piece), how many it has, their size (200: the shape of
   before, 240), the format its pieces name (0: the old shape names none), how long its last piece is, when the last came */
static uint8_t song_rx[SONG_PIECES_MAX * SONG_PIECE]; static uint32_t song_rx_seen[(SONG_PIECES_MAX + 31) / 32];
static int song_rx_K, song_rx_size, song_rx_format, song_rx_last; static uint64_t song_rx_t;
/* how often each piece of it came, and the most of them; after it was taken: the pieces that came again since (a bit a
   piece, and how often), and whether one of them had other bytes */
static uint8_t song_rx_n[SONG_PIECES_MAX], song_rx_n2[SONG_PIECES_MAX]; static int song_rx_most;
static uint32_t song_rx_again[(SONG_PIECES_MAX + 31) / 32]; static int song_rx_done, song_rx_other;
/* What went out last, to know our own echo by (sysex_in): the dump of each slot, and the song as it was sent. */
static uint8_t sent_dump[SLOTS][SFX_PATCH_BYTES], song_tx[SONG_PIECES_MAX * SONG_PIECE];
static int song_tx_n, song_tx_K;                    /* the song as it was sent: its bytes, its pieces */
static uint64_t sent_dump_t, sent_song_t; static uint32_t sent_dump_any; static int sent_song_any;
#define ECHO_FRAMES ((uint64_t)SR / 10)              /* an echo comes back within a tenth of a second */
static const char *const SOFT[5][6] = { { "PRESET", "MUTATE", "UNDO", "RANDOM", "EXPORT", "SAVE" },
                                        { "TO SONG", "UNDO", "FILL", "CLEAR", "COPY", "SAVE" },
                                        { "TO SONG", "UNDO", "CLR PAT", "NEW", "SEND", "SAVE" },
                                        { "MUTE", "SOLO", "RESET", "ALL ON", "PLAY", "SAVE" },
                                        { "TO SONG", "UNDO", "RAMP", "CLEAR", "COPY", "SAVE" } };
static int mix_page(void) { return page == PG_MIX || page == PG_MASTER; }
static int soft_set(void) { return page == PG_SEQ ? 1 : page == PG_SONG ? 2 : mix_page() ? 3 : page == PG_AUTO ? 4 : 0; }
static int sound_page(void) { return page < PG_SEQ; }
static int song_keys(void) { return page == PG_SEQ || page == PG_SONG || page == PG_AUTO; }
static const char *soft_caption(int i)               /* F1-F6 as they are now: F1 says what it will do, F4 erases while a take runs */
{
  if (i == 0 && song_keys()) return loop_mode ? "TO SONG" : "TO LOOP";
  if (i == 3 && page == PG_SEQ && rec && running) return "ERASE";
  if (shift && i >= 4 && (page == PG_SEQ || page == PG_AUTO)) {      /* SHIFT held: F5 and F6 are the pattern's grid, coarser and finer */
    static const char *const TO[2][SFX_GRIDS] = { { "IS 1/16", "TO 1/16", "TO 1/32" }, { "TO 1/32", "TO 1/64", "IS 1/64" } };
    return TO[i - 4][pat_grid(edit_pat)];
  }
  return SOFT[soft_set()][i];
}
static void relabel(void) { for (int i = 0; i < 6; i++) vibe_label(VB_F1 + i, soft_caption(i)); }

/* ---- from the controls to the audio: one queue, one writer, one reader ---------------------------------------------- */
enum { M_TRIGGER, M_RELEASE, M_UPDATE, M_START, M_STOP, M_CUE, M_QUIET, M_LOOP };
typedef struct { uint8_t type, slot, note, kit; int16_t key; float vel; uint32_t need; sfx_patch patch; } msg;   /* need: the put of the song it waits for (those four only) */
static int msg_song(int type) { return type == M_START || type == M_STOP || type == M_CUE || type == M_LOOP; }   /* what starts or changes what plays: it must find the song as it was when this was sent */
#define QN 64
static msg q[QN];
static volatile uint32_t qw, qr;

static void send(int type, int slot, int key, float vel, int note)
{
  uint32_t need = msg_song(type) ? song_way_need(&WAY, &SONG) : 0;   /* the song first, as it is now (whole, wherever one of these is sent): what starts, starts in it - the audio works this only with that song, or a later one */
  uint32_t w = qw;
  if (w - qr >= QN) return;                          /* full: dropped */
  msg *m = &q[w % QN];
  m->type = (uint8_t)type; m->slot = (uint8_t)slot; m->key = (int16_t)key; m->vel = vel; m->note = (uint8_t)note; m->need = need;
  m->patch = patches[slot & 15]; m->kit = (uint8_t)kit;
  VIBE_RELEASE();
  qw = w + 1;
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  (void)in;
  uint32_t w = qw, k;
  VIBE_ACQUIRE();
  song_way_take(&WAY, &P.song);                      /* the song as the controls have made it since, whole - or none this block, should they be writing the next right now. A grid that changed under the playhead: the player finds its step again */
  for (k = qr; k != w; k++) {
    const msg *m = &q[k % QN];
    if (msg_song(m->type) && !song_way_has(&WAY, m->need)) break;   /* its song is not here yet (one of the two stepped back): it waits, and all behind it, in their order - a start never runs in the song from before the edit that came first */
    switch (m->type) {
    case M_TRIGGER: sfx_player_note_on(&P, m->slot + SLOTS * m->kit, m->key, m->vel, m->note, 0.0f, &m->patch); break;   /* (SFX: beside the song) */
    case M_RELEASE: sfx_player_note_off(&P, m->key); break;
    case M_UPDATE:  sfx_player_set_patch(&P, m->slot + SLOTS * m->kit, &m->patch); break;   /* a knob turned: also for what sounds now */
    case M_START:   sfx_player_start(&P, m->slot); break;
    case M_STOP:    sfx_player_stop(&P); break;
    case M_CUE:     sfx_player_cue(&P, m->slot); break;
    case M_LOOP:    sfx_player_set_loop(&P, m->slot); break;                        /* (the controls never write P.loop) */
    case M_QUIET:   for (int i = 0; i < SFX_PLAYER_VOICES; i++) sfx_voice_release(&P.voice[i]); break;   /* every key let go */
    }
  }
  VIBE_RELEASE(); qr = k;

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
  sfx_player_render(&P, mixl, mixr, frames, host ? tp->beat * 4.0 : -1.0, host ? (double)tp->bpm / 60.0 * 4.0 / (double)SR : 0.0);
  uint32_t music = 0, effects = 0;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) if (P.voice[i].active) { if (P.voice[i].slot < SLOTS) music |= 1u << P.voice[i].slot; else effects |= 1u << (P.voice[i].slot - SLOTS); }
  play_k[0] = music; play_k[1] = effects;
  seq_step = P.running && P.step >= 0 ? (uint32_t)P.step : NO_STEP; seq_pattern = (uint32_t)P.pattern;
  seq_pos = P.running && P.step >= 0 ? (uint32_t)P.div << 28 | (uint32_t)(P.step & 0xFF) << 20 | (uint32_t)(vd_clamp(P.frac, 0.0f, 1.0f) * 1048575.0f) : NO_STEP;
  seq_arr = P.running && !P.loop && !P.hold && P.arr_pos >= 0 ? (uint32_t)P.arr_pos : NO_STEP;

  int at = P.running && P.step >= 0 ? P.step / (P.div ? P.div : 1) : -1;   /* (in sixteenths, whatever the pattern's grid) */
  if (at != click_step) {                            /* a new sixteenth: on a beat, the click starts */
    click_step = at;
    if (at >= 0 && at % 4 == 0 && metro && rec_on) { click_env = at == 0 ? 0.30f : 0.20f; click_f = (at == 0 ? 2200.0f : 1500.0f) / SR; click_ph = 0.0f; }
  }
  float vol = vibe_param_get(P_VOLUME), vc = vd_coef(0.01f, SR), cc = vd_coef(0.012f, SR);
  for (int n = 0; n < frames; n++) {
    vol_s = vol + (vol_s - vol) * vc;
    float l = vd_tanh(mixl[n] * vol_s * 1.25f), r = vd_tanh(mixr[n] * vol_s * 1.25f), o = (l + r) * 0.5f;
    if (click_env > 0.0005f) {
      float c = vd_sin(click_ph) * click_env;
      click_ph += click_f; if (click_ph >= 1.0f) click_ph -= 1.0f;
      click_env *= cc;
      l = vd_clamp(l + c, -1.0f, 1.0f); r = vd_clamp(r + c, -1.0f, 1.0f);   /* (the click on top of a full mix stays under full scale) */
    }
    out_l[n] = l; out_r[n] = r;
    if (o < sc_lo) sc_lo = o;
    if (o > sc_hi) sc_hi = o;
    if (++sc_n >= 48) { uint32_t s = scope_w; scope_lo[s % SCOPE] = sc_lo; scope_hi[s % SCOPE] = sc_hi; scope_w = s + 1; sc_lo = sc_hi = o; sc_n = 0; }
  }
}

/* ---- sounds and the song: edit, keep, send ---------------------------------------------------------------------------- */
/* a toast stays 2.25 s of the audio's clock, whatever draws it and how often (the device, the stage, both) */
static void say(const char *t) { toast = t; toast_t = vibe_frames(); toast_live = 1; }
static int toast_on(void) { return toast_live && vibe_frames() - toast_t < (uint64_t)(2.25f * SR); }
static void view_sync(void);                         /* (a song came: the AUTO pages follow – below) */
static void changed(int slot) { send(M_UPDATE, slot, 0, 0, 0); }
static void keep_undo(int slot) { undo[slot] = patches[slot]; undo_preset[slot] = pad_preset[slot]; has_undo[slot] = 1; }
static sfx_pattern *pat(void) { return &SONG.pat[edit_pat]; }
static const char *name_of(int k, int slot) { int n = pad_preset_k[k][slot]; return n >= 0 && n < PRESET_COUNT ? PRESETS[n].name : ENGINE_NAME[kits[k][slot].engine]; }
static const char *pad_name(int slot) { return name_of(kit, slot); }
static void kit_set(int k) { if (k == kit) return; kit = k; for (int i = 0; i < SLOTS; i++) led_cache[i] = 0xFFFFFFFFu; }

/* one step back for the song (F2). A run of edits, or a take, is one step however many writes follow. */
/* While a take runs (REC lit, playing) whatever changes the notes belongs to the take: a knob turned or a key pressed in
   between does not cut it in two. A run of edits ends with the page or the pattern (set_page, pattern_pick). */
static void undo_snap(const char *what, int whole) { undo_song = SONG; undo_has = 1; undo_redo = 0; undo_run = 0; undo_what = what; undo_whole = whole; undo_pat = -1; }
static void undo_open(int run)                       /* 1 = edits, 2 = a take, 3 = bars of the arrangement */
{
  static const char *const WHAT[4] = { "", "EDITS", "TAKE", "BARS" };
  if (rec && running) run = 2;
  if (undo_run != run) { undo_snap(WHAT[run], 0); undo_run = run; }
}
static void undo_mark(const char *what, int whole) { if (!whole && rec && running) undo_open(2); else undo_snap(what, whole); }
static void undo_close(void) { if (undo_run != 2) undo_run = 0; }   /* the next edit is a new step (a take ends with PLAY, STOP or REC) */
static void pattern_pick(int p);
static void swap_bytes(void *a, void *b, int n) { uint8_t *x = (uint8_t *)a, *y = (uint8_t *)b; for (int i = 0; i < n; i++) { uint8_t t = x[i]; x[i] = y[i]; y[i] = t; } }
static void song_undo(void)                          /* back – and, pressed again, forth. The notes only (patterns with their grids, lanes, arrangement), unless a whole song was replaced */
{
  static char t[24];
  if (!undo_has) { say("NOTHING TO UNDO"); return; }
  if (undo_whole) swap_bytes(&SONG, &undo_song, (int)sizeof SONG);
  else {
    swap_bytes(SONG.pat, undo_song.pat, (int)sizeof SONG.pat); swap_bytes(SONG.lane, undo_song.lane, (int)sizeof SONG.lane);
    swap_bytes(&SONG.arr_len, &undo_song.arr_len, 1); swap_bytes(SONG.arr, undo_song.arr, (int)sizeof SONG.arr);
  }
  view_sync(); undo_run = 0; undo_redo = !undo_redo; erase_at = -1;      /* (the cursor follows a grid that came back: cur_sync, with the next poll) */
  if (undo_pat >= 0) { int was = queued >= 0 ? queued : edit_pat; pattern_pick(undo_pat); undo_run = 0; undo_pat = was; }   /* COPY: back on the pattern it was made from (and forth) */
  strcpy(t, undo_redo ? "UNDO: " : "REDO: "); strcpy(t + 6, undo_what); say(t);
}

static int confirm(const char *prompt)               /* a key that asks twice: the second press counts while its question is on the display */
{
  if (toast == prompt && toast_on()) { toast_live = 0; return 1; }
  say(prompt); return 0;
}

static int pattern_holds(int k)                      /* a step or an automation point anywhere in it */
{
  const sfx_pattern *p = &SONG.pat[k];
  for (int i = 0; i < SFX_STEPS; i++) { if (p->on[i]) return 1; for (int l = 0; l < SFX_LANES; l++) if (p->aut[l][i]) return 1; }
  return 0;
}

static void preset_load(int slot, int k)
{
  patches[slot].version = SFX_PATCH_VERSION; patches[slot].engine = PRESETS[k].engine;
  for (int i = 0; i < SFX_P_COUNT; i++) patches[slot].p[i] = PRESETS[k].p[i];
  pad_preset[slot] = (int8_t)k;
}

static void step_set(sfx_pattern *p, int step, int slot, int note) { p->on[step] |= (uint16_t)(1u << slot); p->note[slot][step] = (uint8_t)note; }

/* a song to start from, our own, played by the MUSIC kit: funk in E minor the way a 16-bit console's FM chip played it.
   Drums, an FM bass that jumps octaves on the off-beats, FM keys with the tune, brass stabs, bells, strings, a choir.
   Six patterns – two of the verse, two of the chorus, a breakdown, a turnaround – arranged over 16 bars. Only the kick
   plays at intensity 0; the game's intensity brings the band in. */
static void demo_song(void)
{
  enum { SNARE = 1, KICK = 2, BRASS = 5, BASS = 6, KEYS = 7, HAT = 12, BELLS = 13, CHOIR = 14, PAD = 15, N = 6 };
  static const uint8_t BASS_N[N][SFX_BAR] = {
    { 28,  0,  0, 28,  0, 40,  0, 28,   31,  0, 33,  0,  0, 35, 38,  0 },      /* Em */
    { 28,  0,  0, 28,  0, 40,  0, 28,   33,  0, 33, 45,  0, 31, 35,  0 },      /* Em, A */
    { 36,  0,  0, 36,  0, 48,  0, 36,   38,  0,  0, 38,  0, 50, 38,  0 },      /* C, D */
    { 36,  0,  0, 36,  0, 48,  0, 36,   38,  0, 38,  0, 40,  0, 40, 35 },      /* C, D, back to E */
    { 33,  0,  0,  0, 33,  0,  0, 45,   35,  0,  0,  0, 35,  0, 47,  0 },      /* Am, Bm: the breakdown */
    { 28,  0, 40, 28,  0, 40,  0, 31,   33,  0, 45, 33, 35,  0, 38, 40 } };    /* the turnaround */
  static const uint8_t KEYS_N[N][SFX_BAR] = {
    {  0,  0, 64, 67,  0,  0, 69,  0,    0, 67,  0, 64,  0,  0, 62,  0 },
    { 71,  0,  0, 69,  0,  0, 67,  0,   64,  0, 67,  0, 69,  0, 71, 74 },
    { 67,  0,  0, 64,  0, 67,  0,  0,   69,  0,  0, 66,  0, 69,  0, 71 },
    { 72,  0, 71,  0, 67,  0, 64,  0,   66,  0, 69,  0, 71,  0, 74,  0 },
    {  0,  0,  0,  0, 64,  0,  0, 60,    0,  0,  0,  0, 66,  0, 62,  0 },
    { 64,  0, 67,  0, 69,  0, 71,  0,   74,  0, 71,  0, 69, 67, 64,  0 } };
  static const uint8_t PAD_N[N][2] = { { 52, 50 }, { 55, 57 }, { 60, 62 }, { 60, 62 }, { 57, 59 }, { 52, 47 } };
  static const uint16_t KICKS[N] = { 0x1191, 0x1191, 0x1511, 0x1511, 0x0101, 0x5111 }, SNARES[N] = { 0x1010, 0x1410, 0x9010, 0x9090, 0x1000, 0xF010 },
                        HATS[N] = { 0xD555, 0xD555, 0xFFFF, 0xFFFF, 0x4444, 0xF555 };                    /* a bit a step */
  static const uint8_t STABS[][4] = {                 /* the brass: pattern, step, note, steps it holds */
    { 1, 14, 71, 2 },
    { 2, 3, 67, 1 }, { 2, 6, 67, 2 }, { 2, 11, 69, 1 }, { 2, 14, 69, 2 },
    { 3, 0, 72, 3 }, { 3, 8, 74, 2 }, { 3, 12, 76, 3 },
    { 5, 0, 64, 1 }, { 5, 3, 64, 1 }, { 5, 6, 67, 2 }, { 5, 12, 67, 1 }, { 5, 13, 69, 1 }, { 5, 14, 71, 1 }, { 5, 15, 74, 1 } };
  static const uint8_t CHIMES[][3] = { { 2, 2, 76 }, { 2, 10, 78 }, { 3, 2, 74 }, { 3, 10, 78 }, { 4, 0, 76 }, { 4, 6, 79 }, { 4, 8, 74 }, { 4, 14, 78 } };   /* the bells: pattern, step, note */
  static const uint8_t BARS[16] = { 0, 1, 0, 1,  2, 3, 2, 3,  4, 4, 0, 1,  2, 3, 2, 5 };
  sfx_song_init(&SONG);
  for (int b = 0; b < N; b++) {
    sfx_pattern *p = &SONG.pat[b];
    for (int i = 0; i < SFX_BAR; i++) {
      if ((KICKS[b] >> i) & 1) { step_set(p, i, KICK, 0); p->vel[KICK][i] = (uint8_t)(i % 4 ? 3 : 5); }              /* the pushes between the beats: softer */
      if ((SNARES[b] >> i) & 1) { step_set(p, i, SNARE, 0); p->vel[SNARE][i] = (uint8_t)(i % 8 == 4 ? 6 : b == 5 && i > 12 ? i - 9 : 2); }   /* 2 and 4, ghosts, a fill that grows */
      if ((HATS[b] >> i) & 1) { step_set(p, i, HAT, 0); p->vel[HAT][i] = (uint8_t)(i & 1 ? 2 : i % 4 ? 5 : 3); }     /* the off-beat leads */
      if (BASS_N[b][i]) { step_set(p, i, BASS, BASS_N[b][i]); p->vel[BASS][i] = (uint8_t)(BASS_N[b][i] >= 40 ? 7 : 5); }   /* the octave pops */
      if (KEYS_N[b][i]) { step_set(p, i, KEYS, KEYS_N[b][i]); p->vel[KEYS][i] = (uint8_t)(i & 1 ? 4 : 6); }
    }
    step_set(p, 0, PAD, PAD_N[b][0]); step_set(p, 8, PAD, PAD_N[b][1]);     /* strings: two long notes a bar, each let go before the next */
    p->vel[PAD][0] = p->vel[PAD][8] = 4; p->len[PAD][0] = p->len[PAD][8] = 7;
  }
  for (unsigned k = 0; k < sizeof STABS / sizeof STABS[0]; k++) { step_set(&SONG.pat[STABS[k][0]], STABS[k][1], BRASS, STABS[k][2]); SONG.pat[STABS[k][0]].vel[BRASS][STABS[k][1]] = 6; SONG.pat[STABS[k][0]].len[BRASS][STABS[k][1]] = STABS[k][3]; }
  for (unsigned k = 0; k < sizeof CHIMES / sizeof CHIMES[0]; k++) { step_set(&SONG.pat[CHIMES[k][0]], CHIMES[k][1], BELLS, CHIMES[k][2]); SONG.pat[CHIMES[k][0]].vel[BELLS][CHIMES[k][1]] = 4; }
  step_set(&SONG.pat[4], 0, CHOIR, 64); SONG.pat[4].vel[CHOIR][0] = 4; SONG.pat[4].len[CHOIR][0] = 15;                     /* the breakdown: a choir holds through it */
  SONG.chain = N; SONG.swing = 20;
  for (int i = 0; i < 16; i++) SONG.arr[i] = BARS[i];
  SONG.arr_len = 16;
  SONG.lane[0].track = BASS + 1; SONG.lane[0].to = SFX_AT_PATCH + SFX_P_FILTER_CUTOFF;   /* automation: the bass opens through the turnaround … */
  SONG.lane[1].track = PAD + 1; SONG.lane[1].to = SFX_AT_VOL;                            /* … and the strings swell through the breakdown */
  for (int i = 0; i < SFX_BAR; i++) { SONG.pat[5].aut[0][i] = (uint8_t)(62 + i * 3); if (i % 2 == 0) SONG.pat[4].aut[1][i] = (uint8_t)(19 + i * 3); }
  SONG.pan[HAT] = 84; SONG.pan[KEYS] = 76; SONG.pan[BRASS] = 48; SONG.pan[BELLS] = 36; SONG.pan[CHOIR] = 70;
  SONG.vol[KICK] = 68; SONG.vol[SNARE] = 100; SONG.vol[HAT] = 50; SONG.vol[BASS] = 52; SONG.vol[KEYS] = 24; SONG.vol[BRASS] = 54; SONG.vol[PAD] = 46; SONG.vol[BELLS] = 56; SONG.vol[CHOIR] = 52;   /* measured: tests/mix-report.mjs */
  SONG.level[HAT] = 20; SONG.level[BASS] = 35; SONG.level[SNARE] = 50; SONG.level[PAD] = 60; SONG.level[CHOIR] = 60; SONG.level[KEYS] = 72; SONG.level[BRASS] = 90; SONG.level[BELLS] = 100;
}

static void factory(void)
{
  int was = kit;
  for (int s = 0; s < SLOTS; s++) { kit = KIT_SFX; preset_load(s, s); kit = KIT_MUSIC; preset_load(s, SFX_PRESETS + s); }
  kit = was;
  demo_song();
}

#define BANK_BYTES (2 * SLOTS * (SFX_PATCH_BYTES + 1))
/* What the device stores. kits.bin: both kits. song6.bin: the packed song (format 6) and two bytes more - a check of the
   song.bin that lay beside it when it was written (0: there was none). song.bin is the file of the firmwares up to 1.5.0
   (format 5): it is still read and never written again, so a device put back to an older firmware finds its song. And
   should that older firmware save a song there and the device come forward again: its song.bin no longer fits the
   check, so it is the later one and is loaded over song6.bin - the display says OLDER FIRMWARE'S SONG then, and the
   song of song6.bin is one F2 away (until the next SAVE, which keeps what is there). */
static uint8_t store_buf[SFX_SONG_MAX + 2];
static int store_check(const uint8_t *b, int n) { uint32_t h = 1; for (int i = 0; i < n; i++) h = (h * 31 + b[i]) % 16381; return 1 + (int)h; }

static int bank_save(void)                           /* 0 = kept, -1 = the store did not take it */
{
  static uint8_t b[BANK_BYTES];
  for (int k = 0; k < 2; k++) for (int i = 0; i < SLOTS; i++) {
    sfx_patch_pack(&kits[k][i], b + (k * SLOTS + i) * SFX_PATCH_BYTES);
    b[2 * SLOTS * SFX_PATCH_BYTES + k * SLOTS + i] = (uint8_t)(pad_preset_k[k][i] + 1);
  }
  int failed = vibe_store_write("kits.bin", b, sizeof b) != 0;
  int old = vibe_store_size("song.bin"), c = 0;
  if (old >= 1 && old <= SFX_SONG_BYTES_V5 && vibe_store_read("song.bin", store_buf, old) == old) c = store_check(store_buf, old);
  int n = sfx_song_pack(&SONG, store_buf);
  store_buf[n] = (uint8_t)(c & 0x7F); store_buf[n + 1] = (uint8_t)(c >> 7);
  if (vibe_store_write("song6.bin", store_buf, n + 2)) failed = 1;
  return failed ? -1 : 0;
}
static void save_say(void) { say(bank_save() ? "NOT SAVED: STORE FULL" : "SAVED"); }

static void bank_load(void)
{
  static uint8_t b[BANK_BYTES];
  if (vibe_store_read("kits.bin", b, sizeof b) == (int)sizeof b)
    for (int k = 0; k < 2; k++) for (int i = 0; i < SLOTS; i++)       /* a broken one keeps its factory sound */
      if (!sfx_patch_unpack(&kits[k][i], b + (k * SLOTS + i) * SFX_PATCH_BYTES)) { int n = b[2 * SLOTS * SFX_PATCH_BYTES + k * SLOTS + i]; pad_preset_k[k][i] = (int8_t)(n >= 1 && n <= PRESET_COUNT ? n - 1 : -1); }
  int n6 = vibe_store_size("song6.bin"), c = -1;       /* c: the check song6.bin kept, -1 while none was taken */
  if (n6 >= SFX_SONG_MIN + 2 && n6 <= SFX_SONG_MAX + 2 && vibe_store_read("song6.bin", store_buf, n6) == n6 && !sfx_song_unpack(&SONG, store_buf, n6 - 2))
    c = store_buf[n6 - 2] | store_buf[n6 - 1] << 7;
  int n5 = vibe_store_size("song.bin");
  if (n5 >= 1 && n5 <= SFX_SONG_BYTES_V5 && vibe_store_read("song.bin", store_buf, n5) == n5 && (c < 0 || store_check(store_buf, n5) != c)) {
    if (c >= 0) undo_song = SONG;                      /* (the song of song6.bin: one F2 away) */
    if (!sfx_song_unpack(&SONG, store_buf, n5) && c >= 0) {   /* (one that does not unpack leaves the song as it is) */
      undo_has = 1; undo_whole = 1; undo_redo = 0; undo_run = 0; undo_pat = -1; undo_what = "OLDER SONG";
      say("OLDER FIRMWARE'S SONG");                    /* both files held a song, and the one of the firmware before is the later: it is said, and F2 on SEQ brings the other */
    }
  }
}

static int same(const uint8_t *a, const uint8_t *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }

static int sysex_dump_k(int k, int slot)
{
  uint8_t b[7 + SFX_PATCH_BYTES] = { 0xF0, 0x7D, 0x56, 0x42, SX_DUMP, (uint8_t)slot };
  sfx_patch_pack(&kits[k][slot], b + 6);
  b[6 + SFX_PATCH_BYTES] = 0xF7;
  if (vibe_midi_send_sysex(b, sizeof b)) return -1;
  for (int i = 0; i < SFX_PATCH_BYTES; i++) sent_dump[slot][i] = b[6 + i];
  sent_dump_any |= 1u << slot; sent_dump_t = vibe_frames();
  return 0;
}
static int sysex_dump(int slot) { return sysex_dump_k(kit, slot); }

/* the 16 sounds, then the song in pieces. The host takes one SysEx a block, so this is called again and again (from
   every draw and every event) until all are out. */
static void export_pump(void)
{
  static uint8_t b[11 + SONG_PIECE];
  for (int guard = 0; guard < 2 && export_at >= 0; guard++) {
    if (export_at < SLOTS) { if (sysex_dump_k(KIT_MUSIC, export_at)) return; }     /* (the song's sounds: the MUSIC kit) */
    else {
      int k = export_at - SLOTS;
      if (k == 0) { song_tx_n = sfx_song_pack(&SONG, song_tx); song_tx_K = (song_tx_n + SONG_PIECE - 1) / SONG_PIECE; }
      int n = k == song_tx_K - 1 ? song_tx_n - k * SONG_PIECE : SONG_PIECE;
      b[0] = 0xF0; b[1] = 0x7D; b[2] = 0x56; b[3] = 0x42; b[4] = SX_SONG; b[5] = (uint8_t)(k & 0x7F); b[6] = (uint8_t)(k >> 7);
      b[7] = song_tx[0]; b[8] = (uint8_t)(song_tx_K & 0x7F); b[9] = (uint8_t)(song_tx_K >> 7);
      for (int i = 0; i < n; i++) b[10 + i] = song_tx[k * SONG_PIECE + i];
      b[10 + n] = 0xF7;
      if (vibe_midi_send_sysex(b, 11 + n)) return;
      sent_song_any = 1; sent_song_t = vibe_frames();
    }
    if (++export_at > SLOTS && export_at >= SLOTS + song_tx_K) { export_at = -1; say("SONG SENT"); }
  }
}

/* The answer to SX_REQ_IDENT. The host takes one SysEx a block: an answer that does not fit now (a second request in
   the same block, a song going out) waits, and goes with the next draw or event. */
static int ident_due;
static void ident_pump(void)
{
  static const uint8_t b[6 + 18] = { 0xF0, 0x7D, 0x56, 0x42, SX_IDENT, ID_HARDWARE, FW_MAJOR, FW_MINOR, FW_PATCH, ID_PROFILE,
    SFX_PATCH_VERSION, SFX_SONG_VERSION, SLOTS, 2 /* kits */, SFX_PATTERNS, SFX_STEPS /* the most steps a pattern holds */, SFX_LANES, SFX_ARR_MAX, SFX_PLAYER_VOICES,
    SFX_ENGINE_COUNT, SFX_WT_TABLES, SFX_P_COUNT, ID_FLAGS, 0xF7 };
  while (ident_due > 0 && !vibe_midi_send_sysex(b, sizeof b)) ident_due--;
}

/* A pad's dump that was asked for (SX_REQUEST, or EXPORT on the device) waits in the same way: without this an identity
   that holds the host's one SysEx of the block would make the dump vanish. One bit for each pad; the dump is made of
   the kit that is selected when it goes. While the sounds and the song go out it waits for them to end, so that a
   pad of the selected kit does not land among the song's sounds. */
static uint32_t dump_due;
static void dump_pump(void)
{
  if (export_at >= 0) return;
  for (int i = 0; i < SLOTS && dump_due; i++)
    if (dump_due >> i & 1) { if (sysex_dump(i)) return; dump_due &= ~(1u << i); }
}

/* all that waits to go out, in this order; called from every draw, every event and every request */
static void out_pump(void)
{
  if (ident_due) ident_pump();
  if (export_at >= 0) export_pump();
  if (dump_due) dump_pump();
}

/* A song from the game, piece by piece. Every piece is put where it belongs, and the song is taken when all are here. So
   the pieces may come in any order: a MIDI path can swap two neighbours (a browser's Web MIDI output does, now and
   then). And more than once: a host can deliver everything twice (the emulator does), and a piece that is here already,
   byte for byte, is left alone. A piece that is here already with OTHER bytes belongs to another song: that begins a new
   transfer - as does a piece that names another count of pieces, another format, or has the other shape. A transfer
   that broke off is forgotten after two seconds without a piece.
   After a song is taken its pieces stay here, so that its own late twins do not begin a transfer. What comes then is
   such a twin - or a piece of another song of as many pieces, which may differ from the one taken in one late piece only
   (a tempo is in the first, a step of pattern H in the last). So from then on the pieces are counted again: once every
   piece has come again and one of them had other bytes, that is the next song, and it is taken. A twin is told by a
   count: the same bytes in a piece that has come fewer times than the others (a host that delivers twice still owes it).
   Two shapes (b[6] tells them apart: 39 is the old one's count of pieces, the new one has the piece number's high bits
   there, 0 or 1):  old  F0 7D 56 42 05 <k> 39 <200 bytes, the last piece 99> F7            - a format-5 song, 7699 bytes
                    new  F0 7D 56 42 05 <k lo> <k hi> <format> <K lo> <K hi> <1..240> F7    - any length up to 160 pieces
   (format: the song's first byte, in every piece - a first piece that begins with another byte is no piece) */
static int rx_seen(int k) { return (int)(song_rx_seen[k >> 5] >> (k & 31) & 1); }
static int rx_again(int k) { return (int)(song_rx_again[k >> 5] >> (k & 31) & 1); }
static void rx_again_clear(void) { for (unsigned i = 0; i < sizeof song_rx_again / sizeof song_rx_again[0]; i++) song_rx_again[i] = 0; for (int i = 0; i < SONG_PIECES_MAX; i++) song_rx_n2[i] = 0; song_rx_other = 0; }
static void rx_clear(void) { for (unsigned i = 0; i < sizeof song_rx_seen / sizeof song_rx_seen[0]; i++) song_rx_seen[i] = 0; for (int i = 0; i < SONG_PIECES_MAX; i++) song_rx_n[i] = 0; song_rx_done = 0; rx_again_clear(); }
static void rx_whole(int take)                       /* all its pieces are here (take 0: the song that is here already, sent once more) */
{
  song_rx_done = 1; song_rx_most = 0;
  for (int i = 0; i < song_rx_K; i++) if (song_rx_n[i] > song_rx_most) song_rx_most = song_rx_n[i];
  rx_again_clear();
  if (!take) return;
  undo_mark("LOAD", 1);
  say(!sfx_song_unpack(&SONG, song_rx, (song_rx_K - 1) * song_rx_size + song_rx_last) ? "SONG LOADED" : song_rx[0] > SFX_SONG_VERSION ? "NEWER SONG: UPDATE" : "BAD SONG");
  view_sync();
}
static void song_piece(const uint8_t *b, int n, uint64_t now)
{
  int k, K, size, len, format; const uint8_t *in;
  if (b[6] == SONG_OLD_PIECES) {
    k = b[5]; K = SONG_OLD_PIECES; size = SONG_OLD_PIECE; len = n - 8; in = b + 7; format = 0;
    if (k >= K || len != (k == K - 1 ? SFX_SONG_BYTES_V5 - k * size : size)) return;
  } else if (b[6] <= 1 && n >= 12) {
    k = b[5] | b[6] << 7; K = b[8] | b[9] << 7; size = SONG_PIECE; len = n - 11; in = b + 10; format = b[7];
    if (b[9] > 1 || K < 1 || K > SONG_PIECES_MAX || k >= K || len < 1 || len > size || (k < K - 1 && len != size) || format < 1) return;
    if (format > SFX_SONG_VERSION) { if (k == 0) say("NEWER SONG: UPDATE"); return; }   /* a later firmware's song: nothing of it is collected */
    if (k == 0 && in[0] != format) return;             /* (the format its pieces name is not the song's own) */
    /* our own echo: the piece that just went out, come back while the song is going out or just after */
    if (sent_song_any && (export_at >= SLOTS || now - sent_song_t < ECHO_FRAMES) && K == song_tx_K
        && len == (k == K - 1 ? song_tx_n - k * size : size) && same(in, song_tx + k * size, len)) return;
  } else return;
  uint8_t *at = song_rx + k * size;
  if (now - song_rx_t > (uint64_t)SR * 2 || K != song_rx_K || size != song_rx_size || format != song_rx_format) rx_clear();
  song_rx_t = now;
  int here = rx_seen(k) && len == (k == K - 1 ? song_rx_last : size) && same(in, at, len);
  if (song_rx_done) {                                /* the song before was taken: its twin, or a piece of the next */
    if (here && !song_rx_other && song_rx_n[k] < song_rx_most) { song_rx_n[k]++; return; }   /* the twin this piece still owed */
    if (!here) {
      if (rx_again(k)) rx_again_clear();             /* (a second piece with other bytes in the same place: yet another song begins) */
      for (int i = 0; i < len; i++) at[i] = in[i];
      if (k == K - 1) song_rx_last = len;
      song_rx_other = 1;
    }
    song_rx_again[k >> 5] |= 1u << (k & 31);
    if (song_rx_n2[k] < 255) song_rx_n2[k]++;
    for (int i = 0; i < K; i++) if (!rx_again(i)) return;
    int other = song_rx_other;
    for (int i = 0; i < K; i++) song_rx_n[i] = song_rx_n2[i];
    rx_whole(other);                                 /* (every piece again and all the same: the same song, sent once more - nothing to take) */
    return;
  }
  if (rx_seen(k)) { if (here) { if (song_rx_n[k] < 255) song_rx_n[k]++; return; } rx_clear(); }
  song_rx_K = K; song_rx_size = size; song_rx_format = format;
  for (int i = 0; i < len; i++) at[i] = in[i];
  song_rx_seen[k >> 5] |= 1u << (k & 31); song_rx_n[k] = 1;
  if (k == K - 1) song_rx_last = len;
  for (int i = 0; i < K; i++) if (!rx_seen(i)) return;
  rx_whole(1);
}

static void sysex_in(const uint8_t *b, int n)
{
  if (n < 6 || b[0] != 0xF0 || b[1] != 0x7D || b[2] != 0x56 || b[3] != 0x42 || b[n - 1] != 0xF7) return;
  if (b[4] == SX_IDENT || (b[4] == SX_REQ_IDENT && n != 6)) return;   /* an identity (our own, echoed) is nobody's request; a request is 6 bytes */
  linked = 1;
  if (b[4] == SX_REQ_IDENT) { if (ident_due < 4) ident_due++; out_pump(); return; }
  uint64_t now = vibe_frames();
  /* Our own echo. A virtual MIDI cable's port is an input as well as an output, and a host that listens on every input
     (the emulator does) hands back what was just sent. A dump that comes in while the sounds and the song are going
     out, or within a tenth of a second after a dump, and that is byte for byte what went out for that slot, is that:
     taking it would put the song's sounds into the kit that is selected. Another patch at that moment is taken. */
  if (b[4] == SX_DUMP && n == 7 + SFX_PATCH_BYTES && b[5] < SLOTS && (sent_dump_any >> b[5] & 1)
      && (export_at >= 0 || now - sent_dump_t < ECHO_FRAMES) && same(b + 6, sent_dump[b[5]], SFX_PATCH_BYTES)) return;
  if (b[4] == SX_LOAD && n == 7 + SFX_PATCH_BYTES && b[5] >= SLOTS && b[5] < 2 * SLOTS) {   /* a song's sound: the MUSIC kit's pad */
    sfx_patch p; int slot = b[5] - SLOTS, was = kit;
    if (sfx_patch_unpack(&p, b + 6)) { say("BAD PATCH"); return; }
    kit = KIT_MUSIC; patches[slot] = p; pad_preset[slot] = -1; changed(slot); kit = was;
    say("SOUND LOADED");
  } else if ((b[4] == SX_LOAD || b[4] == SX_DUMP) && n == 7 + SFX_PATCH_BYTES && b[5] < SLOTS) {
    sfx_patch p;
    if (sfx_patch_unpack(&p, b + 6)) { say("BAD PATCH"); return; }
    keep_undo(b[5]); patches[b[5]] = p; pad_preset[b[5]] = -1; sel = b[5]; changed(sel); say("PATCH LOADED");
  } else if (b[4] == SX_TEXT) {
    int k = 0;
    for (int i = 5; i < n - 1 && k < 24; i++) link_text[k++] = b[i] >= 32 && b[i] < 127 ? (char)b[i] : ' ';
    link_text[k] = 0;
  } else if (b[4] == SX_REQUEST && n == 7 && b[5] < SLOTS) { dump_due |= 1u << b[5]; out_pump(); }
  else if (b[4] == SX_REQ_SONG) { export_at = 0; out_pump(); }
  else if (b[4] == SX_SONG && n > 8) song_piece(b, n, now);
}

/* ---- playing -------------------------------------------------------------------------------------------------------- */
static void record(int slot, int note, float vel)    /* REC while it runs: onto the nearest step of the pattern that plays - of its own grid -, as hard as it was hit */
{
  uint32_t w = seq_pos;                              /* (step, grid and fraction: one word, read once) */
  if (rec && running && kit != KIT_MUSIC) { if (!from_outside) say("MUSIC KIT ONLY: K5 SEQ"); return; }   /* (the song plays the MUSIC kit: the SEQ page has it) */
  if (!rec || w == NO_STEP) return;
  undo_open(2);
  sfx_pattern *p = &SONG.pat[seq_pattern % SFX_PATTERNS];
  int st = pos_step(w), div = pos_div(w), pd = sfx_div(p->grid);
  if (pd != div) { st = st * pd / div; div = pd; }   /* (its grid changed since the audio said where it is) */
  int n = bar_len() * div;
  if (pos_frac(w) > 0.5f) st = (st + 1) % n;         /* past the middle of the step: the next one */
  st %= n;
  step_set(p, st, slot, note);
  p->vel[slot][st] = (uint8_t)(vel > 0.0f ? (int)vd_clamp(vel * 7.99f, 0.0f, 7.0f) : SFX_VEL_DEFAULT);      /* (its length stays) */
}

static void pad(int slot, int down, float vel)
{
  if (!down) { send(M_RELEASE, slot, slot, 0, 0); return; }
  sel = slot;
  send(M_TRIGGER, slot, slot, vel > 0.0f ? vel : 0.8f, 0);
  record(slot, 0, vel);
}

/* LOOP or SONG: what PLAY plays. Only this tells the player. 1 = it changed */
static int mode_set(int loop)
{
  loop = loop != 0;
  if (loop == loop_mode) return 0;
  loop_mode = loop; send(M_LOOP, loop, 0, 0, 0); relabel();
  return 1;
}
static void mode_toggle(void) { mode_set(!loop_mode); say(loop_mode ? "LOOP: PATTERN REPEATS" : "SONG: PLAYS IN ORDER"); }

/* the 16 steps the pads and the tiles show: those round the cursor - while a take runs on SEQ, those that play */
static int view_win(void)
{
  if (page == PG_SEQ && rec && running && follow_win >= 0 && follow_win * PADS < gsteps()) return follow_win;
  return cur_step / PADS;
}
static void follow_end(void)                         /* a take ended: the tiles stay where it was */
{
  if (follow_win >= 0 && follow_win * PADS < gsteps() && cur_step / PADS != follow_win) cur_step = follow_win * PADS;
  follow_win = -1;
}
static void cur_sync(void)                           /* another pattern is shown, or its grid changed: the cursor stays on its sixteenth */
{
  int d = pat_div(edit_pat);
  if (edit_pat == cur_pat && d == cur_div) return;
  cur_step = clampi(cur_step * d / cur_div, 0, SFX_BAR * d - 1);
  held_pad = held_step = -1; cur_pat = edit_pat; cur_div = d;
}

static void seq_play(int on)                         /* (it never changes the mode: the top line said what it will play) */
{
  if (!on) follow_end();
  running = on; undo_run = 0;
  if (!on) { if (queued >= 0) edit_pat = queued; f4_held = 0; queued = -1; }   /* (a pattern chosen during a take, not yet begun: it is the one shown now) */
  if (on) send(M_LOOP, loop_mode, 0, 0, 0);          /* again, should the queue have been full when the mode was set */
  send(on ? M_START : M_STOP, edit_pat, 0, 0, 0);
  vibe_led_button(VB_PLAY, 0x6EE7A8, on ? 1.0f : 0.0f);
  relabel();
}

static void seq_rec(int on)                          /* REC lit: PLAY loops the pattern (F1 may still say SONG: then the take goes along the song), and what is played is laid on top */
{
  if (!on) follow_end();
  rec = on; rec_on = on; undo_run = 0; held_pad = held_step = -1;
  if (!on) f4_held = 0;
  vibe_led_button(VB_REC, 0xFF4040, on ? 1.0f : 0.0f);
  if (on && !tour_on && mode_set(1)) say("REC ON: PATTERN LOOPS");
  relabel();
}

/* a pattern is chosen: at once while it stands, on the next bar line while it runs. While a take runs the display stays
   on the pattern that plays, and follows on the bar line (seq_poll) – so what is played lands in what is shown. */
static void pattern_pick(int p)
{
  p = (p % SFX_PATTERNS + SFX_PATTERNS) % SFX_PATTERNS;
  send(M_CUE, p, 0, 0, 0);
  queued = running && p != (int)seq_pattern ? p : -1;
  if (!(rec && running)) edit_pat = p;
  undo_close();
}
static void pattern_step(int d) { pattern_pick((rec && running && queued >= 0 ? queued : edit_pat) + d); }   /* the one before / after */
static void pattern_home(void) { edit_pat = 0; send(M_CUE, 0, 0, 0, 0); queued = running && seq_pattern != 0 ? 0 : -1; }   /* a new song: its pattern A */

/* from every draw and every event: what the audio did since. The queued pattern has started; the view follows a take;
   F4 held erases the selected sound on every step the bar has passed, in the pattern that plays. */
static void seq_poll(void)
{
  uint32_t w = seq_pos, now = seq_pattern % SFX_PATTERNS;
  cur_sync();
  if (!running || (queued >= 0 && now == (uint32_t)queued)) queued = -1;
  if (rec && running && w != NO_STEP) edit_pat = (int)now;
  if (w != NO_STEP) {
    sfx_pattern *p = &SONG.pat[now];
    int pd = sfx_div(p->grid), st = pos_step(w) * pd / pos_div(w);   /* the step that plays, in its pattern's grid as it is now */
    if (rec && running && page == PG_SEQ) follow_win = st / PADS;
    if (f4_held && rec && running) {
      int len = bar_len() * pd, at = st % len;
      if (erase_pat != (int)now) { erase_pat = (int)now; erase_at = -1; }   /* another pattern began: only what passes in it */
      if (erase_at >= len) erase_at = -1;
      for (int n = 0; n < len && erase_at != at; n++) {
        int i = erase_at < 0 ? at : (erase_at + 1) % len;
        undo_open(2);
        p->on[i] &= (uint16_t)~(1u << sel); p->note[sel][i] = 0; p->vel[sel][i] = SFX_VEL_DEFAULT; p->len[sel][i] = 0;
        erase_at = i;
      }
    }
  }
  cur_sync();
}

static void intensity_set(float v) { vibe_param_set(P_INTENSITY, vd_clamp(v, 0.0f, 1.0f)); }
static void step_toggle(int step, int slot)
{
  undo_open(1);
  pat()->on[step] ^= (uint16_t)(1u << slot); cur_step = step;
  if (!running && (pat()->on[step] >> slot) & 1) {   /* switched on: hear it, at its note (while the song stands still) */
    send(M_TRIGGER, slot, slot, SFX_VEL[pat()->vel[slot][step] & 7], pat()->note[slot][step]); send(M_RELEASE, slot, slot, 0, 0);
  }
}

static void note_turn(int d)                         /* the cursor step's note, along the scale; it switches the step on */
{
  int n = pat()->note[sel][cur_step];
  undo_open(1);
  for (int i = 0; i < (d < 0 ? -d : d); i++) n = sfx_scale_step(n, d, SONG.scale);
  step_set(pat(), cur_step, sel, n);
}

static void vel_turn(int d)                          /* the cursor step: how hard (1..8), how long (AUTO, else steps of the pattern's grid, up to a bar less one) */
{
  uint8_t *a = &pat()->vel[sel][cur_step];
  undo_open(1);
  *a = (uint8_t)clampi((*a & 7) + d, 0, 7);
}
static void len_turn(int d)
{
  uint8_t *a = &pat()->len[sel][cur_step];
  undo_open(1);
  *a = (uint8_t)clampi(*a + d, 0, gsteps() - 1);
}

/* the selected sound's automation lane. What its target is without a point (where a new point starts), and how far it goes */
static int auto_own(int to, int track)
{
  return to >= SFX_AT_MASTER ? SONG.fx[to - SFX_AT_MASTER] : to == SFX_AT_PAN ? SONG.pan[track] : to == SFX_AT_VOL ? SONG.vol[track] : to ? kits[KIT_MUSIC][track].p[to - SFX_AT_PATCH] : 0;
}
static int auto_hi(int to) { return to == SFX_AT_MASTER + SFX_M_DELAY_TIME ? SFX_DELAY_TIMES - 1 : to == SFX_AT_PATCH + SFX_P_TABLE ? SFX_WT_TABLES - 1 : 126; }

/* Any knob may have a lane; a song holds SFX_LANES of them. view_to: the knob each sound's AUTO page shows now. The first
   point of a knob takes a lane, its last one gives it back. */
static uint8_t view_to[SLOTS];
static uint8_t last_to; static int last_sel = -1;     /* the knob turned last (a lane's target), and of which sound */
static int lane_of(int track, int to) { for (int l = 0; l < SFX_LANES; l++) if (SONG.lane[l].track == track + 1 && SONG.lane[l].to == to) return l; return -1; }
static int lanes_used(void) { int n = 0; for (int l = 0; l < SFX_LANES; l++) n += SONG.lane[l].track != 0; return n; }
static int aut_at(int track, int step) { int l = lane_of(track, view_to[track]); return l < 0 ? 0 : pat()->aut[l][step]; }

static void lanes_tidy(void)                         /* a lane without a point anywhere is free again */
{
  for (int l = 0; l < SFX_LANES; l++) {
    int any = 0;
    for (int k = 0; k < SFX_PATTERNS && !any; k++) for (int i = 0; i < SFX_STEPS; i++) if (SONG.pat[k].aut[l][i]) { any = 1; break; }
    if (!any) SONG.lane[l].track = 0;
  }
}

static void aut_put(int track, int step, int v)      /* v: 0 = no point, else value + 1 */
{
  int to = view_to[track], l = lane_of(track, to);
  if (l < 0 && v) {
    for (l = 0; l < SFX_LANES && SONG.lane[l].track; l++) { }
    if (l == SFX_LANES) { say("ALL 24 LANES IN USE"); return; }
    for (int k = 0; k < SFX_PATTERNS; k++) for (int i = 0; i < SFX_STEPS; i++) SONG.pat[k].aut[l][i] = 0;
    SONG.lane[l].to = (uint8_t)to; SONG.lane[l].track = (uint8_t)(track + 1);
  }
  if (l < 0) return;
  pat()->aut[l][step] = (uint8_t)v;
  if (!v) lanes_tidy();
}

static void view_sync(void)                          /* a song came: each sound's page shows its first lane */
{
  for (int t = 0; t < SLOTS; t++) view_to[t] = 0;
  for (int l = SFX_LANES - 1; l >= 0; l--) if (SONG.lane[l].track && SONG.lane[l].track <= SLOTS) view_to[SONG.lane[l].track - 1] = SONG.lane[l].to;
}

static void auto_turn(int d)                         /* the point on the cursor step: made where the knob stands, then moved */
{
  int to = view_to[sel], a = aut_at(sel, cur_step);
  if (!to) { say("TURN TARGET FIRST"); return; }
  undo_open(1);
  aut_put(sel, cur_step, clampi((a ? a - 1 : clampi(auto_own(to, sel), 0, auto_hi(to))) + (a ? d : 0), 0, auto_hi(to)) + 1);
}

static void auto_toggle(int step)
{
  int to = view_to[sel];
  cur_step = step;
  if (!to) { say("TURN TARGET FIRST"); return; }
  undo_open(1);
  aut_put(sel, step, aut_at(sel, step) ? 0 : clampi(auto_own(to, sel), 0, auto_hi(to)) + 1);
}

static void auto_ramp(void)                          /* a straight line from each point to the next */
{
  int last = -1, n = 0, l = lane_of(sel, view_to[sel]);
  for (int i = 0; l >= 0 && i < gsteps(); i++) n += pat()->aut[l][i] != 0;
  if (n < 2) { say("SET TWO POINTS FIRST"); return; }
  undo_mark("RAMP", 0);
  for (int i = 0; i < gsteps(); i++) {
    if (!pat()->aut[l][i]) continue;
    if (last >= 0) for (int k = last + 1; k < i; k++) pat()->aut[l][k] = (uint8_t)(pat()->aut[l][last] + (pat()->aut[l][i] - pat()->aut[l][last]) * (k - last) / (i - last));
    last = i;
  }
  say("RAMP");
}

static void set_page(int p)
{
  if (p == PG_AUTO && page != PG_AUTO && last_to && last_sel == sel) view_to[sel] = last_to;   /* AUTO opens on the knob turned last */
  follow_end();
  page = p; held_pad = held_step = -1; f4_held = 0; undo_close();
  if (p >= PG_SEQ && p <= PG_MASTER) kit_set(KIT_MUSIC);      /* the song's pages: its own sounds */
  if (!running && !rec && !tour_on) {                 /* where patterns are made PLAY loops one; on SONG it plays the song */
    if (p == PG_SEQ || p == PG_AUTO) mode_set(1);
    else if (p == PG_SONG) mode_set(0);
  }
  relabel();
  vibe_label(VB_K1 + TAB_SEQ, p == PG_SONG ? "SONG" : p == PG_AUTO ? "AUTO" : "SEQ");
  vibe_label(VB_K1 + TAB_FX, p == PG_MASTER ? "MASTER" : p == PG_MIX ? "MIX" : "FX");
  for (int i = 0; i < SLOTS; i++) led_cache[i] = 0xFFFFFFFFu;
}

/* a K key: its page – or, on it already, its next one (SOUND has three, a fourth for a wavetable; SEQ has SONG) */
static void tab_key(int tab)
{
  if (TAB_OF[page] != tab) { set_page(TAB_FIRST[tab]); return; }
  if (tab == TAB_SOUND) { int last = patches[sel].engine == SFX_ENGINE_WAVETABLE ? PG_WAVE : PG_FILTER; set_page(page >= last ? PG_OSC : page + 1); }
  else if (tab == TAB_SEQ) set_page(page == PG_SEQ ? PG_SONG : page == PG_SONG ? PG_AUTO : PG_SEQ);
  else if (tab == TAB_FX) set_page(page == PG_FX ? PG_MIX : page == PG_MIX ? PG_MASTER : PG_FX);
}


/* LEFT / RIGHT: the sound page before or after (WAVE only for a wavetable) – on SEQ, SONG and AUTO the pattern */
/* SEQ and AUTO, SHIFT + LEFT / RIGHT: the 16 steps before / after (a pattern of 32 or 64 steps has two or four such
   windows - fewer when the bar is short: only those with a step that plays) */
static void window_step(int d)
{
  static char t[24];
  int lim = bar_len() * pat_div(edit_pat), wins = (lim + PADS - 1) / PADS, w = cur_step / PADS;   /* the windows that hold a step of the bar */
  if (gsteps() <= PADS) { say("1/16: ALL STEPS SHOWN"); return; }
  if (wins == 1 && w == 0) { strcpy(t, "BAR ENDS AT STEP "); vg_itoa(lim, t + 17); say(t); return; }   /* (a short bar: all of it is shown) */
  w = wrap(w < wins ? w : d > 0 ? wins - 1 : 0, d, wins);        /* (the cursor beyond the bar: to its first or its last window) */
  cur_step = clampi(w * PADS + cur_step % PADS, 0, lim - 1); held_pad = held_step = -1;
  strcpy(t, "STEPS "); vg_itoa(w * PADS + 1, t + 6); strcpy(t + strlen(t), "-"); vg_itoa(w * PADS + PADS, t + strlen(t));
  say(t);
}

static void arrow(int d)
{
  if (shift && (page == PG_SEQ || page == PG_AUTO)) { window_step(d); return; }
  if (song_keys()) { pattern_step(d); return; }
  if (page >= PG_SEQ) return;
  int to = page;
  do to = wrap(to, d, PG_SEQ); while (to == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE);
  set_page(to);
}

/* one of the four values of a sound page, by steps (an encoder, the wheel) or to a place (dragged on the stage) */
/* MIX: the selected sound's fader and pan, the drive, the output. MASTER: the delay's time and feedback, the reverb's
   size and damping. The byte behind value i (0: the output volume, a host parameter), and how far it goes. */
static uint8_t *mix_value(int pg, int i, int *hi)
{
  *hi = 127;
  if (pg == PG_MIX) return i == 0 ? &SONG.vol[sel] : i == 1 ? &SONG.pan[sel] : i == 2 ? &SONG.fx[SFX_M_DRIVE] : 0;
  if (i == 0) *hi = SFX_DELAY_TIMES - 1;
  return &SONG.fx[i == 0 ? SFX_M_DELAY_TIME : i == 1 ? SFX_M_DELAY_FB : i == 2 ? SFX_M_REVERB_SIZE : SFX_M_REVERB_DAMP];
}

static void param_nudge(int pg, int i, int d)
{
  if (pg == PG_AUTO) {
    if (i == 0) cur_step = clampi(cur_step + d, 0, gsteps() - 1);
    if (i == 1) auto_turn(d);
    if (i == 2) view_to[sel] = (uint8_t)wrap(view_to[sel], d > 0 ? 1 : -1, SFX_AT_COUNT);
    return;
  }
  if (pg == PG_MIX || pg == PG_MASTER) {
    int hi; uint8_t *v = i >= 0 && i < 4 ? mix_value(pg, i, &hi) : 0;
    if (v) *v = (uint8_t)clampi(*v + d, 0, hi);
    else if (i == 3) vibe_param_set(P_VOLUME, vd_clamp(vibe_param_get(P_VOLUME) + (float)d * 0.02f, 0.0f, 1.0f));
    if (v) { last_sel = sel; last_to = (uint8_t)(pg == PG_MASTER ? SFX_AT_MASTER + i : i == 0 ? SFX_AT_VOL : i == 1 ? SFX_AT_PAN : SFX_AT_MASTER + SFX_M_DRIVE); }
    return;
  }
  sfx_patch *p = &patches[sel];
  int k = pg < PG_SEQ && i >= 0 && i < 4 ? PAGE_PARAMS[pg][i] : -1;
  if (k >= 0 && kit == KIT_MUSIC) { last_sel = sel; last_to = (uint8_t)(SFX_AT_PATCH + k); }
  if (k == ENGINE_KNOB) { p->engine = (uint8_t)wrap(p->engine, d, SFX_ENGINE_COUNT); pad_preset[sel] = -1; }
  else if (k == SFX_P_TABLE) p->p[k] = (uint8_t)wrap(p->p[k] % SFX_WT_TABLES, d, SFX_WT_TABLES);
  else if (k >= 0) p->p[k] = (uint8_t)clampi(p->p[k] + d, 0, 127);
  else return;
  changed(sel);
}

static void param_put(int pg, int i, float u)
{
  if (pg == PG_AUTO) {
    int to = view_to[sel];
    if (i == 0) cur_step = clampi((int)(u * (float)gsteps()), 0, gsteps() - 1);
    if (i == 1 && to) { undo_open(1); aut_put(sel, cur_step, clampi((int)(u * (float)auto_hi(to) + 0.5f), 0, auto_hi(to)) + 1); }
    if (i == 2) view_to[sel] = (uint8_t)clampi((int)(u * (float)SFX_AT_COUNT), 0, SFX_AT_COUNT - 1);
    return;
  }
  if (pg == PG_MIX || pg == PG_MASTER) {
    int hi; uint8_t *v = i >= 0 && i < 4 ? mix_value(pg, i, &hi) : 0;
    if (v) *v = (uint8_t)clampi((int)(u * (float)hi + 0.5f), 0, hi);
    else if (i == 3) vibe_param_set(P_VOLUME, vd_clamp(u, 0.0f, 1.0f));
    return;
  }
  const sfx_patch *p = &patches[sel];
  int k = pg < PG_SEQ && i >= 0 && i < 4 ? PAGE_PARAMS[pg][i] : -1, d = 0;
  if (k == ENGINE_KNOB) d = clampi((int)(u * (float)SFX_ENGINE_COUNT), 0, SFX_ENGINE_COUNT - 1) - p->engine;
  else if (k == SFX_P_TABLE) d = clampi((int)(u * (float)SFX_WT_TABLES), 0, SFX_WT_TABLES - 1) - p->p[k] % SFX_WT_TABLES;
  else if (k >= 0) d = (int)(u * 127.0f + 0.5f) - p->p[k];
  if (d) param_nudge(pg, i, d);
}

static void encoder(int id, float turn)
{
  acc[id] += turn * (shift ? 8.0f : 1.0f);
  int d = (int)acc[id];
  if (!d) return;
  acc[id] -= (float)d;
  if (id <= VE_4) { touch_i = id; touch_t = vibe_frames(); }
  if (id == VE_DATA) { sel = wrap(sel, d, SLOTS); return; }
  if (id == VE_A && song_keys()) {                   /* SEQ, SONG, AUTO: A is the tempo (the top line shows it), with SHIFT the swing */
    if (shift) { static char t[12] = "SWING "; SONG.swing = (uint8_t)clampi(SONG.swing + (d > 0 ? 2 : -2), 0, 100); vg_itoa(SONG.swing, t + 6); strcpy(t + strlen(t), "%"); say(t); }
    else SONG.bpm = (uint16_t)clampi(SONG.bpm + d, 40, 240);
    return;
  }
  if (id == VE_A && page != PG_LINK) { vibe_param_set(P_VOLUME, vd_clamp(vibe_param_get(P_VOLUME) + (float)d * 0.02f, 0.0f, 1.0f)); return; }
  if (page == PG_LINK) {                             /* the encoders are the game engine's: CC 20..25 */
    int k = id <= VE_4 ? id : id == VE_A ? 4 : id == VE_B ? 5 : -1;
    if (k < 0) return;
    cc_out[k] = (uint8_t)clampi(cc_out[k] + d, 0, 127);
    vibe_midi_send(0xB0, (uint8_t)(CC_ENCODERS + k), cc_out[k]);
    return;
  }
  if (page == PG_AUTO) {
    if (id == VE_1) cur_step = wrap(cur_step, d, gsteps());
    if (id == VE_2) auto_turn(d);
    if (id == VE_3) view_to[sel] = (uint8_t)wrap(view_to[sel], d > 0 ? 1 : -1, SFX_AT_COUNT);
    return;
  }
  if (page == PG_SEQ) {
    if (id == VE_1) cur_step = wrap(cur_step, d, gsteps());
    if (id == VE_2) note_turn(d);
    if (id == VE_3) vel_turn(d > 0 ? 1 : -1);
    if (id == VE_4) len_turn(gsteps() == PADS ? (d > 0 ? 1 : -1) : d);      /* (a finer grid has more to cross: SHIFT is 8 a click there) */
    if (id == VE_B) { SONG.scale = (uint8_t)wrap(SONG.scale, d, SFX_SCALE_COUNT); say(SCALE_NAME[SONG.scale]); }   /* (said in the top line) */
    return;
  }
  if (page == PG_SONG) {
    if (id == VE_1) pattern_step(d);
    if (id == VE_2) { if (SONG.arr_len) say("ARRANGEMENT IN USE"); else SONG.chain = (uint8_t)clampi(SONG.chain + d, 1, SFX_PATTERNS); }
    if (id == VE_3) intensity_set(vibe_param_get(P_INTENSITY) + (float)d * 0.02f);
    if (id == VE_4) SONG.level[sel] = (uint8_t)clampi(SONG.level[sel] + d * 2, 0, 127);
    if (id == VE_B) { static char t[16]; SONG.len = (uint8_t)clampi(SONG.len + d, 1, SFX_BAR); strcpy(t, "BAR: "); vg_itoa(SONG.len, t + 5); strcpy(t + strlen(t), "/16"); say(t); }
    return;
  }
  if (id <= VE_4) param_nudge(page, id, d);
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
  case 0: { int first = kit == KIT_SFX ? 0 : SFX_PRESETS, n = kit == KIT_SFX ? SFX_PRESETS : PRESET_COUNT - SFX_PRESETS;     /* (each kit has its own names) */
            int at = pad_preset[sel] >= first && pad_preset[sel] < first + n ? pad_preset[sel] - first : (shift ? 0 : -1), k = first + wrap(at, shift ? -1 : 1, n);
            keep_undo(sel); preset_load(sel, k); say(PRESETS[k].name);
            if (page == PG_WAVE && p->engine != SFX_ENGINE_WAVETABLE) set_page(PG_OSC);
            break; }
  case 1: keep_undo(sel); sfx_patch_mutate(p, &rng); say("MUTATED"); break;
  case 2: if (!has_undo[sel]) { say("NOTHING TO UNDO"); return; }
          { sfx_patch t = *p; int8_t n = pad_preset[sel]; *p = undo[sel]; pad_preset[sel] = undo_preset[sel]; undo[sel] = t; undo_preset[sel] = n; } say("UNDO"); break;
  case 3: keep_undo(sel); sfx_patch_random(p, &rng); pad_preset[sel] = -1; say("RANDOM"); break;
  case 4: dump_due |= 1u << sel; out_pump(); say("EXPORTED"); return;
  case 5: save_say(); return;
  }
  changed(sel);
  audition();
}

static void pattern_clear(void)                      /* (one press: F2 brings it back) */
{
  if (!pattern_holds(edit_pat)) { say("NOTHING TO CLEAR"); return; }   /* (and what F2 holds is kept) */
  undo_mark("CLR PAT", 0);
  sfx_pattern_clear(pat());                          /* (its grid stays) */
  lanes_tidy(); say("PAT CLEARED - F2: UNDO");
}

/* a whole song goes: these ask twice, and F2 brings it back – tempo, mix and all. The sounds are not touched; it plays on */
static void song_new(void)
{
  if (!confirm("PRESS AGAIN: NEW SONG")) return;
  undo_mark("NEW", 1);
  sfx_song_init(&SONG); view_sync(); cur_step = 0; pattern_home();
  say("NEW SONG - F2: UNDO");
}
static void song_demo(void)
{
  if (!confirm("PRESS AGAIN: DEMO")) return;
  undo_mark("DEMO", 1);
  demo_song(); view_sync(); pattern_home();
  say("DEMO SONG - F2: UNDO");
}

static void pattern_copy(void)                       /* this pattern into the next empty one, and on to it: a variation to make */
{
  static char said[16] = "COPIED A > B";
  int to = -1;
  for (int i = 1; i < SFX_PATTERNS && to < 0; i++) if (!pattern_holds((edit_pat + i) % SFX_PATTERNS)) to = (edit_pat + i) % SFX_PATTERNS;
  if (to < 0) { say("NO EMPTY PATTERN"); return; }
  undo_mark("COPY", 0);
  if (!(rec && running)) undo_pat = edit_pat;
  SONG.pat[to] = *pat();                             /* (with its grid) */
  said[7] = (char)('A' + edit_pat); said[11] = (char)('A' + to);
  pattern_pick(to); say(said);                       /* (the chain is left alone) */
}

/* the selected sound on every quarter, then every eighth, every sixteenth - at 32 and 64 steps also every thirty-second -, none */
static void fill(void)
{
  static const char *const SAID[5] = { "EVERY 1/4", "EVERY 1/8", "EVERY 1/16", "EVERY 1/32", "SOUND CLEARED" };
  int div = pat_div(edit_pat), n = gsteps(), lim = bar_len() * div, levels = div > 1 ? 4 : 3, next = 0;
  int every[4] = { 4 * div, 2 * div, div, div > 1 ? div / 2 : 1 };
  undo_mark("FILL", 0);
  for (int k = 0; k < levels; k++) {
    int same = 1;
    for (int i = 0; i < n; i++) if (((pat()->on[i] >> sel) & 1) != (i % every[k] == 0 && i < lim)) same = 0;
    if (same) next = k + 1;
  }
  for (int i = 0; i < n; i++) {
    pat()->on[i] &= (uint16_t)~(1u << sel);
    if (next < levels && i % every[next] == 0 && i < lim) pat()->on[i] |= (uint16_t)(1u << sel);
  }
  say(next < levels ? SAID[next] : SAID[4]);
}

/* SEQ and AUTO, SHIFT + F5 / F6: the grid of the pattern shown, a step coarser or finer. Finer moves every step to the
   step that begins at the same moment: it sounds the same. Coarser drops what stands between the steps that stay - that
   asks twice (the second press counts for the same pattern, the same grid and the same number of things dropped only),
   and F2 brings it back. */
static void grid_change(int dir)
{
  static const char *const UP[SFX_GRIDS] = { "", "GRID 1/32", "GRID 1/64" }, *const DOWN[SFX_GRIDS] = { "GRID 1/16 - F2: UNDO", "GRID 1/32 - F2: UNDO", "" };
  static char ask[24]; static int ask_pat = -1, ask_to, ask_loss;
  sfx_pattern *p = pat();
  int to = pat_grid(edit_pat) + (dir > 0 ? 1 : -1);
  if (to >= SFX_GRIDS) { say("FINEST: 1/64"); return; }
  if (to < 0) { say("COARSEST: 1/16"); return; }
  if (dir < 0) {
    int loss = sfx_pattern_regrid_loss(p, to);
    if (loss > 0) {
      if (!(toast == ask && toast_on() && ask_pat == edit_pat && ask_to == to && ask_loss == loss)) {   /* (more or less to drop than it said: it says the number that is true now) */
        strcpy(ask, "AGAIN: A DROPS "); ask[7] = (char)('A' + edit_pat); vg_itoa(loss, ask + 15);
        ask_pat = edit_pat; ask_to = to; ask_loss = loss; say(ask); return;
      }
      toast_live = 0;
    }
  }
  undo_mark("GRID", 0);
  sfx_pattern_regrid(p, to);
  if (dir < 0) lanes_tidy();
  say(dir > 0 ? UP[to] : DOWN[to]);
  erase_at = -1; cur_sync(); relabel();
}

static void soft_seq(int f)                          /* F1-F6 on the SEQ page */
{
  if (shift && f >= 4) { grid_change(f == 4 ? -1 : 1); return; }
  switch (f) {
  case 0: mode_toggle(); break;
  case 1: song_undo(); break;
  case 2: fill(); break;
  case 3: { int any = 0;                             /* (while a take runs F4 is ERASE, held: vibe_on_event) */
          for (int i = 0; i < gsteps(); i++) any |= ((pat()->on[i] >> sel) & 1) || pat()->note[sel][i] || pat()->vel[sel][i] != SFX_VEL_DEFAULT || pat()->len[sel][i];
          if (!any) { say("NOTHING TO CLEAR"); break; } }
          undo_mark("CLEAR", 0);
          for (int i = 0; i < gsteps(); i++) { pat()->on[i] &= (uint16_t)~(1u << sel); pat()->note[sel][i] = 0; pat()->vel[sel][i] = SFX_VEL_DEFAULT; pat()->len[sel][i] = 0; } say("CLEARED - F2: UNDO"); break;
  case 4: pattern_copy(); break;
  case 5: save_say(); break;
  }
}

static void soft_auto(int f)                         /* F1-F6 on the AUTO page */
{
  if (shift && f >= 4) { grid_change(f == 4 ? -1 : 1); return; }
  switch (f) {
  case 0: mode_toggle(); break;
  case 1: song_undo(); break;
  case 2: auto_ramp(); break;
  case 3: { int any = 0; for (int i = 0; i < gsteps(); i++) any |= aut_at(sel, i); if (!any) { say("NOTHING TO CLEAR"); break; } }
          undo_mark("CLEAR", 0); for (int i = 0; i < gsteps(); i++) aut_put(sel, i, 0); say("LANE CLEARED"); break;
  case 4: pattern_copy(); break;
  case 5: save_say(); break;
  }
}

/* the arrangement: a pattern a bar. SONG page, SHIFT + pad 1-8 adds one, SHIFT + pad 9-15 takes the last away, 16 all
   of them: that asks twice. F2 takes back the bars added and removed in a row (one step), or brings all of them back */
static void arr_edit(int pad_id)
{
  if (pad_id < 8) { if (SONG.arr_len >= SFX_ARR_MAX) { say("32 BARS: FULL"); return; } undo_open(3); SONG.arr[SONG.arr_len] = (uint8_t)pad_id; SONG.arr_len++; say("BAR ADDED"); }
  else if (pad_id == 15) {
    if (!SONG.arr_len) { say("NO ARRANGEMENT"); return; }
    if (!confirm("PRESS AGAIN: CLEAR ARR")) return;
    undo_mark("ARR", 0); SONG.arr_len = 0; say("ARR CLEARED - F2: UNDO");
  }
  else if (SONG.arr_len) { undo_open(3); SONG.arr_len--; say("BAR REMOVED"); }
}

static void soft_song(int f)                         /* F1-F6 on the SONG page */
{
  switch (f) {
  case 0: mode_toggle(); break;
  case 1: song_undo(); break;
  case 2: pattern_clear(); break;
  case 3: if (shift) song_demo(); else song_new(); break;
  case 4: export_at = 0; say(linked ? "SENDING" : "SENDING - NO GAME SEEN"); out_pump(); break;
  case 5: save_say(); break;
  }
}

static void soft_mix(int f)                          /* F1-F6 on the MIX and MASTER pages */
{
  switch (f) {
  case 0: SONG.mute ^= (uint16_t)(1u << sel); say((SONG.mute >> sel) & 1 ? "MUTED" : "UNMUTED"); break;
  case 1: SONG.solo ^= (uint16_t)(1u << sel); say((SONG.solo >> sel) & 1 ? "SOLO" : "SOLO OFF"); break;
  case 2: if (page == PG_MASTER) { uint16_t m = SONG.mute, so = SONG.solo; uint8_t v[SLOTS], pn[SLOTS];   /* (the master only: the faders stay) */
            for (int t = 0; t < SLOTS; t++) { v[t] = SONG.vol[t]; pn[t] = SONG.pan[t]; }
            sfx_song_mix_init(&SONG);
            for (int t = 0; t < SLOTS; t++) { SONG.vol[t] = v[t]; SONG.pan[t] = pn[t]; }
            SONG.mute = m; SONG.solo = so; say("MASTER RESET"); }
          else { SONG.vol[sel] = 100; SONG.pan[sel] = 64; say("FADER RESET"); }
          break;
  case 3: SONG.mute = SONG.solo = 0; say("ALL ON"); break;
  case 4: seq_play(!running); break;
  case 5: save_say(); break;
  }
}

/* ---- the tour: SFX FORGE shows by itself what it can do. Everything it touches is put back at the end. --------------- */
enum { T_SAY, T_PAGE, T_PAD, T_SELECT, T_PARAM, T_MUTATE, T_RESTORE, T_NOTE, T_ON, T_OFF, T_PLAY, T_STOP, T_HEAT, T_RAMP, T_CUE, T_KIT, T_END };
typedef struct { float t; uint8_t act, a, b; const char *title, *line; } tour_step;
#define S 5.4f                                      /* (the later parts, after the instruments) */
static const tour_step TOUR[] = {
  {  0.0f, T_SAY, 0, 0, "SOUND EFFECTS", "the SFX kit: 16 of them, all synthesized" },
  {  0.0f, T_KIT, KIT_SFX, 0, 0, 0 }, { 0.0f, T_PAGE, PG_OSC, 0, 0, 0 }, { 0.0f, T_HEAT, 8, 0, 0, 0 },
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
  { 11.2f, T_SAY, 0, 0, "INSTRUMENTS", "the MUSIC kit: 16 more, played on the keys" },
  { 11.2f, T_KIT, KIT_MUSIC, 0, 0, 0 }, { 11.2f, T_SELECT, 7, 0, 0, 0 },
  { 11.4f, T_NOTE, 69, 0, 0, 0 }, { 11.65f, T_NOTE, 72, 0, 0, 0 }, { 11.9f, T_NOTE, 76, 0, 0, 0 }, { 12.15f, T_NOTE, 81, 0, 0, 0 },
  { 12.4f, T_NOTE, 79, 0, 0, 0 }, { 12.65f, T_NOTE, 76, 0, 0, 0 }, { 12.9f, T_NOTE, 72, 0, 0, 0 }, { 13.15f, T_NOTE, 69, 0, 0, 0 },
  { 13.8f, T_SAY, 0, 0, "CHORDS", "strings, brass, bells - with chorus, delay, reverb" },
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
  kit_set(tour_kit0);
  mode_set(tour_mode0);
}

static void tour_start(void)
{
  if (tour_on) { tour_end(); return; }
  tour_heat0 = vibe_param_get(P_INTENSITY); tour_page0 = page; tour_sel0 = sel; tour_kit0 = kit; tour_mode0 = loop_mode;
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
    case T_PLAY:    edit_pat = 0; mode_set(0); seq_play(1); break;                  /* (the tour plays the song, in its order) */
    case T_STOP:    seq_play(0); break;
    case T_HEAT:    tour_ramp_len = 0.0f; intensity_set((float)st->a / 8.0f); break;
    case T_RAMP:    tour_ramp_from = vibe_param_get(P_INTENSITY); tour_ramp_to = (float)st->a / 8.0f; tour_ramp_t0 = t; tour_ramp_len = (float)st->b / 4.0f; break;
    case T_CUE:     pattern_pick(st->a); break;
    case T_KIT:     tour_restore(); tour_kept = -1; kit_set(st->a); break;
    case T_END:     tour_end(); break;
    }
  }
}
#undef S

/* ---- the stage: 960 x 540 – the whole instrument for the mouse. Left: the pads and the selected sound (its picture,
        its pages, four values to drag, its keys). Right: the song – every sound a row of the pattern's steps (16, 32 or
        64 in the same width), intensity, the layer. --- */
#define SW 960
#define SH 540
static uint16_t stage_fb[SW * SH];
#define SP_X 24                                     /* the pads */
#define SP_Y 64
#define SP_W 102
#define SP_H 54
#define SP_PX 108
#define SP_PY 60
#define ST_Y 404                                    /* the sound's pages, seven of them */
#define ST_PX 43
#define ST_N 10
static const uint8_t ST_PAGE[ST_N] = { PG_OSC, PG_TIMBRE, PG_FILTER, PG_WAVE, PG_ENV, PG_LFO, PG_FX, PG_MIX, PG_MASTER, PG_AUTO };
#define SC_Y 430                                    /* its four values */
#define SC_H 56
#define SA_Y 494                                    /* PRESET MUTATE UNDO RANDOM EXPORT SAVE */
#define SA_PX 72
#define SN_X 466                                    /* the song: names, then the steps */
#define SG_X 552
#define SG_Y 64
#define SG_PX 24
#define SG_PY 22
#define SG_W (SFX_BAR * SG_PX)
#define GC_X 788                                    /* the pattern's grid: a click makes it finer, SHIFT or the right button coarser */
#define GC_Y 42
#define SI_Y 426                                    /* INTENSITY, and under it the selected sound's LAYER */
#define SL_Y 450
#define SF_X 712                                    /* FILL CLEAR COPY UNDO DEMO */
#define SF_Y 476
#define SF_PX 46
#define SF_N 5
#define TB_Y 14                                     /* the top row: LOOP / SONG, PLAY REC, tempo, the patterns, TOUR */
#define TM_X 126                                    /* what PLAY plays: a click changes it */
#define TP_X 462
#define TP_PX 40
enum { SD_NONE, SD_HEAT, SD_LAYER, SD_PARAM, SD_AUTO };
#define TA_Y (TB_Y + 23)                            /* the arrangement, under the patterns: 32 bars */
#define TA_PX 10
static int stage_drag, stage_drag_i, stage_x, stage_y, spage;   /* what the mouse drags; where it is; the sound page shown */

static int inside(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && y >= ry && x < rx + rw && y < ry + rh; }
static int stage_cell_at(int x, int y) { return inside(x, y, SP_X, SC_Y, 4 * SP_PX, SC_H) && (x - SP_X) % SP_PX < SP_W ? (x - SP_X) / SP_PX : -1; }

static int stage_page_follows(void) { return sound_page() || mix_page() || page == PG_AUTO; }

/* the AUTO tab turns the rows into lanes: a click or a drag in the selected sound's row puts points where the mouse is */
static void stage_auto(int x, int y)
{
  int to = view_to[sel], step = clampi((x - SG_X) * gsteps() / SG_W, 0, gsteps() - 1);
  cur_step = step;
  if (!to) { say("CHOOSE A TARGET FIRST"); return; }
  undo_open(1);
  aut_put(sel, step, clampi((SG_Y + sel * SG_PY + 19 - y) * auto_hi(to) / 19, 0, auto_hi(to)) + 1);
}

static void stage_dragged(int x)
{
  float u = vd_clamp((float)(x - SG_X) / (float)(SG_W - 2), 0.0f, 1.0f);
  if (stage_drag == SD_HEAT) intensity_set(u);
  if (stage_drag == SD_LAYER) SONG.level[sel] = (uint8_t)(u * 127.0f + 0.5f);
  if (stage_drag == SD_PARAM) param_put(spage, stage_drag_i, vd_clamp((float)(x - SP_X - stage_drag_i * SP_PX - 8) / (float)(SP_W - 16), 0.0f, 1.0f));
}

static void stage_event(const vibe_event *e)
{
  int x = VIBE_STAGE_X(e), y = VIBE_STAGE_Y(e), was = shift;
  if (e->id == VIBE_STAGE_WHEEL) {                   /* the wheel: the value under the mouse – over the steps, the step's note */
    int d = e->value > 0.0f ? 1 : -1, c = stage_cell_at(stage_x, stage_y);
    if (c >= 0) param_nudge(spage, c, d * ((e->midi[1] & VIBE_STAGE_MOD_SHIFT) ? 8 : 1));
    else if (inside(stage_x, stage_y, 342, TB_Y, 104, 36)) SONG.bpm = (uint16_t)clampi(SONG.bpm + d, 40, 240);
    else if (inside(stage_x, stage_y, SG_X, SI_Y, SG_W, 18)) intensity_set(vibe_param_get(P_INTENSITY) + (float)d * 0.02f);
    else if (inside(stage_x, stage_y, SG_X, SL_Y, SG_W, 18)) SONG.level[sel] = (uint8_t)clampi(SONG.level[sel] + d * 2, 0, 127);
    else if (spage == PG_AUTO) auto_turn(d);
    else if (e->midi[1] & VIBE_STAGE_MOD_SHIFT) vel_turn(d);
    else if (e->midi[1] & VIBE_STAGE_MOD_CTRL) len_turn(d);
    else note_turn(d);
    return;
  }
  if (e->id != VIBE_STAGE_DOWN && e->id != VIBE_STAGE_MOVE && e->id != VIBE_STAGE_UP) return;
  stage_x = x; stage_y = y;
  if (e->id == VIBE_STAGE_UP) { if (stage_pad >= 0) pad(stage_pad, 0, 0), stage_pad = -1; stage_drag = SD_NONE; return; }
  if (e->id == VIBE_STAGE_MOVE) { if (e->down && stage_drag == SD_AUTO) stage_auto(x, y); else if (e->down && stage_drag) stage_dragged(x); return; }
  if (tour_on) { tour_end(); return; }
  int px = x - SP_X, py = y - SP_Y, gx = x - SG_X, gy = y - SG_Y, c = stage_cell_at(x, y);
  if (inside(x, y, SP_X, SP_Y, 4 * SP_PX, 4 * SP_PY) && px % SP_PX < SP_W && py % SP_PY < SP_H) {
    stage_pad = py / SP_PY * 4 + px / SP_PX;
    pad(stage_pad, 1, 0.9f);
  } else if (inside(x, y, SP_X + 312, 304, 114, 24)) { kit_set(x < SP_X + 370 ? KIT_SFX : KIT_MUSIC); if (kit == KIT_SFX && spage >= PG_SEQ) spage = PG_OSC; }
  else if (gx >= 0 && gy >= 0 && gx < SG_W && gy < SLOTS * SG_PY && spage == PG_AUTO) {
    kit_set(KIT_MUSIC);    /* a lane: a point – SHIFT or the right button takes it away */
    sel = gy / SG_PY; cur_step = clampi(gx * gsteps() / SG_W, 0, gsteps() - 1);
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) { undo_open(1); aut_put(sel, cur_step, 0); }
    else { stage_drag = SD_AUTO; stage_auto(x, y); }
  } else if (gx >= 0 && gy >= 0 && gx < SG_W && gy < SLOTS * SG_PY) { kit_set(KIT_MUSIC); sel = gy / SG_PY; step_toggle(clampi(gx * gsteps() / SG_W, 0, gsteps() - 1), sel); }
  else if (inside(x, y, SN_X, SG_Y, SG_X - SN_X, SLOTS * SG_PY)) {                         /* a row's name: that sound – SHIFT or the right button: mute */
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) SONG.mute ^= (uint16_t)(1u << (gy / SG_PY));
    else { kit_set(KIT_MUSIC); stage_pad = gy / SG_PY; pad(stage_pad, 1, 0.9f); }
  }
  else if (inside(x, y, SG_X, SI_Y - 4, SG_W, 22)) { stage_drag = SD_HEAT; stage_dragged(x); }
  else if (inside(x, y, SG_X, SL_Y, SG_W, 20)) { stage_drag = SD_LAYER; stage_dragged(x); }
  else if (c >= 0) { keep_undo(sel); stage_drag = SD_PARAM; stage_drag_i = c; stage_dragged(x); }   /* (UNDO takes a drag back) */
  else if (inside(x, y, SP_X, ST_Y, ST_N * ST_PX, 20)) {
    int k = ST_PAGE[(x - SP_X) / ST_PX];
    if (k == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE) return;
    if (k >= PG_SEQ) kit_set(KIT_MUSIC);                 /* MIX, MASTER, AUTO: the song's */
    spage = k; if (stage_page_follows()) set_page(k);
  } else if (inside(x, y, SP_X, SA_Y, 6 * SA_PX, 32)) {                                    /* (PRESET: SHIFT or the right button = the one before) */
    shift = (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) != 0;
    soft_sound((x - SP_X) / SA_PX);
    shift = was;
  } else if (inside(x, y, TM_X, TB_Y, 58, 36)) mode_toggle();
  else if (inside(x, y, GC_X, GC_Y, 62, 14)) grid_change((e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) ? -1 : 1);
  else if (inside(x, y, 190, TB_Y, 76, 36)) seq_play(!running);
  else if (inside(x, y, 272, TB_Y, 64, 36)) seq_rec(!rec);
  else if (inside(x, y, 342, TB_Y, 104, 36)) SONG.bpm = (uint16_t)clampi(SONG.bpm + (x < 394 ? -1 : 1), 40, 240);
  else if (inside(x, y, TP_X, TB_Y, 8 * TP_PX, 21)) {                                    /* a pattern – SHIFT or the right button: one more bar of it */
    int k = (x - TP_X) / TP_PX;
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) arr_edit(k); else pattern_pick(k);
  } else if (inside(x, y, TP_X, TA_Y, SFX_ARR_MAX * TA_PX, 15)) {                          /* a bar: its pattern – SHIFT or right: the arrangement ends before it */
    int k = (x - TP_X) / TA_PX;
    if (k >= SONG.arr_len) return;
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) { undo_mark("ARR", 0); SONG.arr_len = (uint8_t)k; say(k ? "BARS REMOVED" : "ARR CLEARED - F2: UNDO"); } else pattern_pick(SONG.arr[k]);
  } else if (inside(x, y, 856, TB_Y, 80, 36)) tour_start();
  else if (inside(x, y, SF_X, SF_Y, SF_N * SF_PX, 30)) {
    int k = (x - SF_X) / SF_PX;
    if (k == 0) { if (spage == PG_AUTO) auto_ramp(); else fill(); }
    else if (k == 1) { if (spage == PG_AUTO) soft_auto(3); else soft_seq(3); }
    else if (k == 2) pattern_copy(); else if (k == 3) song_undo(); else song_demo();      /* (DEMO asks twice, as on the device) */
  }
}

void vibe_on_init(float sample_rate)
{
  SR = sample_rate;
  sfx_player_init(&P, SR);
  factory(); bank_load(); view_sync();
  song_way_start(&WAY, &SONG, &P.song);              /* (nothing else runs yet: the song as it was loaded is the player's) */
  for (int i = 0; i < SLOTS; i++) P.patch[i] = kits[KIT_MUSIC][i];
  vibe_notes(VIBE_NOTES);
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vibe_stage(stage_fb, SW, SH, 0x180F0B, 0);
  vf_mode(VF_ON_HOLD);                               /* DATA chooses the sound; ENTER held = FOCUS */
  vf_clicks(1);
  for (int i = 0; i < TAB_COUNT; i++) vibe_label(VB_K1 + i, TAB_NAME[i]);
  set_page(PG_OSC);
}

/* HOME: the controls of the page open now – up to 11 rows, so each has the whole width */
static const char *const HELP_SOUND[] = {
  "PADS  DATA", "play a sound and select it · select one",
  "KEYS", "the selected sound as an instrument",
  "SHIFT + ENTER", "the other kit: SFX (effects) / MUSIC",
  "ENCODER 1-4", "the four values shown (SHIFT: fast)",
  "K1 SOUND", "again: OSC, TONE, FILTER (WAVE)",
  "K2 - K6", "ENV · LFO · FX · SEQ · LINK",
  "LEFT RIGHT", "the page before / after",
  "F1 PRESET", "next named sound (SHIFT: the one before)",
  "F2 F3 F4", "MUTATE · UNDO · RANDOM",
  "F5 F6", "EXPORT to a game (SysEx) · SAVE",
  "MENU", "the tour (SHIFT + MENU twice: factory reset)",
};
static const char *const HELP_SEQ[] = {
  "PADS", "16 steps of the bar. REC lit: they play the kit",
  "SHIFT + PAD", "hear and select that pad's sound",
  "PAD + KEY", "hold a step: it plays the key's note",
  "DATA", "select a sound (a row)",
  "ENCODER 1 2", "the step in the frame, and its NOTE",
  "ENCODER 3 4", "its VELOCITY (1-8) · its LENGTH in steps",
  "A   B", "TEMPO (SHIFT: SWING) · the scale of NOTE",
  "LEFT RIGHT", "pattern before / after · SHIFT: the steps shown",
  "F1  F2", "LOOP or SONG plays · UNDO (again: redo)",
  "F3 F4 F5", "FILL · CLEAR · COPY · SHIFT+F5 F6: grid",
  "REC + PLAY", "play it in. F4 held: ERASE. SHIFT+REC: click",
};
static const char *const HELP_SONG[] = {
  "PADS 1-8", "pattern A-H, from the next bar line",
  "PADS 9-16", "INTENSITY, low to high",
  "SHIFT+PAD 1-8", "ARRANGEMENT: add that pattern as a bar",
  "SHIFT+PAD 9-15 16", "take the last bar away · all (press twice)",
  "ENCODER 1 2", "PATTERN · CHAIN: A to here play in a row",
  "ENCODER 3 4", "INTENSITY (CC 14) · LAYER of the sound",
  "A   B", "TEMPO (SHIFT: SWING) · the bar in 16ths",
  "F1", "what PLAY plays: LOOP a pattern, or the SONG",
  "F2 F3", "UNDO (again: redo) · CLEAR the pattern",
  "F4", "NEW empty song (SHIFT: DEMO) - press twice",
  "F5 F6", "SEND to a game · SAVE",
};
static const char *const HELP_AUTO[] = {
  "K5 AUTO", "automation: a lane for every sound",
  "ENCODER 3", "TARGET: any knob - sound, fader, master",
  "PADS", "a point on that step, on / off",
  "ENCODER 1 2", "the step in the frame · its VALUE",
  "A POINT", "holds to the next one, or the bar's end",
  "DATA", "select a sound: its own lane",
  "SHIFT + PAD", "hear and select that pad's sound",
  "LEFT RIGHT", "pattern before / after · SHIFT: the steps shown",
  "F1  F2", "LOOP or SONG plays · UNDO (again: redo)",
  "F3 F4 F5", "RAMP · CLEAR · COPY · SHIFT+F5 F6: grid",
  "A", "TEMPO (SHIFT: SWING)",
};
static const char *const HELP_LINK[] = {
  "ENCODERS", "1-4, A, B send CC 20-25 to the game",
  "PADS", "play, and send notes 36-51",
  "IN: CC 10-13", "channel 2: pitch, cutoff, timbre, level",
  "IN: CC 14 15", "channel 2: intensity · pattern",
  "IN: NOTES", "36-51 play the pads",
  "F5 EXPORT", "the selected sound as SysEx",
  "SONG: F5", "SEND: all sounds and the song",
  "SHIFT + ENTER", "the other kit: SFX (effects) / MUSIC",
  "MENU", "the tour",
};

/* SHIFT + MENU, twice within three seconds: both kits and the song as they came, and kept so (what was saved is gone) */
static void factory_reset(void)
{
  static uint64_t asked;
  static int armed;
  int was = kit;
  if (!armed || vibe_frames() - asked > (uint64_t)(3.0f * SR)) { armed = 1; asked = vibe_frames(); say("AGAIN: FACTORY RESET"); return; }
  armed = 0;
  if (tour_on) tour_end();
  if (running) seq_play(0);
  factory(); view_sync();
  for (int k = 0; k < 2; k++) for (int i = 0; i < SLOTS; i++) has_undo_k[k][i] = 0;
  undo_has = 0; undo_run = 0; undo_redo = 0; undo_pat = -1;
  kit = KIT_MUSIC;
  for (int i = 0; i < SLOTS; i++) changed(i);          /* the song's sounds, to the player */
  kit = was;
  cur_step = 0; pattern_home();
  vibe_store_delete("song.bin");                     /* (what was saved is gone: also the song an older firmware kept) */
  say(bank_save() ? "RESET - NOT SAVED" : "FACTORY RESET");
}

static const char *const HELP_MIX[] = {
  "K4 MIX", "FX again: every sound a fader",
  "PADS", "mute a sound.  SHIFT + pad: solo",
  "DATA", "select a sound",
  "ENCODER 1 2", "VOLUME and PAN of the selected sound",
  "ENCODER 3 4", "DRIVE on the sum · the OUTPUT volume",
  "K4 MASTER", "and again: the delay and the reverb",
  "MASTER 1 2", "the echo's TIME and its FEEDBACK",
  "MASTER 3 4", "the reverb's SIZE and DAMPING",
  "F1 F2", "MUTE · SOLO the selected sound",
  "F3 F4", "RESET this page · ALL ON: no mute, no solo",
  "F5 F6", "PLAY · SAVE",
};

static void pick(int slot, int down)                 /* hear a pad's sound and select it – without recording it */
{
  if (!down) { send(M_RELEASE, slot, slot, 0, 0); return; }
  sel = slot;
  send(M_TRIGGER, slot, slot, 0.8f, 0);
}

#define FOCUS_KEY 0x40001000                         /* what vf_focus() says for a vf_key stand-in: this + the button (the SDK's
                                                        own number, not in its headers: tests/check-grid-keys.mjs watches it) */
static void on_event(const vibe_event *e)
{
  /* SHIFT + ENTER (or DATA pressed), a short press: the other kit. It is ours before FOCUS looks at it, so it does the
     same whether a frame is on a value or a page or on nothing. One case stays FOCUS': the frame on F1-F6. There ENTER
     presses that key, and with SHIFT held it is SHIFT + that key (the preset before, DEMO, the grid) - the only way to
     them for a controller without F keys. (ENTER held is FOCUS' too: the SDK keeps that press and we never see it.) */
  int on_soft = vf_active() && vf_focus() >= FOCUS_KEY + VB_F1 && vf_focus() <= FOCUS_KEY + VB_F6;
  int kit_key = e->type == VIBE_EV_BUTTON && (e->id == VB_ENTER || e->id == VB_DATA_PUSH) && shift && !on_soft;
  int id, r = kit_key ? VF_NO : vf_event(e, &id);
  out_pump(); seq_poll();
  if (r != VF_NO) return;
  if (tour_on && e->down && (e->type == VIBE_EV_PAD || e->type == VIBE_EV_NOTE || (e->type == VIBE_EV_BUTTON && e->id != VB_SHIFT))) { tour_end(); return; }   /* any touch ends the tour */
  switch (e->type) {
  case VIBE_EV_PAD:
    if (e->id >= SLOTS) break;
    if (page == PG_SEQ) {                            /* the pads are the steps; held, a key gives the step its note */
      if (!e->down) { send(M_RELEASE, e->id, e->id, 0, 0); if (held_pad == e->id) held_pad = held_step = -1; break; }   /* (let go: whatever it began – REC or SHIFT may have gone since) */
      if (shift) { pick(e->id, e->down); break; }                             /* SHIFT: the pad's sound – hear it, select it */
      if (rec) { pad(e->id, e->down, e->value); break; }                      /* REC lit: the pads are the kit – while it plays they are played in, as hard as they are hit */
      { int step = cur_step / PADS * PADS + e->id;      /* (the 16 steps round the cursor) */
        step_toggle(step, sel); held_pad = e->id; held_step = step; }
      break;
    }
    if (page == PG_SONG) { if (e->down) { if (shift) arr_edit(e->id); else if (e->id < 8) pattern_pick(e->id); else intensity_set((float)(e->id - 7) / 8.0f); } break; }
    if (page == PG_AUTO) { if (!e->down) send(M_RELEASE, e->id, e->id, 0, 0); else if (shift) pick(e->id, 1); else auto_toggle(cur_step / PADS * PADS + e->id); break; }     /* the pads: a point on / off */
    if (page == PG_MIX) { if (e->down) { sel = e->id; if (shift) SONG.solo ^= (uint16_t)(1u << sel); else SONG.mute ^= (uint16_t)(1u << sel); } break; }   /* the pads: mute, SHIFT: solo */
    pad(e->id, e->down, e->value);
    if (e->down && page == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE) set_page(PG_OSC);
    vibe_midi_send(e->down ? 0x90 : 0x80, (uint8_t)(NOTE_PADS + e->id), e->down ? (uint8_t)vd_clamp(e->value * 127.0f, 1.0f, 127.0f) : 0);
    break;
  case VIBE_EV_NOTE: {
    int outside = e->midi[1] == VIBE_NOTE_MIDI || e->midi[1] == VIBE_NOTE_HOST;
    if (outside && e->id >= NOTE_PADS && e->id < NOTE_PADS + SLOTS) { from_outside = 1; pad(e->id - NOTE_PADS, e->down, e->value); from_outside = 0; break; }   /* a game or a DAW plays the kit */
    if (!e->down) { send(M_RELEASE, sel, 100 + (e->id % 96), 0, 0); break; }
    if (page == PG_SEQ && held_step >= 0) { undo_open(1); step_set(pat(), held_step, sel, e->id); }
    else record(sel, e->id, e->value);
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
      if (k == 5 && (e->midi[2] >> 4) != (queued >= 0 ? queued : edit_pat)) pattern_pick(e->midi[2] >> 4);   /* 0..127 = pattern A..H (a value that repeats is one cue) */
    }
    break;
  case VIBE_EV_SYSEX: sysex_in(vibe_sysex_in(), (int)e->value); break;
  case VIBE_EV_SAVE: bank_save(); break;
  case VIBE_EV_STAGE: stage_event(e); break;
  case VIBE_EV_ENCODER: if (e->id < VE_COUNT) encoder(e->id, e->value); break;
  case VIBE_EV_BUTTON:
    if (e->id == VB_SHIFT) { shift = e->down; relabel(); break; }      /* (on SEQ and AUTO, F5 and F6 say what they do with it) */
    if (e->id == VB_F4) {                              /* SEQ, REC while it plays: F4 is ERASE for as long as it is held */
      if (!e->down) f4_held = 0;
      else if (page == PG_SEQ && rec && running) { f4_held = 1; erase_at = -1; erase_pat = -1; say("ERASE: HOLD F4"); seq_poll(); break; }
    }
    if (!e->down) break;
    if (e->id == VB_HOME) help = !help;
    else if (e->id == VB_RETURN) { if (help) help = 0; else set_page(PG_OSC); }
    else if (e->id == VB_PLAY) seq_play(!running);
    else if (e->id == VB_STOP) seq_play(0);
    else if (e->id == VB_REC) { if (shift) { metro = !metro; say(metro ? "CLICK ON" : "CLICK OFF"); } else seq_rec(!rec); }
    else if (e->id == VB_MENU) { if (shift) factory_reset(); else tour_start(); }
    else if (e->id == VB_LEFT || e->id == VB_RIGHT) arrow(e->id == VB_LEFT ? -1 : 1);
    else if (e->id >= VB_F1 && e->id <= VB_F6) { int f = e->id - VB_F1; if (page == PG_SEQ) soft_seq(f); else if (page == PG_SONG) soft_song(f); else if (page == PG_AUTO) soft_auto(f); else if (mix_page()) soft_mix(f); else soft_sound(f); }
    else if (kit_key) { kit_set(!kit); if (!sound_page()) set_page(PG_OSC); say(kit == KIT_SFX ? "SFX KIT" : "MUSIC KIT"); }
    /* (K1-K6 are the pages with SHIFT held too. On the VIBE itself SHIFT + K1-K6 never get here: there the VIBE plays
       notes with them, SDK 28, and SHIFT + RETURN locks that. Nothing of ours is on either.) */
    else if (e->id >= VB_K1 && e->id < VB_K1 + TAB_COUNT) tab_key(e->id - VB_K1);
    break;
  default: break;
  }
}

/* ---- the display ------------------------------------------------------------------------------------------------------ */
#define BG     0x180F0B
#define PANEL  0x2B1C15
#define LINE   0x4A3026
#define DIM    0x907A6F
#define TEXT   0xF4ECE8

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

#define LOC_X 364                                    /* the six marks of K1-K6 */

/* a piece of a circle: from a0 to a1 in turns, 0 = the top, clockwise */
static void arc(float cx, float cy, float r, float a0, float a1, float w, uint32_t rgb)
{
  int n = (int)((a1 - a0) * 28.0f) + 1;
  float px = cx + r * vd_sin(a0 + 1.0f), py = cy - r * vd_sin(a0 + 1.25f);
  for (int k = 1; k <= n; k++) {
    float a = a0 + (a1 - a0) * (float)k / (float)n, x = cx + r * vd_sin(a + 1.0f), y = cy - r * vd_sin(a + 1.25f);
    vg_line(px, py, x, y, w, rgb);
    px = x; py = y;
  }
}

/* A page is one of two kinds, and nothing else:
     a PICTURE of what the knobs shape, with their four values in its corners (the sound's pages, MASTER) – the knob
     you turn is written large across the middle for a moment;
     the 16 PADS as tiles, laid out as the pads are, with the four values in a column beside them (SEQ, SONG, AUTO, MIX). */
#define CY 58                                       /* a page's own room: from here … */
#define CH 228                                      /* … this high, down to the soft keys */
static int16_t enc_rect[4][4];                      /* where each encoder's value is drawn (for FOCUS and the mouse) */
static int touched(int i) { return touch_i == i && vibe_frames() - touch_t < (uint64_t)(1.3f * SR); }

static void dial(float cx, float cy, float r, int i, float bar, int centre, uint32_t rgb)   /* a ring as the knob stands, its number inside */
{
  static const char *const NUM[4] = { "1", "2", "3", "4" };
  float to = -0.375f + 0.75f * vd_clamp(bar, 0.0f, 1.0f);
  arc(cx, cy, r, -0.375f, 0.375f, 3.0f, LINE);
  if (bar >= 0.0f) {
    if (!centre) arc(cx, cy, r, -0.375f, to, 3.0f, rgb);
    else if (to > 0.0f) arc(cx, cy, r, 0.0f, to, 3.0f, rgb);
    else arc(cx, cy, r, to, 0.0f, 3.0f, rgb);
    vg_circle(cx + r * vd_sin(to + 1.0f), cy - r * vd_sin(to + 1.25f), 3.5f, TEXT);
  }
  vg_text(NUM[i], (int)cx, (int)cy - 7, VG_SMALL, DIM, VG_CENTER);
}

/* a value in a corner of the picture: 0 top left, 1 top right, 2 bottom left, 3 bottom right. i: its encoder */
static void hud(int corner, int i, const char *name, const char *value, float bar, int centre, uint32_t rgb)
{
  int right = corner & 1, w = 150, x = right ? 462 - w : 18, y = (corner >> 1) ? CY + CH - 52 : CY + 6;
  float cx = (float)(right ? x + w - 20 : x + 20);
  vibe_link_encoder(VE_1 + i, name, value);
  if (!name[0]) return;
  enc_rect[i][0] = (int16_t)x; enc_rect[i][1] = (int16_t)y; enc_rect[i][2] = (int16_t)w; enc_rect[i][3] = 46;
  vg_rect_a(x, y, w, 46, BG, 0.66f);
  dial(cx, (float)y + 23.0f, 15.0f, i, bar, centre, rgb);
  vg_text(name, right ? x + w - 42 : x + 42, y + 4, VG_SMALL, touched(i) ? rgb : DIM, right ? VG_RIGHT : VG_LEFT);
  vg_text(value, right ? x + w - 42 : x + 42, y + 20, VG_BIG, TEXT, right ? VG_RIGHT : VG_LEFT);
  if (touched(i)) {                                   /* the knob in the hand: large, in the middle */
    vg_rect_a(120, CY + CH / 2 - 40, 240, 80, BG, 0.82f);
    vg_text(name, 240, CY + CH / 2 - 34, VG_BOLD, rgb, VG_CENTER);
    vg_text(value, 240, CY + CH / 2 - 14, VG_BIG | VG_X2, TEXT, VG_CENTER);
  }
}

/* a value in the column beside the tiles */
static void row(int i, const char *name, const char *value, float bar, int centre, uint32_t rgb)
{
  int x = 256, y = CY + i * 47;
  vibe_link_encoder(VE_1 + i, name, value);
  if (!name[0]) return;
  enc_rect[i][0] = (int16_t)x; enc_rect[i][1] = (int16_t)y; enc_rect[i][2] = 212; enc_rect[i][3] = 43;
  vg_round(x, y, 212, 43, 8, touched(i) ? vg_mix(PANEL, rgb, 0.22f) : PANEL);
  dial((float)x + 24.0f, (float)y + 22.0f, 14.0f, i, bar, centre, rgb);
  vg_text(name, x + 48, y + 3, VG_SMALL, touched(i) ? TEXT : DIM, VG_LEFT);
  vg_text(value, x + 48, y + 18, VG_BIG, TEXT, VG_LEFT);
}

/* two lines under the column: what else there is to know here */
static void footer(const char *a, uint32_t rgb, const char *b)
{
  vg_text(a, 262, CY + 191, VG_BOLD, rgb, VG_LEFT);
  vg_text(b, 262, CY + 209, VG_SMALL, DIM, VG_LEFT);
}

static void tile_at(int s, int *x, int *y) { *x = 12 + (s % 4) * 59; *y = CY + (s / 4) * 58; }   /* the tile of pad s: 55 x 54 */

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
    vg_rect(x + 3 + i, y0, 1, y1 - y0 + 1, vg_mix(PANEL, rgb, 0.42f));
    vg_rect(x + 3 + i, mid - (mid - y0) * 6 / 10, 1, (y1 - y0) * 6 / 10 + 1, rgb);
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
  vg_rect(x + w - 10 - vg_text_width(label, VG_SMALL), y + h - 18, vg_text_width(label, VG_SMALL) + 8, 15, PANEL);
  vg_text(label, x + w - 6, y + h - 18, VG_SMALL, DIM, VG_RIGHT);
}

/* a sound page's picture: the sound's whole shape – and over it, in white, what this page's knobs shape */
static void scene(const sfx_patch *p, uint32_t rgb, int pg)
{
  char buf[16];
  preview_render(p);
  int x = 12, y = CY, w = 456, h = CH, mid = y + h / 2, own = pg == PG_OSC || pg == PG_FX, from = 48;
  float gain = 58.0f / pv_peak, x0 = (float)x + 44.0f, pw = (float)w - 88.0f, top = (float)mid - 58.0f, bot = (float)mid + 58.0f;
  const char *what = "the whole sound";
  vg_round(x, y, w, h, 8, PANEL);
  vg_rect(x + 4, mid, w - 8, 1, LINE);
  for (int i = 0; i < w - 8; i++) {
    int c = i * PV_COLS / (w - 8), y0 = mid - (int)(pv_hi[c] * gain), y1 = mid - (int)(pv_lo[c] * gain);
    vg_rect(x + 4 + i, y0, 1, y1 - y0 + 1, vg_mix(PANEL, rgb, own ? 0.42f : 0.18f));
    if (own) vg_rect(x + 4 + i, mid - (mid - y0) * 6 / 10, 1, (y1 - y0) * 6 / 10 + 1, rgb);
  }
  if (pg == PG_ENV) {
    float a = (float)p->p[SFX_P_ATTACK] / 127.0f, d = (float)p->p[SFX_P_AMP_DECAY] / 127.0f, su = (float)p->p[SFX_P_SUSTAIN] / 127.0f, r = (float)p->p[SFX_P_RELEASE] / 127.0f;
    int hold = p->p[SFX_P_SUSTAIN] > 0;
    float seg = pw * 0.24f, xa = x0 + 6.0f + a * seg, xd = xa + 10.0f + d * seg, xs = hold ? xd + pw * 0.16f : xd, xr = hold ? xs + 10.0f + r * seg : xd, ys = hold ? bot - (bot - top) * su : bot;
    float xy[10] = { x0, bot, xa, top, xd, ys, xs, ys, xr, bot };
    path(xy, 5, 0xFFFFFF);
    if (hold) vg_rect((int)xs, (int)top, 1, (int)(bot - top), LINE);                 /* (the pad is let go here) */
    what = hold ? "held, then released" : "a one-shot";
  } else if (pg == PG_FILTER) {
    float cx = x0 + 20.0f + (float)p->p[SFX_P_FILTER_CUTOFF] / 127.0f * (pw - 70.0f), base = top + (bot - top) * 0.45f, peak = base - (float)p->p[SFX_P_FILTER_RES] / 127.0f * (bot - top) * 0.42f;
    if (p->engine == SFX_ENGINE_FOLEY_NOISE) { float xy[6] = { cx - 26.0f, bot, cx, peak, cx + 44.0f, bot }; path(xy, 3, 0xFFFFFF); what = "the noise's band"; }
    else { float xy[8] = { x0, base, cx - 20.0f > x0 ? cx - 20.0f : x0, base, cx, peak, cx + 44.0f, bot }; path(xy, 4, 0xFFFFFF); what = "the filter"; }
  } else if (pg == PG_LFO) {
    int depth = p->p[SFX_P_LFO_PITCH] > p->p[SFX_P_LFO_FILTER] ? p->p[SFX_P_LFO_PITCH] : p->p[SFX_P_LFO_FILTER];
    if (p->p[SFX_P_LFO_TIMBRE] > depth) depth = p->p[SFX_P_LFO_TIMBRE];
    float cyc = 1.0f + (float)p->p[SFX_P_LFO_RATE] / 127.0f * 7.0f, amp = (bot - top) * 0.5f * (float)depth / 127.0f, py = (float)mid;
    for (int i = 1; i < (int)pw; i++) {
      float ny = (float)mid - vd_sin((float)i / pw * cyc) * amp;
      vg_line(x0 + (float)i - 1.0f, py, x0 + (float)i, ny, 2.0f, depth ? 0xFFFFFF : DIM);
      py = ny;
    }
    what = depth ? "the slow wave" : "LFO: off";
  } else if (pg == PG_TIMBRE || pg == PG_WAVE) {      /* the wave itself: 10 ms, where the sound is loudest */
    for (int i = 48; i < pv_n - 240; i += 16) if (vd_absf(pv_buf[i]) > pv_peak * 0.7f) { from = i; break; }
    int prev = mid - (int)(pv_buf[from] * gain);
    for (int i = 0; i < (int)pw; i++) {
      int now = mid - (int)(pv_buf[from + i * 240 / (int)pw] * gain), a = now < prev ? now : prev, b = now < prev ? prev : now;
      vg_rect((int)x0 + i, a, 2, b - a + 1, 0xFFFFFF);
      prev = now;
    }
    what = "its wave, 10 ms";
  }
  vg_text(what, 240, y + 12, VG_SMALL, DIM, VG_CENTER);
  vg_text(with(vg_ftoa((float)pv_n / PV_SR, 2, buf), "s long"), 240, y + h - 26, VG_SMALL, DIM, VG_CENTER);
}

static void tour_caption(void)                       /* where F1-F6 are, while the tour runs */
{
  vg_round(2, 286, 476, 30, 5, 0x36241C);
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
static void header(uint32_t rgb, uint32_t pos, uint32_t now)
{
  uint32_t step = pos == NO_STEP ? NO_STEP : (uint32_t)pos_step(pos);
  int pdiv = pos_div(pos);
  char buf[16], t[16];
  int pill = page >= PG_SEQ && page <= PG_MASTER, k = 0;     /* the song's pages (their kit is MUSIC): what PLAY plays, where the kit is said elsewhere */
  vg_round(8, 5, 30, 24, 5, rgb);
  vg_text(vg_itoa(sel + 1, buf), 23, 10, VG_BOLD, BG, VG_CENTER);
  if (tour_on) vg_text(tour_title, 46, 6, VG_BIG, 0x36C5FF, VG_LEFT);
  else if (toast_on()) vg_text(toast, 46, 6, VG_BIG, TEXT, VG_LEFT);
  else {
    int w = vg_text(pad_name(sel), 46, 6, VG_BIG, TEXT, VG_LEFT);
    if (pad_preset[sel] >= 0 && 54 + w < 230) vg_text(ENGINE_NAME[patches[sel].engine], 54 + w, 12, VG_SMALL, rgb, VG_LEFT);
    if (!pill) {
      vg_round(318, 8, 50, 18, 4, kit == KIT_SFX ? 0x3A2A22 : 0x4A3222);                /* the kit (SHIFT + ENTER) */
      vg_text(KIT_NAME[kit], 343, 9, VG_SMALL, kit == KIT_SFX ? 0xFF8A5C : 0x7FC4FF, VG_CENTER);
    }
  }
  if (pill) {                                         /* (also over a toast and the tour's title) */
    vg_rect(314, 4, 58, 26, BG);
    vg_round(318, 8, 50, 18, 4, loop_mode ? 0x36C5FF : 0x3A2A22);
    vg_text(loop_mode ? "LOOP" : "SONG", 343, 9, VG_SMALL, loop_mode ? BG : TEXT, VG_CENTER);
  }
  t[k++] = (char)('A' + (step != NO_STEP ? (int)now : edit_pat));                      /* "A  120" – "A>C 120" while C waits for the bar line */
  if (queued >= 0) { t[k++] = '>'; t[k++] = (char)('A' + queued); t[k++] = ' '; } else { t[k++] = ' '; t[k++] = ' '; }
  vg_itoa(SONG.bpm, t + k);
  vg_circle(467, 17, 4, linked ? 0x6EE7A8 : 0x48332A);
  int ix = 454 - vg_text(t, 454, 10, VG_BOLD, step != NO_STEP ? TEXT : DIM, VG_RIGHT) - 17;
  if (rec) vg_circle((float)ix + 5.0f, 17.0f, step != NO_STEP && ((int)step / pdiv) % 4 == 0 && ((float)((int)step % pdiv) + pos_frac(pos)) / (float)pdiv < 0.6f ? 7.5f : 5.0f, 0xFF4040);   /* (it beats with the metronome) */
  else if (step != NO_STEP) vg_tri((float)ix, 11.0f, (float)ix, 23.0f, (float)ix + 10.0f, 17.0f, 0x6EE7A8);
  else vg_rect(ix, 12, 10, 10, 0x6A5248);
}

/* where the song is, for whatever draws now (the display, the stage): the step that plays, in which steps a sixteenth */
static uint32_t dr_step = NO_STEP; static int dr_div = 1;
static void dr_read(uint32_t pos) { dr_step = pos == NO_STEP ? NO_STEP : (uint32_t)pos_step(pos); dr_div = pos_div(pos); }
/* step g of a grid with div steps a sixteenth is where the playhead is: at 16 steps the step itself, at 32 and 64 every
   step of the sixteenth that plays (a single sixty-fourth is over before it can be seen) */
static int head_at(int g, int div) { return dr_step != NO_STEP && (div == 1 && dr_div == 1 ? (int)dr_step == g : g / div == (int)dr_step / dr_div); }
static const char *step_text(char *buf)              /* the cursor: "5" at 16 steps, "17/32" at more */
{
  vg_itoa(cur_step + 1, buf);
  if (gsteps() > PADS) { strcpy(buf + strlen(buf), "/"); vg_itoa(gsteps(), buf + strlen(buf)); }
  return buf;
}
static const char *len_text(int len, char *buf)      /* a length: "3" at 16 steps; at more also what it is of a whole bar: "6 = 3/32" */
{
  int n = gsteps(), a = len, b = n;
  if (!len) return "AUTO";
  vg_itoa(len, buf);
  if (n == PADS) return buf;
  while (b) { int t = a % b; a = b; b = t; }
  strcpy(buf + strlen(buf), " = "); vg_itoa(len / a, buf + strlen(buf)); strcpy(buf + strlen(buf), "/"); vg_itoa(n / a, buf + strlen(buf));
  return buf;
}

/* ---- SEQ: 16 steps of the selected sound as the 16 pads – a row is a beat at 16 steps a bar, an eighth at 32, a
        sixteenth at 64 ---------------------------------------------------------------------------------------------- */
static void draw_seq(uint32_t rgb, uint32_t step, uint32_t now)
{
  char buf[16];
  int here = now == (uint32_t)edit_pat && step != NO_STEP, div = pat_div(edit_pat), lim = bar_len() * div, w = view_win();
  for (int s = 0; s < PADS; s++) {                    /* a step is as high as it is hard, and says its note */
    int g = w * PADS + s;
    int x, y, set = (pat()->on[g] >> sel) & 1, head = here && head_at(g, div), off = g >= lim, note = pat()->note[sel][g];
    int hgt = 24 + (pat()->vel[sel][g] & 7) * 4, len = pat()->len[sel][g];
    tile_at(s, &x, &y);
    vg_round(x, y, 55, 54, 7, head && !set ? 0x64463A : set ? vg_mix(BG, rgb, 0.25f) : off ? 0x1A120E : (s / 4) & 1 ? 0x2A1D17 : 0x35251D);
    if (set) vg_round(x, y + 54 - hgt, 55, hgt, 7, head ? 0xFFFFFF : rgb);
    if (set && note) vg_text(note_text(note, buf), x + 27, y + 33, VG_BOLD, BG, VG_CENTER);
    else if (set) vg_circle((float)x + 27.0f, (float)y + 41.0f, 4.0f, BG);
    else if (!off) vg_text(vg_itoa(g + 1, buf), x + 27, y + 20, VG_SMALL, div > 1 && g % div == 0 ? DIM : 0x6A5248, VG_CENTER);   /* (a finer grid: the steps that begin a sixteenth stand out) */
    if (set && len) { buf[0] = '>'; vg_itoa(len, buf + 1); vg_text(buf, x + 51, y + 2, VG_SMALL, hgt > 46 ? BG : TEXT, VG_RIGHT); }   /* (it holds this many steps) */
    if (g == cur_step) vg_frame(x - 2, y - 2, 59, 58, 2, 0xFFFFFF);
  }
}

/* SEQ and AUTO at 32 or 64 steps, in the footer's first line: the whole bar, a column a step - the selected sound's steps
   (AUTO: its lane's points), a line at every beat, a frame round the 16 steps the pads show, the playhead, a mark under
   the cursor's step; and the numbers of the 16 beside it */
static void bar_strip(uint32_t rgb, uint32_t step, uint32_t now, int points)
{
  char t[16];
  int div = pat_div(edit_pat), n = gsteps(), px = 128 / n, x0 = 262, y0 = CY + 192, w = view_win(), lim = bar_len() * div;
  vg_rect(x0, y0, 128, 12, 0x1A120E);
  vg_rect(x0, y0, lim * px, 12, 0x2A1D17);            /* (the bar: what lies beyond it stays dark) */
  for (int b = 1; b < 4; b++) vg_rect(x0 + b * 32, y0, 1, 12, LINE);
  for (int i = 0; i < lim; i++) if (points ? aut_at(sel, i) : (pat()->on[i] >> sel) & 1) vg_rect(x0 + i * px, y0 + 2, px > 2 ? px - 1 : px, 8, rgb);
  if (step != NO_STEP && now == (uint32_t)edit_pat) vg_rect(x0 + clampi((int)dr_step * div / dr_div, 0, n - 1) * px, y0, px, 12, 0xFFFFFF);
  vg_frame(x0 + w * PADS * px - 1, y0 - 1, PADS * px + 2, 14, 1, 0xFFFFFF);
  vg_rect(x0 + cur_step * px, y0 + 14, px, 2, rgb);   /* under it: the cursor's step (what STEP names) - during a take it may lie outside the 16 shown */
  vg_itoa(w * PADS + 1, t); strcpy(t + strlen(t), "-"); vg_itoa(w * PADS + PADS, t + strlen(t)); strcpy(t + strlen(t), "/"); vg_itoa(n, t + strlen(t));
  vg_text(t, 398, CY + 191, VG_SMALL, TEXT, VG_LEFT);
}

/* ---- SONG: pads 1-8 are the patterns, pads 9-16 the intensity – as on the device -------------------------------------- */
static void draw_song(uint32_t step, uint32_t now, int heat)
{
  char buf[16];
  for (int s = 0; s < SLOTS; s++) {
    int x, y;
    tile_at(s, &x, &y);
    if (s < SFX_PATTERNS) {
      int playing_it = (uint32_t)s == now && step != NO_STEP, mine = s == edit_pat, next = s == queued && BLINK;   /* (what waits for the bar line blinks) */
      uint32_t bg = next ? 0xFFD23F : playing_it ? 0xFFFFFF : mine ? 0x36C5FF : s < SONG.chain || SONG.arr_len ? 0x3A2A22 : 0x1A120E, fg = next || playing_it || mine ? BG : TEXT;
      char letter[2] = { (char)('A' + s), 0 };
      vg_round(x, y, 55, 54, 7, bg);
      vg_text(letter, x + 7, y + 3, VG_BIG, fg, VG_LEFT);
      int div = pat_div(s);
      for (int i = 0; i < SFX_BAR; i++) {            /* what is in it: how many sounds in each sixteenth */
        int n = 0; uint16_t m = 0;
        for (int j = 0; j < div; j++) m |= SONG.pat[s].on[i * div + j];
        for (; m; m &= (uint16_t)(m - 1)) n++;
        if (n) vg_rect(x + 4 + i * 3, y + 50 - (n > 9 ? 20 : 2 + n * 2), 2, n > 9 ? 20 : 2 + n * 2, vg_mix(bg, fg, 0.7f));
      }
      if (div > 1) vg_text(div == 2 ? "32" : "64", x + 51, y + 3, VG_SMALL, vg_mix(bg, fg, 0.8f), VG_RIGHT);   /* a finer grid: its steps a bar */
    } else {
      int lit = heat >= (s - 7) * 127 / 8;
      vg_round(x, y, 55, 54, 7, lit ? 0xFF5A36 : 0x2E1A12);
      vg_text(vg_itoa((s - 7) * 100 / 8, buf), x + 27, y + 18, VG_BIG, lit ? BG : 0x7A4A38, VG_CENTER);
    }
  }
  if (!SONG.arr_len) footer("NO ARRANGEMENT", DIM, "SHIFT + pad 1-8 adds a bar");
  else for (int i = 0; i < SONG.arr_len && i < SFX_ARR_MAX; i++) {     /* the arrangement: a pattern a bar */
    char letter[2] = { (char)('A' + SONG.arr[i] % SFX_PATTERNS), 0 };
    int at = seq_arr == (uint32_t)i, x = 258 + (i % 16) * 13, y = CY + 190 + (i / 16) * 19;
    vg_round(x, y, 12, 17, 3, at ? 0xFFFFFF : SONG.arr[i] == edit_pat ? 0x36C5FF : 0x3A2A22);
    vg_text(letter, x + 6, y + 1, VG_SMALL, at || SONG.arr[i] == edit_pat ? BG : TEXT, VG_CENTER);
  }
}

/* SEQ and AUTO, under the column: the eight patterns – which plays, which is shown, which waits for the bar line, which
   hold something – and a word beside them */
static void strip(uint32_t step, uint32_t now, const char *hint, uint32_t hint_rgb)
{
  for (int k = 0; k < SFX_PATTERNS; k++) {
    char letter[2] = { (char)('A' + k), 0 };
    int x = 262 + k * 13, y = CY + 209, next = k == queued && BLINK, plays = step != NO_STEP && now == (uint32_t)k, mine = k == edit_pat, holds = pattern_holds(k);
    vg_round(x, y, 12, 17, 3, next ? 0xFFD23F : plays ? 0xFFFFFF : mine ? 0x36C5FF : holds ? 0x3A2A22 : 0x1A120E);
    vg_text(letter, x + 6, y + 1, VG_SMALL, next || plays || mine ? BG : holds ? TEXT : 0x6A5248, VG_CENTER);
  }
  vg_text(hint, 376, CY + 209, VG_SMALL, hint_rgb, VG_LEFT);
}

/* ---- LINK: what the game sends ----------------------------------------------------------------------------------- */
static void draw_link(void)
{
  static const char *const IN[6] = { "PITCH", "CUTOFF", "TIMBRE", "LEVEL", "INTENSITY", "PATTERN" };
  char buf[16];
  vg_round(12, CY, 232, CH, 8, PANEL);
  vg_text("FROM THE GAME", 24, CY + 10, VG_BOLD, 0xFFD23F, VG_LEFT);
  for (int k = 0; k < 6; k++) {
    int y = CY + 40 + k * 31;
    vg_text(IN[k], 24, y, VG_SMALL, TEXT, VG_LEFT);
    vg_text(vg_itoa(telemetry[k], buf), 232, y, VG_SMALL, DIM, VG_RIGHT);
    vg_rect(24, y + 17, 208, 5, LINE);
    vg_rect(24, y + 17, telemetry[k] * 208 / 127, 5, 0xFFD23F);
  }
  footer(linked ? "LINKED" : "WAITING FOR A GAME", linked ? 0x6EE7A8 : DIM, link_text[0] ? link_text : "the knobs send CC 20-23 to it");
}

/* the name, text and bar of value i on MIX or MASTER */
static const char *mix_text(int pg, int i, const char **name, char *buf, float *bar, int *centre)
{
  static const char *const N[2][4] = { { "VOLUME", "PAN", "DRIVE", "OUTPUT" }, { "DELAY TIME", "FEEDBACK", "ROOM SIZE", "DAMPING" } };
  static const char *const TIME[SFX_DELAY_TIMES] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
  int hi; const uint8_t *v = mix_value(pg, i, &hi);
  *name = N[pg == PG_MASTER][i]; *centre = 0;
  if (!v) { *bar = vibe_param_get(P_VOLUME); return percent((int)(*bar * 127.0f + 0.5f), buf); }
  *bar = (float)*v / (float)hi;
  if (pg == PG_MASTER && i == 0) { *bar = ((float)(*v % SFX_DELAY_TIMES) + 0.5f) / (float)SFX_DELAY_TIMES; return TIME[*v % SFX_DELAY_TIMES]; }
  if (pg == PG_MIX && i == 0) return vg_itoa(*v, buf);
  if (pg == PG_MIX && i == 1) { *centre = 1; if (*v == 64) return "C"; buf[0] = *v < 64 ? 'L' : 'R'; vg_itoa(*v < 64 ? 64 - *v : *v - 64, buf + 1); return buf; }
  return percent(*v, buf);
}

static int silent(int s) { return (SONG.mute >> s) & 1 || (SONG.solo && !((SONG.solo >> s) & 1)); }

static const char *auto_name(int to, int track)
{
  static const char *const M[5] = { "DELAY TIME", "FEEDBACK", "ROOM SIZE", "DAMPING", "DRIVE" };
  return !to ? "OFF" : to >= SFX_AT_MASTER ? M[to - SFX_AT_MASTER] : to == SFX_AT_PAN ? "PAN" : to == SFX_AT_VOL ? "VOLUME" : param_name(to - SFX_AT_PATCH, kits[KIT_MUSIC][track].engine);
}

static const char *auto_text(int to, int track, int v, char *buf)
{
  static const char *const TIME[SFX_DELAY_TIMES] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
  if (to == SFX_AT_MASTER + SFX_M_DELAY_TIME) return TIME[v % SFX_DELAY_TIMES];
  if (to >= SFX_AT_MASTER) return percent(v, buf);
  if (to == SFX_AT_VOL) return vg_itoa(v, buf);
  if (to == SFX_AT_PAN) { if (v == 64) return "C"; buf[0] = v < 64 ? 'L' : 'R'; vg_itoa(v < 64 ? 64 - v : v - 64, buf + 1); return buf; }
  sfx_patch e = kits[KIT_MUSIC][track];
  e.p[to - SFX_AT_PATCH] = (uint8_t)v;
  return param_text(to - SFX_AT_PATCH, &e, buf);
}

/* ---- AUTO: the selected sound's lane as the 16 pads (16 steps of it) – a point holds until the next one ----------------- */
static void draw_auto(uint32_t rgb, uint32_t step, uint32_t now)
{
  char buf[16];
  int here = now == (uint32_t)edit_pat && step != NO_STEP, to = view_to[sel], hi = auto_hi(to), last = 0;
  int div = pat_div(edit_pat), lim = bar_len() * div, w = view_win();
  for (int g = 0; g < w * PADS && g < lim; g++) if (aut_at(sel, g)) last = aut_at(sel, g);   /* (what holds from the steps before the ones shown) */
  for (int s = 0; s < PADS; s++) {
    int g = w * PADS + s;
    int x, y, a = aut_at(sel, g), off = g >= lim, head = here && head_at(g, div);
    tile_at(s, &x, &y);
    vg_round(x, y, 55, 54, 7, off ? 0x1A120E : head ? 0x563A2E : (s / 4) & 1 ? 0x2A1D17 : 0x35251D);
    if (a) last = a;
    if (to && last && !off) {
      int hgt = 6 + (last - 1 > hi ? hi : last - 1) * 48 / hi;
      vg_round(x, y + 54 - hgt, 55, hgt, 7, a ? rgb : vg_mix(BG, rgb, 0.30f));
      if (a) vg_rect(x + 6, y + 54 - hgt + 3, 43, 3, 0xFFFFFF);
    }
    vg_text(vg_itoa(g + 1, buf), x + 5, y + 2, VG_SMALL, to && last && last - 1 > hi * 3 / 4 ? BG : div > 1 && g % div == 0 ? DIM : 0x6A5248, VG_LEFT);
    if ((pat()->on[g] >> sel) & 1) vg_circle((float)x + 46.0f, (float)y + 9.0f, 3.5f, head ? 0xFFFFFF : TEXT);   /* the sound plays here */
    if (g == cur_step) vg_frame(x - 2, y - 2, 59, 58, 2, 0xFFFFFF);
  }
}

/* ---- MIX: every sound a tile, where its pad is – its fader, its pan under it; muted ones grey ----------------------- */
static void draw_mix(void)
{
  char buf[16];
  for (int s = 0; s < SLOTS; s++) {
    uint32_t c = ENGINE_RGB[patches[s].engine];
    int x, y, off = silent(s), solo = (SONG.solo >> s) & 1, k = 0;
    int room = solo || ((SONG.mute >> s) & 1) ? 4 : 6;                         /* (the S or M in the corner takes two letters' room) */
    const char *n = pad_name(s);
    tile_at(s, &x, &y);
    vg_round(x, y, 55, 54, 7, s == sel ? 0x3E2C23 : PANEL);
    while (k < room && n[k]) { buf[k] = n[k]; k++; }
    buf[k] = 0;
    vg_text(buf, x + 5, y + 3, VG_SMALL, off ? 0x6A5248 : TEXT, VG_LEFT);
    vg_rect(x + 5, y + 26, 45, 7, LINE);                                       /* the fader; the mark: 100, as designed */
    vg_rect(x + 5, y + 26, SONG.vol[s] * 45 / 127, 7, off ? 0x6A5248 : (play_k[KIT_MUSIC] >> s) & 1 ? 0xFFFFFF : c);
    vg_rect(x + 5 + 100 * 45 / 127, y + 23, 1, 13, DIM);
    vg_rect(x + 5, y + 43, 45, 1, LINE);                                       /* the pan */
    vg_rect(x + 4 + SONG.pan[s] * 43 / 127, y + 39, 3, 9, off ? DIM : TEXT);
    if (solo) vg_text("S", x + 50, y + 3, VG_SMALL, 0xFFD23F, VG_RIGHT);
    else if ((SONG.mute >> s) & 1) vg_text("M", x + 50, y + 3, VG_SMALL, 0xFF4040, VG_RIGHT);
    if (s == sel) vg_frame(x - 2, y - 2, 59, 58, 2, 0xFFFFFF);
  }
}

/* ---- MASTER: the delay as its echoes, the reverb as its fading ----------------------------------------------------- */
static void draw_master(void)
{
  static const uint8_t SIX[SFX_DELAY_TIMES] = { 1, 2, 3, 4, 6, 8 };
  float fb = (float)SONG.fx[SFX_M_DELAY_FB] * (0.45f / 64.0f), a = 1.0f, tau = 14.0f + (float)SONG.fx[SFX_M_REVERB_SIZE] * 0.9f;
  int gap = 8 * SIX[SONG.fx[SFX_M_DELAY_TIME] % SFX_DELAY_TIMES], base = CY + CH - 62;
  vg_round(12, CY, 226, CH, 8, PANEL);
  vg_round(242, CY, 226, CH, 8, PANEL);
  vg_text("DELAY", 125, CY + 60, VG_BOLD, 0x36C5FF, VG_CENTER);
  vg_text("REVERB", 355, CY + 60, VG_BOLD, 0xC48BFF, VG_CENTER);
  vg_rect(24, base, 202, 1, LINE);
  for (int k = 0, x = 26; x < 222 && a > 0.02f; k++, x += gap) {                /* the sound, then its echoes: left, right, left … */
    int hgt = (int)(84.0f * a);
    vg_rect(x, base - hgt, 5, hgt, k == 0 ? 0xFFFFFF : (k & 1) ? 0x36C5FF : 0xFFD23F);
    a *= k == 0 ? 0.7f : fb;
  }
  vg_rect(254, base, 202, 1, LINE);
  uint32_t tone = vg_mix(0xE9D6FF, 0x5B3F86, (float)SONG.fx[SFX_M_REVERB_DAMP] / 127.0f);   /* bright .. damped */
  for (int i = 0; i < 200; i += 2) {
    int hgt = (int)(84.0f * vd_exp2(-(float)i / tau));
    if (hgt > 0) vg_rect(255 + i, base - hgt, 2, hgt, vg_mix(tone, 0x5B3F86, (float)i / 200.0f * (float)SONG.fx[SFX_M_REVERB_DAMP] / 127.0f));
  }
}

static void on_draw(uint16_t *fb)
{
  static const char *const SUB[4] = { "OSC", "TONE", "FILTER", "WAVE" };
  char buf[24];
  (void)fb;
  out_pump();
  tour_pump();
  seq_poll(); blink++;
  vg_clear(BG);
  if (help) {
    if (page == PG_SEQ) vg_controls("SFX FORGE: SEQ", HELP_SEQ, sizeof HELP_SEQ / sizeof HELP_SEQ[0] / 2);
    else if (page == PG_SONG) vg_controls("SFX FORGE: SONG", HELP_SONG, sizeof HELP_SONG / sizeof HELP_SONG[0] / 2);
    else if (page == PG_AUTO) vg_controls("SFX FORGE: AUTO", HELP_AUTO, sizeof HELP_AUTO / sizeof HELP_AUTO[0] / 2);
    else if (mix_page()) vg_controls("SFX FORGE: MIX", HELP_MIX, sizeof HELP_MIX / sizeof HELP_MIX[0] / 2);
    else if (page == PG_LINK) vg_controls("SFX FORGE: LINK", HELP_LINK, sizeof HELP_LINK / sizeof HELP_LINK[0] / 2);
    else vg_controls("SFX FORGE: SOUND", HELP_SOUND, sizeof HELP_SOUND / sizeof HELP_SOUND[0] / 2);
    return;
  }
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine], on = play_k[kit], pos = seq_pos, now = seq_pattern;
  dr_read(pos);
  uint32_t step = dr_step;
  int div = pat_div(edit_pat), lim = bar_len() * div, win = view_win();
  int seq = page == PG_SEQ, song = page == PG_SONG, mix = page == PG_MIX, aut = page == PG_AUTO, heat = (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f), wt = p->engine == SFX_ENGINE_WAVETABLE;

  header(rgb, pos, now);

  {                                                   /* where you are: the page's name – and K1-K6 as six marks, with a dot for each page of the key you are on */
    static const char *const TITLE[PG_COUNT] = { "SOUND   OSC", "SOUND   TONE", "SOUND   FILTER", "SOUND   WAVE", "ENVELOPE", "LFO", "EFFECT SENDS",
                                                 "SEQUENCER", "SONG", "AUTOMATION", "MIXER", "MASTER EFFECTS", "GAME LINK" };
    static const uint8_t FIRST[PG_COUNT] = { PG_OSC, PG_OSC, PG_OSC, PG_OSC, PG_ENV, PG_LFO, PG_FX, PG_SEQ, PG_SEQ, PG_SEQ, PG_FX, PG_FX, PG_LINK };
    int tab = TAB_OF[page], pages = tab == TAB_SOUND ? (wt ? 4 : 3) : tab == TAB_FX || tab == TAB_SEQ ? 3 : 1;
    int at = tab == TAB_FX ? (page == PG_FX ? 0 : page == PG_MIX ? 1 : 2) : page - FIRST[page];
    int tw = vg_text(TITLE[page], 12, 36, VG_BOLD, rgb, VG_LEFT);
    if (seq || song || aut) {                         /* … and the pattern; on AUTO the knob the lane moves */
      char t[40] = "PATTERN  ";
      int other = step != NO_STEP && now != (uint32_t)edit_pat;               /* what is shown is not what plays: say both */
      if (other) { strcpy(t, "EDIT B - PLAYS A"); t[5] = (char)('A' + edit_pat); t[15] = (char)('A' + now % SFX_PATTERNS); } else t[8] = (char)('A' + edit_pat);
      strcpy(t + strlen(t), "  "); strcpy(t + strlen(t), GRID_NAME[pat_grid(edit_pat)]);      /* … and its grid: the steps it cuts the bar in */
      tw += vg_text(t, 26 + tw, 36, VG_BOLD, other ? 0xFF8A3C : TEXT, VG_LEFT);
      if (aut) { const char *n = auto_name(view_to[sel], sel); if (40 + tw + vg_text_width(n, VG_BOLD) < LOC_X - 8) vg_text(n, 40 + tw, 36, VG_BOLD, view_to[sel] ? TEXT : DIM, VG_LEFT); }
      else if (seq && !layer_on(sel) && 40 + tw + vg_text_width("layer off", VG_SMALL) < LOC_X - 8) vg_text("layer off", 40 + tw, 36, VG_SMALL, DIM, VG_LEFT);
    }
    for (int i = 0; i < TAB_COUNT; i++) vg_round(LOC_X + i * 18, 37, 12, 9, 2, i == tab ? rgb : LINE);
    for (int i = 0; pages > 1 && i < pages; i++) vg_rect(LOC_X + tab * 18 + 6 - pages * 2 + i * 4, 49, 3, 3, i == at ? TEXT : DIM);
  }

  /* the 16 pads as they are on this page: sounds, the selected sound's steps, or patterns and intensity */
  for (int s = 0; s < SLOTS; s++) {
    int state, frame = 0, g = win * PADS + s, strong = 0;      /* g: the step this pad stands for on SEQ and AUTO */
    uint32_t base;                                   /* state: 0 off, 1 dim, 2 set, 3 bright */
    if (seq && !rec) {                               /* (REC lit: the pads are the kit, and look it – below) */
      int set = (pat()->on[g] >> sel) & 1, head = now == (uint32_t)edit_pat && head_at(g, div);
      base = rgb; state = head ? 3 : set ? 2 : g >= lim ? 0 : 1;
      strong = state == 1 && div > 1 && g % div == 0;                           /* a finer grid: the steps that begin a sixteenth are lit a little more */
    } else if (song) {
      if (s < 8) { base = 0x36C5FF; state = (uint32_t)s == now && step != NO_STEP ? 3 : s == queued ? (BLINK ? 2 : 1) : s == edit_pat ? 2 : s < SONG.chain ? 1 : 0; }
      else { base = 0xFF5A36; state = heat >= (s - 7) * 127 / 8 ? 2 : 1; }
    } else if (aut) {
      base = rgb; state = now == (uint32_t)edit_pat && head_at(g, div) ? 3 : aut_at(sel, g) ? 2 : g >= lim ? 0 : 1;
      strong = state == 1 && div > 1 && g % div == 0;
    } else if (mix) {
      base = ENGINE_RGB[patches[s].engine]; state = silent(s) ? 1 : (on >> s) & 1 ? 3 : 2;
    } else {
      base = ENGINE_RGB[patches[s].engine]; state = (on >> s) & 1 ? 3 : s == sel ? 2 : 1; frame = s == sel;
    }
    (void)frame;                                     /* (the pads themselves show it: their lights) */
    uint32_t led = (base & 0xFFFFFF) | (uint32_t)(state + 4 * (seq + 2 * song + 3 * mix + 4 * aut + 20 * (seq && rec)) + 100 * strong) << 24;
    if (led != led_cache[s]) { led_cache[s] = led; vibe_led_pad(s, state == 3 ? 0xFFFFFF : base, state == 3 ? 1.0f : state == 2 ? 0.75f : state == 1 ? (strong ? 0.30f : 0.12f) : 0.0f); }
  }

  /* the page itself, and the four values over encoders 1-4 */
  vibe_link_page(page <= PG_WAVE ? SUB[page] : song ? "SONG" : aut ? "AUTO" : mix ? "MIX" : page == PG_MASTER ? "MASTER" : TAB_NAME[TAB_OF[page]]);
  for (int i = 0; i < 4; i++) enc_rect[i][2] = 0;
  if (aut) {
    int to = view_to[sel], a = aut_at(sel, cur_step);
    draw_auto(rgb, step, now);
    row(0, "STEP", step_text(buf), (float)(cur_step + 1) / (float)gsteps(), 0, rgb);
    row(1, "VALUE", a && to ? auto_text(to, sel, a - 1, buf) : "--", a && to ? (float)(a - 1) / (float)auto_hi(to) : 0.0f, 0, rgb);
    row(2, "TARGET", auto_name(to, sel), (float)to / (float)(SFX_AT_COUNT - 1), 0, to ? TEXT : DIM);
    if (to && lane_of(sel, to) >= 0) vg_circle(456.0f, (float)(CY + 2 * 47 + 12), 4.0f, 0xFF8A3C);             /* this knob has a lane */
    if (to && div > 1) bar_strip(rgb, step, now, 1);
    else footer(to ? "PADS: A POINT ON / OFF" : "TURN TARGET FIRST", to ? TEXT : 0xFF8A3C, "");
    vg_itoa(lanes_used(), buf); strcpy(buf + strlen(buf), "/24 lanes");
    strip(step, now, buf, DIM);
  } else if (mix) {
    draw_mix();
    for (int i = 0; i < 4; i++) {
      const char *name; float bar; int centre;
      const char *value = mix_text(page, i, &name, buf, &bar, &centre);
      row(i, name, value, bar, centre, i < 2 ? rgb : TEXT);
    }
    footer("PADS: MUTE", TEXT, "SHIFT + pad: solo");
  } else if (page == PG_MASTER) {
    static const uint8_t CORNER[4] = { 0, 2, 1, 3 };  /* the delay's two on the left, the reverb's on the right */
    draw_master();
    for (int i = 0; i < 4; i++) {
      const char *name; float bar; int centre;
      const char *value = mix_text(page, i, &name, buf, &bar, &centre);
      hud(CORNER[i], i, name, value, bar, centre, i < 2 ? 0x36C5FF : 0xC48BFF);
    }
  } else if (page == PG_LINK) {
    draw_link();
    for (int i = 0; i < 4; i++) {
      char n[8] = "CC "; vg_itoa(CC_ENCODERS + i, n + 3);
      row(i, n, vg_itoa(cc_out[i], buf), (float)cc_out[i] / 127.0f, 0, 0x6EE7A8);
    }
  } else if (seq) {
    int note = pat()->note[sel][cur_step], vel = pat()->vel[sel][cur_step] & 7, len = pat()->len[sel][cur_step];
    draw_seq(rgb, step, now);
    row(0, "STEP", step_text(buf), (float)(cur_step + 1) / (float)gsteps(), 0, rgb);
    row(1, "NOTE", note_text(note, buf), note ? (float)(note - 24) / 84.0f : 0.0f, 0, rgb);
    row(2, "VELOCITY", vg_itoa(vel + 1, buf), (float)(vel + 1) / 8.0f, 0, rgb);
    row(3, "LENGTH", len_text(len, buf), (float)len / (float)(gsteps() - 1), 0, rgb);
    if (rec && div > 1) bar_strip(rgb, step, now, 0); /* a take at 32 or 64 steps: the 16 steps shown go with the bar, and the strip says which they are */
    else if (rec) footer(running ? "REC: PLAY IT IN - F4 ERASE" : "REC READY: PRESS PLAY", 0xFF4040, "");   /* played in live: each hit lands on the step it is nearest */
    else if (!layer_on(sel)) footer("ITS LAYER IS OFF NOW", 0xFF8A3C, "");
    else if (div > 1) bar_strip(rgb, step, now, 0);   /* 32 or 64 steps: where the 16 shown are in the bar */
    else footer("PADS: THE 16 STEPS", TEXT, "");
    strip(step, now, rec ? div > 1 ? running ? "REC: F4 ERASE" : "REC: PRESS PLAY" : "REC off: steps" : SCALE_NAME[SONG.scale % SFX_SCALE_COUNT], rec && div > 1 ? 0xFF4040 : DIM);
  } else if (song) {
    char chain[4] = "A-A";
    draw_song(step, now, heat);
    chain[2] = (char)('A' + SONG.chain - 1); if (SONG.chain == 1) chain[1] = 0;
    buf[0] = (char)('A' + edit_pat); buf[1] = 0;
    row(0, "PATTERN", buf, (float)(edit_pat + 1) / (float)SFX_PATTERNS, 0, 0x36C5FF);
    row(1, SONG.arr_len ? "CHAIN (unused)" : "CHAIN", chain, (float)SONG.chain / (float)SFX_PATTERNS, 0, SONG.arr_len ? DIM : 0x36C5FF);
    row(2, "INTENSITY", percent(heat, buf), (float)heat / 127.0f, 0, 0xFF5A36);
    row(3, "LAYER of this sound", percent(SONG.level[sel], buf), (float)SONG.level[sel] / 127.0f, 0, layer_on(sel) ? rgb : DIM);
  } else {
    scene(p, rgb, page);
    for (int i = 0; i < 4; i++) {
      int k = PAGE_PARAMS[page][i];
      if (k == ENGINE_KNOB) hud(i, i, "ENGINE", ENGINE_NAME[p->engine], (float)(p->engine + 1) / (float)SFX_ENGINE_COUNT, 0, rgb);
      else if (k == SFX_P_TABLE) hud(i, i, "TABLE", param_text(k, p, buf), (float)(p->p[k] % SFX_WT_TABLES + 1) / (float)SFX_WT_TABLES, 0, rgb);
      else if (k >= 0) hud(i, i, param_name(k, p->engine), param_text(k, p, buf), (float)p->p[k] / 127.0f, k == SFX_P_PITCH_ENV_AMT || k == SFX_P_FILTER_ENV, rgb);
      else hud(i, i, "", "", -1.0f, 0, rgb);
      if (k >= 0 && kit == KIT_MUSIC && lane_of(sel, SFX_AT_PATCH + k) >= 0)                               /* a lane moves this knob: a mark at its corner */
        vg_circle((i & 1) ? 318.0f : 162.0f, (float)((i >> 1) ? CY + CH - 46 : CY + 12), 4.0f, 0xFF8A3C);
    }
  }

  /* F1-F6 */
  if (tour_on) tour_caption();
  else for (int i = 0; i < 6; i++) {
    int lit = mix_page() && i == 4 && running, held = seq && i == 3 && f4_held;      /* (ERASE, while it erases) */
    int dim = (sound_page() && i == 2 && !has_undo[sel]) || (song_keys() && i == 1 && !undo_has);   /* an UNDO with nothing to take back */
    vg_round(4 + i * 79, 292, 73, 24, 12, held ? 0xFF4040 : lit ? 0x6EE7A8 : 0x36241C);
    vg_text(soft_caption(i), 40 + i * 79, 297, VG_BOLD, held || lit ? BG : dim ? 0x6A5248 : TEXT, VG_CENTER);
  }

  /* FOCUS: everything above with DATA + ENTER (and the mouse) */
  vf_begin();
  for (int i = 0; i < TAB_COUNT; i++) vf_key(VB_K1 + i, LOC_X - 3 + i * 18, 34, 18, 20);
  for (int i = 0; i < 4; i++) if (enc_rect[i][2]) vf_encoder(VE_1 + i, enc_rect[i][0], enc_rect[i][1], enc_rect[i][2], enc_rect[i][3]);
  for (int i = 0; i < 6; i++) vf_key(VB_F1 + i, 4 + i * 79, 292, 73, 24);
  vf_end(0xFFFFFF);
}

static void stage_button(int x, int y, int w, int h, const char *name, int font, uint32_t bg, uint32_t fg)
{
  vg_round(x, y, w, h, 6, bg);
  vg_text(name, x + w / 2, y + (h - vg_font_height(font)) / 2, font, fg, VG_CENTER);
}

static void stage_value(int i, const char *name, const char *value, float bar, int centre, uint32_t rgb)
{
  int x = SP_X + i * SP_PX, bw = SP_W - 16;
  vg_round(x, SC_Y, SP_W, SC_H, 6, stage_drag == SD_PARAM && stage_drag_i == i ? 0x40281E : PANEL);
  vg_text(name, x + SP_W / 2, SC_Y + 3, VG_SMALL, DIM, VG_CENTER);
  vg_text(value, x + SP_W / 2, SC_Y + 18, VG_BIG, TEXT, VG_CENTER);
  vg_rect(x + 8, SC_Y + 45, bw, 6, LINE);
  if (bar < 0.0f) return;
  int at = (int)((float)bw * vd_clamp(bar, 0.0f, 1.0f));
  if (!centre) vg_rect(x + 8, SC_Y + 45, at, 6, rgb);
  else vg_rect(x + 8 + (at < bw / 2 ? at : bw / 2), SC_Y + 45, at < bw / 2 ? bw / 2 - at : at - bw / 2, 6, rgb);
  vg_rect(x + 7 + at, SC_Y + 42, 3, 12, TEXT);
}

static void on_stage(uint16_t *fb, int w, int h)
{
  static const char *const TAB[ST_N] = { "OSC", "TONE", "FILTER", "WAVE", "ENV", "LFO", "FX", "MIX", "MAST", "AUTO" };
  static const char *const FILLS[2][SF_N] = { { "FILL", "CLEAR", "COPY", "UNDO", "DEMO" }, { "RAMP", "CLEAR", "COPY", "UNDO", "DEMO" } };
  char buf[24], t[64];
  (void)fb; (void)w; (void)h;
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine], on = play_k[kit], now = seq_pattern;
  cur_sync(); dr_read(seq_pos);
  uint32_t step = dr_step;
  int heat = (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f), here = now == (uint32_t)edit_pat && step != NO_STEP, wt = p->engine == SFX_ENGINE_WAVETABLE;
  int div = pat_div(edit_pat), cells = gsteps(), lim = bar_len() * div, pitch = SG_PX / div, cw = pitch - 2;   /* the rows: all the pattern's steps in the same width */
  uint16_t used = 0;
  for (int k = 0; k < SFX_PATTERNS; k++) for (int i = 0; i < SFX_STEPS; i++) used |= SONG.pat[k].on[i];
  if (stage_page_follows()) spage = page;             /* the device's page, while it is one of these */
  if (kit == KIT_SFX && spage >= PG_SEQ) spage = PG_OSC;
  int lanes = spage == PG_AUTO;                       /* the AUTO tab: the rows are the sounds' automation lanes */
  if (spage == PG_WAVE && !wt) spage = PG_OSC;
  vg_clear(BG);

  /* the top row */
  { int small = vg_text_width("SFX FORGE", VG_BIG) > TM_X - 28;          /* (what is said now: over the scope, below) */
    vg_text("SFX FORGE", 24, small ? 24 : 21, small ? VG_MED : VG_BIG, TEXT, VG_LEFT); }
  stage_button(TM_X, TB_Y, 58, 36, loop_mode ? "LOOP" : "SONG", VG_BOLD, loop_mode ? 0x36C5FF : 0x36241C, loop_mode ? BG : TEXT);   /* what PLAY plays */
  stage_button(190, TB_Y, 76, 36, running ? "STOP" : "PLAY", VG_BIG, running ? 0x6EE7A8 : 0x36241C, running ? BG : TEXT);
  stage_button(272, TB_Y, 64, 36, "REC", VG_BIG, rec ? 0xFF4040 : 0x36241C, rec ? BG : TEXT);
  vg_round(342, TB_Y, 104, 36, 6, PANEL);             /* the tempo: click its left or right half, or the wheel */
  vg_text("-", 351, TB_Y + 10, VG_BOLD, DIM, VG_LEFT); vg_text("+", 438, TB_Y + 10, VG_BOLD, DIM, VG_RIGHT);
  vg_text(vg_itoa(SONG.bpm, buf), 395, TB_Y + 7, VG_BIG, TEXT, VG_RIGHT);
  vg_text("BPM", 399, TB_Y + 13, VG_SMALL, DIM, VG_LEFT);
  for (int k = 0; k < SFX_PATTERNS; k++) {
    int playing_it = (uint32_t)k == now && step != NO_STEP, mine = k == edit_pat, next = k == queued && BLINK;
    char letter[2] = { (char)('A' + k), 0 };
    stage_button(TP_X + k * TP_PX, TB_Y, 36, 20, letter, VG_BOLD, next ? 0xFFD23F : playing_it ? 0xFFFFFF : mine ? 0x36C5FF : k < SONG.chain ? 0x523A1F : PANEL, next || playing_it || mine ? BG : k < SONG.chain ? TEXT : DIM);
  }
  if (!SONG.arr_len) vg_text("right-click a pattern: a bar of the arrangement", TP_X, TA_Y, VG_SMALL, 0x6A5248, VG_LEFT);
  else for (int i = 0; i < SONG.arr_len && i < SFX_ARR_MAX; i++) {     /* the arrangement: a pattern a bar */
    char letter[2] = { (char)('A' + SONG.arr[i] % SFX_PATTERNS), 0 };
    int at = seq_arr == (uint32_t)i;
    vg_round(TP_X + i * TA_PX, TA_Y, 9, 15, 2, at ? 0xFFFFFF : SONG.arr[i] == edit_pat ? 0x36C5FF : 0x523A1F);
    vg_text(letter, TP_X + i * TA_PX + 4, TA_Y, VG_SMALL, at || SONG.arr[i] == edit_pat ? BG : TEXT, VG_CENTER);
  }
  vg_circle(800.0f, 32.0f, 5.0f, linked ? 0x6EE7A8 : 0x48332A);
  vg_text("LINK", 810, 25, VG_SMALL, linked ? 0x6EE7A8 : 0x6A5248, VG_LEFT);
  strcpy(t, "GRID "); strcpy(t + 5, GRID_NAME[pat_grid(edit_pat)]);      /* the grid of the pattern shown */
  stage_button(GC_X, GC_Y, 62, 14, t, VG_SMALL, div > 1 ? 0x36C5FF : 0x36241C, div > 1 ? BG : TEXT);
  stage_button(856, TB_Y, 80, 36, "TOUR", VG_BIG, tour_on ? 0x36C5FF : 0x36241C, tour_on ? BG : TEXT);

  /* the pads */
  for (int s = 0; s < SLOTS; s++) {
    int x = SP_X + (s % 4) * SP_PX, y = SP_Y + (s / 4) * SP_PY, lit = (on >> s) & 1;
    uint32_t c = ENGINE_RGB[patches[s].engine];
    vg_round(x, y, SP_W, SP_H, 7, lit ? 0xFFFFFF : s == sel ? c : vg_mix(BG, c, 0.32f));
    vg_text(vg_itoa(s + 1, buf), x + 8, y + 5, VG_BOLD, lit || s == sel ? BG : vg_mix(BG, c, 0.9f), VG_LEFT);
    vg_text(pad_name(s), x + SP_W / 2, y + 29, VG_MED, lit || s == sel ? BG : TEXT, VG_CENTER);
  }

  /* the selected sound: its name, its picture, its pages and their four values, its keys */
  vg_round(SP_X, 304, 34, 24, 5, rgb);
  vg_text(vg_itoa(sel + 1, buf), SP_X + 17, 309, VG_BOLD, BG, VG_CENTER);
  int nw = vg_text(pad_name(sel), SP_X + 44, 305, VG_BIG, TEXT, VG_LEFT);
  vg_text(ENGINE_NAME[p->engine], SP_X + 54 + nw, 311, VG_SMALL, rgb, VG_LEFT);
  stage_button(SP_X + 312, 304, 54, 24, "SFX", VG_BOLD, kit == KIT_SFX ? TEXT : PANEL, kit == KIT_SFX ? BG : DIM);          /* the kit the pads show */
  stage_button(SP_X + 370, 304, 56, 24, "MUSIC", VG_BOLD, kit == KIT_MUSIC ? TEXT : PANEL, kit == KIT_MUSIC ? BG : DIM);
  preview(SP_X, 334, 4 * SP_PX - 6, 64, p, rgb, spage);
  for (int i = 0; i < ST_N; i++) {
    int there = ST_PAGE[i] == spage, none = ST_PAGE[i] == PG_WAVE && !wt;
    vg_round(SP_X + i * ST_PX, ST_Y, 41, 20, 5, there ? rgb : PANEL);
    vg_text(TAB[i], SP_X + i * ST_PX + 20, ST_Y + 3, VG_SMALL, there ? BG : none ? 0x523C33 : DIM, VG_CENTER);
  }
  for (int i = 0; i < 4; i++) {
    int k = spage < PG_SEQ ? PAGE_PARAMS[spage][i] : -1;
    if (spage == PG_AUTO) {
      int to = view_to[sel], a = aut_at(sel, cur_step);
      if (i == 0) stage_value(i, "STEP", step_text(buf), ((float)cur_step + 0.5f) / (float)cells, 0, rgb);
      else if (i == 1) stage_value(i, "VALUE", a && to ? auto_text(to, sel, a - 1, buf) : "--", a && to ? (float)(a - 1) / (float)auto_hi(to) : 0.0f, 0, rgb);
      else if (i == 2) stage_value(i, "TARGET", auto_name(to, sel), ((float)to + 0.5f) / (float)SFX_AT_COUNT, 0, rgb);
      else stage_value(i, "", "", -1.0f, 0, rgb);
    } else if (spage >= PG_SEQ) {
      const char *name; float bar; int centre;
      const char *value = mix_text(spage, i, &name, buf, &bar, &centre);
      stage_value(i, name, value, bar, centre, rgb);
    } else if (k == ENGINE_KNOB) stage_value(i, "ENGINE", ENGINE_SHORT[p->engine], ((float)p->engine + 0.5f) / (float)SFX_ENGINE_COUNT, 0, rgb);
    else if (k == SFX_P_TABLE) stage_value(i, "TABLE", param_text(k, p, buf), ((float)(p->p[k] % SFX_WT_TABLES) + 0.5f) / (float)SFX_WT_TABLES, 0, rgb);
    else if (k >= 0) stage_value(i, param_name(k, p->engine), param_text(k, p, buf), (float)p->p[k] / 127.0f, k == SFX_P_PITCH_ENV_AMT || k == SFX_P_FILTER_ENV, rgb);
    else stage_value(i, "", "", -1.0f, 0, rgb);
  }
  for (int i = 0; i < 6; i++)
    stage_button(SP_X + i * SA_PX, SA_Y, 68, 32, SOFT[0][i], VG_BOLD, 0x36241C, i == 2 && !has_undo[sel] ? 0x6A5248 : TEXT);

  /* the song: a row a sound – dim while its layer is silent */
  for (int s = 0; s < SLOTS; s++) {
    uint32_t c = ENGINE_RGB[kits[KIT_MUSIC][s].engine];
    int y = SG_Y + s * SG_PY, layer = heat >= SONG.level[s];
    if (s == sel) vg_round(SN_X, y, SG_X - SN_X - 6, 20, 4, c);
    if ((play_k[KIT_MUSIC] >> s) & 1) vg_rect(SN_X - 7, y + 2, 3, 16, 0xFFFFFF);
    vg_text(lanes && view_to[s] ? auto_name(view_to[s], s) : name_of(KIT_MUSIC, s), SN_X + 5, y + 3, VG_SMALL, s == sel ? BG : silent(s) ? 0x58433A : lanes ? (view_to[s] ? TEXT : vg_mix(BG, c, 0.55f)) : layer && ((used >> s) & 1) ? c : vg_mix(BG, c, 0.55f), VG_LEFT);
    if (silent(s)) { vg_rect(SN_X + 3, y + 10, SG_X - SN_X - 12, 1, s == sel ? BG : 0x7A6055); layer = 0; }      /* muted: struck out, its steps dim */
    if (lanes) {                                      /* its lane: a point holds until the next one; the dots are its steps */
      int to = view_to[s], hi = auto_hi(to), last = 0;
      for (int i = 0; i < cells; i++) {
        int x = SG_X + i * pitch, a = aut_at(s, i), head = here && head_at(i, div), off = i >= lim;
        uint32_t cell = head ? 0x5F4134 : off ? 0x1C130F : s == sel ? 0x503328 : (i / (4 * div)) & 1 ? 0x2B1D17 : 0x38261E;
        if (div == 1) vg_round(x, y, cw, 20, 3, cell); else vg_rect(x, y, cw, 20, cell);
        if (a) last = a;
        if (to && last && !off) {
          int hgt = 2 + (last - 1 > hi ? hi : last - 1) * 18 / hi;
          vg_rect(x, y + 20 - hgt, cw, hgt, a ? c : vg_mix(BG, c, 0.40f));
          if (a) vg_rect(x, y + 20 - hgt, cw, 2, 0xFFFFFF);
        }
        if ((pat()->on[i] >> s) & 1) vg_rect(x + (div == 1 ? 9 : cw / 2 - 1), y + 1, div == 1 ? 4 : 2, 3, to && last ? BG : vg_mix(BG, c, 0.7f));
        if (s == sel && i == cur_step) vg_frame(x - 2, y - 2, cw + 4, 24, 2, 0xFFFFFF);
      }
      continue;
    }
    for (int i = 0; i < cells; i++) {
      int x = SG_X + i * pitch, set = (pat()->on[i] >> s) & 1, head = here && head_at(i, div), off = i >= lim, note = pat()->note[s][i];
      uint32_t cell = set ? (head && layer ? 0xFFFFFF : layer ? c : vg_mix(BG, c, 0.35f)) : head ? 0x5F4134 : off ? 0x1C130F : (i / (4 * div)) & 1 ? 0x2B1D17 : 0x38261E;
      int hgt = 6 + (pat()->vel[s][i] & 7) * 2;
      if (div == 1) {
        if (!set) vg_round(x, y, cw, 20, 3, s == sel && !head && !off ? vg_mix(cell, 0xFFFFFF, 0.08f) : cell);
        else { vg_round(x, y, cw, 20, 3, vg_mix(BG, cell, 0.30f)); vg_round(x, y + 20 - hgt, cw, hgt, 3, cell); }   /* as high as it is hard */
        if (set && note) vg_text(note_text(note, buf), x + 11, y + 3, VG_SMALL, layer ? BG : TEXT, VG_CENTER);      /* (the notes are written at 16 steps only: a finer cell has no room) */
      } else {
        if (!set) vg_rect(x, y, cw, 20, s == sel && !head && !off ? vg_mix(cell, 0xFFFFFF, 0.08f) : cell);
        else { vg_rect(x, y, cw, 20, vg_mix(BG, cell, 0.30f)); vg_rect(x, y + 20 - hgt, cw, hgt, cell); }
      }
    }
    for (int i = 0; i < cells; i++) {                 /* a step with a length: a line as far as it holds */
      int len = pat()->len[s][i];
      if (((pat()->on[i] >> s) & 1) && len > 1) vg_rect(SG_X + i * pitch + cw, y + 16, (i + len > cells ? cells - i : len) * pitch - pitch, 3, layer ? 0xFFFFFF : 0x7A6055);
    }
    if (s == sel && cur_step < cells) vg_frame(SG_X + cur_step * pitch - 2, y - 2, cw + 4, 24, 2, 0xFFFFFF);
  }

  /* INTENSITY with every sound's mark over it, and the selected sound's own mark: its LAYER */
  vg_text("INTENSITY", SN_X + 5, SI_Y, VG_SMALL, 0xFF5A36, VG_LEFT);
  vg_round(SG_X, SI_Y, SG_W - 2, 16, 5, PANEL);
  vg_round(SG_X, SI_Y, 6 + (SG_W - 8) * heat / 127, 16, 5, 0xFF5A36);
  for (int s = 0; s < SLOTS; s++) if ((used >> s) & 1) vg_rect(SG_X + 2 + (SG_W - 8) * SONG.level[s] / 127, SI_Y - 6, 3, 5, heat >= SONG.level[s] ? ENGINE_RGB[kits[KIT_MUSIC][s].engine] : vg_mix(BG, ENGINE_RGB[kits[KIT_MUSIC][s].engine], 0.45f));
  vg_text(percent(heat, buf), SG_X + SG_W - 10, SI_Y + 1, VG_SMALL, TEXT, VG_RIGHT);
  vg_text("LAYER", SN_X + 5, SL_Y, VG_SMALL, DIM, VG_LEFT);
  vg_round(SG_X, SL_Y, SG_W - 2, 16, 5, PANEL);
  vg_round(SG_X, SL_Y, 6 + (SG_W - 8) * SONG.level[sel] / 127, 16, 5, vg_mix(BG, rgb, layer_on(sel) ? 0.75f : 0.35f));
  strcpy(t, name_of(KIT_MUSIC, sel)); strcpy(t + strlen(t), " plays from "); strcpy(t + strlen(t), percent(SONG.level[sel], buf));
  vg_text(t, SG_X + SG_W - 10, SL_Y + 1, VG_SMALL, TEXT, VG_RIGHT);

  /* under it: what plays now, and the pattern's keys – or the tour's words */
  if (tour_on) {
    vg_round(SN_X, SF_Y, 470, 52, 6, 0x36241C);
    vg_text(tour_title, SN_X + 12, SF_Y + 4, VG_BIG, 0x36C5FF, VG_LEFT);
    vg_text(tour_line, SN_X + 12, SF_Y + 30, VG_MED, TEXT, VG_LEFT);
    vg_rect(SN_X, SF_Y + 50, (int)(470.0f * vd_clamp(tour_progress, 0.0f, 1.0f)), 2, 0x36C5FF);
  } else {
    scope(SN_X, SF_Y, 238, 52, rgb);
    if (toast_on()) {                                 /* what was just done, or asked: beside the keys that did it */
      vg_rect_a(SN_X + 2, SF_Y + 14, 234, 24, BG, 0.82f);
      vg_text(toast, SN_X + 119, SF_Y + 18, VG_MED, TEXT, VG_CENTER);
    }
    for (int k = 0; k < SF_N; k++) stage_button(SF_X + k * SF_PX, SF_Y, SF_PX - 4, 30, FILLS[lanes][k], VG_BOLD, 0x36241C, k == 3 && !undo_has ? 0x6A5248 : TEXT);
    if (lanes) vg_text("drag in a lane: points   right-click: none", SF_X + SF_N * SF_PX - 4, SF_Y + 35, VG_SMALL, 0x7A6055, VG_RIGHT);
    else {
    strcpy(t, "wheel: note "); strcpy(t + strlen(t), note_text(pat()->note[sel][cur_step], buf)); strcpy(t + strlen(t), "  SHIFT: vel  CTRL: len");
    vg_text(t, SF_X + SF_N * SF_PX - 4, SF_Y + 35, VG_SMALL, 0x7A6055, VG_RIGHT);
    }
  }
}

/* ---- what the host calls in the control flow. After each of them the song as it is now goes on its way to the player:
   whatever was done to it - one step or all eight patterns - the audio takes whole, with its next block. ------------- */
void vibe_on_event(const vibe_event *e) { on_event(e); song_hand(); }
void vibe_on_draw(uint16_t *fb) { on_draw(fb); song_hand(); }
void vibe_on_stage(uint16_t *fb, int w, int h) { on_stage(fb, w, h); song_hand(); }
