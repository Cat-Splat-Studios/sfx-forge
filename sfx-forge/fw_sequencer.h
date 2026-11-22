/* fw_sequencer.h – playing, recording and editing the song's patterns. A part of sfx-forge.c.
 *
 * Holds, in this order:
 *   playing      record (a hit lands on a step), pad (a pad played), mode_set / mode_toggle (LOOP or SONG)
 *   the view     view_win (which 16 steps the pads show), follow_end, cur_sync (the cursor after a grid change)
 *   transport    seq_play (PLAY, STOP), seq_rec (REC), pattern_pick / pattern_step / pattern_home
 *   polling      seq_poll: what the audio did since the last look - and F4 ERASE as the bar passes
 *   steps        intensity_set, step_toggle, note_turn, vel_turn, len_turn
 *   automation   auto_own, auto_hi, view_to, lane_of, lanes_used, aut_at, lanes_tidy, aut_put, view_sync,
 *                auto_turn, auto_toggle, auto_ramp
 * Needs:  fw_state.h, fw_audio.h (send), fw_edit.h (say, pat, undo_open, undo_mark, undo_close), fw_factory.h
 *         (step_set), core/sfx_song.h.
 * Read:   a pattern (sfx_pattern, core/sfx_song.h) holds for every step: `on` (a bit a sound), and for each sound a
 *         note, a velocity (0-7) and a length. A step is counted in the pattern's own grid: 16, 32 or 64 a bar
 *         ("div" is the steps a sixteenth: 1, 2 or 4). The controls only write the song (SONG) and send messages;
 *         where the song is they read from seq_pos, which the audio writes.
 */

/* ---- playing --------------------------------------------------------------------------------------------------- */

/* REC while it runs: onto the nearest step of the pattern that plays - of its own grid -, as hard as it was hit.
   The step is the PLAYING pattern's (seq_pattern), also for a hit that rounds up over the bar line: it lands on that
   pattern's first step, not on the pattern the next bar may bring (a cue, the arrangement). The fraction comes from
   the player and already allows for swing. The timing is as fine as one audio block: that is how often the audio
   writes seq_pos. */
static void record(int slot, int note, float vel) {
  /* (step, grid and fraction: one word, read once) */
  uint32_t w = seq_pos;
  /* (the song plays the MUSIC kit: the SEQ page has it) */
  if (rec && running && kit != KIT_MUSIC) {
    if (!from_outside) {
      say("MUSIC KIT ONLY: K5 SEQ");
    }
    return;
  }
  if (!rec || w == NO_STEP) {
    return;
  }
  undo_open(2);
  sfx_pattern *p = &SONG.pat[seq_pattern % SFX_PATTERNS];
  /* st: the step that plays. div: the grid the audio counted it in. pd: the pattern's grid as it is now */
  int st = pos_step(w);
  int div = pos_div(w);
  int pd = sfx_div(p->grid);
  /* (its grid changed since the audio said where it is) */
  if (pd != div) {
    st = st * pd / div;
    div = pd;
  }
  int n = bar_len() * div;
  /* past the middle of the step: the next one */
  if (pos_frac(w) > 0.5f) {
    st = (st + 1) % n;
  }
  st %= n;
  step_set(p, st, slot, note);
  /* (its length stays) */
  p->vel[slot][st] = (uint8_t)(vel > 0.0f ? (int)vd_clamp(vel * 7.99f, 0.0f, 7.0f) : SFX_VEL_DEFAULT);
}

/* a pad is played (or let go): it sounds, it is the selected sound, and - REC while it runs - it is recorded */
static void pad(int slot, int down, float vel) {
  if (!down) {
    send(M_RELEASE, slot, slot, 0, 0);
    return;
  }
  sel = slot;
  send(M_TRIGGER, slot, slot, vel > 0.0f ? vel : 0.8f, 0);
  record(slot, 0, vel);
}

/* LOOP or SONG: what PLAY plays. Only this tells the player. 1 = it changed */
static int mode_set(int loop) {
  loop = loop != 0;
  if (loop == loop_mode) {
    return 0;
  }
  loop_mode = loop;
  send(M_LOOP, loop, 0, 0, 0);
  relabel();
  return 1;
}

/* F1 on the song's pages */
static void mode_toggle(void) {
  mode_set(!loop_mode);
  say(loop_mode ? "LOOP: PATTERN REPEATS" : "SONG: PLAYS IN ORDER");
}

/* ---- the view: which steps are shown --------------------------------------------------------------------------- */

/* the 16 steps the pads and the tiles show: those round the cursor - while a take runs on SEQ, those that play.
   The answer is a window: 0 = steps 1-16, 1 = steps 17-32, ... */
static int view_win(void) {
  if (page == PG_SEQ && rec && running && follow_win >= 0 && follow_win * PADS < gsteps()) {
    return follow_win;
  }
  return cur_step / PADS;
}

/* a take ended: the tiles stay where it was */
static void follow_end(void) {
  if (follow_win >= 0 && follow_win * PADS < gsteps() && cur_step / PADS != follow_win) {
    cur_step = follow_win * PADS;
  }
  follow_win = -1;
}

/* another pattern is shown, or its grid changed: the cursor stays on its sixteenth */
static void cur_sync(void) {
  int d = pat_div(edit_pat);
  if (edit_pat == cur_pat && d == cur_div) {
    return;
  }
  cur_step = clampi(cur_step * d / cur_div, 0, SFX_BAR * d - 1);
  held_step = -1;
  held_pad = -1;
  cur_pat = edit_pat;
  cur_div = d;
}

/* ---- transport ------------------------------------------------------------------------------------------------- */

/* PLAY (on = 1) and STOP. (It never changes the mode: the top line said what it will play) */
static void seq_play(int on) {
  if (!on) {
    follow_end();
  }
  running = on;
  undo_run = 0;
  if (!on) {
    /* (a pattern chosen during a take, not yet begun: it is the one shown now) */
    if (queued >= 0) {
      edit_pat = queued;
    }
    f4_held = 0;
    queued = -1;
  }
  /* again, should the queue have been full when the mode was set */
  if (on) {
    send(M_LOOP, loop_mode, 0, 0, 0);
  }
  send(on ? M_START : M_STOP, edit_pat, 0, 0, 0);
  /* (0x6EE7A8: GREEN of fw_draw_parts.h, which comes after this part) */
  vibe_led_button(VB_PLAY, 0x6EE7A8, on ? 1.0f : 0.0f);
  relabel();
}

/* REC lit: PLAY loops the pattern (F1 may still say SONG: then the take goes along the song), and what is played is
   laid on top */
static void seq_rec(int on) {
  if (!on) {
    follow_end();
  }
  rec = on;
  rec_on = on;
  undo_run = 0;
  held_step = -1;
  held_pad = -1;
  if (!on) {
    f4_held = 0;
  }
  /* (0xFF4040: RED of fw_draw_parts.h) */
  vibe_led_button(VB_REC, 0xFF4040, on ? 1.0f : 0.0f);
  if (on && !tour_on && mode_set(1)) {
    say("REC ON: PATTERN LOOPS");
  }
  relabel();
}

/* a pattern is chosen: at once while it stands, on the next bar line while it runs. While a take runs the display stays
   on the pattern that plays, and follows on the bar line (seq_poll) – so what is played lands in what is shown. */
static void pattern_pick(int p) {
  p = (p % SFX_PATTERNS + SFX_PATTERNS) % SFX_PATTERNS;
  send(M_CUE, p, 0, 0, 0);
  queued = running && p != (int)seq_pattern ? p : -1;
  if (!(rec && running)) {
    edit_pat = p;
  }
  undo_close();
}

/* the one before / after */
static void pattern_step(int d) {
  pattern_pick((rec && running && queued >= 0 ? queued : edit_pat) + d);
}

/* a new song: its pattern A */
static void pattern_home(void) {
  edit_pat = 0;
  send(M_CUE, 0, 0, 0, 0);
  queued = running && seq_pattern != 0 ? 0 : -1;
}

/* ---- polling --------------------------------------------------------------------------------------------------- */

/* from every draw and every event: what the audio did since. The queued pattern has started; the view follows a take;
   F4 held erases the selected sound on every step the bar has passed, in the pattern that plays. */
static void seq_poll(void) {
  /* w: where the song is (one word, read once). now: the pattern that plays */
  uint32_t w = seq_pos;
  uint32_t now = seq_pattern % SFX_PATTERNS;
  cur_sync();
  if (!running || (queued >= 0 && now == (uint32_t)queued)) {
    queued = -1;
  }
  if (rec && running && w != NO_STEP) {
    edit_pat = (int)now;
  }
  if (w != NO_STEP) {
    sfx_pattern *p = &SONG.pat[now];
    /* the step that plays, in its pattern's grid as it is now */
    int pd = sfx_div(p->grid);
    int st = pos_step(w) * pd / pos_div(w);
    if (rec && running && page == PG_SEQ) {
      follow_win = st / PADS;
    }
    /* ERASE: every step from the one erased last up to the one that plays now */
    if (f4_held && rec && running) {
      int len = bar_len() * pd;
      int at = st % len;
      /* another pattern began: only what passes in it */
      if (erase_pat != (int)now) {
        erase_pat = (int)now;
        erase_at = -1;
      }
      if (erase_at >= len) {
        erase_at = -1;
      }
      for (int n = 0; n < len && erase_at != at; n++) {
        int i = erase_at < 0 ? at : (erase_at + 1) % len;
        undo_open(2);
        p->on[i] &= (uint16_t) ~(1u << sel);
        p->note[sel][i] = 0;
        p->vel[sel][i] = SFX_VEL_DEFAULT;
        p->len[sel][i] = 0;
        erase_at = i;
      }
    }
  }
  cur_sync();
}

/* ---- steps ----------------------------------------------------------------------------------------------------- */

/* INTENSITY (0..1): a host parameter, so that a DAW records it and a game's CC 14 moves the same value */
static void intensity_set(float v) {
  vibe_param_set(P_INTENSITY, vd_clamp(v, 0.0f, 1.0f));
}

/* a step of the pattern shown, on / off for a sound; the cursor goes there */
static void step_toggle(int step, int slot) {
  undo_open(1);
  pat()->on[step] ^= (uint16_t)(1u << slot);
  cur_step = step;
  /* switched on: hear it, at its note (while the song stands still) */
  if (!running && (pat()->on[step] >> slot) & 1) {
    send(M_TRIGGER, slot, slot, SFX_VEL[pat()->vel[slot][step] & 7], pat()->note[slot][step]);
    send(M_RELEASE, slot, slot, 0, 0);
  }
}

/* the cursor step's note, along the scale; it switches the step on */
static void note_turn(int d) {
  int n = pat()->note[sel][cur_step];
  undo_open(1);
  for (int i = 0; i < (d < 0 ? -d : d); i++) {
    n = sfx_scale_step(n, d, SONG.scale);
  }
  step_set(pat(), cur_step, sel, n);
}

/* the cursor step: how hard (1..8) … */
static void vel_turn(int d) {
  uint8_t *a = &pat()->vel[sel][cur_step];
  undo_open(1);
  *a = (uint8_t)clampi((*a & 7) + d, 0, 7);
}

/* … and how long: 0 = the sound's own length (the display says "AUTO"; not the AUTO page), else steps of the
   pattern's grid, up to a bar less one */
static void len_turn(int d) {
  uint8_t *a = &pat()->len[sel][cur_step];
  undo_open(1);
  *a = (uint8_t)clampi(*a + d, 0, gsteps() - 1);
}

/* ---- automation ------------------------------------------------------------------------------------------------ */
/* A lane moves one knob of one sound: `track` (the sound + 1, 0 = the lane is free) and `to` (the knob: SFX_AT_...,
   core/sfx_song.h - one of the sound's values, its fader or pan, or one of the master's). Its points are in the
   patterns: aut[lane][step], 0 = no point, else the value + 1. A point holds until the next one. */

/* the selected sound's automation lane. What its target is without a point (where a new point starts) … */
static int auto_own(int to, int track) {
  // clang-format off
  return to >= SFX_AT_MASTER ? SONG.fx[to - SFX_AT_MASTER]
       : to == SFX_AT_PAN    ? SONG.pan[track]
       : to == SFX_AT_VOL    ? SONG.vol[track]
       : to                  ? kits[KIT_MUSIC][track].p[to - SFX_AT_PATCH]
       :                       0;
  // clang-format on
}

/* … and how far it goes */
static int auto_hi(int to) {
  // clang-format off
  return to == SFX_AT_MASTER + SFX_M_DELAY_TIME ? SFX_DELAY_TIMES - 1
       : to == SFX_AT_PATCH + SFX_P_TABLE       ? SFX_WT_TABLES - 1
       :                                          126;
  // clang-format on
}

/* Any knob may have a lane; a song holds SFX_LANES of them. view_to: the knob each sound's AUTO page shows now. The
   first point of a knob takes a lane, its last one gives it back. */
static uint8_t view_to[SLOTS];
/* the knob turned last (a lane's target), and of which sound */
static uint8_t last_to;
static int last_sel = -1;

/* the lane that moves knob `to` of a sound, -1: none */
static int lane_of(int track, int to) {
  for (int l = 0; l < SFX_LANES; l++) {
    if (SONG.lane[l].track == track + 1 && SONG.lane[l].to == to) {
      return l;
    }
  }
  return -1;
}

static int lanes_used(void) {
  int n = 0;
  for (int l = 0; l < SFX_LANES; l++) {
    n += SONG.lane[l].track != 0;
  }
  return n;
}

/* the point on a step of the lane a sound's AUTO page shows: 0 = none, else value + 1 */
static int aut_at(int track, int step) {
  int l = lane_of(track, view_to[track]);
  return l < 0 ? 0 : pat()->aut[l][step];
}

/* a lane without a point anywhere is free again */
static void lanes_tidy(void) {
  for (int l = 0; l < SFX_LANES; l++) {
    int any = 0;
    for (int k = 0; k < SFX_PATTERNS && !any; k++) {
      for (int i = 0; i < SFX_STEPS; i++) {
        if (SONG.pat[k].aut[l][i]) {
          any = 1;
          break;
        }
      }
    }
    if (!any) {
      SONG.lane[l].track = 0;
    }
  }
}

/* a point of the lane a sound's AUTO page shows. v: 0 = no point, else value + 1. The first point takes a free lane */
static void aut_put(int track, int step, int v) {
  int to = view_to[track];
  int l = lane_of(track, to);
  if (l < 0 && v) {
    /* the first free lane */
    for (l = 0; l < SFX_LANES && SONG.lane[l].track; l++) {
    }
    if (l == SFX_LANES) {
      say("ALL 24 LANES IN USE");
      return;
    }
    for (int k = 0; k < SFX_PATTERNS; k++) {
      for (int i = 0; i < SFX_STEPS; i++) {
        SONG.pat[k].aut[l][i] = 0;
      }
    }
    SONG.lane[l].to = (uint8_t)to;
    SONG.lane[l].track = (uint8_t)(track + 1);
  }
  if (l < 0) {
    return;
  }
  pat()->aut[l][step] = (uint8_t)v;
  if (!v) {
    lanes_tidy();
  }
}

/* a song came: each sound's page shows its first lane */
static void view_sync(void) {
  for (int t = 0; t < SLOTS; t++) {
    view_to[t] = 0;
  }
  for (int l = SFX_LANES - 1; l >= 0; l--) {
    if (SONG.lane[l].track && SONG.lane[l].track <= SLOTS) {
      view_to[SONG.lane[l].track - 1] = SONG.lane[l].to;
    }
  }
}

/* the point on the cursor step: made where the knob stands, then moved */
static void auto_turn(int d) {
  int to = view_to[sel];
  int a = aut_at(sel, cur_step);
  if (!to) {
    say("TURN TARGET FIRST");
    return;
  }
  undo_open(1);
  /* a is 0 for no point, else the point's value + 1.
     A point that is there: its value, moved by d. None yet: a new one where the knob stands (auto_own), not moved by
     this first turn. Either way kept within 0 .. auto_hi(to), and stored as the value + 1 */
  // clang-format off
  aut_put(sel, cur_step,
          clampi((a ? a - 1
                    : clampi(auto_own(to, sel), 0, auto_hi(to)))
                 + (a ? d : 0),
                 0, auto_hi(to))
          + 1);
  // clang-format on
}

/* a pad on AUTO: a point on that step, on (where the knob stands) / off */
static void auto_toggle(int step) {
  int to = view_to[sel];
  cur_step = step;
  if (!to) {
    say("TURN TARGET FIRST");
    return;
  }
  undo_open(1);
  aut_put(sel, step, aut_at(sel, step) ? 0 : clampi(auto_own(to, sel), 0, auto_hi(to)) + 1);
}

/* F3 RAMP: a straight line from each point to the next */
static void auto_ramp(void) {
  /* last: the point before. n: the points of the lane. l: the lane */
  int last = -1;
  int n = 0;
  int l = lane_of(sel, view_to[sel]);
  for (int i = 0; l >= 0 && i < gsteps(); i++) {
    n += pat()->aut[l][i] != 0;
  }
  if (n < 2) {
    say("SET TWO POINTS FIRST");
    return;
  }
  undo_mark("RAMP", 0);
  for (int i = 0; i < gsteps(); i++) {
    if (!pat()->aut[l][i]) {
      continue;
    }
    if (last >= 0) {
      for (int k = last + 1; k < i; k++) {
        pat()->aut[l][k] =
            (uint8_t)(pat()->aut[l][last] + (pat()->aut[l][i] - pat()->aut[l][last]) * (k - last) / (i - last));
      }
    }
    last = i;
  }
  say("RAMP");
}
