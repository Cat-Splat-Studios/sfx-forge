/* fw_draw.h – a whole frame of the display (480 x 320), and the pads' lights. A part of sfx-forge.c.
 *
 * Holds:  on_draw (vibe_on_draw in sfx-forge.c calls it for every frame).
 * Needs:  every part before it: fw_draw_parts.h (hud, row, footer), fw_draw_sound.h (scene), fw_draw_pages.h (header,
 *         draw_seq, draw_song, ...), fw_events.h (the HOME texts), fw_link.h, fw_tour.h, fw_sequencer.h.
 * Read:   on_draw is also the firmware's clock in the control flow: every frame it sends on what waits to go out
 *         (out_pump), moves the tour on (tour_pump) and takes in what the audio did (seq_poll) - before it draws.
 *         To change what a page shows, find its branch in part 5 below; the picture of each page is drawn by one
 *         function (scene, draw_seq, draw_song, draw_auto, draw_mix, draw_master, draw_link), the four values beside
 *         or over it by hud (a picture's corners) or row (the column beside the tiles).
 */

/* A frame, from top to bottom:
     1  what must go on whether or not anything is seen: the link, the tour, the sequencer; the blink
     2  HOME: the page's controls instead of the page (the SDK draws the table) - and nothing else
     3  the top line (header), and under it where you are: the page's name, the pattern, six marks for K1-K6
     4  the 16 pads' lights, as this page uses the pads
     5  the page itself with its four values: AUTO, MIX, MASTER, LINK, SEQ, SONG, or a sound page
     6  F1-F6 (or the tour's caption)
     7  FOCUS: where the SDK's frame may go */
static void on_draw(uint16_t *fb) {
  /* what K1's pages are called over the encoders */
  static const char *const SUB[4] = {"OSC", "TONE", "FILTER", "WAVE"};
  char buf[24];
  (void)fb;

  /* 1. */
  out_pump();
  tour_pump();
  seq_poll();
  blink++;
  vg_clear(BG);

  /* 2. HOME */
  if (help) {
    if (page == PG_SEQ) {
      vg_controls("SFX FORGE: SEQ", HELP_SEQ, sizeof HELP_SEQ / sizeof HELP_SEQ[0] / 2);
    } else if (page == PG_SONG) {
      vg_controls("SFX FORGE: SONG", HELP_SONG, sizeof HELP_SONG / sizeof HELP_SONG[0] / 2);
    } else if (page == PG_AUTO) {
      vg_controls("SFX FORGE: AUTO", HELP_AUTO, sizeof HELP_AUTO / sizeof HELP_AUTO[0] / 2);
    } else if (mix_page()) {
      vg_controls("SFX FORGE: MIX", HELP_MIX, sizeof HELP_MIX / sizeof HELP_MIX[0] / 2);
    } else if (page == PG_LINK) {
      vg_controls("SFX FORGE: LINK", HELP_LINK, sizeof HELP_LINK / sizeof HELP_LINK[0] / 2);
    } else {
      vg_controls("SFX FORGE: SOUND", HELP_SOUND, sizeof HELP_SOUND / sizeof HELP_SOUND[0] / 2);
    }
    return;
  }

  /* what the whole frame draws from.
     p, rgb: the selected sound and its engine's colour. on: what sounds, a bit a sound. pos: where the song is.
     now: the pattern that plays. step: the step that plays (NO_STEP: the song stands) */
  const sfx_patch *p = &patches[sel];
  uint32_t rgb = ENGINE_RGB[p->engine];
  uint32_t on = play_k[kit];
  uint32_t pos = seq_pos;
  uint32_t now = seq_pattern;
  dr_read(pos);
  uint32_t step = dr_step;
  /* div: the shown pattern's steps a sixteenth. lim: the steps of its bar. win: which 16 of them the pads show */
  int div = pat_div(edit_pat);
  int lim = bar_len() * div;
  int win = view_win();
  /* the page, as four yes / no; heat: the intensity 0..127; wt: the selected sound is a wavetable */
  int seq = page == PG_SEQ;
  int song = page == PG_SONG;
  int mix = page == PG_MIX;
  int aut = page == PG_AUTO;
  int heat = (int)(vibe_param_get(P_INTENSITY) * 127.0f + 0.5f);
  int wt = p->engine == SFX_ENGINE_WAVETABLE;

  /* 3. the top line */
  header(rgb, pos, now);

  /* where you are: the page's name – and K1-K6 as six marks, with a dot for each page of the key you are on */
  {
    // clang-format off
    /* the page's name. (No size is written, so that the assert under the tables sees a page without a name.) */
    static const char *const TITLE[] = {
      "SOUND   OSC", "SOUND   TONE", "SOUND   FILTER", "SOUND   WAVE",
      "ENVELOPE", "LFO", "EFFECT SENDS",
      "SEQUENCER", "SONG", "AUTOMATION",
      "MIXER", "MASTER EFFECTS",
      "GAME LINK" };
    /* the first page of each page's K key (for K1 and K5: the dot is the page's place after it) */
    static const uint8_t FIRST[] = {
      PG_OSC, PG_OSC, PG_OSC, PG_OSC,
      PG_ENV, PG_LFO, PG_FX,
      PG_SEQ, PG_SEQ, PG_SEQ,
      PG_FX, PG_FX,
      PG_LINK };
    // clang-format on
    _Static_assert(sizeof TITLE / sizeof TITLE[0] == PG_COUNT, "a title for every page");
    _Static_assert(sizeof FIRST / sizeof FIRST[0] == PG_COUNT, "a first page for every page");

    /* tab: the K key of this page */
    int tab = TAB_OF[page];
    /* pages: how many pages that key has - SOUND three, a fourth for a wavetable; FX and SEQ three; the others one */
    // clang-format off
    int pages = tab == TAB_SOUND                 ? (wt ? 4 : 3)
              : tab == TAB_FX || tab == TAB_SEQ  ? 3
              :                                    1;
    // clang-format on
    /* at: which of them this page is. The pages of a key follow each other in the PG_ list - but FX's: MIX and MASTER
       stand after SEQ's three */
    // clang-format off
    int at = tab == TAB_FX ? (page == PG_FX    ? 0
                              : page == PG_MIX ? 1
                              :                  2)
                           : page - FIRST[page];
    // clang-format on
    /* tw: how wide what is written so far is */
    int tw = vg_text(TITLE[page], 12, 36, VG_BOLD, rgb, VG_LEFT);
    /* … and the pattern; on AUTO the knob the lane moves */
    if (seq || song || aut) {
      char t[40] = "PATTERN  ";
      /* what is shown is not what plays: say both */
      int other = step != NO_STEP && now != (uint32_t)edit_pat;
      if (other) {
        strcpy(t, "EDIT B - PLAYS A");
        t[5] = (char)('A' + edit_pat);
        t[15] = (char)('A' + now % SFX_PATTERNS);
      } else {
        t[8] = (char)('A' + edit_pat);
      }
      /* … and its grid: the steps it cuts the bar in */
      strcpy(t + strlen(t), "  ");
      strcpy(t + strlen(t), GRID_NAME[pat_grid(edit_pat)]);
      tw += vg_text(t, 26 + tw, 36, VG_BOLD, other ? ORANGE : TEXT, VG_LEFT);
      if (aut) {
        const char *n = auto_name(view_to[sel], sel);
        if (40 + tw + vg_text_width(n, VG_BOLD) < LOC_X - 8) {
          vg_text(n, 40 + tw, 36, VG_BOLD, view_to[sel] ? TEXT : DIM, VG_LEFT);
        }
      } else if (seq && !layer_on(sel) && 40 + tw + vg_text_width("layer off", VG_SMALL) < LOC_X - 8) {
        vg_text("layer off", 40 + tw, 36, VG_SMALL, DIM, VG_LEFT);
      }
    }
    /* the six marks, and the dots under the one that is lit */
    for (int i = 0; i < TAB_COUNT; i++) {
      vg_round(LOC_X + i * 18, 37, 12, 9, 2, i == tab ? rgb : LINE);
    }
    for (int i = 0; pages > 1 && i < pages; i++) {
      vg_rect(LOC_X + tab * 18 + 6 - pages * 2 + i * 4, 49, 3, 3, i == at ? TEXT : DIM);
    }
  }

  /* 4. the 16 pads as they are on this page: sounds, the selected sound's steps, or patterns and intensity.
     For each: a colour (base) and a state - 0 off, 1 dim, 2 set, 3 bright (white). A light is sent to the host only
     when it changes: led is everything that decides it, in one word, kept in led_cache. */
  for (int s = 0; s < SLOTS; s++) {
    /* g: the step this pad stands for on SEQ and AUTO. strong: a dim light that is a little brighter */
    int state;
    int frame = 0;
    int g = win * PADS + s;
    int strong = 0;
    uint32_t base;
    if (seq && !rec) {
      /* SEQ: the steps. (REC lit: the pads are the kit, and look it – the last branch) */
      int set = (pat()->on[g] >> sel) & 1;
      int head = now == (uint32_t)edit_pat && head_at(g, div);
      base = rgb;
      // clang-format off
      state = head     ? 3
            : set      ? 2
            : g >= lim ? 0
            :            1;
      // clang-format on
      /* a finer grid: the steps that begin a sixteenth are lit a little more */
      strong = state == 1 && div > 1 && g % div == 0;
    } else if (song) {
      if (s < 8) {
        /* SONG, pads 1-8: the patterns - the one that plays, the one that waits (it blinks), the one shown, those of
           the chain */
        base = BLUE;
        // clang-format off
        state = (uint32_t)s == now && step != NO_STEP ? 3
              : s == queued                           ? (BLINK ? 2 : 1)
              : s == edit_pat                         ? 2
              : s < SONG.chain                        ? 1
              :                                         0;
        // clang-format on
      } else {
        /* SONG, pads 9-16: the intensity */
        base = 0xFF5A36;
        state = heat >= (s - 7) * 127 / 8 ? 2 : 1;
      }
    } else if (aut) {
      /* AUTO: the lane's points */
      base = rgb;
      // clang-format off
      state = now == (uint32_t)edit_pat && head_at(g, div) ? 3
            : aut_at(sel, g)                               ? 2
            : g >= lim                                     ? 0
            :                                                1;
      // clang-format on
      strong = state == 1 && div > 1 && g % div == 0;
    } else if (mix) {
      /* MIX: the sounds - dim when not heard, white while they play */
      base = ENGINE_RGB[patches[s].engine];
      state = silent(s) ? 1 : (on >> s) & 1 ? 3 : 2;
    } else {
      /* every other page: the kit's sounds - white while they play, the selected one set */
      base = ENGINE_RGB[patches[s].engine];
      state = (on >> s) & 1 ? 3 : s == sel ? 2 : 1;
      frame = s == sel;
    }
    /* (the pads themselves show it: their lights) */
    (void)frame;
    // clang-format off
    /* the colour in the low 24 bits; the state, the page and `strong` in the high 8
       (at most 3 + 4 * 21 + 100 = 187: it fits the 8 bits) */
    uint32_t led = (base & 0xFFFFFF)
                   | (uint32_t)(state + 4 * (seq + 2 * song + 3 * mix + 4 * aut + 20 * (seq && rec)) + 100 * strong)
                         << 24;
    if (led != led_cache[s]) {
      led_cache[s] = led;
      vibe_led_pad(s, state == 3 ? 0xFFFFFF : base,
                   state == 3   ? 1.0f
                   : state == 2 ? 0.75f
                   : state == 1 ? (strong ? 0.30f : 0.12f)
                   :              0.0f);
    }
    // clang-format on
  }

  /* 5. the page itself, and the four values over encoders 1-4.
     (vibe_link_page: what the host calls the page; enc_rect: no encoder has a place until hud or row gives it one) */
  // clang-format off
  vibe_link_page(page <= PG_WAVE     ? SUB[page]
                 : song              ? "SONG"
                 : aut               ? "AUTO"
                 : mix               ? "MIX"
                 : page == PG_MASTER ? "MASTER"
                 :                     TAB_NAME[TAB_OF[page]]);
  // clang-format on
  for (int i = 0; i < 4; i++) {
    enc_rect[i][2] = 0;
  }
  if (aut) {
    /* AUTO: the lane as tiles; STEP, VALUE, TARGET; the bar's strip or a hint; the patterns and the lanes in use */
    int to = view_to[sel];
    int a = aut_at(sel, cur_step);
    draw_auto(rgb, step, now);
    row(0, "STEP", step_text(buf), (float)(cur_step + 1) / (float)gsteps(), 0, rgb);
    row(1, "VALUE", a && to ? auto_text(to, sel, a - 1, buf) : "--",
        a && to ? (float)(a - 1) / (float)auto_hi(to) : 0.0f, 0, rgb);
    row(2, "TARGET", auto_name(to, sel), (float)to / (float)(SFX_AT_COUNT - 1), 0, to ? TEXT : DIM);
    /* this knob has a lane */
    if (to && lane_of(sel, to) >= 0) {
      vg_circle(456.0f, (float)(CY + 2 * 47 + 12), 4.0f, ORANGE);
    }
    if (to && div > 1) {
      bar_strip(rgb, step, now, 1);
    } else {
      footer(to ? "PADS: A POINT ON / OFF" : "TURN TARGET FIRST", to ? TEXT : ORANGE, "");
    }
    vg_itoa(lanes_used(), buf);
    strcpy(buf + strlen(buf), "/24 lanes");
    strip(step, now, buf, DIM);
  } else if (mix) {
    /* MIX: the sounds as tiles; VOLUME, PAN, DRIVE, OUTPUT */
    draw_mix();
    for (int i = 0; i < 4; i++) {
      const char *name;
      float bar;
      int centre;
      const char *value = mix_text(page, i, &name, buf, &bar, &centre);
      row(i, name, value, bar, centre, i < 2 ? rgb : TEXT);
    }
    footer("PADS: MUTE", TEXT, "SHIFT + pad: solo");
  } else if (page == PG_MASTER) {
    /* MASTER: a picture; its four values in the corners - the delay's two on the left, the reverb's on the right */
    static const uint8_t CORNER[4] = {0, 2, 1, 3};
    draw_master();
    for (int i = 0; i < 4; i++) {
      const char *name;
      float bar;
      int centre;
      const char *value = mix_text(page, i, &name, buf, &bar, &centre);
      hud(CORNER[i], i, name, value, bar, centre, i < 2 ? BLUE : 0xC48BFF);
    }
  } else if (page == PG_LINK) {
    /* LINK: what comes in; the four encoders as the CCs they send */
    draw_link();
    for (int i = 0; i < 4; i++) {
      char n[8] = "CC ";
      vg_itoa(CC_ENCODERS + i, n + 3);
      row(i, n, vg_itoa(cc_out[i], buf), (float)cc_out[i] / 127.0f, 0, GREEN);
    }
  } else if (seq) {
    /* SEQ: the steps as tiles; STEP, NOTE, VELOCITY, LENGTH of the cursor's step; a line that depends on REC, the
       layer and the grid; the patterns and a word */
    int note = pat()->note[sel][cur_step];
    int vel = pat()->vel[sel][cur_step] & 7;
    int len = pat()->len[sel][cur_step];
    draw_seq(rgb, step, now);
    row(0, "STEP", step_text(buf), (float)(cur_step + 1) / (float)gsteps(), 0, rgb);
    row(1, "NOTE", note_text(note, buf), note ? (float)(note - 24) / 84.0f : 0.0f, 0, rgb);
    row(2, "VELOCITY", vg_itoa(vel + 1, buf), (float)(vel + 1) / 8.0f, 0, rgb);
    row(3, "LENGTH", len_text(len, buf), (float)len / (float)(gsteps() - 1), 0, rgb);
    if (rec && div > 1) {
      /* a take at 32 or 64 steps: the 16 steps shown go with the bar, and the strip says which they are */
      bar_strip(rgb, step, now, 0);
    } else if (rec) {
      /* played in live: each hit lands on the step it is nearest */
      footer(running ? "REC: PLAY IT IN - F4 ERASE" : "REC READY: PRESS PLAY", RED, "");
    } else if (!layer_on(sel)) {
      footer("ITS LAYER IS OFF NOW", ORANGE, "");
    } else if (div > 1) {
      /* 32 or 64 steps: where the 16 shown are in the bar */
      bar_strip(rgb, step, now, 0);
    } else {
      footer("PADS: THE 16 STEPS", TEXT, "");
    }
    /* beside the patterns: REC's state (at 32 or 64 steps, where the footer's line is the strip), else the scale */
    // clang-format off
    strip(step, now,
          rec ? (div > 1 ? (running ? "REC: F4 ERASE" : "REC: PRESS PLAY") : "REC off: steps")
              : SCALE_NAME[SONG.scale % SFX_SCALE_COUNT],
          rec && div > 1 ? RED : DIM);
    // clang-format on
  } else if (song) {
    /* SONG: patterns and intensity as tiles; PATTERN, CHAIN ("A-D"), INTENSITY, the selected sound's LAYER */
    char chain[4] = "A-A";
    draw_song(step, now, heat);
    chain[2] = (char)('A' + SONG.chain - 1);
    if (SONG.chain == 1) {
      chain[1] = 0;
    }
    buf[0] = (char)('A' + edit_pat);
    buf[1] = 0;
    row(0, "PATTERN", buf, (float)(edit_pat + 1) / (float)SFX_PATTERNS, 0, BLUE);
    row(1, SONG.arr_len ? "CHAIN (unused)" : "CHAIN", chain, (float)SONG.chain / (float)SFX_PATTERNS, 0,
        SONG.arr_len ? DIM : BLUE);
    row(2, "INTENSITY", percent(heat, buf), (float)heat / 127.0f, 0, 0xFF5A36);
    row(3, "LAYER of this sound", percent(SONG.level[sel], buf), (float)SONG.level[sel] / 127.0f, 0,
        layer_on(sel) ? rgb : DIM);
  } else {
    /* a sound page: the sound's picture; the page's four values (PAGE_PARAMS) in its corners */
    scene(p, rgb, page);
    for (int i = 0; i < 4; i++) {
      int k = PAGE_PARAMS[page][i];
      if (k == ENGINE_KNOB) {
        hud(i, i, "ENGINE", ENGINE_NAME[p->engine], (float)(p->engine + 1) / (float)SFX_ENGINE_COUNT, 0, rgb);
      } else if (k == SFX_P_TABLE) {
        hud(i, i, "TABLE", param_text(k, p, buf), (float)(p->p[k] % SFX_WT_TABLES + 1) / (float)SFX_WT_TABLES, 0, rgb);
      } else if (k >= 0) {
        hud(i, i, param_name(k, p->engine), param_text(k, p, buf), (float)p->p[k] / 127.0f,
            k == SFX_P_PITCH_ENV_AMT || k == SFX_P_FILTER_ENV, rgb);
      } else {
        hud(i, i, "", "", -1.0f, 0, rgb);
      }
      /* a lane moves this knob: a mark at its corner */
      if (k >= 0 && kit == KIT_MUSIC && lane_of(sel, SFX_AT_PATCH + k) >= 0) {
        vg_circle((i & 1) ? 318.0f : 162.0f, (float)((i >> 1) ? CY + CH - 46 : CY + 12), 4.0f, ORANGE);
      }
    }
  }

  /* 6. F1-F6 */
  if (tour_on) {
    tour_caption();
  } else {
    for (int i = 0; i < 6; i++) {
      /* lit: PLAY on MIX, while it plays. held: ERASE, while it erases */
      int lit = mix_page() && i == 4 && running;
      int held = seq && i == 3 && f4_held;
      /* an UNDO with nothing to take back */
      int dim = (sound_page() && i == 2 && !sound_has_undo[sel]) || (song_keys() && i == 1 && !undo_has);
      // clang-format off
      /* the key: red while it erases, green while it plays */
      vg_round(4 + i * 79, 292, 73, 24, 12,
               held  ? RED
               : lit ? GREEN
               :       0x36241C);
      /* its caption: dark on a lit key, faint when there is nothing to take back */
      vg_text(soft_caption(i), 40 + i * 79, 297, VG_BOLD,
              held || lit ? BG
              : dim       ? 0x6A5248
              :             TEXT,
              VG_CENTER);
      // clang-format on
    }
  }

  /* 7. FOCUS: everything above with DATA + ENTER (and the mouse): the six marks of K1-K6, the encoders' values where
     they were drawn, F1-F6 */
  vf_begin();
  for (int i = 0; i < TAB_COUNT; i++) {
    vf_key(VB_K1 + i, LOC_X - 3 + i * 18, 34, 18, 20);
  }
  for (int i = 0; i < 4; i++) {
    if (enc_rect[i][2]) {
      vf_encoder(VE_1 + i, enc_rect[i][0], enc_rect[i][1], enc_rect[i][2], enc_rect[i][3]);
    }
  }
  for (int i = 0; i < 6; i++) {
    vf_key(VB_F1 + i, 4 + i * 79, 292, 73, 24);
  }
  vf_end(0xFFFFFF);
}
