/* fw_softkeys.h – F1-F6 on every page. A part of sfx-forge.c.
 *
 * Holds:  the sound pages   soft_sound (PRESET MUTATE UNDO RANDOM EXPORT SAVE), audition
 *         SEQ               soft_seq (mode, UNDO, FILL, CLEAR, COPY, SAVE) with fill, sound_clear, pattern_copy,
 *                           grid_change
 *         AUTO              soft_auto (mode, UNDO, RAMP, CLEAR, COPY, SAVE)
 *         SONG              soft_song (mode, UNDO, CLR PAT, NEW / DEMO, SEND, SAVE) with pattern_clear, song_new,
 *                           song_demo; arr_edit (the arrangement: SHIFT + a pad)
 *         MIX, MASTER       soft_mix (MUTE, SOLO, RESET, ALL ON, PLAY, SAVE)
 *         PERFORM           perform_mode, soft_perform (STEPS, SOUNDS, MUTES, PATTERN, FILL, UNDO - with SHIFT: what
 *                           PLAY plays, CLEAR, ALL ON, COPY, the grid)
 * Needs:  fw_state.h, fw_edit.h (say, confirm, the undo), fw_factory.h, fw_store.h (save_say), fw_link.h (out_pump),
 *         fw_sequencer.h, fw_pages.h (set_page).
 * Read:   each soft_... takes f = 0..5 for F1..F6; on_event (fw_events.h) chooses the one for the page that is open.
 *         The captions are in fw_state.h (SOFT, soft_caption): a key moved here must be moved there.
 */

/* ---- the sound pages ------------------------------------------------------------------------------------------- */

/* hear the selected sound: a moment of it if it would hold */
static void audition(void) {
  send(M_TRIGGER, sel, sel, 0.8f, 0);
  send(M_RELEASE, sel, sel, 0, 0);
}

/* F1-F6 on the sound pages */
static void soft_sound(int f) {
  sfx_patch *p = &patches[sel];
  rng ^= vd_rand();
  switch (f) {
  case 0: {
    /* PRESET: the next named sound of this kit (SHIFT: the one before). Each kit has its own names:
       first, n: where they begin in PRESETS and how many. at: the one the pad holds now. k: the one it gets.
       A pad that holds a sound of its own has no place in the list: at is -1 so that + 1 gives the first name, and
       with SHIFT 0 so that - 1 wraps to the last */
    int first = kit == KIT_SFX ? 0 : SFX_PRESETS;
    int n = kit == KIT_SFX ? SFX_PRESETS : PRESET_COUNT - SFX_PRESETS;
    int at = pad_preset[sel] >= first && pad_preset[sel] < first + n ? pad_preset[sel] - first : (shift ? 0 : -1);
    int k = first + wrap(at, shift ? -1 : 1, n);
    keep_undo(sel);
    preset_load(sel, k);
    say(PRESETS[k].name);
    if (page == PG_WAVE && p->engine != SFX_ENGINE_WAVETABLE) {
      set_page(PG_OSC);
    }
    break;
  }
  case 1:
    /* MUTATE */
    keep_undo(sel);
    sfx_patch_mutate(p, &rng);
    say("MUTATED");
    break;
  case 2:
    /* UNDO: the sound and the one kept change places (so a second press is the redo) */
    if (!sound_has_undo[sel]) {
      say("NOTHING TO UNDO");
      return;
    }
    {
      sfx_patch t = *p;
      int8_t n = pad_preset[sel];
      *p = sound_undo[sel];
      pad_preset[sel] = sound_undo_preset[sel];
      sound_undo[sel] = t;
      sound_undo_preset[sel] = n;
    }
    say("UNDO");
    break;
  case 3:
    /* RANDOM */
    keep_undo(sel);
    sfx_patch_random(p, &rng);
    pad_preset[sel] = -1;
    say("RANDOM");
    break;
  case 4:
    /* EXPORT: the sound as SysEx, to a game */
    dump_due |= 1u << sel;
    out_pump();
    say("EXPORTED");
    return;
  case 5:
    save_say();
    return;
  }
  /* the sound is another one now: to the player, and heard */
  changed(sel);
  audition();
}

/* ---- the song: a pattern, a whole song ------------------------------------------------------------------------- */

/* SONG, F3 CLR PAT: the pattern shown, emptied (one press: F2 brings it back) */
static void pattern_clear(void) {
  /* (and what F2 holds is kept) */
  if (!pattern_holds(edit_pat)) {
    say("NOTHING TO CLEAR");
    return;
  }
  undo_mark("CLR PAT", 0);
  /* (its grid stays) */
  sfx_pattern_clear(pat());
  lanes_tidy();
  say("PAT CLEARED - F2: UNDO");
}

/* A whole song goes: these two ask twice, and F2 brings it back – tempo, mix and all. (The beat is the device's:
   they leave it.) The sounds are not touched; it
   plays on. */

/* SONG, F4 NEW: an empty song */
static void song_new(void) {
  if (!confirm("PRESS AGAIN: NEW SONG")) {
    return;
  }
  undo_mark("NEW", 1);
  sfx_song_init(&SONG);
  view_sync();
  cur_step = 0;
  pattern_home();
  say("NEW SONG - F2: UNDO");
}

/* SONG, SHIFT + F4 DEMO: the song the device came with */
static void song_demo(void) {
  if (!confirm("PRESS AGAIN: DEMO")) {
    return;
  }
  undo_mark("DEMO", 1);
  demo_song();
  view_sync();
  pattern_home();
  say("DEMO SONG - F2: UNDO");
}

/* F5 COPY: this pattern into the next empty one, and on to it: a variation to make */
static void pattern_copy(void) {
  static char said[16] = "COPIED A > B";
  int to = -1;
  for (int i = 1; i < SFX_PATTERNS && to < 0; i++) {
    if (!pattern_holds((edit_pat + i) % SFX_PATTERNS)) {
      to = (edit_pat + i) % SFX_PATTERNS;
    }
  }
  if (to < 0) {
    say("NO EMPTY PATTERN");
    return;
  }
  undo_mark("COPY", 0);
  if (!(rec && running)) {
    undo_pat = edit_pat;
  }
  /* (with its grid) */
  SONG.pat[to] = *pat();
  said[7] = (char)('A' + edit_pat);
  said[11] = (char)('A' + to);
  /* (the chain is left alone) */
  pattern_pick(to);
  say(said);
}

/* F3 FILL: the selected sound on every beat (a quarter, unless SHIFT + B chose another), then every eighth (where the
   beat is made of eighths), every sixteenth - at 32 and 64 steps also every thirty-second -, none. Each press goes one
   further: it looks which of these the row is now, and writes the next. */
static void fill(void) {
  /* what is said, by how many thirty-seconds lie between two steps of the fill (1, 2, 4, 6, 8, 12) */
  static const char *const SAID[13] = {"", "EVERY 1/32", "EVERY 1/16", "", "EVERY 1/8", "",         "EVERY 3/16",
                                       "", "EVERY 1/4",  "",           "", "",          "EVERY 3/8"};
  /* n: the pattern's steps. lim: those of the bar. levels: how many fills there are. next: the one to write */
  int div = pat_div(edit_pat);
  int n = gsteps();
  int lim = bar_len() * div;
  int levels = 0;
  int next = 0;
  /* six[k]: fill k has a step every so many sixteenths - the beat first, then the eighth (where the beat is made of
     eighths), then the sixteenth. every[k]: the same in steps of the pattern; at 32 and 64 steps the thirty-second
     comes last */
  int b = beat();
  int six[3];
  int every[4];
  six[levels++] = b;
  if (b > 2 && b % 2 == 0) {
    six[levels++] = 2;
  }
  six[levels++] = 1;
  for (int k = 0; k < levels; k++) {
    every[k] = six[k] * div;
  }
  if (div > 1) {
    every[levels++] = div / 2;
  }
  undo_mark("FILL", 0);
  for (int k = 0; k < levels; k++) {
    int same = 1;
    for (int i = 0; i < n; i++) {
      if (((pat()->on[i] >> sel) & 1) != (i % every[k] == 0 && i < lim)) {
        same = 0;
      }
    }
    if (same) {
      next = k + 1;
    }
  }
  for (int i = 0; i < n; i++) {
    pat()->on[i] &= (uint16_t) ~(1u << sel);
    if (next < levels && i % every[next] == 0 && i < lim) {
      pat()->on[i] |= (uint16_t)(1u << sel);
    }
  }
  say(next < levels ? SAID[every[next] * 2 / div] : "SOUND CLEARED");
}

/* SEQ, AUTO and PERFORM, SHIFT + F5 / F6: the grid of the pattern shown, a step coarser or finer. Finer moves every
   step to the step that begins at the same moment: it sounds the same. Coarser drops what stands between the steps that
   stay - that asks twice (the second press counts for the same pattern, the same grid and the same number of things
   dropped only), and F2 (on PERFORM: F6) brings it back. */
static void grid_change(int dir) {
  // clang-format off
  /* what is said after it, by the grid it goes to */
  static const char *const UP[SFX_GRIDS]   = { "", "GRID 1/32", "GRID 1/64" };
  static const char *const DOWN[SFX_GRIDS] = { "GRID 1/16 - F2: UNDO", "GRID 1/32 - F2: UNDO", "" };
  /* (on PERFORM, where UNDO is F6) */
  static const char *const DOWN_PERFORM[SFX_GRIDS] = { "GRID 1/16 - F6: UNDO", "GRID 1/32 - F6: UNDO", "" };
  // clang-format on
  /* the question, and what it was asked about: the pattern, the grid, how much would be dropped */
  static char ask[24];
  static int ask_pat = -1;
  static int ask_to;
  static int ask_loss;
  sfx_pattern *p = pat();
  int to = pat_grid(edit_pat) + (dir > 0 ? 1 : -1);
  if (to >= SFX_GRIDS) {
    say("FINEST: 1/64");
    return;
  }
  if (to < 0) {
    say("COARSEST: 1/16");
    return;
  }
  /* coarser, and something would be dropped: the first press asks ("AGAIN: B DROPS 5"), the second does it */
  if (dir < 0) {
    int loss = sfx_pattern_regrid_loss(p, to);
    if (loss > 0) {
      /* (more or less to drop than it said: it says the number that is true now) */
      if (!(toast == ask && toast_on() && ask_pat == edit_pat && ask_to == to && ask_loss == loss)) {
        strcpy(ask, "AGAIN: A DROPS ");
        ask[7] = (char)('A' + edit_pat);
        vg_itoa(loss, ask + 15);
        ask_pat = edit_pat;
        ask_to = to;
        ask_loss = loss;
        say(ask);
        return;
      }
      toast_live = 0;
    }
  }
  undo_mark("GRID", 0);
  sfx_pattern_regrid(p, to);
  if (dir < 0) {
    lanes_tidy();
  }
  say(dir > 0 ? UP[to] : page == PG_PERFORM ? DOWN_PERFORM[to] : DOWN[to]);
  erase_at = -1;
  cur_sync();
  relabel();
}

/* ---- SEQ and AUTO (PERFORM uses sound_clear too) -------------------------------------------------------------- */

/* CLEAR: the selected sound's steps in this pattern, with their notes, velocities and lengths */
static void sound_clear(void) {
  int any = 0;
  for (int i = 0; i < gsteps(); i++) {
    any |= ((pat()->on[i] >> sel) & 1) || pat()->note[sel][i] || pat()->vel[sel][i] != SFX_VEL_DEFAULT ||
           pat()->len[sel][i];
  }
  if (!any) {
    say("NOTHING TO CLEAR");
    return;
  }
  undo_mark("CLEAR", 0);
  for (int i = 0; i < gsteps(); i++) {
    pat()->on[i] &= (uint16_t) ~(1u << sel);
    pat()->note[sel][i] = 0;
    pat()->vel[sel][i] = SFX_VEL_DEFAULT;
    pat()->len[sel][i] = 0;
  }
  say(page == PG_PERFORM ? "CLEARED - F6: UNDO" : "CLEARED - F2: UNDO");
}

/* F1-F6 on the SEQ page */
static void soft_seq(int f) {
  if (shift && f >= 4) {
    grid_change(f == 4 ? -1 : 1);
    return;
  }
  switch (f) {
  case 0:
    mode_toggle();
    break;
  case 1:
    song_undo();
    break;
  case 2:
    fill();
    break;
  case 3:
    /* CLEAR (while a take runs F4 is ERASE, held: on_event) */
    sound_clear();
    break;
  case 4:
    pattern_copy();
    break;
  case 5:
    save_say();
    break;
  }
}

/* F1-F6 on the AUTO page */
static void soft_auto(int f) {
  if (shift && f >= 4) {
    grid_change(f == 4 ? -1 : 1);
    return;
  }
  switch (f) {
  case 0:
    mode_toggle();
    break;
  case 1:
    song_undo();
    break;
  case 2:
    auto_ramp();
    break;
  case 3: {
    /* CLEAR: the points of the lane shown, in this pattern */
    int any = 0;
    for (int i = 0; i < gsteps(); i++) {
      any |= aut_at(sel, i);
    }
    if (!any) {
      say("NOTHING TO CLEAR");
      break;
    }
    undo_mark("CLEAR", 0);
    for (int i = 0; i < gsteps(); i++) {
      aut_put(sel, i, 0);
    }
    say("LANE CLEARED");
    break;
  }
  case 4:
    pattern_copy();
    break;
  case 5:
    save_say();
    break;
  }
}

/* ---- SONG ------------------------------------------------------------------------------------------------------ */

/* the arrangement: a pattern a bar. SONG page, SHIFT + pad 1-8 adds one, SHIFT + pad 9-15 takes the last away, 16 all
   of them: that asks twice. F2 takes back the bars added and removed in a row (one step), or brings all of them back */
static void arr_edit(int pad_id) {
  if (pad_id < 8) {
    if (SONG.arr_len >= SFX_ARR_MAX) {
      say("32 BARS: FULL");
      return;
    }
    undo_open(3);
    SONG.arr[SONG.arr_len] = (uint8_t)pad_id;
    SONG.arr_len++;
    say("BAR ADDED");
  } else if (pad_id == 15) {
    if (!SONG.arr_len) {
      say("NO ARRANGEMENT");
      return;
    }
    if (!confirm("PRESS AGAIN: CLEAR ARR")) {
      return;
    }
    undo_mark("ARR", 0);
    SONG.arr_len = 0;
    say("ARR CLEARED - F2: UNDO");
  } else if (SONG.arr_len) {
    undo_open(3);
    SONG.arr_len--;
    say("BAR REMOVED");
  }
}

/* F1-F6 on the SONG page */
static void soft_song(int f) {
  switch (f) {
  case 0:
    mode_toggle();
    break;
  case 1:
    song_undo();
    break;
  case 2:
    pattern_clear();
    break;
  case 3:
    if (shift) {
      song_demo();
    } else {
      song_new();
    }
    break;
  case 4:
    /* SEND: the 16 sounds of the MUSIC kit and the song, as SysEx (fw_link.h, export_pump) */
    export_at = 0;
    say(linked ? "SENDING" : "SENDING - NO GAME SEEN");
    out_pump();
    break;
  case 5:
    save_say();
    break;
  }
}

/* ---- MIX and MASTER -------------------------------------------------------------------------------------------- */

/* F1-F6 on the MIX and MASTER pages */
static void soft_mix(int f) {
  switch (f) {
  case 0:
    SONG.mute ^= (uint16_t)(1u << sel);
    say((SONG.mute >> sel) & 1 ? "MUTED" : "UNMUTED");
    break;
  case 1:
    SONG.solo ^= (uint16_t)(1u << sel);
    say((SONG.solo >> sel) & 1 ? "SOLO" : "SOLO OFF");
    break;
  case 2:
    /* RESET. On MASTER the master only: faders, pans, mute and solo are kept over it. On MIX the selected fader */
    if (page == PG_MASTER) {
      uint16_t m = SONG.mute;
      uint16_t so = SONG.solo;
      uint8_t v[SLOTS];
      uint8_t pn[SLOTS];
      for (int t = 0; t < SLOTS; t++) {
        v[t] = SONG.vol[t];
        pn[t] = SONG.pan[t];
      }
      sfx_song_mix_init(&SONG);
      for (int t = 0; t < SLOTS; t++) {
        SONG.vol[t] = v[t];
        SONG.pan[t] = pn[t];
      }
      SONG.mute = m;
      SONG.solo = so;
      say("MASTER RESET");
    } else {
      SONG.vol[sel] = 100;
      SONG.pan[sel] = 64;
      say("FADER RESET");
    }
    break;
  case 3:
    SONG.solo = 0;
    SONG.mute = 0;
    say("ALL ON");
    break;
  case 4:
    seq_play(!running);
    break;
  case 5:
    save_say();
    break;
  }
}

/* ---- PERFORM --------------------------------------------------------------------------------------------------- */

/* what the pads are on PERFORM: PERF_... A pad that is held is let go of as a step; every pad's light is set again */
static void perform_mode(int m) {
  perf_mode = m;
  held_step = -1;
  held_pad = -1;
  for (int i = 0; i < SLOTS; i++) {
    led_cache[i] = 0xFFFFFFFFu;
  }
}

/* F1-F6 on the PERFORM page. F1-F4 choose what the pads are; F5 FILL and F6 UNDO are SEQ's F3 and F2. With SHIFT:
   what PLAY plays, CLEAR the selected sound, ALL ON (no mute, no solo), COPY the pattern, and the grid. Each of these
   is the key of another page, and F6 takes back what F2 takes back there. (What they say names F6 where it names F2
   there. The page has no SAVE: that is F6 on SEQ.) */
static void soft_perform(int f) {
  if (shift) {
    switch (f) {
    case 0:
      mode_toggle();
      break;
    case 1:
      sound_clear();
      break;
    case 2:
      SONG.solo = 0;
      SONG.mute = 0;
      say("ALL ON");
      break;
    case 3:
      pattern_copy();
      break;
    default:
      grid_change(f == 4 ? -1 : 1);
      break;
    }
    return;
  }
  if (f < PERF_COUNT) {
    perform_mode(f);
  } else if (f == 4) {
    fill();
  } else {
    song_undo();
  }
}
