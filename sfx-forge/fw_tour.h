/* fw_tour.h – the tour (MENU): SFX FORGE shows by itself what it can do. A part of sfx-forge.c.
 *
 * Holds:  the script (TOUR: what happens at which second), tour_start, tour_end, tour_pump (which runs the script).
 * Needs:  fw_state.h (tour_...), fw_audio.h (send), fw_edit.h, fw_sequencer.h (pad, seq_play, pattern_pick,
 *         intensity_set, mode_set), fw_pages.h (set_page).
 * Read:   the tour does what a player would do - it selects sounds, turns their knobs, plays pads and keys, starts
 *         the song - through the same functions the keys call. Everything it touches is put back at the end
 *         (tour_end): the sound it changed, the page, the kit, the selected sound, the intensity, the mode. Any touch
 *         ends it (on_event, stage_event). It runs from the display (on_draw calls tour_pump): the tour is something
 *         to watch.
 */

/* ---- the script ------------------------------------------------------------------------------------------------ */

/* what a step of the script does; a and b are its two numbers */
enum {
  T_SAY,     /* the caption: title and line */
  T_PAGE,    /* page a is opened */
  T_PAD,     /* pad a is played */
  T_SELECT,  /* sound a is selected, and kept: it will be put back as it is now */
  T_PARAM,   /* value a (SFX_P_...) of the selected sound becomes b */
  T_MUTATE,  /* the selected sound is mutated, and played */
  T_RESTORE, /* the sound that was kept is put back */
  T_NOTE,    /* the selected sound plays note a, a short one */
  T_ON,      /* note a is held … */
  T_OFF,     /* … and let go */
  T_PLAY,    /* the song starts, in its order */
  T_STOP,    /* the song stops */
  T_HEAT,    /* the intensity becomes a / 8 */
  T_RAMP,    /* the intensity glides to a / 8 within b / 4 seconds */
  T_CUE,     /* pattern a is cued */
  T_KIT,     /* kit a is chosen */
  T_END      /* the tour is over: everything is put back */
};

typedef struct {
  float t;     /* when, in seconds from the start */
  uint8_t act; /* what happens: T_... */
  uint8_t a;   /* its two numbers (see the list above) */
  uint8_t b;
  const char *title; /* T_SAY: the caption's two lines */
  const char *line;
} tour_step;

/* (the later parts, after the instruments, are written as LATER + their time before the instruments were put in.
   TOUR_SECONDS uses it too, so it stays defined) */
#define LATER 5.4f

// clang-format off
/* in the order of time: tour_pump stops at the first step whose time has not come */
static const tour_step TOUR[] = {
  /* the SFX kit: eight of its pads */
  {  0.0f, T_SAY, 0, 0, "SOUND EFFECTS", "the SFX kit: 16 of them, all synthesized" },
  {  0.0f, T_KIT, KIT_SFX, 0, 0, 0 }, { 0.0f, T_PAGE, PG_OSC, 0, 0, 0 }, { 0.0f, T_HEAT, 8, 0, 0, 0 },
  {  0.2f, T_PAD, 0, 0, 0, 0 }, { 0.65f, T_PAD, 4, 0, 0, 0 }, { 1.1f, T_PAD, 8, 0, 0, 0 }, { 1.55f, T_PAD, 12, 0, 0, 0 },
  {  2.0f, T_PAD, 3, 0, 0, 0 }, { 2.45f, T_PAD, 5, 0, 0, 0 }, { 2.9f, T_PAD, 9, 0, 0, 0 }, { 3.35f, T_PAD, 14, 0, 0, 0 },

  /* pad 9 (LASER): five of its knobs, one after the other, played after each */
  {  4.2f, T_SAY, 0, 0, "SHAPE IT", "four knobs a page - watch the wave change" },
  {  4.2f, T_SELECT, 8, 0, 0, 0 },
  {  4.4f, T_PARAM, SFX_P_PITCH_ENV_DEC, 75, 0, 0 }, { 4.4f, T_PAD, 8, 0, 0, 0 },
  {  5.0f, T_PARAM, SFX_P_BASE_FREQ, 48, 0, 0 },     { 5.0f, T_PAD, 8, 0, 0, 0 },
  {  5.6f, T_PARAM, SFX_P_PITCH_ENV_AMT, 24, 0, 0 }, { 5.6f, T_PAD, 8, 0, 0, 0 },
  {  6.2f, T_PAGE, PG_TIMBRE, 0, 0, 0 }, { 6.2f, T_PARAM, SFX_P_FM_DEPTH, 112, 0, 0 }, { 6.2f, T_PAD, 8, 0, 0, 0 },
  {  6.8f, T_PARAM, SFX_P_BITCRUSH, 64, 0, 0 },      { 6.8f, T_PAD, 8, 0, 0, 0 },
  {  7.6f, T_RESTORE, 0, 0, 0, 0 },

  /* the same pad, mutated four times */
  {  7.8f, T_SAY, 0, 0, "MUTATE", "one key - a new variation every time" },
  {  7.8f, T_PAGE, PG_OSC, 0, 0, 0 },
  {  8.0f, T_MUTATE, 0, 0, 0, 0 }, { 8.7f, T_MUTATE, 0, 0, 0, 0 }, { 9.4f, T_MUTATE, 0, 0, 0, 0 },
  { 10.1f, T_MUTATE, 0, 0, 0, 0 },
  { 10.9f, T_RESTORE, 0, 0, 0, 0 },

  /* the MUSIC kit: pad 8 (FM KEYS) plays a tune */
  { 11.2f, T_SAY, 0, 0, "INSTRUMENTS", "the MUSIC kit: 16 more, played on the keys" },
  { 11.2f, T_KIT, KIT_MUSIC, 0, 0, 0 }, { 11.2f, T_SELECT, 7, 0, 0, 0 },
  { 11.4f, T_NOTE, 69, 0, 0, 0 }, { 11.65f, T_NOTE, 72, 0, 0, 0 }, { 11.9f, T_NOTE, 76, 0, 0, 0 },
  { 12.15f, T_NOTE, 81, 0, 0, 0 },
  { 12.4f, T_NOTE, 79, 0, 0, 0 }, { 12.65f, T_NOTE, 76, 0, 0, 0 }, { 12.9f, T_NOTE, 72, 0, 0, 0 },
  { 13.15f, T_NOTE, 69, 0, 0, 0 },

  /* pad 16 (STRINGS): a chord, held */
  { 13.8f, T_SAY, 0, 0, "CHORDS", "strings, brass, bells - with chorus, delay, reverb" },
  { 13.8f, T_SELECT, 15, 0, 0, 0 }, { 13.8f, T_PAGE, PG_ENV, 0, 0, 0 },
  { 14.0f, T_ON, 57, 0, 0, 0 }, { 14.0f, T_ON, 60, 0, 0, 0 }, { 14.0f, T_ON, 64, 0, 0, 0 },
  { 15.7f, T_OFF, 57, 0, 0, 0 }, { 15.7f, T_OFF, 60, 0, 0, 0 }, { 15.7f, T_OFF, 64, 0, 0, 0 },

  /* pad 12 (WT SWEEP): a held note while its wavetable position is turned */
  { 16.2f, T_SAY, 0, 0, "WAVETABLES", "a wave that morphs - by hand, LFO or the game" },
  { 16.2f, T_SELECT, 11, 0, 0, 0 }, { 16.2f, T_PAGE, PG_TIMBRE, 0, 0, 0 },
  { 16.2f, T_PARAM, SFX_P_LFO_TIMBRE, 0, 0, 0 },
  { 16.4f, T_ON, 45, 0, 0, 0 },
  { 16.8f, T_PARAM, SFX_P_FM_RATIO, 40, 0, 0 }, { 17.2f, T_PARAM, SFX_P_FM_RATIO, 70, 0, 0 },
  { 17.6f, T_PARAM, SFX_P_FM_RATIO, 100, 0, 0 },
  { 18.0f, T_PARAM, SFX_P_FM_RATIO, 127, 0, 0 }, { 18.4f, T_OFF, 45, 0, 0, 0 }, { 18.9f, T_RESTORE, 0, 0, 0, 0 },

  /* the song: from intensity 0 up, a pattern cued and back, the LINK page */
  { LATER + 13.8f, T_SAY, 0, 0, "A SONG", "patterns over the 16 sounds - here at intensity 0" },
  { LATER + 13.8f, T_PAGE, PG_SONG, 0, 0, 0 }, { LATER + 13.8f, T_HEAT, 0, 0, 0, 0 },
  { LATER + 13.9f, T_PLAY, 0, 0, 0, 0 },
  { LATER + 17.8f, T_SAY, 0, 0, "LAYERS FOLLOW THE GAME", "its intensity brings the sounds in" },
  { LATER + 17.8f, T_RAMP, 8, 24, 0, 0 },
  { LATER + 25.8f, T_SAY, 0, 0, "A GAME CUES PATTERNS", "they change on the bar line" },
  { LATER + 25.8f, T_PAGE, PG_SEQ, 0, 0, 0 },
  { LATER + 26.0f, T_CUE, 1, 0, 0, 0 }, { LATER + 28.0f, T_CUE, 0, 0, 0, 0 },
  { LATER + 29.8f, T_SAY, 0, 0, "LINKED TO THE GAME", "Unity plays these sounds with the same engine" },
  { LATER + 29.8f, T_PAGE, PG_LINK, 0, 0, 0 }, { LATER + 29.8f, T_RAMP, 2, 12, 0, 0 },
  { LATER + 33.0f, T_STOP, 0, 0, 0, 0 }, { LATER + 33.0f, T_PAGE, PG_OSC, 0, 0, 0 },
  { LATER + 33.0f, T_SAY, 0, 0, "YOUR TURN", "HOME shows what every control does" },
  { LATER + 35.5f, T_END, 0, 0, 0, 0 },
};
// clang-format on

/* how long it is */
#define TOUR_SECONDS (LATER + 35.5f)

/* ---- running it ------------------------------------------------------------------------------------------------ */

/* the seconds since it began (the audio's clock) */
static float tour_time(void) {
  return (float)(vibe_frames() - tour_t0) / SR;
}

/* the sound the tour changed is as it was again (it is kept until the next sound or the end) */
static void tour_restore(void) {
  if (tour_kept >= 0) {
    patches[tour_kept] = tour_keep;
    changed(tour_kept);
  }
}

/* the end - its own, or any touch: everything it changed is put back */
static void tour_end(void) {
  if (!tour_on) {
    return;
  }
  tour_on = 0;
  tour_restore();
  tour_kept = -1;
  send(M_QUIET, 0, 0, 0, 0);
  if (running) {
    seq_play(0);
  }
  pattern_pick(0);
  intensity_set(tour_heat0);
  sel = tour_sel0;
  set_page(tour_page0);
  kit_set(tour_kit0);
  mode_set(tour_mode0);
}

/* MENU: the tour starts - or, running, ends. What it will put back is noted first */
static void tour_start(void) {
  if (tour_on) {
    tour_end();
    return;
  }
  tour_heat0 = vibe_param_get(P_INTENSITY);
  tour_page0 = page;
  tour_sel0 = sel;
  tour_kit0 = kit;
  tour_mode0 = loop_mode;
  if (running) {
    seq_play(0);
  }
  seq_rec(0);
  help = 0;
  tour_on = 1;
  tour_i = 0;
  tour_t0 = vibe_frames();
  tour_ramp_len = 0.0f;
  tour_line = "";
  tour_title = "";
}

/* From the display (on_draw): the tour is something to watch. An intensity that glides is moved on; then every step
   of the script whose time has come is done, in order. */
static void tour_pump(void) {
  if (!tour_on) {
    return;
  }
  float t = tour_time();
  tour_progress = t / TOUR_SECONDS;
  /* T_RAMP: the intensity on its way */
  if (tour_ramp_len > 0.0f) {
    float k = vd_clamp((t - tour_ramp_t0) / tour_ramp_len, 0.0f, 1.0f);
    intensity_set(tour_ramp_from + (tour_ramp_to - tour_ramp_from) * k);
    if (k >= 1.0f) {
      tour_ramp_len = 0.0f;
    }
  }
  while (tour_on && TOUR[tour_i].t <= t) {
    const tour_step *st = &TOUR[tour_i++];
    switch (st->act) {
    case T_SAY:
      tour_title = st->title;
      tour_line = st->line;
      break;
    case T_PAGE:
      set_page(st->a);
      break;
    case T_PAD:
      pad(st->a, 1, 0.9f);
      pad(st->a, 0, 0);
      break;
    case T_SELECT:
      tour_restore();
      sel = st->a;
      tour_kept = sel;
      tour_keep = patches[sel];
      break;
    case T_PARAM:
      /* (the sound the tour selected, should DATA have selected another since: only that one is put back) */
      if (tour_kept >= 0) {
        sel = tour_kept;
      }
      patches[sel].p[st->a] = st->b;
      changed(sel);
      break;
    case T_MUTATE:
      if (tour_kept >= 0) {
        sel = tour_kept;
      }
      rng ^= vd_rand();
      sfx_patch_mutate(&patches[sel], &rng);
      changed(sel);
      pad(sel, 1, 0.9f);
      pad(sel, 0, 0);
      break;
    case T_RESTORE:
      tour_restore();
      break;
    case T_NOTE:
      /* (a key's voice is named 100 + its note: on_event does the same) */
      send(M_TRIGGER, sel, 100 + st->a, 0.9f, st->a);
      send(M_RELEASE, sel, 100 + st->a, 0, 0);
      break;
    case T_ON:
      send(M_TRIGGER, sel, 100 + st->a, 0.8f, st->a);
      break;
    case T_OFF:
      send(M_RELEASE, sel, 100 + st->a, 0, 0);
      break;
    case T_PLAY:
      /* (the tour plays the song, in its order) */
      edit_pat = 0;
      mode_set(0);
      seq_play(1);
      break;
    case T_STOP:
      seq_play(0);
      break;
    case T_HEAT:
      tour_ramp_len = 0.0f;
      intensity_set((float)st->a / 8.0f);
      break;
    case T_RAMP:
      tour_ramp_from = vibe_param_get(P_INTENSITY);
      tour_ramp_to = (float)st->a / 8.0f;
      tour_ramp_t0 = t;
      tour_ramp_len = (float)st->b / 4.0f;
      break;
    case T_CUE:
      pattern_pick(st->a);
      break;
    case T_KIT:
      tour_restore();
      tour_kept = -1;
      kit_set(st->a);
      break;
    case T_END:
      tour_end();
      break;
    }
  }
}
