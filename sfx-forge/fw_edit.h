/* fw_edit.h – what every edit leans on: a word on the display, one step back, a key that asks twice.
 * A part of sfx-forge.c.
 *
 * Holds:  say / toast_on (what the display says for a moment); changed (a sound was edited: tell the audio);
 *         keep_undo (a sound, before it is changed); the names of the pads; kit_set;
 *         the song's undo (undo_snap, undo_open, undo_mark, undo_close, song_undo); confirm; pattern_holds.
 * Needs:  fw_state.h, fw_audio.h (send), fw_sounds.h (PRESETS, ENGINE_NAME).
 * Read:   the song's undo is ONE copy of the song (undo_song). Whatever is about to change the song calls undo_mark
 *         (a single act: "FILL", "COPY") or undo_open (a run of small edits, a take, bars added: one step for all of
 *         them) BEFORE it writes. F2 swaps the song and the copy, so a second F2 is the redo.
 */

/* ---- a word on the display ------------------------------------------------------------------------------------- */

/* a toast stays 2.25 s of the audio's clock, whatever draws it and how often (the device, the stage, both) */
static void say(const char *t) {
  toast = t;
  toast_t = vibe_frames();
  toast_live = 1;
}

static int toast_on(void) {
  return toast_live && vibe_frames() - toast_t < (uint64_t)(2.25f * SR);
}

/* (a song came: the AUTO pages follow – fw_sequencer.h) */
static void view_sync(void);

/* ---- the sounds ------------------------------------------------------------------------------------------------ */

/* A sound of the CHOSEN kit was edited: its patch goes to the player. (patches is kits[kit], and send() takes the
   patch from it: for a sound of the other kit set kit first and put it back, as factory_reset and sysex_in do. The
   same holds for keep_undo below and for preset_load.) */
static void changed(int slot) {
  send(M_UPDATE, slot, 0, 0, 0);
}

/* before a sound is changed: what F3 UNDO on the sound pages brings back */
static void keep_undo(int slot) {
  sound_undo[slot] = patches[slot];
  sound_undo_preset[slot] = pad_preset[slot];
  sound_has_undo[slot] = 1;
}

/* the pattern that is shown and edited */
static sfx_pattern *pat(void) {
  return &SONG.pat[edit_pat];
}

/* what a pad of kit k is called: its named sound, or - once it is its own - its engine */
static const char *name_of(int k, int slot) {
  int n = pad_preset_k[k][slot];
  return n >= 0 && n < PRESET_COUNT ? PRESETS[n].name : ENGINE_NAME[kits[k][slot].engine];
}

static const char *pad_name(int slot) {
  return name_of(kit, slot);
}

/* the other kit is chosen: every pad's light is set again */
static void kit_set(int k) {
  if (k == kit) {
    return;
  }
  kit = k;
  for (int i = 0; i < SLOTS; i++) {
    led_cache[i] = 0xFFFFFFFFu;
  }
}

/* ---- one step back for the song (F2) --------------------------------------------------------------------------- */
/* A run of edits, or a take, is one step however many writes follow.
   While a take runs (REC lit, playing) whatever changes the notes belongs to the take: a knob turned or a key pressed
   in between does not cut it in two. A run of edits ends with the page or the pattern (set_page, pattern_pick). */

/* the song as it is now is what F2 brings back. what: the word for it. whole: tempo and mix too, else its notes */
static void undo_snap(const char *what, int whole) {
  undo_song = SONG;
  undo_has = 1;
  undo_redo = 0;
  undo_run = 0;
  undo_what = what;
  undo_whole = whole;
  undo_pat = -1;
}

/* before an edit that is one of a run: 1 = edits, 2 = a take, 3 = bars of the arrangement. Only the first of a run
   keeps the song */
static void undo_open(int run) {
  static const char *const WHAT[4] = {"", "EDITS", "TAKE", "BARS"};
  if (rec && running) {
    run = 2;
  }
  if (undo_run != run) {
    undo_snap(WHAT[run], 0);
    undo_run = run;
  }
}

/* before an edit that is a step by itself (during a take it is part of the take, unless it replaces the whole song) */
static void undo_mark(const char *what, int whole) {
  if (!whole && rec && running) {
    undo_open(2);
  } else {
    undo_snap(what, whole);
  }
}

/* the next edit is a new step (a take ends with PLAY, STOP or REC) */
static void undo_close(void) {
  if (undo_run != 2) {
    undo_run = 0;
  }
}

/* (fw_sequencer.h) */
static void pattern_pick(int p);

static void swap_bytes(void *a, void *b, int n) {
  uint8_t *x = (uint8_t *)a;
  uint8_t *y = (uint8_t *)b;
  for (int i = 0; i < n; i++) {
    uint8_t t = x[i];
    x[i] = y[i];
    y[i] = t;
  }
}

/* F2: back – and, pressed again, forth. The notes only (patterns with their grids, lanes, arrangement), unless a whole
   song was replaced */
static void song_undo(void) {
  static char t[24];
  if (!undo_has) {
    say("NOTHING TO UNDO");
    return;
  }
  if (undo_whole) {
    swap_bytes(&SONG, &undo_song, (int)sizeof SONG);
  } else {
    swap_bytes(SONG.pat, undo_song.pat, (int)sizeof SONG.pat);
    swap_bytes(SONG.lane, undo_song.lane, (int)sizeof SONG.lane);
    swap_bytes(&SONG.arr_len, &undo_song.arr_len, 1);
    swap_bytes(SONG.arr, undo_song.arr, (int)sizeof SONG.arr);
  }
  /* (the cursor follows a grid that came back: cur_sync, with the next poll) */
  view_sync();
  undo_run = 0;
  undo_redo = !undo_redo;
  erase_at = -1;
  /* COPY: back on the pattern it was made from (and forth) */
  if (undo_pat >= 0) {
    int was = queued >= 0 ? queued : edit_pat;
    pattern_pick(undo_pat);
    undo_run = 0;
    undo_pat = was;
  }
  strcpy(t, undo_redo ? "UNDO: " : "REDO: ");
  strcpy(t + 6, undo_what);
  say(t);
}

/* ---- asking twice ---------------------------------------------------------------------------------------------- */

/* a key that asks twice: the second press counts while its question is on the display. 1 = go on */
static int confirm(const char *prompt) {
  if (toast == prompt && toast_on()) {
    toast_live = 0;
    return 1;
  }
  say(prompt);
  return 0;
}

/* pattern k holds a step or an automation point anywhere in it */
static int pattern_holds(int k) {
  const sfx_pattern *p = &SONG.pat[k];
  for (int i = 0; i < SFX_STEPS; i++) {
    if (p->on[i]) {
      return 1;
    }
    for (int l = 0; l < SFX_LANES; l++) {
      if (p->aut[l][i]) {
        return 1;
      }
    }
  }
  return 0;
}
