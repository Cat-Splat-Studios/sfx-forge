/* fw_stage_input.h – the stage: where its things are, and what the mouse does on it. A part of sfx-forge.c.
 *
 * The stage is 960 x 540 – the whole instrument for the mouse (the plugin's window, the browser). Left: the pads and
 * the selected sound (its picture, its pages, four values to drag, its keys). Right: the song – every sound a row of
 * the pattern's steps (16, 32 or 64 in the same width), intensity, the layer.
 *
 * Holds:  the layout (the #defines below: fw_stage_draw.h draws at the same numbers); stage_fb, the picture itself;
 *         inside, stage_cell_at, stage_page_follows; stage_auto and stage_dragged (a drag); stage_event (every click,
 *         move, release and turn of the wheel).
 * Needs:  everything before it: the stage calls what the keys call (pad, step_toggle, soft_sound, grid_change, ...).
 * Read:   stage_event is one long chain of "is the mouse inside this rectangle": the rectangles are in the order of
 *         the list at its top. A thing moved on the stage is moved in the layout here, and both files follow. Widths
 *         and heights of single buttons (76 x 36 for PLAY, ...) are still plain numbers, the same in both files.
 */

/* ---- the layout, in stage pixels -------------------------------------------------------------------------------
   The names: the first letters say what, _X _Y where its top left is, _W _H its size, _PX _PY the pitch from one to
   the next, _N how many. */
#define SW 960
#define SH 540
static uint16_t stage_fb[SW * SH];

/* the 16 pads, four by four */
#define SP_X  24
#define SP_Y  64
#define SP_W  102
#define SP_H  54
#define SP_PX 108
#define SP_PY 60

/* the tabs under the sound's picture: ten pages */
#define ST_Y  404
#define ST_PX 43
#define ST_N  10
/* the page of each tab. Their names, in the same order: TAB in on_stage (fw_stage_draw.h) - the two must match.
   (No size is written, so that the assert sees a tab without a page.) */
// clang-format off
static const uint8_t ST_PAGE[] = {
  PG_OSC, PG_TIMBRE, PG_FILTER, PG_WAVE, PG_ENV, PG_LFO, PG_FX, /* the sound's seven pages */
  PG_MIX, PG_MASTER, PG_AUTO                                    /* three of the song's */
};
// clang-format on
_Static_assert(sizeof ST_PAGE / sizeof ST_PAGE[0] == ST_N, "a page for every tab of the stage");

/* the page's four values (as wide as the pads' columns) */
#define SC_Y 430
#define SC_H 56

/* PRESET MUTATE UNDO RANDOM EXPORT SAVE */
#define SA_Y  494
#define SA_PX 72

/* the song: the sounds' names (SN), then the grid of steps (SG) - a row a sound, SG_PX a sixteenth */
#define SN_X  466
#define SG_X  552
#define SG_Y  64
#define SG_PX 24
#define SG_PY 22
#define SG_W  (SFX_BAR * SG_PX)

/* the pattern's grid: a click makes it finer, SHIFT or the right button coarser */
#define GC_X 788
#define GC_Y 42

/* INTENSITY, and under it the selected sound's LAYER */
#define SI_Y 426
#define SL_Y 450

/* FILL CLEAR COPY UNDO DEMO */
#define SF_X  712
#define SF_Y  476
#define SF_PX 46
#define SF_N  5

/* the top row: LOOP / SONG, PLAY REC, tempo, the patterns, TOUR */
#define TB_Y 14
/* what PLAY plays: a click changes it */
#define TM_X 126
/* PLAY, REC, the tempo, TOUR */
#define TB_PLAY_X  190
#define TB_REC_X   272
#define TB_TEMPO_X 342
#define TB_TOUR_X  856
/* the eight patterns */
#define TP_X  462
#define TP_PX 40
/* the arrangement, under the patterns: 32 bars */
#define TA_Y  (TB_Y + 23)
#define TA_PX 10

/* the line over the sound's picture: its number and name, and the kit's two buttons */
#define SK_Y 304

/* what the mouse drags */
enum {
  SD_NONE,  /* nothing */
  SD_HEAT,  /* the INTENSITY bar */
  SD_LAYER, /* the LAYER bar */
  SD_PARAM, /* one of the four values (stage_drag_i) */
  SD_AUTO   /* points into a lane */
};

/* what the mouse drags (and which of the four values); where it is; the sound page shown */
static int stage_drag;
static int stage_drag_i;
static int stage_x;
static int stage_y;
static int spage;

/* ---- helpers --------------------------------------------------------------------------------------------------- */

/* the point is inside the rectangle */
static int inside(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

/* which of the four values is there (0-3), -1: none */
static int stage_cell_at(int x, int y) {
  return inside(x, y, SP_X, SC_Y, 4 * SP_PX, SC_H) && (x - SP_X) % SP_PX < SP_W ? (x - SP_X) / SP_PX : -1;
}

/* the device is on a page the stage has a tab for: the stage shows that one */
static int stage_page_follows(void) {
  return sound_page() || mix_page() || page == PG_AUTO;
}

/* ---- dragging -------------------------------------------------------------------------------------------------- */

/* the AUTO tab turns the rows into lanes: a click or a drag in the selected sound's row puts points where the mouse
   is - the step from x, the value from how high in the row */
static void stage_auto(int x, int y) {
  int to = view_to[sel];
  int step = clampi((x - SG_X) * gsteps() / SG_W, 0, gsteps() - 1);
  cur_step = step;
  if (!to) {
    say("CHOOSE A TARGET FIRST");
    return;
  }
  undo_open(1);
  aut_put(sel, step, clampi((SG_Y + sel * SG_PY + 19 - y) * auto_hi(to) / 19, 0, auto_hi(to)) + 1);
}

/* the mouse moved with something held: the intensity, the layer, or one of the four values follows it */
static void stage_dragged(int x) {
  float u = vd_clamp((float)(x - SG_X) / (float)(SG_W - 2), 0.0f, 1.0f);
  if (stage_drag == SD_HEAT) {
    intensity_set(u);
  }
  if (stage_drag == SD_LAYER) {
    SONG.level[sel] = (uint8_t)(u * 127.0f + 0.5f);
  }
  if (stage_drag == SD_PARAM) {
    param_put(spage, stage_drag_i,
              vd_clamp((float)(x - SP_X - stage_drag_i * SP_PX - 8) / (float)(SP_W - 16), 0.0f, 1.0f));
  }
}

/* ---- the mouse ------------------------------------------------------------------------------------------------- */
/* Every event of the stage. e->id says which: the wheel, the button down, a move, the button up. e->midi[1] holds the
   keys held with it (VIBE_STAGE_MOD_SHIFT, _CTRL) and the right button (_RIGHT). In this order:
     the wheel      the value under the mouse – over the steps, the step's note
     up, move       a held pad is let go, a drag ends; a drag goes on
     down           (it ends the tour, and nothing else, while the tour runs) then the first rectangle it is in:
                      the pads · the kit's two buttons · the song's rows (a lane's point on the AUTO tab, else a
                      step) · a row's name · the INTENSITY bar · the LAYER bar · one of the four values · the tabs ·
                      PRESET..SAVE · LOOP/SONG · GRID · PLAY · REC · the tempo · the patterns · the arrangement ·
                      TOUR · FILL..DEMO */
static void stage_event(const vibe_event *e) {
  int x = VIBE_STAGE_X(e);
  int y = VIBE_STAGE_Y(e);
  int was = shift;

  /* the wheel: the value under the mouse – over the steps, the step's note */
  if (e->id == VIBE_STAGE_WHEEL) {
    int d = e->value > 0.0f ? 1 : -1;
    int c = stage_cell_at(stage_x, stage_y);
    if (c >= 0) {
      /* one of the four values (SHIFT: eight steps) */
      param_nudge(spage, c, d * ((e->midi[1] & VIBE_STAGE_MOD_SHIFT) ? 8 : 1));
    } else if (inside(stage_x, stage_y, TB_TEMPO_X, TB_Y, 104, 36)) {
      SONG.bpm = (uint16_t)clampi(SONG.bpm + d, 40, 240);
    } else if (inside(stage_x, stage_y, SG_X, SI_Y, SG_W, 18)) {
      intensity_set(vibe_param_get(P_INTENSITY) + (float)d * 0.02f);
    } else if (inside(stage_x, stage_y, SG_X, SL_Y, SG_W, 18)) {
      SONG.level[sel] = (uint8_t)clampi(SONG.level[sel] + d * 2, 0, 127);
    } else if (spage == PG_AUTO) {
      auto_turn(d);
    } else if (e->midi[1] & VIBE_STAGE_MOD_SHIFT) {
      vel_turn(d);
    } else if (e->midi[1] & VIBE_STAGE_MOD_CTRL) {
      len_turn(d);
    } else {
      note_turn(d);
    }
    return;
  }
  if (e->id != VIBE_STAGE_DOWN && e->id != VIBE_STAGE_MOVE && e->id != VIBE_STAGE_UP) {
    return;
  }
  stage_x = x;
  stage_y = y;

  /* the button up */
  if (e->id == VIBE_STAGE_UP) {
    if (stage_pad >= 0) {
      pad(stage_pad, 0, 0);
      stage_pad = -1;
    }
    stage_drag = SD_NONE;
    return;
  }

  /* a move: with the button down, what is dragged follows */
  if (e->id == VIBE_STAGE_MOVE) {
    if (e->down && stage_drag == SD_AUTO) {
      stage_auto(x, y);
    } else if (e->down && stage_drag) {
      stage_dragged(x);
    }
    return;
  }

  /* the button down */
  if (tour_on) {
    tour_end();
    return;
  }
  /* px, py: the mouse from the pads' corner. gx, gy: from the grid's corner. c: which of the four values, or -1 */
  int px = x - SP_X;
  int py = y - SP_Y;
  int gx = x - SG_X;
  int gy = y - SG_Y;
  int c = stage_cell_at(x, y);
  if (inside(x, y, SP_X, SP_Y, 4 * SP_PX, 4 * SP_PY) && px % SP_PX < SP_W && py % SP_PY < SP_H) {
    /* a pad */
    stage_pad = py / SP_PY * 4 + px / SP_PX;
    pad(stage_pad, 1, 0.9f);
  } else if (inside(x, y, SP_X + 312, SK_Y, 114, 24)) {
    /* SFX | MUSIC: the kit the pads show */
    kit_set(x < SP_X + 370 ? KIT_SFX : KIT_MUSIC);
    if (kit == KIT_SFX && spage >= PG_SEQ) {
      spage = PG_OSC;
    }
  } else if (gx >= 0 && gy >= 0 && gx < SG_W && gy < SLOTS * SG_PY && spage == PG_AUTO) {
    /* a lane: a point – SHIFT or the right button takes it away */
    kit_set(KIT_MUSIC);
    sel = gy / SG_PY;
    cur_step = clampi(gx * gsteps() / SG_W, 0, gsteps() - 1);
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) {
      undo_open(1);
      aut_put(sel, cur_step, 0);
    } else {
      stage_drag = SD_AUTO;
      stage_auto(x, y);
    }
  } else if (gx >= 0 && gy >= 0 && gx < SG_W && gy < SLOTS * SG_PY) {
    /* a step of a sound's row: on / off */
    kit_set(KIT_MUSIC);
    sel = gy / SG_PY;
    step_toggle(clampi(gx * gsteps() / SG_W, 0, gsteps() - 1), sel);
  } else if (inside(x, y, SN_X, SG_Y, SG_X - SN_X, SLOTS * SG_PY)) {
    /* a row's name: that sound – SHIFT or the right button: mute */
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) {
      SONG.mute ^= (uint16_t)(1u << (gy / SG_PY));
    } else {
      kit_set(KIT_MUSIC);
      stage_pad = gy / SG_PY;
      pad(stage_pad, 1, 0.9f);
    }
  } else if (inside(x, y, SG_X, SI_Y - 4, SG_W, 22)) {
    /* the INTENSITY bar */
    stage_drag = SD_HEAT;
    stage_dragged(x);
  } else if (inside(x, y, SG_X, SL_Y, SG_W, 20)) {
    /* the LAYER bar */
    stage_drag = SD_LAYER;
    stage_dragged(x);
  } else if (c >= 0) {
    /* one of the four values: a drag (UNDO takes a drag back) */
    keep_undo(sel);
    stage_drag = SD_PARAM;
    stage_drag_i = c;
    stage_dragged(x);
  } else if (inside(x, y, SP_X, ST_Y, ST_N * ST_PX, 20)) {
    /* a tab: that page (WAVE only for a wavetable) */
    int k = ST_PAGE[(x - SP_X) / ST_PX];
    if (k == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE) {
      return;
    }
    /* MIX, MASTER, AUTO: the song's */
    if (k >= PG_SEQ) {
      kit_set(KIT_MUSIC);
    }
    spage = k;
    if (stage_page_follows()) {
      set_page(k);
    }
  } else if (inside(x, y, SP_X, SA_Y, 6 * SA_PX, 32)) {
    /* PRESET .. SAVE: the sound pages' F1-F6 (PRESET: SHIFT or the right button = the one before) */
    shift = (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) != 0;
    soft_sound((x - SP_X) / SA_PX);
    shift = was;
  } else if (inside(x, y, TM_X, TB_Y, 58, 36)) {
    /* LOOP / SONG */
    mode_toggle();
  } else if (inside(x, y, GC_X, GC_Y, 62, 14)) {
    /* GRID: finer – SHIFT or the right button: coarser */
    grid_change((e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) ? -1 : 1);
  } else if (inside(x, y, TB_PLAY_X, TB_Y, 76, 36)) {
    /* PLAY / STOP */
    seq_play(!running);
  } else if (inside(x, y, TB_REC_X, TB_Y, 64, 36)) {
    /* REC - on PERFORM it makes the pads the kit, as the REC key does */
    seq_rec(!rec);
    if (rec && page == PG_PERFORM) {
      perform_mode(PERF_SOUNDS);
    }
  } else if (inside(x, y, TB_TEMPO_X, TB_Y, 104, 36)) {
    /* the tempo: its left half slower, its right half faster */
    SONG.bpm = (uint16_t)clampi(SONG.bpm + (x < 394 ? -1 : 1), 40, 240);
  } else if (inside(x, y, TP_X, TB_Y, 8 * TP_PX, 21)) {
    /* a pattern – SHIFT or the right button: one more bar of it */
    int k = (x - TP_X) / TP_PX;
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) {
      arr_edit(k);
    } else {
      pattern_pick(k);
    }
  } else if (inside(x, y, TP_X, TA_Y, SFX_ARR_MAX * TA_PX, 15)) {
    /* a bar: its pattern – SHIFT or right: the arrangement ends before it */
    int k = (x - TP_X) / TA_PX;
    if (k >= SONG.arr_len) {
      return;
    }
    if (e->midi[1] & (VIBE_STAGE_MOD_SHIFT | VIBE_STAGE_MOD_RIGHT)) {
      undo_mark("ARR", 0);
      SONG.arr_len = (uint8_t)k;
      say(k ? "BARS REMOVED" : "ARR CLEARED - F2: UNDO");
    } else {
      pattern_pick(SONG.arr[k]);
    }
  } else if (inside(x, y, TB_TOUR_X, TB_Y, 80, 36)) {
    /* TOUR */
    tour_start();
  } else if (inside(x, y, SF_X, SF_Y, SF_N * SF_PX, 30)) {
    /* FILL CLEAR COPY UNDO DEMO - on the AUTO tab the first two are RAMP and the lane's CLEAR */
    int k = (x - SF_X) / SF_PX;
    if (k == 0) {
      if (spage == PG_AUTO) {
        auto_ramp();
      } else {
        fill();
      }
    } else if (k == 1) {
      if (spage == PG_AUTO) {
        soft_auto(3);
      } else {
        soft_seq(3);
      }
    } else if (k == 2) {
      pattern_copy();
    } else if (k == 3) {
      song_undo();
    } else {
      /* (DEMO asks twice, as on the device) */
      song_demo();
    }
  }
}
