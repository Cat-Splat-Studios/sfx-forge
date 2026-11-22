/* fw_pages.h – the pages, and what the arrows and the encoders do on each of them. A part of sfx-forge.c.
 *
 * Holds:  set_page (a page is opened), tab_key (K1-K6), window_step and arrow (LEFT / RIGHT),
 *         mix_value (the byte behind a value of MIX or MASTER),
 *         param_nudge / param_put (one of a page's four values, by steps or to a place),
 *         encoder (every encoder, on every page).
 * Needs:  fw_state.h (the pages, PAGE_PARAMS), fw_edit.h, fw_sequencer.h (the steps and the lanes).
 * Read:   "the four values" of a page are what encoders 1-4 turn and what the display shows over them. Three kinds:
 *         a sound page's (a row of PAGE_PARAMS: values of the selected sound), MIX and MASTER's (bytes of the song:
 *         mix_value), and those of SEQ, SONG, AUTO and LINK, which encoder() handles one by one.
 */

/* a page is opened. p: PG_... */
static void set_page(int p) {
  /* AUTO opens on the knob turned last */
  if (p == PG_AUTO && page != PG_AUTO && last_to && last_sel == sel) {
    view_to[sel] = last_to;
  }
  follow_end();
  page = p;
  held_step = -1;
  held_pad = -1;
  f4_held = 0;
  undo_close();
  /* the song's pages: its own sounds */
  if (p >= PG_SEQ && p <= PG_MASTER) {
    kit_set(KIT_MUSIC);
  }
  /* where patterns are made PLAY loops one; on SONG it plays the song */
  if (!running && !rec && !tour_on) {
    if (p == PG_SEQ || p == PG_AUTO) {
      mode_set(1);
    } else if (p == PG_SONG) {
      mode_set(0);
    }
  }
  /* F1-F6 and the two K keys that have several pages say what they are now; every pad's light is set again */
  relabel();
  vibe_label(VB_K1 + TAB_SEQ, p == PG_SONG ? "SONG" : p == PG_AUTO ? "AUTO" : "SEQ");
  vibe_label(VB_K1 + TAB_FX, p == PG_MASTER ? "MASTER" : p == PG_MIX ? "MIX" : "FX");
  for (int i = 0; i < SLOTS; i++) {
    led_cache[i] = 0xFFFFFFFFu;
  }
}

/* a K key: its page – or, on it already, its next one (SOUND has three pages, a fourth for a
   wavetable; SEQ has SONG and AUTO; FX has MIX and MASTER) */
static void tab_key(int tab) {
  if (TAB_OF[page] != tab) {
    set_page(TAB_FIRST[tab]);
    return;
  }
  if (tab == TAB_SOUND) {
    int last = patches[sel].engine == SFX_ENGINE_WAVETABLE ? PG_WAVE : PG_FILTER;
    set_page(page >= last ? PG_OSC : page + 1);
  } else if (tab == TAB_SEQ) {
    set_page(page == PG_SEQ ? PG_SONG : page == PG_SONG ? PG_AUTO : PG_SEQ);
  } else if (tab == TAB_FX) {
    set_page(page == PG_FX ? PG_MIX : page == PG_MIX ? PG_MASTER : PG_FX);
  }
}

/* SEQ and AUTO, SHIFT + LEFT / RIGHT: the 16 steps before / after (a pattern of 32 or 64 steps has two or four such
   windows - fewer when the bar is short: only those with a step that plays) */
static void window_step(int d) {
  static char t[24];
  /* lim: the steps of the bar. wins: the windows that hold a step of the bar. w: the window the cursor is in */
  int lim = bar_len() * pat_div(edit_pat);
  int wins = (lim + PADS - 1) / PADS;
  int w = cur_step / PADS;
  if (gsteps() <= PADS) {
    say("1/16: ALL STEPS SHOWN");
    return;
  }
  /* (a short bar: all of it is shown) */
  if (wins == 1 && w == 0) {
    strcpy(t, "BAR ENDS AT STEP ");
    vg_itoa(lim, t + 17);
    say(t);
    return;
  }
  /* (the cursor beyond the bar: to its first or its last window) */
  w = wrap(w < wins ? w : d > 0 ? wins - 1 : 0, d, wins);
  cur_step = clampi(w * PADS + cur_step % PADS, 0, lim - 1);
  held_step = -1;
  held_pad = -1;
  /* "STEPS 17-32" */
  strcpy(t, "STEPS ");
  vg_itoa(w * PADS + 1, t + 6);
  strcpy(t + strlen(t), "-");
  vg_itoa(w * PADS + PADS, t + strlen(t));
  say(t);
}

/* LEFT / RIGHT: the sound page before or after (WAVE only for a wavetable) – on SEQ, SONG and AUTO the pattern */
static void arrow(int d) {
  if (shift && (page == PG_SEQ || page == PG_AUTO)) {
    window_step(d);
    return;
  }
  if (song_keys()) {
    pattern_step(d);
    return;
  }
  if (page >= PG_SEQ) {
    return;
  }
  int to = page;
  do {
    to = wrap(to, d, PG_SEQ);
  } while (to == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE);
  set_page(to);
}

/* MIX: the selected sound's fader and pan, the drive, the output. MASTER: the delay's time and feedback, the reverb's
   size and damping. The byte behind value i (0: the output volume, a host parameter), and how far it goes. */
static uint8_t *mix_value(int pg, int i, int *hi) {
  *hi = 127;
  if (pg == PG_MIX) {
    // clang-format off
    return i == 0 ? &SONG.vol[sel]
         : i == 1 ? &SONG.pan[sel]
         : i == 2 ? &SONG.fx[SFX_M_DRIVE]
         :          0;
    // clang-format on
  }
  if (i == 0) {
    *hi = SFX_DELAY_TIMES - 1;
  }
  // clang-format off
  return &SONG.fx[i == 0 ? SFX_M_DELAY_TIME
                : i == 1 ? SFX_M_DELAY_FB
                : i == 2 ? SFX_M_REVERB_SIZE
                :          SFX_M_REVERB_DAMP];
  // clang-format on
}

/* one of the four values of a page (pg: AUTO, MIX, MASTER or a sound page), by steps: an encoder, the wheel.
   i: which of the four (0-3). d: the steps, + or - */
static void param_nudge(int pg, int i, int d) {
  /* AUTO: the step, the point's value, the lane's target */
  if (pg == PG_AUTO) {
    if (i == 0) {
      cur_step = clampi(cur_step + d, 0, gsteps() - 1);
    }
    if (i == 1) {
      auto_turn(d);
    }
    if (i == 2) {
      view_to[sel] = (uint8_t)wrap(view_to[sel], d > 0 ? 1 : -1, SFX_AT_COUNT);
    }
    return;
  }

  /* MIX and MASTER: a byte of the song, or the output volume. The knob is remembered: AUTO opens on it */
  if (pg == PG_MIX || pg == PG_MASTER) {
    int hi;
    uint8_t *v = i >= 0 && i < 4 ? mix_value(pg, i, &hi) : 0;
    if (v) {
      *v = (uint8_t)clampi(*v + d, 0, hi);
    } else if (i == 3) {
      vibe_param_set(P_VOLUME, vd_clamp(vibe_param_get(P_VOLUME) + (float)d * 0.02f, 0.0f, 1.0f));
    }
    if (v) {
      last_sel = sel;
      // clang-format off
      last_to = (uint8_t)(pg == PG_MASTER ? SFX_AT_MASTER + i
                        : i == 0          ? SFX_AT_VOL
                        : i == 1          ? SFX_AT_PAN
                        :                   SFX_AT_MASTER + SFX_M_DRIVE);
      // clang-format on
    }
    return;
  }

  /* a sound page: a value of the selected sound (k), or its engine, or its wavetable */
  sfx_patch *p = &patches[sel];
  int k = pg < PG_SEQ && i >= 0 && i < 4 ? PAGE_PARAMS[pg][i] : -1;
  if (k >= 0 && kit == KIT_MUSIC) {
    last_sel = sel;
    last_to = (uint8_t)(SFX_AT_PATCH + k);
  }
  if (k == ENGINE_KNOB) {
    p->engine = (uint8_t)wrap(p->engine, d, SFX_ENGINE_COUNT);
    pad_preset[sel] = -1;
  } else if (k == SFX_P_TABLE) {
    p->p[k] = (uint8_t)wrap(p->p[k] % SFX_WT_TABLES, d, SFX_WT_TABLES);
  } else if (k >= 0) {
    p->p[k] = (uint8_t)clampi(p->p[k] + d, 0, 127);
  } else {
    return;
  }
  changed(sel);
}

/* the same value, to a place: dragged on the stage. u: 0..1 along its range */
static void param_put(int pg, int i, float u) {
  if (pg == PG_AUTO) {
    int to = view_to[sel];
    if (i == 0) {
      cur_step = clampi((int)(u * (float)gsteps()), 0, gsteps() - 1);
    }
    if (i == 1 && to) {
      undo_open(1);
      aut_put(sel, cur_step, clampi((int)(u * (float)auto_hi(to) + 0.5f), 0, auto_hi(to)) + 1);
    }
    if (i == 2) {
      view_to[sel] = (uint8_t)clampi((int)(u * (float)SFX_AT_COUNT), 0, SFX_AT_COUNT - 1);
    }
    return;
  }

  if (pg == PG_MIX || pg == PG_MASTER) {
    int hi;
    uint8_t *v = i >= 0 && i < 4 ? mix_value(pg, i, &hi) : 0;
    if (v) {
      *v = (uint8_t)clampi((int)(u * (float)hi + 0.5f), 0, hi);
    } else if (i == 3) {
      vibe_param_set(P_VOLUME, vd_clamp(u, 0.0f, 1.0f));
    }
    return;
  }

  /* a sound page: how far the value is from that place (d), and param_nudge goes there */
  const sfx_patch *p = &patches[sel];
  int k = pg < PG_SEQ && i >= 0 && i < 4 ? PAGE_PARAMS[pg][i] : -1;
  int d = 0;
  if (k == ENGINE_KNOB) {
    d = clampi((int)(u * (float)SFX_ENGINE_COUNT), 0, SFX_ENGINE_COUNT - 1) - p->engine;
  } else if (k == SFX_P_TABLE) {
    d = clampi((int)(u * (float)SFX_WT_TABLES), 0, SFX_WT_TABLES - 1) - p->p[k] % SFX_WT_TABLES;
  } else if (k >= 0) {
    d = (int)(u * 127.0f + 0.5f) - p->p[k];
  }
  if (d) {
    param_nudge(pg, i, d);
  }
}

/* An encoder was turned. id: VE_1..VE_4 (the four values), VE_A, VE_B, VE_DATA. turn: clicks, + or -.
   In this order: the turn is gathered into whole steps (SHIFT: eight times as fast); DATA selects a sound; A is the
   tempo on the song's pages and the output volume elsewhere; then one block for each of LINK, AUTO, SEQ and SONG; any
   other page's four values go to param_nudge. */
static void encoder(int id, float turn) {
  acc[id] += turn * (shift ? 8.0f : 1.0f);
  int d = (int)acc[id];
  if (!d) {
    return;
  }
  acc[id] -= (float)d;

  /* (the display writes the knob in the hand large for a moment: touched, fw_draw_parts.h) */
  if (id <= VE_4) {
    touch_i = id;
    touch_t = vibe_frames();
  }
  if (id == VE_DATA) {
    sel = wrap(sel, d, SLOTS);
    return;
  }

  /* SEQ, SONG, AUTO: A is the tempo (the top line shows it), with SHIFT the swing */
  if (id == VE_A && song_keys()) {
    if (shift) {
      static char t[12] = "SWING ";
      SONG.swing = (uint8_t)clampi(SONG.swing + (d > 0 ? 2 : -2), 0, 100);
      vg_itoa(SONG.swing, t + 6);
      strcpy(t + strlen(t), "%");
      say(t);
    } else {
      SONG.bpm = (uint16_t)clampi(SONG.bpm + d, 40, 240);
    }
    return;
  }
  /* every other page but LINK: A is the output volume */
  if (id == VE_A && page != PG_LINK) {
    vibe_param_set(P_VOLUME, vd_clamp(vibe_param_get(P_VOLUME) + (float)d * 0.02f, 0.0f, 1.0f));
    return;
  }

  /* LINK: the encoders are the game engine's: CC 20..25 */
  if (page == PG_LINK) {
    /* k: which of the six CCs - encoders 1-4, then A and B (-1: DATA, not one of them) */
    // clang-format off
    int k = id <= VE_4  ? id
          : id == VE_A  ? 4
          : id == VE_B  ? 5
          :               -1;
    // clang-format on
    if (k < 0) {
      return;
    }
    cc_out[k] = (uint8_t)clampi(cc_out[k] + d, 0, 127);
    vibe_midi_send(0xB0, (uint8_t)(CC_ENCODERS + k), cc_out[k]);
    return;
  }

  /* AUTO: 1 the step, 2 the point's value, 3 the target */
  if (page == PG_AUTO) {
    if (id == VE_1) {
      cur_step = wrap(cur_step, d, gsteps());
    }
    if (id == VE_2) {
      auto_turn(d);
    }
    if (id == VE_3) {
      view_to[sel] = (uint8_t)wrap(view_to[sel], d > 0 ? 1 : -1, SFX_AT_COUNT);
    }
    return;
  }

  /* SEQ: 1 the step, 2 its note, 3 its velocity, 4 its length; B the scale */
  if (page == PG_SEQ) {
    if (id == VE_1) {
      cur_step = wrap(cur_step, d, gsteps());
    }
    if (id == VE_2) {
      note_turn(d);
    }
    if (id == VE_3) {
      vel_turn(d > 0 ? 1 : -1);
    }
    /* (a finer grid has more to cross: SHIFT is 8 a click there) */
    if (id == VE_4) {
      len_turn(gsteps() == PADS ? (d > 0 ? 1 : -1) : d);
    }
    /* (said in the top line) */
    if (id == VE_B) {
      SONG.scale = (uint8_t)wrap(SONG.scale, d, SFX_SCALE_COUNT);
      say(SCALE_NAME[SONG.scale]);
    }
    return;
  }

  /* SONG: 1 the pattern, 2 the chain, 3 the intensity, 4 the selected sound's layer; B the bar's length */
  if (page == PG_SONG) {
    if (id == VE_1) {
      pattern_step(d);
    }
    if (id == VE_2) {
      if (SONG.arr_len) {
        say("ARRANGEMENT IN USE");
      } else {
        SONG.chain = (uint8_t)clampi(SONG.chain + d, 1, SFX_PATTERNS);
      }
    }
    if (id == VE_3) {
      intensity_set(vibe_param_get(P_INTENSITY) + (float)d * 0.02f);
    }
    if (id == VE_4) {
      SONG.level[sel] = (uint8_t)clampi(SONG.level[sel] + d * 2, 0, 127);
    }
    if (id == VE_B) {
      static char t[16];
      SONG.len = (uint8_t)clampi(SONG.len + d, 1, SFX_BAR);
      strcpy(t, "BAR: ");
      vg_itoa(SONG.len, t + 5);
      strcpy(t + strlen(t), "/16");
      say(t);
    }
    return;
  }

  /* the sound pages, MIX and MASTER */
  if (id <= VE_4) {
    param_nudge(page, id, d);
  }
}
