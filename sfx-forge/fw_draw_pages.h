/* fw_draw_pages.h – the display's top line, and the pages that are not a sound's. A part of sfx-forge.c.
 *
 * Holds, in this order:
 *   tour_caption, sound_title, header (the top line)
 *   dr_read, head_at (where the song is, for whatever draws now), step_text, len_text
 *   SEQ      draw_seq, bar_strip (the whole bar in one line, at 32 and 64 steps)
 *   SONG     draw_song
 *            strip (the eight patterns in a row, under SEQ and AUTO)
 *   LINK     draw_link
 *   MIX      mix_text, silent
 *   AUTO     auto_name, auto_text, draw_auto
 *   MIX      draw_mix
 *   MASTER   draw_master
 * Needs:  fw_draw_parts.h (colours, tile_at, footer, texts), fw_sequencer.h (view_win, aut_at, auto_hi, view_to),
 *         fw_pages.h (mix_value), fw_edit.h (pat, pad_name, toast_on, pattern_holds).
 * Read:   these only draw - on_draw (fw_draw.h) decides which of them, and draws the four values beside them. A tile
 *         is one of the 16 pads, 55 x 54 pixels, laid out as the pads are (tile_at). Colours written as numbers are
 *         shades for one state of one thing; the named ones are in fw_draw_parts.h. Three recur:
 *         BLUE - the pattern that is shown, LOOP; YELLOW - what waits for the bar line;
 *         0xFFFFFF white - what plays now.
 */

/* ---- the top line ---------------------------------------------------------------------------------------------- */

/* where F1-F6 are, while the tour runs: its line, and over it how far it is */
static void tour_caption(void) {
  vg_round(2, 286, 476, 30, 5, 0x36241C);
  vg_text(tour_line, 240, 293, VG_MED, TEXT, VG_CENTER);
  vg_rect(2, 282, (int)(476.0f * vd_clamp(tour_progress, 0.0f, 1.0f)), 2, BLUE);
}

/* "PAD 5  COIN" (written for the stage; nothing calls it at present) */
static char *sound_title(char *t) {
  strcpy(t, "PAD ");
  vg_itoa(sel + 1, t + 4);
  int n = (int)strlen(t);
  t[n++] = ' ';
  t[n++] = ' ';
  strcpy(t + n, pad_name(sel));
  return t;
}

/* The top line: the selected sound – and, right, the song: playing or not, its pattern and tempo, the link.
   rgb: the selected sound's colour. pos: where the song is (seq_pos). now: the pattern that plays.
   From left to right: the sound's number; its name (or the tour's title, or what is being said); the kit - on the
   song's pages LOOP / SONG instead; a mark for REC, playing or stopped; the pattern and the tempo; the link's light. */
static void header(uint32_t rgb, uint32_t pos, uint32_t now) {
  uint32_t step = pos == NO_STEP ? NO_STEP : (uint32_t)pos_step(pos);
  int pdiv = pos_div(pos);
  char buf[16];
  char t[16];
  /* the song's pages (their kit is MUSIC): what PLAY plays, where the kit is said elsewhere */
  int pill = page >= PG_SEQ && page <= PG_MASTER;
  int k = 0;

  /* the sound's number and its name */
  vg_round(8, 5, 30, 24, 5, rgb);
  vg_text(vg_itoa(sel + 1, buf), 23, 10, VG_BOLD, BG, VG_CENTER);
  if (tour_on) {
    vg_text(tour_title, 46, 6, VG_BIG, BLUE, VG_LEFT);
  } else if (toast_on()) {
    vg_text(toast, 46, 6, VG_BIG, TEXT, VG_LEFT);
  } else {
    int w = vg_text(pad_name(sel), 46, 6, VG_BIG, TEXT, VG_LEFT);
    /* a named sound: its engine beside the name, if there is room */
    if (pad_preset[sel] >= 0 && 54 + w < 230) {
      vg_text(ENGINE_NAME[patches[sel].engine], 54 + w, 12, VG_SMALL, rgb, VG_LEFT);
    }
    if (!pill) {
      /* the kit (SHIFT + ENTER) */
      vg_round(318, 8, 50, 18, 4, kit == KIT_SFX ? 0x3A2A22 : 0x4A3222);
      vg_text(KIT_NAME[kit], 343, 9, VG_SMALL, kit == KIT_SFX ? 0xFF8A5C : 0x7FC4FF, VG_CENTER);
    }
  }
  /* LOOP / SONG (also over a toast and the tour's title) */
  if (pill) {
    vg_rect(314, 4, 58, 26, BG);
    vg_round(318, 8, 50, 18, 4, loop_mode ? BLUE : 0x3A2A22);
    vg_text(loop_mode ? "LOOP" : "SONG", 343, 9, VG_SMALL, loop_mode ? BG : TEXT, VG_CENTER);
  }

  /* the pattern and the tempo: "A  120" – "A>C 120" while C waits for the bar line */
  t[k++] = (char)('A' + (step != NO_STEP ? (int)now : edit_pat));
  if (queued >= 0) {
    t[k++] = '>';
    t[k++] = (char)('A' + queued);
    t[k++] = ' ';
  } else {
    t[k++] = ' ';
    t[k++] = ' ';
  }
  vg_itoa(SONG.bpm, t + k);
  /* the link's light */
  vg_circle(467, 17, 4, linked ? GREEN : 0x48332A);
  /* ix: where the mark left of the pattern goes */
  int ix = 454 - vg_text(t, 454, 10, VG_BOLD, step != NO_STEP ? TEXT : DIM, VG_RIGHT) - 17;
  if (rec) {
    /* REC: a red dot. It beats with the metronome: larger for the first six tenths of the sixteenth that begins a
       beat */
    // clang-format off
    vg_circle((float)ix + 5.0f, 17.0f,
              step != NO_STEP && ((int)step / pdiv) % 4 == 0
                  && ((float)((int)step % pdiv) + pos_frac(pos)) / (float)pdiv < 0.6f ? 7.5f : 5.0f,
              RED);
    // clang-format on
  } else if (step != NO_STEP) {
    /* playing: a triangle */
    vg_tri((float)ix, 11.0f, (float)ix, 23.0f, (float)ix + 10.0f, 17.0f, GREEN);
  } else {
    /* stopped: a square */
    vg_rect(ix, 12, 10, 10, 0x6A5248);
  }
}

/* ---- where the song is ----------------------------------------------------------------------------------------- */

/* where the song is, for whatever draws now (the display, the stage): the step that plays, in which steps a
   sixteenth. ("dr" is "draw": on_draw and on_stage call dr_read once a frame, and what they call reads these two) */
static uint32_t dr_step = NO_STEP;
static int dr_div = 1;

static void dr_read(uint32_t pos) {
  dr_step = pos == NO_STEP ? NO_STEP : (uint32_t)pos_step(pos);
  dr_div = pos_div(pos);
}

/* step g of a grid with div steps a sixteenth is where the playhead is: at 16 steps the step itself, at 32 and 64 every
   step of the sixteenth that plays (a single sixty-fourth is over before it can be seen) */
static int head_at(int g, int div) {
  return dr_step != NO_STEP && (div == 1 && dr_div == 1 ? (int)dr_step == g : g / div == (int)dr_step / dr_div);
}

/* the cursor: "5" at 16 steps, "17/32" at more */
static const char *step_text(char *buf) {
  vg_itoa(cur_step + 1, buf);
  if (gsteps() > PADS) {
    strcpy(buf + strlen(buf), "/");
    vg_itoa(gsteps(), buf + strlen(buf));
  }
  return buf;
}

/* a length: "3" at 16 steps; at more also what it is of a whole bar: "6 = 3/32" */
static const char *len_text(int len, char *buf) {
  int n = gsteps();
  int a = len;
  int b = n;
  if (!len) {
    return "AUTO";
  }
  vg_itoa(len, buf);
  if (n == PADS) {
    return buf;
  }
  /* a: what len and n both divide by (the fraction is written in its lowest terms) */
  while (b) {
    int t = a % b;
    a = b;
    b = t;
  }
  strcpy(buf + strlen(buf), " = ");
  vg_itoa(len / a, buf + strlen(buf));
  strcpy(buf + strlen(buf), "/");
  vg_itoa(n / a, buf + strlen(buf));
  return buf;
}

/* ---- SEQ ------------------------------------------------------------------------------------------------------- */
/* 16 steps of the selected sound as the 16 pads – a row is a beat at 16 steps a bar, an eighth at 32, a sixteenth at
   64. rgb: the sound's colour. step: the step that plays (NO_STEP: none). now: the pattern that plays. */
static void draw_seq(uint32_t rgb, uint32_t step, uint32_t now) {
  char buf[16];
  /* here: the pattern shown is the one that plays. lim: the steps of the bar. w: which 16 steps are shown */
  int here = now == (uint32_t)edit_pat && step != NO_STEP;
  int div = pat_div(edit_pat);
  int lim = bar_len() * div;
  int w = view_win();
  /* a step is as high as it is hard, and says its note */
  for (int s = 0; s < PADS; s++) {
    /* g: the step this tile stands for. set: the sound plays there. head: the playhead is there. off: beyond the
       bar. hgt: the height for its velocity */
    int g = w * PADS + s;
    int x;
    int y;
    int set = (pat()->on[g] >> sel) & 1;
    int head = here && head_at(g, div);
    int off = g >= lim;
    int note = pat()->note[sel][g];
    int hgt = 24 + (pat()->vel[sel][g] & 7) * 4;
    int len = pat()->len[sel][g];
    tile_at(s, &x, &y);
    /* the tile: lit under the playhead, tinted when set, dark beyond the bar, else two shades by its row */
    // clang-format off
    vg_round(x, y, 55, 54, 7,
             head && !set  ? 0x64463A
             : set         ? vg_mix(BG, rgb, 0.25f)
             : off         ? 0x1A120E
             : (s / 4) & 1 ? 0x2A1D17
             :               0x35251D);
    // clang-format on
    if (set) {
      vg_round(x, y + 54 - hgt, 55, hgt, 7, head ? 0xFFFFFF : rgb);
    }
    if (set && note) {
      vg_text(note_text(note, buf), x + 27, y + 33, VG_BOLD, BG, VG_CENTER);
    } else if (set) {
      vg_circle((float)x + 27.0f, (float)y + 41.0f, 4.0f, BG);
    } else if (!off) {
      /* an empty step: its number (a finer grid: the steps that begin a sixteenth stand out) */
      vg_text(vg_itoa(g + 1, buf), x + 27, y + 20, VG_SMALL, div > 1 && g % div == 0 ? DIM : 0x6A5248, VG_CENTER);
    }
    /* ">3": it holds this many steps */
    if (set && len) {
      buf[0] = '>';
      vg_itoa(len, buf + 1);
      vg_text(buf, x + 51, y + 2, VG_SMALL, hgt > 46 ? BG : TEXT, VG_RIGHT);
    }
    /* the cursor */
    if (g == cur_step) {
      vg_frame(x - 2, y - 2, 59, 58, 2, 0xFFFFFF);
    }
  }
}

/* SEQ and AUTO at 32 or 64 steps, in the footer's first line: the whole bar, a column a step - the selected sound's
   steps (AUTO: its lane's points), a line at every beat, a frame round the 16 steps the pads show, the playhead, a mark
   under the cursor's step; and the numbers of the 16 beside it. points: 1 = AUTO */
static void bar_strip(uint32_t rgb, uint32_t step, uint32_t now, int points) {
  char t[16];
  /* n: the pattern's steps. px: a step's width. x0, y0: the strip's corner. w: which 16 are shown. lim: the bar's */
  int div = pat_div(edit_pat);
  int n = gsteps();
  int px = 128 / n;
  int x0 = 262;
  int y0 = CY + 192;
  int w = view_win();
  int lim = bar_len() * div;
  vg_rect(x0, y0, 128, 12, 0x1A120E);
  /* (the bar: what lies beyond it stays dark) */
  vg_rect(x0, y0, lim * px, 12, 0x2A1D17);
  for (int b = 1; b < 4; b++) {
    vg_rect(x0 + b * 32, y0, 1, 12, LINE);
  }
  for (int i = 0; i < lim; i++) {
    if (points ? aut_at(sel, i) : (pat()->on[i] >> sel) & 1) {
      vg_rect(x0 + i * px, y0 + 2, px > 2 ? px - 1 : px, 8, rgb);
    }
  }
  /* the playhead */
  if (step != NO_STEP && now == (uint32_t)edit_pat) {
    vg_rect(x0 + clampi((int)dr_step * div / dr_div, 0, n - 1) * px, y0, px, 12, 0xFFFFFF);
  }
  /* the 16 steps the pads show */
  vg_frame(x0 + w * PADS * px - 1, y0 - 1, PADS * px + 2, 14, 1, 0xFFFFFF);
  /* under it: the cursor's step (what STEP names) - during a take it may lie outside the 16 shown */
  vg_rect(x0 + cur_step * px, y0 + 14, px, 2, rgb);
  /* "17-32/64" */
  vg_itoa(w * PADS + 1, t);
  strcpy(t + strlen(t), "-");
  vg_itoa(w * PADS + PADS, t + strlen(t));
  strcpy(t + strlen(t), "/");
  vg_itoa(n, t + strlen(t));
  vg_text(t, 398, CY + 191, VG_SMALL, TEXT, VG_LEFT);
}

/* ---- SONG ------------------------------------------------------------------------------------------------------ */
/* pads 1-8 are the patterns, pads 9-16 the intensity – as on the device; under the column the arrangement.
   heat: the intensity, 0..127 */
static void draw_song(uint32_t step, uint32_t now, int heat) {
  char buf[16];
  for (int s = 0; s < SLOTS; s++) {
    int x;
    int y;
    tile_at(s, &x, &y);
    if (s < SFX_PATTERNS) {
      /* a pattern: its letter, and what is in it. (What waits for the bar line blinks) */
      int playing_it = (uint32_t)s == now && step != NO_STEP;
      int mine = s == edit_pat;
      int next = s == queued && BLINK;
      // clang-format off
      uint32_t bg = next                             ? YELLOW
                    : playing_it                     ? 0xFFFFFF
                    : mine                           ? BLUE
                    : s < SONG.chain || SONG.arr_len ? 0x3A2A22
                    :                                  0x1A120E;
      uint32_t fg = next || playing_it || mine ? BG : TEXT;
      // clang-format on
      char letter[2] = {(char)('A' + s), 0};
      vg_round(x, y, 55, 54, 7, bg);
      vg_text(letter, x + 7, y + 3, VG_BIG, fg, VG_LEFT);
      int div = pat_div(s);
      /* what is in it: how many sounds in each sixteenth (n), as 16 small bars */
      for (int i = 0; i < SFX_BAR; i++) {
        int n = 0;
        uint16_t m = 0;
        for (int j = 0; j < div; j++) {
          m |= SONG.pat[s].on[i * div + j];
        }
        /* (n: the bits set in m) */
        for (; m; m &= (uint16_t)(m - 1)) {
          n++;
        }
        if (n) {
          vg_rect(x + 4 + i * 3, y + 50 - (n > 9 ? 20 : 2 + n * 2), 2, n > 9 ? 20 : 2 + n * 2, vg_mix(bg, fg, 0.7f));
        }
      }
      /* a finer grid: its steps a bar */
      if (div > 1) {
        vg_text(div == 2 ? "32" : "64", x + 51, y + 3, VG_SMALL, vg_mix(bg, fg, 0.8f), VG_RIGHT);
      }
    } else {
      /* a level of intensity: 12 .. 100 per cent, lit up to where the intensity is */
      int lit = heat >= (s - 7) * 127 / 8;
      vg_round(x, y, 55, 54, 7, lit ? 0xFF5A36 : 0x2E1A12);
      vg_text(vg_itoa((s - 7) * 100 / 8, buf), x + 27, y + 18, VG_BIG, lit ? BG : 0x7A4A38, VG_CENTER);
    }
  }
  if (!SONG.arr_len) {
    footer("NO ARRANGEMENT", DIM, "SHIFT + pad 1-8 adds a bar");
  } else {
    /* the arrangement: a pattern a bar, 16 bars a line; white: the bar that plays */
    for (int i = 0; i < SONG.arr_len && i < SFX_ARR_MAX; i++) {
      char letter[2] = {(char)('A' + SONG.arr[i] % SFX_PATTERNS), 0};
      int at = seq_arr == (uint32_t)i;
      int x = 258 + (i % 16) * 13;
      int y = CY + 190 + (i / 16) * 19;
      vg_round(x, y, 12, 17, 3, at ? 0xFFFFFF : SONG.arr[i] == edit_pat ? BLUE : 0x3A2A22);
      vg_text(letter, x + 6, y + 1, VG_SMALL, at || SONG.arr[i] == edit_pat ? BG : TEXT, VG_CENTER);
    }
  }
}

/* SEQ and AUTO, under the column: the eight patterns – which plays, which is shown, which waits for the bar line, which
   hold something – and a word beside them */
static void strip(uint32_t step, uint32_t now, const char *hint, uint32_t hint_rgb) {
  for (int k = 0; k < SFX_PATTERNS; k++) {
    char letter[2] = {(char)('A' + k), 0};
    int x = 262 + k * 13;
    int y = CY + 209;
    int next = k == queued && BLINK;
    int plays = step != NO_STEP && now == (uint32_t)k;
    int mine = k == edit_pat;
    int holds = pattern_holds(k);
    // clang-format off
    vg_round(x, y, 12, 17, 3,
             next    ? YELLOW
             : plays ? 0xFFFFFF
             : mine  ? BLUE
             : holds ? 0x3A2A22
             :         0x1A120E);
    vg_text(letter, x + 6, y + 1, VG_SMALL,
            next || plays || mine ? BG
            : holds               ? TEXT
            :                       0x6A5248,
            VG_CENTER);
    // clang-format on
  }
  vg_text(hint, 376, CY + 209, VG_SMALL, hint_rgb, VG_LEFT);
}

/* ---- LINK ------------------------------------------------------------------------------------------------------ */
/* what the game sends: its six CCs as bars; under the column whether a game was seen, and its text */
static void draw_link(void) {
  static const char *const IN[6] = {"PITCH", "CUTOFF", "TIMBRE", "LEVEL", "INTENSITY", "PATTERN"};
  char buf[16];
  vg_round(12, CY, 232, CH, 8, PANEL);
  vg_text("FROM THE GAME", 24, CY + 10, VG_BOLD, YELLOW, VG_LEFT);
  for (int k = 0; k < 6; k++) {
    int y = CY + 40 + k * 31;
    vg_text(IN[k], 24, y, VG_SMALL, TEXT, VG_LEFT);
    vg_text(vg_itoa(telemetry[k], buf), 232, y, VG_SMALL, DIM, VG_RIGHT);
    vg_rect(24, y + 17, 208, 5, LINE);
    vg_rect(24, y + 17, telemetry[k] * 208 / 127, 5, YELLOW);
  }
  footer(linked ? "LINKED" : "WAITING FOR A GAME", linked ? GREEN : DIM,
         link_text[0] ? link_text : "the knobs send CC 20-23 to it");
}

/* ---- MIX and MASTER: their four values as text ----------------------------------------------------------------- */

/* the name, text and bar of value i on MIX or MASTER. It returns the text; *name, *bar (0..1) and *centre (the bar
   grows from the middle: the pan) are filled in */
static const char *mix_text(int pg, int i, const char **name, char *buf, float *bar, int *centre) {
  static const char *const N[2][4] = {{"VOLUME", "PAN", "DRIVE", "OUTPUT"},
                                      {"DELAY TIME", "FEEDBACK", "ROOM SIZE", "DAMPING"}};
  static const char *const TIME[SFX_DELAY_TIMES] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2"};
  int hi;
  const uint8_t *v = mix_value(pg, i, &hi);
  *name = N[pg == PG_MASTER][i];
  *centre = 0;
  /* (no byte behind it: the output volume, a host parameter) */
  if (!v) {
    *bar = vibe_param_get(P_VOLUME);
    return percent((int)(*bar * 127.0f + 0.5f), buf);
  }
  *bar = (float)*v / (float)hi;
  /* the delay's time: a note value */
  if (pg == PG_MASTER && i == 0) {
    *bar = ((float)(*v % SFX_DELAY_TIMES) + 0.5f) / (float)SFX_DELAY_TIMES;
    return TIME[*v % SFX_DELAY_TIMES];
  }
  /* the fader: its number */
  if (pg == PG_MIX && i == 0) {
    return vg_itoa(*v, buf);
  }
  /* the pan: "L20", "C", "R5" */
  if (pg == PG_MIX && i == 1) {
    *centre = 1;
    if (*v == 64) {
      return "C";
    }
    buf[0] = *v < 64 ? 'L' : 'R';
    vg_itoa(*v < 64 ? 64 - *v : *v - 64, buf + 1);
    return buf;
  }
  return percent(*v, buf);
}

/* sound s is not heard in the song: muted, or another one is solo */
static int silent(int s) {
  return (SONG.mute >> s) & 1 || (SONG.solo && !((SONG.solo >> s) & 1));
}

/* ---- AUTO ------------------------------------------------------------------------------------------------------ */

/* what a lane's target is called (to: SFX_AT_..., 0 = none) */
static const char *auto_name(int to, int track) {
  static const char *const M[5] = {"DELAY TIME", "FEEDBACK", "ROOM SIZE", "DAMPING", "DRIVE"};
  // clang-format off
  return !to                 ? "OFF"
       : to >= SFX_AT_MASTER ? M[to - SFX_AT_MASTER]
       : to == SFX_AT_PAN    ? "PAN"
       : to == SFX_AT_VOL    ? "VOLUME"
       :                       param_name(to - SFX_AT_PATCH, kits[KIT_MUSIC][track].engine);
  // clang-format on
}

/* a point's value v as text, in the unit of its target */
static const char *auto_text(int to, int track, int v, char *buf) {
  static const char *const TIME[SFX_DELAY_TIMES] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2"};
  if (to == SFX_AT_MASTER + SFX_M_DELAY_TIME) {
    return TIME[v % SFX_DELAY_TIMES];
  }
  if (to >= SFX_AT_MASTER) {
    return percent(v, buf);
  }
  if (to == SFX_AT_VOL) {
    return vg_itoa(v, buf);
  }
  if (to == SFX_AT_PAN) {
    if (v == 64) {
      return "C";
    }
    buf[0] = v < 64 ? 'L' : 'R';
    vg_itoa(v < 64 ? 64 - v : v - 64, buf + 1);
    return buf;
  }
  /* one of the sound's values: as param_text writes it, for a copy of the sound with that value */
  sfx_patch e = kits[KIT_MUSIC][track];
  e.p[to - SFX_AT_PATCH] = (uint8_t)v;
  return param_text(to - SFX_AT_PATCH, &e, buf);
}

/* the selected sound's lane as the 16 pads (16 steps of it) – a point holds until the next one. A tile is filled as
   high as the value that holds there: bright where a point is, pale where the one before still holds. */
static void draw_auto(uint32_t rgb, uint32_t step, uint32_t now) {
  char buf[16];
  /* to: the lane's target. hi: how far it goes. last: the value that holds (a point's value + 1, 0: none yet) */
  int here = now == (uint32_t)edit_pat && step != NO_STEP;
  int to = view_to[sel];
  int hi = auto_hi(to);
  int last = 0;
  int div = pat_div(edit_pat);
  int lim = bar_len() * div;
  int w = view_win();
  /* (what holds from the steps before the ones shown) */
  for (int g = 0; g < w * PADS && g < lim; g++) {
    if (aut_at(sel, g)) {
      last = aut_at(sel, g);
    }
  }
  for (int s = 0; s < PADS; s++) {
    /* g: the step this tile stands for. a: its point. off: beyond the bar. head: the playhead is there */
    int g = w * PADS + s;
    int x;
    int y;
    int a = aut_at(sel, g);
    int off = g >= lim;
    int head = here && head_at(g, div);
    tile_at(s, &x, &y);
    /* the tile: dark past the bar's end, lit under the playhead, else the pads' two shades row by row */
    // clang-format off
    vg_round(x, y, 55, 54, 7,
             off           ? 0x1A120E
             : head        ? 0x563A2E
             : (s / 4) & 1 ? 0x2A1D17
             :               0x35251D);
    // clang-format on
    if (a) {
      last = a;
    }
    if (to && last && !off) {
      int hgt = 6 + (last - 1 > hi ? hi : last - 1) * 48 / hi;
      vg_round(x, y + 54 - hgt, 55, hgt, 7, a ? rgb : vg_mix(BG, rgb, 0.30f));
      if (a) {
        vg_rect(x + 6, y + 54 - hgt + 3, 43, 3, 0xFFFFFF);
      }
    }
    /* the step's number: dark where the fill reaches up to it */
    // clang-format off
    vg_text(vg_itoa(g + 1, buf), x + 5, y + 2, VG_SMALL,
            to && last && last - 1 > hi * 3 / 4 ? BG
            : div > 1 && g % div == 0           ? DIM
            :                                     0x6A5248,
            VG_LEFT);
    // clang-format on
    /* a dot: the sound plays here */
    if ((pat()->on[g] >> sel) & 1) {
      vg_circle((float)x + 46.0f, (float)y + 9.0f, 3.5f, head ? 0xFFFFFF : TEXT);
    }
    if (g == cur_step) {
      vg_frame(x - 2, y - 2, 59, 58, 2, 0xFFFFFF);
    }
  }
}

/* ---- MIX ------------------------------------------------------------------------------------------------------- */
/* every sound a tile, where its pad is – its name, its fader, its pan under it; muted ones grey */
static void draw_mix(void) {
  char buf[16];
  for (int s = 0; s < SLOTS; s++) {
    uint32_t c = ENGINE_RGB[patches[s].engine];
    int x;
    int y;
    int off = silent(s);
    int solo = (SONG.solo >> s) & 1;
    int k = 0;
    /* the letters of its name that fit (the S or M in the corner takes two letters' room) */
    int room = solo || ((SONG.mute >> s) & 1) ? 4 : 6;
    const char *n = pad_name(s);
    tile_at(s, &x, &y);
    vg_round(x, y, 55, 54, 7, s == sel ? 0x3E2C23 : PANEL);
    while (k < room && n[k]) {
      buf[k] = n[k];
      k++;
    }
    buf[k] = 0;
    vg_text(buf, x + 5, y + 3, VG_SMALL, off ? 0x6A5248 : TEXT, VG_LEFT);
    /* the fader (white while the sound plays); the mark: 100, as designed */
    vg_rect(x + 5, y + 26, 45, 7, LINE);
    vg_rect(x + 5, y + 26, SONG.vol[s] * 45 / 127, 7, off ? 0x6A5248 : (play_k[KIT_MUSIC] >> s) & 1 ? 0xFFFFFF : c);
    vg_rect(x + 5 + 100 * 45 / 127, y + 23, 1, 13, DIM);
    /* the pan */
    vg_rect(x + 5, y + 43, 45, 1, LINE);
    vg_rect(x + 4 + SONG.pan[s] * 43 / 127, y + 39, 3, 9, off ? DIM : TEXT);
    if (solo) {
      vg_text("S", x + 50, y + 3, VG_SMALL, YELLOW, VG_RIGHT);
    } else if ((SONG.mute >> s) & 1) {
      vg_text("M", x + 50, y + 3, VG_SMALL, RED, VG_RIGHT);
    }
    if (s == sel) {
      vg_frame(x - 2, y - 2, 59, 58, 2, 0xFFFFFF);
    }
  }
}

/* ---- MASTER ---------------------------------------------------------------------------------------------------- */
/* the delay as its echoes (left), the reverb as its fading (right) */
static void draw_master(void) {
  /* the delay's six times, in sixteenths */
  static const uint8_t SIX[SFX_DELAY_TIMES] = {1, 2, 3, 4, 6, 8};
  /* fb: what each echo keeps of the one before. a: the level of the echo being drawn. tau: how slowly the reverb's
     picture fades */
  float fb = (float)SONG.fx[SFX_M_DELAY_FB] * (0.45f / 64.0f);
  float a = 1.0f;
  float tau = 14.0f + (float)SONG.fx[SFX_M_REVERB_SIZE] * 0.9f;
  /* gap: the pixels from one echo to the next. base: the line both pictures stand on */
  int gap = 8 * SIX[SONG.fx[SFX_M_DELAY_TIME] % SFX_DELAY_TIMES];
  int base = CY + CH - 62;
  vg_round(12, CY, 226, CH, 8, PANEL);
  vg_round(242, CY, 226, CH, 8, PANEL);
  vg_text("DELAY", 125, CY + 60, VG_BOLD, BLUE, VG_CENTER);
  vg_text("REVERB", 355, CY + 60, VG_BOLD, 0xC48BFF, VG_CENTER);
  vg_rect(24, base, 202, 1, LINE);
  /* the sound, then its echoes: left, right, left … */
  for (int k = 0, x = 26; x < 222 && a > 0.02f; k++, x += gap) {
    int hgt = (int)(84.0f * a);
    vg_rect(x, base - hgt, 5, hgt, k == 0 ? 0xFFFFFF : (k & 1) ? BLUE : YELLOW);
    a *= k == 0 ? 0.7f : fb;
  }
  vg_rect(254, base, 202, 1, LINE);
  /* the reverb: bright .. damped */
  uint32_t tone = vg_mix(0xE9D6FF, 0x5B3F86, (float)SONG.fx[SFX_M_REVERB_DAMP] / 127.0f);
  for (int i = 0; i < 200; i += 2) {
    int hgt = (int)(84.0f * vd_exp2(-(float)i / tau));
    if (hgt > 0) {
      vg_rect(255 + i, base - hgt, 2, hgt,
              vg_mix(tone, 0x5B3F86, (float)i / 200.0f * (float)SONG.fx[SFX_M_REVERB_DAMP] / 127.0f));
    }
  }
}
