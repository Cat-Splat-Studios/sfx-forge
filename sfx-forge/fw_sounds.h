/* fw_sounds.h – what the device can sound like before anybody edits it. A part of sfx-forge.c.
 *
 * Holds:  the parameters a DAW automates (PARAMS: six of its own, then the mixer's 65); the named sounds (PRESETS:
 *         16 for the SFX kit, 23 for the MUSIC kit); the names and colours of the five engines; the names of the
 *         scales.
 * Needs:  core/sfx_core.h (SFX_P_COUNT, the engines), vibe.h (vibe_param).
 * Read:   core/sfx_core.h says what each of a sound's 25 values does, in the order of the columns here.
 */

/* ---- parameters the DAW automates: the same a game moves over MIDI --------------------------------------------- */
enum {
  P_VOLUME,
  P_MOD_PITCH,
  P_MOD_CUTOFF,
  P_MOD_FM,
  P_MOD_LEVEL,
  P_INTENSITY,
  /* the mixer, as the SDK's convention names it (SDK 11): four for each of the song's 16 sounds, in the order of
     MIX_..., then the selected sound. A mixer controller and the DAW work them: fw_pages.h, mix_report */
  P_MIX,
  P_MIX_SELECT = P_MIX + 4 * 16,
  P_COUNT
};
/* the four of a sound: P_MIX + 4 * sound + one of these */
enum {
  MIX_LEVEL,
  MIX_PAN,
  MIX_MUTE,
  MIX_SOLO
};

// clang-format off
static const vibe_param PARAMS[P_COUNT] = {
  /* id            name          min    max    start  unit  steps, choices */
  { "volume",     "Volume",      0.0f,  1.0f,  0.8f,  "",    0, 0 },
  { "mod_pitch",  "Mod Pitch",   0.0f, 24.0f,  0.0f,  "st",  0, 0 },
  { "mod_cutoff", "Mod Cutoff", -4.0f,  4.0f,  0.0f,  "oct", 0, 0 },
  { "mod_fm",     "Mod Timbre",  0.0f,  1.0f,  0.0f,  "",    0, 0 },
  { "mod_level",  "Mod Level",   0.0f,  1.0f,  1.0f,  "",    0, 0 },
  { "intensity",  "Intensity",   0.0f,  1.0f,  1.0f,  "",    0, 0 },
  /* the mixer. The ids are the convention's and must stay as they are; a fader starts where a new song has it (100 of
     127), a pan in the middle. There is no mix_<n>_arm: REC is one key for the song, not one a sound */
#define MIX_TRACK(n)                                                              \
  { "mix_" #n "_level", "Level " #n,   0.0f, 1.0f, 100.0f / 127.0f, "", 0, 0 },    \
  { "mix_" #n "_pan",   "Pan " #n,    -1.0f, 1.0f, 0.0f,            "", 0, 0 },    \
  { "mix_" #n "_mute",  "Mute " #n,    0.0f, 1.0f, 0.0f,            "", 2, 0 },    \
  { "mix_" #n "_solo",  "Solo " #n,    0.0f, 1.0f, 0.0f,            "", 2, 0 },
  MIX_TRACK(1)  MIX_TRACK(2)  MIX_TRACK(3)  MIX_TRACK(4)  MIX_TRACK(5)  MIX_TRACK(6)  MIX_TRACK(7)  MIX_TRACK(8)
  MIX_TRACK(9)  MIX_TRACK(10) MIX_TRACK(11) MIX_TRACK(12) MIX_TRACK(13) MIX_TRACK(14) MIX_TRACK(15) MIX_TRACK(16)
#undef MIX_TRACK
  { "mix_select", "Selected sound", 1.0f, 16.0f, 1.0f, "", 16, 0 },
};

/* the group each of them is listed under in the host */
static const char *const GROUPS[P_COUNT] = {
  "OUTPUT", "TELEMETRY", "TELEMETRY", "TELEMETRY", "TELEMETRY", "MUSIC",
#define MIX_GROUP(n) "MIX " #n, "MIX " #n, "MIX " #n, "MIX " #n,
  MIX_GROUP(1)  MIX_GROUP(2)  MIX_GROUP(3)  MIX_GROUP(4)  MIX_GROUP(5)  MIX_GROUP(6)  MIX_GROUP(7)  MIX_GROUP(8)
  MIX_GROUP(9)  MIX_GROUP(10) MIX_GROUP(11) MIX_GROUP(12) MIX_GROUP(13) MIX_GROUP(14) MIX_GROUP(15) MIX_GROUP(16)
#undef MIX_GROUP
  "MIX",
};
// clang-format on

/* ---- the sounds it knows --------------------------------------------------------------------------------------- */
/* A row: name, engine (0 IMPACT, 1 UI BLIP, 2 SCI-FI FM, 3 FOLEY, 4 WAVETABLE), then the sound's 25 values in the
 * order of core/sfx_core.h (SFX_P_...). The numbers over the columns are these:
 *
 *    0 FREQ      1 P.ENV      2 P.TIME    3 A        4 B        5 NOISE    6 CUTOFF   7 RESO    8 DECAY   9 CRUSH
 *   10 ATTACK   11 SUSTAIN   12 RELEASE  13 F.ENV   14 F.TIME
 *   15 LFO RATE 16 LFO>PITCH 17 LFO>FILTER 18 LFO>TIMBRE
 *   19 SPREAD   20 CHORUS    21 DELAY    22 REVERB  23 LEVEL   24 TABLE
 *
 * (A and B are the engine's two own knobs: FM ratio and depth, the arpeggio's step and speed, the grain's rate and
 * amount, the wavetable's position and warp - fw_draw_parts.h, param_name.)
 * The first 16 are on the pads of the SFX kit at the start, the next 16 on those of the MUSIC kit; F1 PRESET puts any
 * of a kit's own on the selected pad. The table is wider than 120 columns: a sound a row reads better than a wrapped
 * one.
 */
typedef struct {
  const char *name;
  uint8_t engine;
  uint8_t p[SFX_P_COUNT];
} preset;

// clang-format off
static const preset PRESETS[] = {
  /* name   engine     0    1   2    3    4    5    6   7    8   9    10   11  12   13  14    15  16  17   18    19  20  21  22   23 24 */
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
// clang-format on

#define SFX_PRESETS  16 /* the first 16: the SFX kit's. The rest: the MUSIC kit's */
#define PRESET_COUNT ((int)(sizeof PRESETS / sizeof PRESETS[0]))

/* ---- the engines and the scales, as the display names them ------------------------------------------------------ */
static const char *const ENGINE_NAME[SFX_ENGINE_COUNT] = {"IMPACT", "UI BLIP", "SCI-FI FM", "FOLEY", "WAVETABLE"};
static const char *const ENGINE_SHORT[SFX_ENGINE_COUNT] = {"IMPACT", "BLIP", "FM", "FOLEY", "WAVE"};
static const uint32_t ENGINE_RGB[SFX_ENGINE_COUNT] = {0xFF5A36, 0xFFD23F, 0x36C5FF, 0x7BE07B, 0xC48BFF};
static const char *const SCALE_NAME[SFX_SCALE_COUNT] = {"CHROMATIC", "MAJOR", "MINOR", "PENTATONIC"};
