/* fw_state.h – every variable the parts of the firmware share, and the smallest helpers. A part of sfx-forge.c.
 *
 * Holds, in this order:
 *   1  the audio flow's own: the player, and what it shows the controls (the step that plays, what sounds, the scope,
 *      the metronome)
 *   2  the song as the controls have it (SONG) and its way to the player (WAY)
 *   3  the pages and the K keys they hang on; what encoders 1-4 turn on each sound page (PAGE_PARAMS)
 *   4  the two kits and the selected one
 *   5  the control flow's own: what is selected, the sequencer's view, undo, the tour, the link's transfers
 *   6  F1-F6: their captions on each page (SOFT, soft_caption, relabel)
 *      (PERFORM's pad mode, perf_mode, stands at the end of 5)
 * Needs:  fw_protocol.h (SLOTS, the song's pieces), core/sfx_song.h, song_way.h.
 * Read:   the rule of the two flows first (sfx-forge.c). A variable here is written by ONE flow. What the audio flow
 *         writes for the controls to read is a single 32-bit value each, marked volatile; everything else the audio
 *         never touches but metro, rec_on and beat_len, which it only reads. The order of the definitions is the
 *         order of the data in the built .wasm: a new variable goes at the end of its group (beat_len stands with
 *         metro and rec_on: the values the controls write for the audio), and moving one changes the binary.
 */

/* ---- 1. the audio flow's own ----------------------------------------------------------------------------------- */

/* the player, with the song it plays (P.song: the controls never touch it) */
static sfx_player P;

/* ---- 2. the song, as the controls have it ---------------------------------------------------------------------- */
/* Every edit is made here, and this is what is shown, kept and sent. The audio never looks at it. WAY takes it to the
   player (song_way.h): song_hand when it may have changed, song_way_take at the start of a block. Two songs more than
   the one that plays: 2 x 38 KB. */
static sfx_song song_ed;
#define SONG song_ed
static song_way WAY;

/* Hand the song on. If the audio is taking the one before at this moment, nothing is handed on now: the next event or
   draw calls this again and hands it on then. */
static void song_hand(void) {
  song_way_hand(&WAY, &SONG);
}

/* ---- 1 again: the audio flow's own ----------------------------------------------------------------------------- */

/* the sample rate */
static float SR;
/* the block the player renders into, left and right */
static float mixl[VIBE_MAX_BLOCK];
static float mixr[VIBE_MAX_BLOCK];
/* the telemetry as it glides (pitch, cutoff, timbre, level) */
static sfx_mods mods = {0, 0, 0, 1};
/* the output volume as it glides */
static float vol_s;

/* What the audio shows the controls: each a 32-bit value only the audio writes. */
#define NO_STEP 0xFFFFFFFFu
#define SCOPE   164 /* the columns of the scope */

/* the bar of the arrangement that plays (NO_STEP: none) */
static volatile uint32_t seq_arr;
/* the step that plays, in sixteenths (written by the audio; nothing reads it at present: seq_pos is what is read) */
static volatile uint32_t seq_step = NO_STEP;
/* the pattern that plays */
static volatile uint32_t seq_pattern;
/* what sounds now, a bit a sound, for each kit */
static volatile uint32_t play_k[2];
/* where the song is, in one word (so that step, grid and fraction are read together): div << 28 | step << 20 | fraction
   of the step in 20 bits. step counts in the grid of the pattern that plays, div is its steps a sixteenth. */
static volatile uint32_t seq_pos = NO_STEP;

static int pos_step(uint32_t w) {
  return (int)(w >> 20 & 0xFF);
}

static int pos_div(uint32_t w) {
  int d = (int)(w >> 28);
  return d == 2 || d == 4 ? d : 1;
}

static float pos_frac(uint32_t w) {
  return (float)(w & 0xFFFFFu) * (1.0f / 1048575.0f);
}

/* the scope: the lowest and the highest sample of every 48 frames, in a ring */
static volatile float scope_lo[SCOPE];
static volatile float scope_hi[SCOPE];
/* how many columns of it were written, counted on and on */
static volatile uint32_t scope_w;
/* (the 48 frames being gathered: their lowest, their highest, how many) */
static float sc_lo;
static float sc_hi;
static int sc_n;

/* the metronome: a click on every beat while REC is on and the song runs – higher on the bar line. The firmware's own:
   it is not part of the song, and a game never hears it. No count-in: REC loops the pattern, the first pass is one.

   These are the values the CONTROLS write and the audio only reads:
   metro: the metronome is on (SHIFT + REC). rec_on: the audio's copy of rec (REC). beat_len: under them. */
static volatile int metro = 1;
static volatile int rec_on;
/* and a third: the beat, in sixteenths - 2 (an eighth), 3, 4 (a quarter) or 6 (a dotted quarter). SHIFT + B on SONG
   and PERFORM. The click falls on it; the display shades the bar by it and names the bar's length by it (meter_text);
   FILL begins with it. It is the device's own, as the click is: it is not in the song, a game never gets it, F2 does
   not take it back, and a new song, the demo song or a song from a game leave it as it is - only the factory reset
   puts the quarter back. It is kept in meter.bin. (Swing holds back every other sixteenth: with a beat of three
   sixteenths every second click is on such a one and comes late, with the notes of its step.) */
static volatile int beat_len = 4;
/* the click that sounds: its level, phase and frequency */
static float click_env;
static float click_ph;
static float click_f;
/* the sixteenth a click was last decided for */
static int click_step = -1;

/* ---- 3. the pages -------------------------------------------------------------------------------------------------
   A page is what the display shows and what encoders 1-4 and F1-F6 do. The first seven are the sound's pages (a row of
   PAGE_PARAMS each); K1-K6 are six tabs, and a tab pressed again goes to its next page.

   The ORDER of this list is used by tests on the page number:
     page < PG_SEQ          a sound page (sound_page(), PAGE_PARAMS)
     PG_SEQ .. PG_MASTER    the song's pages: they mean the MUSIC kit and show LOOP / SONG in the top line
                            (set_page and arrow in fw_pages.h, header in fw_draw_pages.h)
     >= PG_SEQ, the stage   every such tab but AUTO is taken for MIX or MASTER (fw_stage_draw.h, fw_stage_input.h)
   A new page goes after PG_LINK unless it is one of these. docs/MAINTAINING.md, "Add a page", lists every table that
   has an entry a page. */
enum {
  PG_OSC,     /* K1 SOUND: engine, frequency, the pitch envelope */
  PG_TIMBRE,  /* K1 again ("TONE"): the engine's two knobs A and B, noise, crush */
  PG_FILTER,  /* K1 again: cutoff, resonance, the filter envelope */
  PG_WAVE,    /* K1 again, for a WAVETABLE sound only: table, spread */
  PG_ENV,     /* K2: attack, decay, sustain, release */
  PG_LFO,     /* K3: rate, and how far it moves pitch, filter and timbre */
  PG_FX,      /* K4: the sends to chorus, delay and reverb; the level */
  PG_SEQ,     /* K5: the steps of the selected sound in the pattern (from here on: not a sound page) */
  PG_SONG,    /* K5 again: patterns, intensity, the arrangement */
  PG_AUTO,    /* K5 again: the selected sound's automation lane */
  PG_PERFORM, /* K5 again: the whole pattern as a grid, a row a sound; F1-F4 say what the pads are */
  PG_MIX,     /* K4 again: faders, pans, mute and solo */
  PG_MASTER,  /* K4 again: the delay and the reverb */
  PG_LINK,    /* K6: what the game sends, and the encoders as CCs to it */
  PG_COUNT
};

enum {
  TAB_SOUND,
  TAB_ENV,
  TAB_LFO,
  TAB_FX,
  TAB_SEQ,
  TAB_LINK,
  TAB_COUNT
};

// clang-format off
/* the tab (K key) each page hangs on, in the order of the pages. (No size is written, so that the assert under it
   sees a page without a tab.) */
static const uint8_t TAB_OF[] = {
  TAB_SOUND, TAB_SOUND, TAB_SOUND, TAB_SOUND,   /* OSC, TIMBRE, FILTER, WAVE */
  TAB_ENV, TAB_LFO, TAB_FX,                     /* ENV, LFO, FX */
  TAB_SEQ, TAB_SEQ, TAB_SEQ, TAB_SEQ,           /* SEQ, SONG, AUTO, PERFORM */
  TAB_FX, TAB_FX,                               /* MIX, MASTER */
  TAB_LINK,                                     /* LINK */
};
// clang-format on
_Static_assert(sizeof TAB_OF / sizeof TAB_OF[0] == PG_COUNT, "a tab for every page");

/* the page a tab opens on */
static const uint8_t TAB_FIRST[TAB_COUNT] = {PG_OSC, PG_ENV, PG_LFO, PG_FX, PG_SEQ, PG_LINK};
static const char *const TAB_NAME[TAB_COUNT] = {"SOUND", "ENV", "LFO", "FX", "SEQ", "LINK"};

/* (in PAGE_PARAMS: not one of the sound's 25 values but its engine; -1: no value there) */
#define ENGINE_KNOB (-2)

// clang-format off
/* what encoders 1-4 turn on each sound page */
static const int8_t PAGE_PARAMS[PG_SEQ][4] = {
  /* OSC    */ { ENGINE_KNOB,         SFX_P_BASE_FREQ,  SFX_P_PITCH_ENV_AMT, SFX_P_PITCH_ENV_DEC  },
  /* TIMBRE */ { SFX_P_FM_RATIO,      SFX_P_FM_DEPTH,   SFX_P_NOISE_MIX,     SFX_P_BITCRUSH       },
  /* FILTER */ { SFX_P_FILTER_CUTOFF, SFX_P_FILTER_RES, SFX_P_FILTER_ENV,    SFX_P_FILTER_ENV_DEC },
  /* WAVE   */ { SFX_P_TABLE,         SFX_P_SPREAD,     -1,                  -1                   },
  /* ENV    */ { SFX_P_ATTACK,        SFX_P_AMP_DECAY,  SFX_P_SUSTAIN,       SFX_P_RELEASE        },
  /* LFO    */ { SFX_P_LFO_RATE,      SFX_P_LFO_PITCH,  SFX_P_LFO_FILTER,    SFX_P_LFO_TIMBRE     },
  /* FX     */ { SFX_P_CHORUS,        SFX_P_DELAY,      SFX_P_REVERB,        SFX_P_LEVEL          },
};
// clang-format on

/* ---- 4. the kits ----------------------------------------------------------------------------------------------- */
/* two kits of 16 sounds: MUSIC – the instruments the song plays, through the mixer – and SFX – the game's sound
   effects, beside the song. The pads, the keys and the sound pages work on the kit that is chosen; the song pages mean
   MUSIC. */
enum {
  KIT_MUSIC,
  KIT_SFX
};
static const char *const KIT_NAME[2] = {"MUSIC", "SFX"};

/* the sounds of both kits */
static sfx_patch kits[2][SLOTS];
/* for each of them the sound it was before the last change (F3 UNDO on the sound pages), and whether there is one */
static sfx_patch undo_k[2][SLOTS];
static uint8_t has_undo_k[2][SLOTS];
/* which named sound a pad holds, -1 = its own; and which it held before the last change */
static int8_t pad_preset_k[2][SLOTS];
static int8_t undo_preset_k[2][SLOTS];
/* the kit that is chosen */
static int kit = KIT_SFX;

/* The same five, of the kit that is CHOSEN: patches[3] is kits[kit][3]. They look like variables and are not: code
   that must reach the other kit sets kit first and puts it back (factory_reset, sysex_in and factory do).
   The sound_undo names are the SOUND's undo (F3 on a sound page); the song's undo is undo_song, undo_has, ... below. */
#define patches           kits[kit]
#define sound_undo        undo_k[kit]
#define sound_has_undo    has_undo_k[kit]
#define pad_preset        pad_preset_k[kit]
#define sound_undo_preset undo_preset_k[kit]

/* ---- 5. the control flow's own --------------------------------------------------------------------------------- */

/* the selected sound (0-15) */
static int sel;
/* the page that is open: PG_... */
static int page;
/* HOME's text is shown */
static int help;
/* SHIFT is held */
static int shift;
/* REC is lit */
static int rec;
/* the song plays (as the controls asked: the audio may be a block behind) */
static int running;
/* what PLAY plays: 1 LOOP (the pattern repeats), 0 SONG (the arrangement, else the chain) */
static int loop_mode;
/* the pattern chosen while it runs that has not started yet */
static int queued = -1;

/* F2 on SEQ, SONG, AUTO, F6 on PERFORM – the SONG's undo: the song as it was one step ago */
static sfx_song undo_song;
/* there is one */
static int undo_has;
/* it holds tempo and mix too, else only the notes */
static int undo_whole;
/* F2 was pressed: the next press goes forth again */
static int undo_redo;
/* what is being gathered into one step: the edits (1), the take (2) or the bars (3) */
static int undo_run;
/* COPY went on to its copy: the pattern to go back to with it */
static int undo_pat = -1;
/* the word the display says with UNDO: and REDO: */
static const char *undo_what = "";

/* SEQ, REC while it plays: F4 held erases the selected sound as the bar passes */
static int f4_held;
/* the step erased last (-1: none yet), and in which pattern */
static int erase_at;
static int erase_pat;
/* a game's or a DAW's note is being played: nothing is said about it */
static int from_outside;
/* (the tour: the mode it puts back) */
static int tour_mode0;

/* draws counted: what is queued blinks */
static uint32_t blink;
#define BLINK ((blink >> 3) & 1)

/* the pattern being edited (the one the pages show) */
static int edit_pat;
/* the step being edited, a step of that pattern's grid */
static int cur_step;
/* the pad held on SEQ (-1: none), and the step it stands for */
static int held_pad = -1;
static int held_step = -1;
/* the 16 steps a take showed last (-1: none) */
static int follow_win = -1;
/* what cur_step was counted in: the pattern, and its steps a sixteenth */
static int cur_pat = -1;
static int cur_div = 1;
/* the three grids' names, as the display says them */
static const char *const GRID_NAME[SFX_GRIDS] = {"1/16", "1/32", "1/64"};

/* v + d, wrapped into 0..n-1 */
static int wrap(int v, int d, int n) {
  return ((v + d) % n + n) % n;
}

/* v, kept within a..b */
static int clampi(int v, int a, int b) {
  return v < a ? a : v > b ? b : v;
}

/* the bar in sixteenths */
static int bar_len(void) {
  return SONG.len >= 1 && SONG.len <= SFX_BAR ? SONG.len : SFX_BAR;
}

/* the beat in sixteenths: 2, 3, 4 or 6 (whatever beat_len may hold: read once) */
static int beat(void) {
  int b = beat_len;
  return b == 2 || b == 3 || b == 6 ? b : 4;
}

/* The bar as a time signature, into t (room for 6 characters): its length counted in the beat's kind of note where it
   comes out whole, else in the next finer one. A beat of a quarter counts quarters ("3/4"), then eighths ("7/8"), then
   sixteenths; a beat of an eighth or a dotted quarter counts eighths ("6/8"), then sixteenths; a beat of three
   sixteenths counts sixteenths ("12/16"). */
static const char *meter_text(char *t) {
  int len = bar_len();
  int b = beat();
  /* den: the note that is counted. one: how many sixteenths it has */
  int den = b == 4 ? 4 : b == 3 ? 16 : 8;
  int one = 16 / den;
  while (len % one) {
    den *= 2;
    one /= 2;
  }
  vg_itoa(len / one, t);
  strcpy(t + strlen(t), "/");
  vg_itoa(den, t + strlen(t));
  return t;
}

/* the bar is not four quarters: the display says what it is */
static int meter_shown(void) {
  return bar_len() != SFX_BAR || beat() != 4;
}

/* pattern k's grid: 0 = 1/16, 1 = 1/32, 2 = 1/64 */
static int pat_grid(int k) {
  int g = SONG.pat[k % SFX_PATTERNS].grid;
  return g < SFX_GRIDS ? g : 0;
}

/* pattern k's steps a sixteenth: 1, 2 or 4 */
static int pat_div(int k) {
  return sfx_div(SONG.pat[k % SFX_PATTERNS].grid);
}

/* the cells of the pattern shown: 16, 32, 64 */
static int gsteps(void) {
  return SFX_BAR * pat_div(edit_pat);
}

/* the encoder (1-4) turned last, and when */
static int touch_i = -1;
static uint64_t touch_t;
/* encoder turns not yet a whole step */
static float acc[VE_COUNT];
/* the random numbers of MUTATE and RANDOM */
static uint32_t rng = 0x5F3759DFu;

/* LINK: what the game sent last, CC 10..17 (10-15 do something, fw_protocol.h; 16 and 17 are only kept: the page
   shows 10-15, and any of the eight switches the link's light on) */
static uint8_t telemetry[8];
/* LINK: what the encoders sent to the game (CC 20..25) */
static uint8_t cc_out[6] = {64, 64, 64, 64, 64, 64};
/* something came in from the other side */
static int linked;
/* the text a game sent (SX_TEXT) */
static char link_text[25];

/* the toast: what the display says for a moment (fw_edit.h: say), since when, and whether it still shows */
static const char *toast;
static uint64_t toast_t;
static int toast_live;

/* what each pad's light was last set to (on_draw sets a light only when it changes; 0xFFFFFFFF: set it anyway) */
static uint32_t led_cache[SLOTS];
/* the pad the mouse holds on the stage */
static int stage_pad = -1;

/* the tour (MENU): it runs; its next step */
static int tour_on;
static int tour_i;
/* when it began */
static uint64_t tour_t0;
/* the caption it shows, two lines */
static const char *tour_title = "";
static const char *tour_line = "";
/* how far it is (0..1) */
static float tour_progress;
/* the intensity it puts back */
static float tour_heat0;
/* an intensity that glides: from, to, since when, how long */
static float tour_ramp_from;
static float tour_ramp_to;
static float tour_ramp_t0;
static float tour_ramp_len;
/* what it puts back: the kit, the page, the selected sound */
static int tour_kit0;
static int tour_page0;
static int tour_sel0;
/* the sound it changed (-1: none), and what that sound was */
static int tour_kept = -1;
static sfx_patch tour_keep;

/* sending the sounds and the song: the next message, -1 = not */
static int export_at = -1;

/* A song coming in (song_piece in fw_link.h). Its pieces, side by side: */
static uint8_t song_rx[SONG_PIECES_MAX * SONG_PIECE];
/* which of them are here, a bit a piece */
static uint32_t song_rx_seen[(SONG_PIECES_MAX + 31) / 32];
/* how many pieces it has */
static int song_rx_K;
/* the size of a piece: 240, or 200 for the shape of before */
static int song_rx_size;
/* the format its pieces name (0: the old shape names none) */
static int song_rx_format;
/* how long its last piece is */
static int song_rx_last;
/* when the last piece came */
static uint64_t song_rx_t;
/* how often each piece of it came (the wire may deliver every message twice) */
static uint8_t song_rx_n[SONG_PIECES_MAX];
/* after it was taken: how often each piece came again since */
static uint8_t song_rx_n2[SONG_PIECES_MAX];
/* the most any piece came */
static int song_rx_most;
/* after it was taken: the pieces that came again since, a bit a piece */
static uint32_t song_rx_again[(SONG_PIECES_MAX + 31) / 32];
/* it was taken; and one of the pieces that came again had other bytes */
static int song_rx_done;
static int song_rx_other;

/* What went out last, to know our own echo by (sysex_in): the dump of each slot ... */
static uint8_t sent_dump[SLOTS][SFX_PATCH_BYTES];
/* ... and the song as it was sent: its pieces, the number of its bytes, the number of its pieces */
static uint8_t song_tx[SONG_PIECES_MAX * SONG_PIECE];
static int song_tx_n;
static int song_tx_K;
/* when a dump last went out, and when a piece of the song */
static uint64_t sent_dump_t;
static uint64_t sent_song_t;
/* which slots' dumps have gone out (a bit a slot); whether a song has */
static uint32_t sent_dump_any;
static int sent_song_any;
/* an echo comes back within a tenth of a second */
#define ECHO_FRAMES ((uint64_t)SR / 10)

/* PERFORM: what the 16 pads are there. F1-F4 choose it, in this order (soft_perform, fw_softkeys.h) */
enum {
  PERF_STEPS,    /* 16 steps of the selected sound, as on SEQ */
  PERF_SOUNDS,   /* the kit: a pad plays its sound and selects it - with REC, into the pattern */
  PERF_MUTES,    /* a sound muted or not, as on MIX */
  PERF_PATTERNS, /* pads 1-8 the patterns, 9-16 the intensity, as on SONG */
  PERF_COUNT
};
static int perf_mode;

/* ---- 6. F1-F6 -------------------------------------------------------------------------------------------------- */

// clang-format off
/* their captions; a row for each set of pages (soft_set says which) */
static const char *const SOFT[7][6] = {
  /*                      F1         F2        F3         F4        F5        F6     */
  /* 0 the sound pages */ { "PRESET",  "MUTATE", "UNDO",    "RANDOM", "EXPORT", "SAVE" },
  /* 1 SEQ             */ { "TO SONG", "UNDO",   "FILL",    "CLEAR",  "COPY",   "SAVE" },
  /* 2 SONG            */ { "TO SONG", "UNDO",   "CLR PAT", "NEW",    "SEND",   "SAVE" },
  /* 3 MIX, MASTER     */ { "MUTE",    "SOLO",   "RESET",   "ALL ON", "PLAY",   "SAVE" },
  /* 4 AUTO            */ { "TO SONG", "UNDO",   "RAMP",    "CLEAR",  "COPY",   "SAVE" },
  /* 5 PERFORM         */ { "STEPS",   "SOUNDS", "MUTES",   "PATTERN", "FILL",  "UNDO" },
  /* 6 … with SHIFT    */ { "TO SONG", "CLEAR",  "ALL ON",  "COPY",   "",       ""     },
};
// clang-format on

static int mix_page(void) {
  return page == PG_MIX || page == PG_MASTER;
}

/* the row of SOFT for the page that is open */
static int soft_set(void) {
  // clang-format off
  return page == PG_SEQ  ? 1
       : page == PG_SONG ? 2
       : mix_page()      ? 3
       : page == PG_AUTO ? 4
       : page == PG_PERFORM ? 5
       :                   0;
  // clang-format on
}

static int sound_page(void) {
  return page < PG_SEQ;
}

/* the pages where F1 and F2 belong to the song (LEFT / RIGHT and A: pattern_keys) */
static int song_keys(void) {
  return page == PG_SEQ || page == PG_SONG || page == PG_AUTO;
}

/* the pages where LEFT / RIGHT are the pattern and A is the tempo */
static int pattern_keys(void) {
  return song_keys() || page == PG_PERFORM;
}

/* F1-F6 as they are now: F1 says what it will do, F4 erases while a take runs */
static const char *soft_caption(int i) {
  if (i == 0 && (song_keys() || (page == PG_PERFORM && shift))) {
    return loop_mode ? "TO SONG" : "TO LOOP";
  }
  /* PERFORM, SHIFT held: F2-F4 are CLEAR, ALL ON, COPY (F5 and F6 the grid: below) */
  if (page == PG_PERFORM && shift && i < 4) {
    return SOFT[6][i];
  }
  if (i == 3 && page == PG_SEQ && rec && running) {
    return "ERASE";
  }
  /* SHIFT held: F5 and F6 are the pattern's grid, coarser and finer */
  if (shift && i >= 4 && (page == PG_SEQ || page == PG_AUTO || page == PG_PERFORM)) {
    static const char *const TO[2][SFX_GRIDS] = {{"IS 1/16", "TO 1/16", "TO 1/32"}, {"TO 1/32", "TO 1/64", "IS 1/64"}};
    return TO[i - 4][pat_grid(edit_pat)];
  }
  return SOFT[soft_set()][i];
}

/* tells the host what F1-F6 are called now */
static void relabel(void) {
  for (int i = 0; i < 6; i++) {
    vibe_label(VB_F1 + i, soft_caption(i));
  }
}
