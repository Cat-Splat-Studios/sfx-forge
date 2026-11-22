/* fw_stage_draw.h – a whole frame of the stage (960 x 540). A part of sfx-forge.c.
 *
 * Holds:  stage_button, stage_value (one of the four values, with its bar), on_stage (vibe_on_stage in sfx-forge.c
 *         calls it for every frame of the stage).
 * Needs:  fw_stage_input.h (the layout: SP_, ST_, SC_, SA_, SN_, SG_, GC_, SI_, SL_, SF_, TB_, TM_, TP_, TA_; spage,
 *         stage_drag), fw_draw_parts.h, fw_draw_sound.h (preview), fw_draw_pages.h (dr_read, head_at, mix_text,
 *         auto_name, auto_text, silent, step_text).
 * Read:   the stage shows the same state as the display, all at once, for a mouse. What is drawn here is clicked in
 *         stage_event (fw_stage_input.h) at the same numbers: change both. The vg_... calls draw into the stage while
 *         the host calls on_stage, as they draw into the display during on_draw.
 */

/* a button: a rounded rectangle with its name in the middle */
static void stage_button(int x, int y, int w, int h, const char *name, int font, uint32_t bg, uint32_t fg) {
  vg_round(x, y, w, h, 6, bg);
  vg_text(name, x + w / 2, y + (h - vg_font_height(font)) / 2, font, fg, VG_CENTER);
}

/* one of the four values under the sound's picture: its name, its text, and a bar to drag. bar: 0..1 (below 0: no
   value there). centre: the bar grows from the middle */
static void stage_value(int i, const char *name, const char *value, float bar, int centre, uint32_t rgb) {
  int x = SP_X + i * SP_PX;
  int bw = SP_W - 16;
  vg_round(x, SC_Y, SP_W, SC_H, 6, stage_drag == SD_PARAM && stage_drag_i == i ? 0x40281E : PANEL);
  vg_text(name, x + SP_W / 2, SC_Y + 3, VG_SMALL, DIM, VG_CENTER);
  vg_text(value, x + SP_W / 2, SC_Y + 18, VG_BIG, TEXT, VG_CENTER);
  vg_rect(x + 8, SC_Y + 45, bw, 6, LINE);
  if (bar < 0.0f) {
    return;
  }
  /* at: where the value is on the bar, in pixels */
  int at = (int)((float)bw * vd_clamp(bar, 0.0f, 1.0f));
  if (!centre) {
    vg_rect(x + 8, SC_Y + 45, at, 6, rgb);
  } else {
    vg_rect(x + 8 + (at < bw / 2 ? at : bw / 2), SC_Y + 45, at < bw / 2 ? bw / 2 - at : at - bw / 2, 6, rgb);
  }
  vg_rect(x + 7 + at, SC_Y + 42, 3, 12, TEXT);
}

/* A frame of the stage, in this order:
     1  what the frame draws from; which sound page the stage shows (spage)
     2  the top row: the name, LOOP / SONG, PLAY, REC, the tempo, the eight patterns, the arrangement, LINK, GRID, TOUR
     3  left: the 16 pads
     4  left, under them: the selected sound - its name, the kit's two buttons, its picture, the tabs, the four
        values, PRESET .. SAVE
     5  right: the song - a row a sound: its name, then its steps (on the AUTO tab: its automation lane)
     6  right, under it: INTENSITY and LAYER
     7  right, at the bottom: the scope, what was just said, FILL .. DEMO - or the tour's words */
static void on_stage(uint16_t *fb, int w, int h) {
  /* the tabs' names, in the order of their pages: ST_PAGE (fw_stage_input.h) - the two must match */
  static const char *const TAB[ST_N] = {"OSC", "TONE", "FILTER", "WAVE", "ENV", "LFO", "FX", "MIX", "MAST", "AUTO"};
  /* the five keys at the bottom right: on the steps, on the AUTO tab */
  static const char *const FILLS[2][SF_N] = {{"FILL", "CLEAR", "COPY", "UNDO", "DEMO"},
                                             {"RAMP", "CLEAR", "COPY", "UNDO", "DEMO"}};
  char buf[24];
  char t[64];
  (void)fb;
  (void)w;
  (void)h;

  /* 1. p, rgb: the selected sound and its colour. on: what sounds, a bit a sound. now: the pattern that plays.
     step: the step that plays (NO_STEP: the song stands) */
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine];
  uint32_t on = play_k[kit];
  uint32_t now = seq_pattern;
  cur_sync();
  dr_read(seq_pos);
  uint32_t step = dr_step;
  /* heat: the intensity 0..127. here: the pattern shown is the one that plays. wt: the sound is a wavetable */
  int heat = (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f);
  int here = now == (uint32_t)edit_pat && step != NO_STEP;
  int wt = p->engine == SFX_ENGINE_WAVETABLE;
  /* the rows: all the pattern's steps in the same width. cells: how many. lim: those of the bar. pitch: from one
     cell to the next, in pixels. cw: a cell's width */
  int div = pat_div(edit_pat);
  int cells = gsteps();
  int lim = bar_len() * div;
  int pitch = SG_PX / div;
  int cw = pitch - 2;
  /* used: the sounds that play anywhere in the song, a bit a sound */
  uint16_t used = 0;
  for (int k = 0; k < SFX_PATTERNS; k++) {
    for (int i = 0; i < SFX_STEPS; i++) {
      used |= SONG.pat[k].on[i];
    }
  }
  /* the stage's sound page: the device's page, while it is one of these; never a song's page for the SFX kit; never
     WAVE for a sound that is no wavetable */
  if (stage_page_follows()) {
    spage = page;
  }
  if (kit == KIT_SFX && spage >= PG_SEQ) {
    spage = PG_OSC;
  }
  /* the AUTO tab: the rows are the sounds' automation lanes */
  int lanes = spage == PG_AUTO;
  if (spage == PG_WAVE && !wt) {
    spage = PG_OSC;
  }
  vg_clear(BG);

  /* 2. the top row. The name (smaller where the font is wide; what is said now is over the scope, below) */
  {
    int small = vg_text_width("SFX FORGE", VG_BIG) > TM_X - 28;
    vg_text("SFX FORGE", 24, small ? 24 : 21, small ? VG_MED : VG_BIG, TEXT, VG_LEFT);
  }
  /* what PLAY plays */
  stage_button(TM_X, TB_Y, 58, 36, loop_mode ? "LOOP" : "SONG", VG_BOLD, loop_mode ? BLUE : 0x36241C,
               loop_mode ? BG : TEXT);
  stage_button(TB_PLAY_X, TB_Y, 76, 36, running ? "STOP" : "PLAY", VG_BIG, running ? GREEN : 0x36241C,
               running ? BG : TEXT);
  stage_button(TB_REC_X, TB_Y, 64, 36, "REC", VG_BIG, rec ? RED : 0x36241C, rec ? BG : TEXT);
  /* the tempo: click its left or right half, or the wheel */
  vg_round(TB_TEMPO_X, TB_Y, 104, 36, 6, PANEL);
  vg_text("-", 351, TB_Y + 10, VG_BOLD, DIM, VG_LEFT);
  vg_text("+", 438, TB_Y + 10, VG_BOLD, DIM, VG_RIGHT);
  vg_text(vg_itoa(SONG.bpm, buf), 395, TB_Y + 7, VG_BIG, TEXT, VG_RIGHT);
  vg_text("BPM", 399, TB_Y + 13, VG_SMALL, DIM, VG_LEFT);
  /* the eight patterns: yellow - it waits for the bar line (and blinks); white - it plays; blue - it is shown;
     brown - it is in the chain */
  for (int k = 0; k < SFX_PATTERNS; k++) {
    int playing_it = (uint32_t)k == now && step != NO_STEP;
    int mine = k == edit_pat;
    int next = k == queued && BLINK;
    char letter[2] = {(char)('A' + k), 0};
    // clang-format off
    stage_button(TP_X + k * TP_PX, TB_Y, 36, 20, letter, VG_BOLD,
                 next             ? YELLOW
                 : playing_it     ? 0xFFFFFF
                 : mine           ? BLUE
                 : k < SONG.chain ? 0x523A1F
                 :                  PANEL,
                 next || playing_it || mine ? BG
                 : k < SONG.chain           ? TEXT
                 :                            DIM);
    // clang-format on
  }
  if (!SONG.arr_len) {
    vg_text("right-click a pattern: a bar of the arrangement", TP_X, TA_Y, VG_SMALL, 0x6A5248, VG_LEFT);
  } else {
    /* the arrangement: a pattern a bar */
    for (int i = 0; i < SONG.arr_len && i < SFX_ARR_MAX; i++) {
      char letter[2] = {(char)('A' + SONG.arr[i] % SFX_PATTERNS), 0};
      int at = seq_arr == (uint32_t)i;
      vg_round(TP_X + i * TA_PX, TA_Y, 9, 15, 2, at ? 0xFFFFFF : SONG.arr[i] == edit_pat ? BLUE : 0x523A1F);
      vg_text(letter, TP_X + i * TA_PX + 4, TA_Y, VG_SMALL, at || SONG.arr[i] == edit_pat ? BG : TEXT, VG_CENTER);
    }
  }
  /* the link's light */
  vg_circle(800.0f, 32.0f, 5.0f, linked ? GREEN : 0x48332A);
  vg_text("LINK", 810, 25, VG_SMALL, linked ? GREEN : 0x6A5248, VG_LEFT);
  /* the grid of the pattern shown */
  strcpy(t, "GRID ");
  strcpy(t + 5, GRID_NAME[pat_grid(edit_pat)]);
  stage_button(GC_X, GC_Y, 62, 14, t, VG_SMALL, div > 1 ? BLUE : 0x36241C, div > 1 ? BG : TEXT);
  stage_button(TB_TOUR_X, TB_Y, 80, 36, "TOUR", VG_BIG, tour_on ? BLUE : 0x36241C, tour_on ? BG : TEXT);

  /* 3. the pads: white while they sound, their engine's colour when selected, else a shade of it */
  for (int s = 0; s < SLOTS; s++) {
    int x = SP_X + (s % 4) * SP_PX;
    int y = SP_Y + (s / 4) * SP_PY;
    int lit = (on >> s) & 1;
    uint32_t c = ENGINE_RGB[patches[s].engine];
    vg_round(x, y, SP_W, SP_H, 7, lit ? 0xFFFFFF : s == sel ? c : vg_mix(BG, c, 0.32f));
    vg_text(vg_itoa(s + 1, buf), x + 8, y + 5, VG_BOLD, lit || s == sel ? BG : vg_mix(BG, c, 0.9f), VG_LEFT);
    vg_text(pad_name(s), x + SP_W / 2, y + 29, VG_MED, lit || s == sel ? BG : TEXT, VG_CENTER);
  }

  /* 4. the selected sound: its name, its picture, its pages and their four values, its keys */
  vg_round(SP_X, SK_Y, 34, 24, 5, rgb);
  vg_text(vg_itoa(sel + 1, buf), SP_X + 17, 309, VG_BOLD, BG, VG_CENTER);
  int nw = vg_text(pad_name(sel), SP_X + 44, 305, VG_BIG, TEXT, VG_LEFT);
  vg_text(ENGINE_NAME[p->engine], SP_X + 54 + nw, 311, VG_SMALL, rgb, VG_LEFT);
  /* the kit the pads show */
  stage_button(SP_X + 312, SK_Y, 54, 24, "SFX", VG_BOLD, kit == KIT_SFX ? TEXT : PANEL, kit == KIT_SFX ? BG : DIM);
  stage_button(SP_X + 370, SK_Y, 56, 24, "MUSIC", VG_BOLD, kit == KIT_MUSIC ? TEXT : PANEL,
               kit == KIT_MUSIC ? BG : DIM);
  preview(SP_X, 334, 4 * SP_PX - 6, 64, p, rgb, spage);
  /* the tabs (WAVE is faint for a sound that is no wavetable) */
  for (int i = 0; i < ST_N; i++) {
    int there = ST_PAGE[i] == spage;
    int none = ST_PAGE[i] == PG_WAVE && !wt;
    vg_round(SP_X + i * ST_PX, ST_Y, 41, 20, 5, there ? rgb : PANEL);
    vg_text(TAB[i], SP_X + i * ST_PX + 20, ST_Y + 3, VG_SMALL, there ? BG : none ? 0x523C33 : DIM, VG_CENTER);
  }
  /* the four values of the tab: AUTO's three, MIX's or MASTER's four, or a sound page's (k: which value of the
     sound, as in on_draw) */
  for (int i = 0; i < 4; i++) {
    int k = spage < PG_SEQ ? PAGE_PARAMS[spage][i] : -1;
    if (spage == PG_AUTO) {
      int to = view_to[sel];
      int a = aut_at(sel, cur_step);
      if (i == 0) {
        stage_value(i, "STEP", step_text(buf), ((float)cur_step + 0.5f) / (float)cells, 0, rgb);
      } else if (i == 1) {
        stage_value(i, "VALUE", a && to ? auto_text(to, sel, a - 1, buf) : "--",
                    a && to ? (float)(a - 1) / (float)auto_hi(to) : 0.0f, 0, rgb);
      } else if (i == 2) {
        stage_value(i, "TARGET", auto_name(to, sel), ((float)to + 0.5f) / (float)SFX_AT_COUNT, 0, rgb);
      } else {
        stage_value(i, "", "", -1.0f, 0, rgb);
      }
    } else if (spage >= PG_SEQ) {
      const char *name;
      float bar;
      int centre;
      const char *value = mix_text(spage, i, &name, buf, &bar, &centre);
      stage_value(i, name, value, bar, centre, rgb);
    } else if (k == ENGINE_KNOB) {
      stage_value(i, "ENGINE", ENGINE_SHORT[p->engine], ((float)p->engine + 0.5f) / (float)SFX_ENGINE_COUNT, 0, rgb);
    } else if (k == SFX_P_TABLE) {
      stage_value(i, "TABLE", param_text(k, p, buf), ((float)(p->p[k] % SFX_WT_TABLES) + 0.5f) / (float)SFX_WT_TABLES,
                  0, rgb);
    } else if (k >= 0) {
      stage_value(i, param_name(k, p->engine), param_text(k, p, buf), (float)p->p[k] / 127.0f,
                  k == SFX_P_PITCH_ENV_AMT || k == SFX_P_FILTER_ENV, rgb);
    } else {
      stage_value(i, "", "", -1.0f, 0, rgb);
    }
  }
  /* PRESET .. SAVE (UNDO faint while there is nothing to take back) */
  for (int i = 0; i < 6; i++) {
    stage_button(SP_X + i * SA_PX, SA_Y, 68, 32, SOFT[0][i], VG_BOLD, 0x36241C,
                 i == 2 && !sound_has_undo[sel] ? 0x6A5248 : TEXT);
  }

  /* 5. the song: a row a sound – dim while its layer is silent */
  for (int s = 0; s < SLOTS; s++) {
    /* c: the sound's colour. y: the row's top. layer: the intensity lets it play */
    uint32_t c = ENGINE_RGB[kits[KIT_MUSIC][s].engine];
    int y = SG_Y + s * SG_PY;
    int layer = heat >= SONG.level[s];

    /* its name (on the AUTO tab: the knob its lane moves): on its colour when selected; a white mark while it sounds */
    if (s == sel) {
      vg_round(SN_X, y, SG_X - SN_X - 6, 20, 4, c);
    }
    if ((play_k[KIT_MUSIC] >> s) & 1) {
      vg_rect(SN_X - 7, y + 2, 3, 16, 0xFFFFFF);
    }
    // clang-format off
    vg_text(lanes && view_to[s] ? auto_name(view_to[s], s) : name_of(KIT_MUSIC, s), SN_X + 5, y + 3, VG_SMALL,
            s == sel                     ? BG
            : silent(s)                  ? 0x58433A
            : lanes                      ? (view_to[s] ? TEXT : vg_mix(BG, c, 0.55f))
            : layer && ((used >> s) & 1) ? c
            :                              vg_mix(BG, c, 0.55f),
            VG_LEFT);
    // clang-format on
    /* muted: struck out, its steps dim */
    if (silent(s)) {
      vg_rect(SN_X + 3, y + 10, SG_X - SN_X - 12, 1, s == sel ? BG : 0x7A6055);
      layer = 0;
    }

    /* the AUTO tab. Its lane: a point holds until the next one; the dots are its steps */
    if (lanes) {
      /* to: the lane's target. hi: how far it goes. last: the value that holds (a point's value + 1) */
      int to = view_to[s];
      int hi = auto_hi(to);
      int last = 0;
      for (int i = 0; i < cells; i++) {
        int x = SG_X + i * pitch;
        int a = aut_at(s, i);
        int head = here && head_at(i, div);
        int off = i >= lim;
        /* the cell: under the playhead, beyond the bar, in the selected row, else two shades by its beat */
        // clang-format off
        uint32_t cell = head                  ? 0x5F4134
                        : off                 ? 0x1C130F
                        : s == sel            ? 0x503328
                        : (i / (4 * div)) & 1 ? 0x2B1D17
                        :                       0x38261E;
        // clang-format on
        if (div == 1) {
          vg_round(x, y, cw, 20, 3, cell);
        } else {
          vg_rect(x, y, cw, 20, cell);
        }
        if (a) {
          last = a;
        }
        /* filled as high as the value that holds: bright where a point is, with a white top */
        if (to && last && !off) {
          int hgt = 2 + (last - 1 > hi ? hi : last - 1) * 18 / hi;
          vg_rect(x, y + 20 - hgt, cw, hgt, a ? c : vg_mix(BG, c, 0.40f));
          if (a) {
            vg_rect(x, y + 20 - hgt, cw, 2, 0xFFFFFF);
          }
        }
        /* a dot: the sound plays on this step */
        if ((pat()->on[i] >> s) & 1) {
          vg_rect(x + (div == 1 ? 9 : cw / 2 - 1), y + 1, div == 1 ? 4 : 2, 3, to && last ? BG : vg_mix(BG, c, 0.7f));
        }
        if (s == sel && i == cur_step) {
          vg_frame(x - 2, y - 2, cw + 4, 24, 2, 0xFFFFFF);
        }
      }
      continue;
    }

    /* every other tab. Its steps: a cell each */
    for (int i = 0; i < cells; i++) {
      int x = SG_X + i * pitch;
      int set = (pat()->on[i] >> s) & 1;
      int head = here && head_at(i, div);
      int off = i >= lim;
      int note = pat()->note[s][i];
      /* the cell. A step that is set: white under the playhead, the sound's colour, or a shade of it while its layer
         is silent. An empty one: lit under the playhead, dark beyond the bar, else two shades by its beat */
      // clang-format off
      uint32_t cell = set                   ? (head && layer ? 0xFFFFFF : layer ? c : vg_mix(BG, c, 0.35f))
                      : head                ? 0x5F4134
                      : off                 ? 0x1C130F
                      : (i / (4 * div)) & 1 ? 0x2B1D17
                      :                       0x38261E;
      // clang-format on
      /* a set step is as high as it is hard */
      int hgt = 6 + (pat()->vel[s][i] & 7) * 2;
      if (div == 1) {
        /* 16 steps: rounded cells, with the note written in */
        if (!set) {
          vg_round(x, y, cw, 20, 3, s == sel && !head && !off ? vg_mix(cell, 0xFFFFFF, 0.08f) : cell);
        } else {
          vg_round(x, y, cw, 20, 3, vg_mix(BG, cell, 0.30f));
          vg_round(x, y + 20 - hgt, cw, hgt, 3, cell);
        }
        /* (the notes are written at 16 steps only: a finer cell has no room) */
        if (set && note) {
          vg_text(note_text(note, buf), x + 11, y + 3, VG_SMALL, layer ? BG : TEXT, VG_CENTER);
        }
      } else {
        /* 32 or 64 steps: plain narrow cells */
        if (!set) {
          vg_rect(x, y, cw, 20, s == sel && !head && !off ? vg_mix(cell, 0xFFFFFF, 0.08f) : cell);
        } else {
          vg_rect(x, y, cw, 20, vg_mix(BG, cell, 0.30f));
          vg_rect(x, y + 20 - hgt, cw, hgt, cell);
        }
      }
    }
    /* a step with a length: a line as far as it holds */
    for (int i = 0; i < cells; i++) {
      int len = pat()->len[s][i];
      if (((pat()->on[i] >> s) & 1) && len > 1) {
        vg_rect(SG_X + i * pitch + cw, y + 16, (i + len > cells ? cells - i : len) * pitch - pitch, 3,
                layer ? 0xFFFFFF : 0x7A6055);
      }
    }
    /* the cursor */
    if (s == sel && cur_step < cells) {
      vg_frame(SG_X + cur_step * pitch - 2, y - 2, cw + 4, 24, 2, 0xFFFFFF);
    }
  }

  /* 6. INTENSITY with every sound's mark over it, and the selected sound's own mark: its LAYER */
  vg_text("INTENSITY", SN_X + 5, SI_Y, VG_SMALL, 0xFF5A36, VG_LEFT);
  vg_round(SG_X, SI_Y, SG_W - 2, 16, 5, PANEL);
  vg_round(SG_X, SI_Y, 6 + (SG_W - 8) * heat / 127, 16, 5, 0xFF5A36);
  for (int s = 0; s < SLOTS; s++) {
    if ((used >> s) & 1) {
      vg_rect(SG_X + 2 + (SG_W - 8) * SONG.level[s] / 127, SI_Y - 6, 3, 5,
              heat >= SONG.level[s] ? ENGINE_RGB[kits[KIT_MUSIC][s].engine]
                                    : vg_mix(BG, ENGINE_RGB[kits[KIT_MUSIC][s].engine], 0.45f));
    }
  }
  vg_text(percent(heat, buf), SG_X + SG_W - 10, SI_Y + 1, VG_SMALL, TEXT, VG_RIGHT);
  vg_text("LAYER", SN_X + 5, SL_Y, VG_SMALL, DIM, VG_LEFT);
  vg_round(SG_X, SL_Y, SG_W - 2, 16, 5, PANEL);
  vg_round(SG_X, SL_Y, 6 + (SG_W - 8) * SONG.level[sel] / 127, 16, 5, vg_mix(BG, rgb, layer_on(sel) ? 0.75f : 0.35f));
  /* "BRASS plays from 71%" */
  strcpy(t, name_of(KIT_MUSIC, sel));
  strcpy(t + strlen(t), " plays from ");
  strcpy(t + strlen(t), percent(SONG.level[sel], buf));
  vg_text(t, SG_X + SG_W - 10, SL_Y + 1, VG_SMALL, TEXT, VG_RIGHT);

  /* 7. under it: what plays now, and the pattern's keys – or the tour's words */
  if (tour_on) {
    vg_round(SN_X, SF_Y, 470, 52, 6, 0x36241C);
    vg_text(tour_title, SN_X + 12, SF_Y + 4, VG_BIG, BLUE, VG_LEFT);
    vg_text(tour_line, SN_X + 12, SF_Y + 30, VG_MED, TEXT, VG_LEFT);
    vg_rect(SN_X, SF_Y + 50, (int)(470.0f * vd_clamp(tour_progress, 0.0f, 1.0f)), 2, BLUE);
  } else {
    scope(SN_X, SF_Y, 238, 52, rgb);
    /* what was just done, or asked: beside the keys that did it */
    if (toast_on()) {
      vg_rect_a(SN_X + 2, SF_Y + 14, 234, 24, BG, 0.82f);
      vg_text(toast, SN_X + 119, SF_Y + 18, VG_MED, TEXT, VG_CENTER);
    }
    for (int k = 0; k < SF_N; k++) {
      stage_button(SF_X + k * SF_PX, SF_Y, SF_PX - 4, 30, FILLS[lanes][k], VG_BOLD, 0x36241C,
                   k == 3 && !undo_has ? 0x6A5248 : TEXT);
    }
    /* a hint under the keys: what the mouse does in the rows */
    if (lanes) {
      vg_text("drag in a lane: points   right-click: none", SF_X + SF_N * SF_PX - 4, SF_Y + 35, VG_SMALL, 0x7A6055,
              VG_RIGHT);
    } else {
      strcpy(t, "wheel: note ");
      strcpy(t + strlen(t), note_text(pat()->note[sel][cur_step], buf));
      strcpy(t + strlen(t), "  SHIFT: vel  CTRL: len");
      vg_text(t, SF_X + SF_N * SF_PX - 4, SF_Y + 35, VG_SMALL, 0x7A6055, VG_RIGHT);
    }
  }
}
