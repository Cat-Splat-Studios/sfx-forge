/* fw_events.h – the start, and every event: pads, keys, notes, MIDI, the mouse. A part of sfx-forge.c.
 *
 * Holds:  vibe_on_init (the host's first call); the HOME texts (HELP_...); factory_reset; pick; perform_pad;
 *         on_event (vibe_on_event in sfx-forge.c calls it for every event).
 * Needs:  every part before it.
 * Read:   on_event is where to begin when the question is "what does this key do": it decides nothing itself, it
 *         sends each event to the function that does the work. Its top comment lists the order.
 */

/* ---- the start ------------------------------------------------------------------------------------------------- */

/* The host calls this once, before anything else. The sounds and the song: the factory's, then what the store holds
   over them. Then the player gets them, and the host is told what the firmware has (notes, parameters, the stage,
   FOCUS, the names of K1-K6). */
void vibe_on_init(float sample_rate) {
  SR = sample_rate;
  sfx_player_init(&P, SR);
  factory();
  bank_load();
  view_sync();
  /* (nothing else runs yet: the song as it was loaded is the player's) */
  song_way_start(&WAY, &SONG, &P.song);
  for (int i = 0; i < SLOTS; i++) {
    P.patch[i] = kits[KIT_MUSIC][i];
  }
  vibe_notes(VIBE_NOTES);
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  /* the mixer as the song that was loaded has it: all of it is told (vibe_params has just put the host's values to
     where a new song has them) */
  mix_known = 0;
  mix_report();
  vibe_stage(stage_fb, SW, SH, 0x180F0B, 0);
  /* DATA chooses the sound; ENTER held = FOCUS */
  vf_mode(VF_ON_HOLD);
  vf_clicks(1);
  for (int i = 0; i < TAB_COUNT; i++) {
    vibe_label(VB_K1 + i, TAB_NAME[i]);
  }
  set_page(PG_OSC);
}

/* ---- HOME: the controls of the page that is open ---------------------------------------------------------------- */
/* Up to 11 rows, so each has the whole width. A row: the control, what it does (the SDK's vg_controls draws them;
   on_draw chooses the table). HELP_MIX is further down, after factory_reset: the tables are in the built .wasm in this
   order. */
// clang-format off
static const char *const HELP_SOUND[] = {
  "PADS  DATA",        "play a sound and select it · select one",
  "KEYS",              "the selected sound as an instrument",
  "SHIFT + ENTER",     "the other kit: SFX (effects) / MUSIC",
  "ENCODER 1-4",       "the four values shown (SHIFT: fast)",
  "K1 SOUND",          "again: OSC, TONE, FILTER (WAVE)",
  "K2 - K6",           "ENV · LFO · FX · SEQ · LINK",
  "LEFT RIGHT",        "the page before / after",
  "F1 PRESET",         "next named sound (SHIFT: the one before)",
  "F2 F3 F4",          "MUTATE · UNDO · RANDOM",
  "F5 F6",             "EXPORT to a game (SysEx) · SAVE",
  "MENU",              "the tour (SHIFT + MENU twice: factory reset)",
};

static const char *const HELP_SEQ[] = {
  "PADS",              "16 steps of the bar. REC lit: they play the kit",
  "SHIFT + PAD",       "hear and select that pad's sound",
  "PAD + KEY",         "hold a step: it plays the key's note",
  "DATA",              "select a sound (a row)",
  "ENCODER 1 2",       "the step in the frame, and its NOTE",
  "ENCODER 3 4",       "its VELOCITY (1-8) · its LENGTH in steps",
  "A   B",             "TEMPO (SHIFT: SWING) · scale (SHIFT: the beat)",
  "LEFT RIGHT",        "pattern before / after · SHIFT: the steps shown",
  "F1  F2",            "LOOP or SONG plays · UNDO (again: redo)",
  "F3 F4 F5",          "FILL · CLEAR · COPY · SHIFT+F5 F6: grid",
  "REC + PLAY",        "play it in. F4 held: ERASE. SHIFT+REC: click",
};

static const char *const HELP_SONG[] = {
  "PADS 1-8",          "pattern A-H, from the next bar line",
  "PADS 9-16",         "INTENSITY, low to high",
  "SHIFT+PAD 1-8",     "ARRANGEMENT: add that pattern as a bar",
  "SHIFT+PAD 9-15 16", "take the last bar away · all (press twice)",
  "ENCODER 1 2",       "PATTERN · CHAIN: A to here play in a row",
  "ENCODER 3 4",       "INTENSITY (CC 14) · LAYER of the sound",
  "A   B",             "TEMPO (SHIFT: SWING) · bar (SHIFT: the beat)",
  "F1",                "what PLAY plays: LOOP a pattern, or the SONG",
  "F2 F3",             "UNDO (again: redo) · CLEAR the pattern",
  "F4",                "NEW empty song (SHIFT: DEMO) - press twice",
  "F5 F6",             "SEND to a game · SAVE",
};

static const char *const HELP_AUTO[] = {
  "K5 AUTO",           "automation: a lane for every sound",
  "ENCODER 3",         "TARGET: any knob - sound, fader, master",
  "PADS",              "a point on that step, on / off",
  "ENCODER 1 2",       "the step in the frame · its VALUE",
  "A POINT",           "holds to the next one, or the bar's end",
  "DATA",              "select a sound: its own lane",
  "SHIFT + PAD",       "hear and select that pad's sound",
  "LEFT RIGHT",        "pattern before / after · SHIFT: the steps shown",
  "F1  F2",            "LOOP or SONG plays · UNDO (again: redo)",
  "F3 F4 F5",          "RAMP · CLEAR · COPY · SHIFT+F5 F6: grid",
  "A",                 "TEMPO (SHIFT: SWING)",
};

static const char *const HELP_PERFORM[] = {
  "K5 PERFORM",        "a row a sound. SAVE: F6 on SEQ (K5 again)",
  "F1 STEPS",          "pads: 16 steps. PAD + KEY: the step's note",
  "F2 SOUNDS",         "pads: the kit. REC + PLAY: played in",
  "F3 MUTES",          "pads: mute a sound.  SHIFT + pad: solo",
  "F4 PATTERN",        "pads 1-8: pattern A-H · 9-16: INTENSITY",
  "F5  F6",            "FILL the selected sound · UNDO (again: redo)",
  "SHIFT + F1-F6",     "LOOP/SONG · CLEAR · ALL ON · COPY · grid - +",
  "DATA  SHIFT+PAD",   "select a sound (STEPS: SHIFT + its pad)",
  "ENCODER 1-4",       "VOLUME · PAN · INTENSITY · LAYER",
  "A   B",             "TEMPO (SHIFT: SWING) · bar (SHIFT: the beat)",
  "LEFT RIGHT",        "pattern before / after · SHIFT: the steps shown",
};

static const char *const HELP_LINK[] = {
  "ENCODERS",          "1-4, A, B send CC 20-25 to the game",
  "PADS",              "play, and send notes 36-51",
  "IN: CC 10-13",      "channel 2: pitch, cutoff, timbre, level",
  "IN: CC 14 15",      "channel 2: intensity · pattern",
  "IN: NOTES",         "36-51 play the pads",
  "F5 EXPORT",         "the selected sound as SysEx",
  "SONG: F5",          "SEND: all sounds and the song",
  "SHIFT + ENTER",     "the other kit: SFX (effects) / MUSIC",
  "MENU",              "the tour",
};
// clang-format on

/* ---- factory reset --------------------------------------------------------------------------------------------- */

/* SHIFT + MENU, twice within three seconds: both kits and the song as they came, and kept so (what was saved is gone).
   The first press asks; the second stops what runs, puts the factory's sounds and song back, forgets every undo, tells
   the player its sounds, and saves. */
static void factory_reset(void) {
  /* the first press: when it was, and that it was */
  static uint64_t asked;
  static int armed;
  int was = kit;
  if (!armed || vibe_frames() - asked > (uint64_t)(3.0f * SR)) {
    armed = 1;
    asked = vibe_frames();
    say("AGAIN: FACTORY RESET");
    return;
  }
  armed = 0;
  if (tour_on) {
    tour_end();
  }
  if (running) {
    seq_play(0);
  }
  factory();
  view_sync();
  for (int k = 0; k < 2; k++) {
    for (int i = 0; i < SLOTS; i++) {
      has_undo_k[k][i] = 0;
    }
  }
  undo_has = 0;
  undo_run = 0;
  undo_redo = 0;
  undo_pat = -1;
  beat_len = 4;
  kit = KIT_MUSIC;
  /* the song's sounds, to the player */
  for (int i = 0; i < SLOTS; i++) {
    changed(i);
  }
  kit = was;
  cur_step = 0;
  pattern_home();
  /* (what was saved is gone: also the song an older firmware kept) */
  vibe_store_delete("song.bin");
  say(bank_save() ? "RESET - NOT SAVED" : "FACTORY RESET");
}

/* (HOME on MIX and MASTER: see the other tables above) */
// clang-format off
static const char *const HELP_MIX[] = {
  "K4 MIX",            "FX again: every sound a fader",
  "PADS",              "mute a sound.  SHIFT + pad: solo",
  "DATA",              "select a sound",
  "ENCODER 1 2",       "VOLUME and PAN of the selected sound",
  "ENCODER 3 4",       "DRIVE on the sum · the OUTPUT volume",
  "K4 MASTER",         "and again: the delay and the reverb",
  "MASTER 1 2",        "the echo's TIME and its FEEDBACK",
  "MASTER 3 4",        "the reverb's SIZE and DAMPING",
  "F1 F2",             "MUTE · SOLO the selected sound",
  "F3 F4",             "RESET this page · ALL ON: no mute, no solo",
  "F5 F6",             "PLAY · SAVE",
};
// clang-format on

/* SHIFT + a pad on SEQ, AUTO and PERFORM's STEPS: hear a pad's sound and select it – without recording it */
static void pick(int slot, int down) {
  if (!down) {
    send(M_RELEASE, slot, slot, 0, 0);
    return;
  }
  sel = slot;
  send(M_TRIGGER, slot, slot, 0.8f, 0);
}

/* A pad on PERFORM: what it is there is perf_mode (F1-F4). Each mode is the pads of another page: STEPS those of SEQ,
   SOUNDS those of a sound page, MUTES those of MIX, PATTERNS those of SONG. */
static void perform_pad(int id, int down, float vel) {
  /* let go: whatever it began – the mode or SHIFT may have gone since */
  if (!down) {
    send(M_RELEASE, id, id, 0, 0);
    if (held_pad == id) {
      held_step = -1;
      held_pad = -1;
    }
    return;
  }
  switch (perf_mode) {
  case PERF_STEPS:
    /* SHIFT: the pad's sound - hear it, select it. Else the step this pad stands for, on / off; while it is held a
       key gives the step its note */
    if (shift) {
      pick(id, 1);
    } else {
      int step = cur_step / PADS * PADS + id;
      step_toggle(step, sel);
      held_pad = id;
      held_step = step;
    }
    break;
  case PERF_SOUNDS:
    /* it sounds, it is selected - and with REC, while the song runs, it is recorded */
    pad(id, 1, vel);
    break;
  case PERF_MUTES:
    sel = id;
    if (shift) {
      SONG.solo ^= (uint16_t)(1u << id);
    } else {
      SONG.mute ^= (uint16_t)(1u << id);
    }
    break;
  default:
    /* PATTERNS: pads 1-8 a pattern (on the bar line while it plays), 9-16 the intensity */
    if (id < 8) {
      pattern_pick(id);
    } else {
      intensity_set((float)(id - 7) / 8.0f);
    }
    break;
  }
}

/* ---- events ---------------------------------------------------------------------------------------------------- */

/* what vf_focus() says for a vf_key stand-in: this + the button (the SDK's own number, not in its headers:
   tests/check-grid-keys.mjs watches it) */
#define FOCUS_KEY 0x40001000

/* Every event of the control flow: a pad, a key, a note, a MIDI message, SysEx, an encoder, the mouse. In this order:
     1  FOCUS (the SDK's frame that DATA moves and ENTER presses) gets the event first - but for SHIFT + ENTER, which
        is ours: the other kit
     2  what waits to go out is sent on, and what the audio did since is taken in (out_pump, seq_poll)
     3  any touch ends the tour
     4  one case for each kind of event:
          a pad       by page: SEQ a step, SONG a pattern or the intensity, AUTO a point, MIX mute / solo, PERFORM
                      whatever F1-F4 chose (perform_pad); else it plays its sound (and sends its note to the game)
          a note      from outside, notes 36-51 are the pads; else a key plays the selected sound at that pitch - and
                      gives a held step its note, or is recorded
          MIDI        the game's telemetry: CC 10-15 on channel 2
          SysEx       sysex_in (fw_link.h)
          a parameter set by the host: one of the mixer's goes where MIX has it (mix_from_host, fw_pages.h)
          an encoder  encoder (fw_pages.h)
          the stage   stage_event (fw_stage_input.h)
          a button    SHIFT; F4 held (ERASE); then on the press: HOME, RETURN, PLAY, STOP, REC, MENU, the arrows,
                      F1-F6 by page (fw_softkeys.h), the kit key, K1-K6 */
static void on_event(const vibe_event *e) {
  /* 1. SHIFT + ENTER (or DATA pressed), a short press: the other kit. It is ours before FOCUS looks at it, so it does
     the same whether a frame is on a value or a page or on nothing. One case stays FOCUS': the frame on F1-F6. There
     ENTER presses that key, and with SHIFT held it is SHIFT + that key (the preset before, DEMO, the grid) - the only
     way to them for a controller without F keys. (ENTER held is FOCUS' too: the SDK keeps that press and we never see
     it.) */
  int on_soft = vf_active() && vf_focus() >= FOCUS_KEY + VB_F1 && vf_focus() <= FOCUS_KEY + VB_F6;
  int kit_key = e->type == VIBE_EV_BUTTON && (e->id == VB_ENTER || e->id == VB_DATA_PUSH) && shift && !on_soft;
  /* r: what FOCUS made of the event (VF_NO: nothing, it is ours). id: vf_event wants a place for the control it
     means; it is not used here */
  int id;
  /* An event's value that is no number is 0, and none is beyond a million: a velocity, a place on the stage, a wheel.
     An encoder's turn is at most a hundred detents (FOCUS counts a turn down detent by detent, and would not come
     back from one without end). A parameter's value and a SysEx's length are looked at where they are taken. */
  vibe_event tamed;
  if (e->type != VIBE_EV_PARAM && e->type != VIBE_EV_SYSEX) {
    float most = e->type == VIBE_EV_ENCODER ? 100.0f : 1.0e6f;
    tamed = *e;
    tamed.value = number_in(e->value, -most, most, 0.0f);
    e = &tamed;
  }
  int r = kit_key ? VF_NO : vf_event(e, &id);

  /* 2. */
  out_pump();
  seq_poll();
  if (r != VF_NO) {
    return;
  }

  /* 3. any touch ends the tour */
  if (tour_on && e->down &&
      (e->type == VIBE_EV_PAD || e->type == VIBE_EV_NOTE || (e->type == VIBE_EV_BUTTON && e->id != VB_SHIFT))) {
    tour_end();
    return;
  }

  /* 4. */
  switch (e->type) {
  case VIBE_EV_PAD:
    if (e->id >= SLOTS) {
      break;
    }
    /* SEQ: the pads are the steps; held, a key gives the step its note */
    if (page == PG_SEQ) {
      /* (let go: whatever it began – REC or SHIFT may have gone since) */
      if (!e->down) {
        send(M_RELEASE, e->id, e->id, 0, 0);
        if (held_pad == e->id) {
          held_step = -1;
          held_pad = -1;
        }
        break;
      }
      /* SHIFT: the pad's sound – hear it, select it */
      if (shift) {
        pick(e->id, e->down);
        break;
      }
      /* REC lit: the pads are the kit – while it plays they are played in, as hard as they are hit */
      if (rec) {
        pad(e->id, e->down, e->value);
        break;
      }
      /* else: the step this pad stands for, on / off (the 16 steps round the cursor) */
      {
        int step = cur_step / PADS * PADS + e->id;
        step_toggle(step, sel);
        held_pad = e->id;
        held_step = step;
      }
      break;
    }
    /* SONG: pads 1-8 a pattern, 9-16 the intensity; with SHIFT the arrangement */
    if (page == PG_SONG) {
      if (e->down) {
        if (shift) {
          arr_edit(e->id);
        } else if (e->id < 8) {
          pattern_pick(e->id);
        } else {
          intensity_set((float)(e->id - 7) / 8.0f);
        }
      }
      break;
    }
    /* AUTO: the pads: a point on / off */
    if (page == PG_AUTO) {
      if (!e->down) {
        send(M_RELEASE, e->id, e->id, 0, 0);
      } else if (shift) {
        pick(e->id, 1);
      } else {
        auto_toggle(cur_step / PADS * PADS + e->id);
      }
      break;
    }
    /* PERFORM: the pads are what F1-F4 chose */
    if (page == PG_PERFORM) {
      perform_pad(e->id, e->down, e->value);
      break;
    }
    /* MIX: the pads: mute, SHIFT: solo */
    if (page == PG_MIX) {
      if (e->down) {
        sel = e->id;
        if (shift) {
          SONG.solo ^= (uint16_t)(1u << sel);
        } else {
          SONG.mute ^= (uint16_t)(1u << sel);
        }
      }
      break;
    }
    /* every other page: the pad plays its sound, and its note goes out to the game (36-51) */
    pad(e->id, e->down, e->value);
    if (e->down && page == PG_WAVE && patches[sel].engine != SFX_ENGINE_WAVETABLE) {
      set_page(PG_OSC);
    }
    vibe_midi_send(e->down ? 0x90 : 0x80, (uint8_t)(NOTE_PADS + e->id),
                   e->down ? (uint8_t)vd_clamp(e->value * 127.0f, 1.0f, 127.0f) : 0);
    break;

  case VIBE_EV_NOTE: {
    int outside = e->midi[1] == VIBE_NOTE_MIDI || e->midi[1] == VIBE_NOTE_HOST;
    /* a game or a DAW plays the kit */
    if (outside && e->id >= NOTE_PADS && e->id < NOTE_PADS + SLOTS) {
      from_outside = 1;
      pad(e->id - NOTE_PADS, e->down, e->value);
      from_outside = 0;
      break;
    }
    /* a key: the selected sound at this pitch. (Its voice is named 100 + the note, so that it does not let a pad's
       voice go: those are named 0-15) */
    if (!e->down) {
      send(M_RELEASE, sel, 100 + (e->id % 96), 0, 0);
      break;
    }
    if ((page == PG_SEQ || page == PG_PERFORM) && held_step >= 0) {
      undo_open(1);
      step_set(pat(), held_step, sel, e->id);
    } else {
      record(sel, e->id, e->value);
    }
    /* the sound at this pitch */
    send(M_TRIGGER, sel, 100 + (e->id % 96), e->value > 0.0f ? e->value : 0.8f, e->id);
    break;
  }

  case VIBE_EV_MIDI:
    /* telemetry: channel 2, CC 10.. (k: which of them, v: its value 0..1) */
    if (e->midi[0] == 0xB1 && e->midi[1] >= CC_TELEMETRY && e->midi[1] < CC_TELEMETRY + 8) {
      int k = e->midi[1] - CC_TELEMETRY;
      float v = (float)e->midi[2] / 127.0f;
      telemetry[k] = e->midi[2];
      linked = 1;
      if (k == 0) {
        vibe_param_set(P_MOD_PITCH, v * 24.0f);
      }
      if (k == 1) {
        vibe_param_set(P_MOD_CUTOFF, vd_clamp(((float)e->midi[2] - 64.0f) / 16.0f, -4.0f, 4.0f));
      }
      if (k == 2) {
        vibe_param_set(P_MOD_FM, v);
      }
      if (k == 3) {
        vibe_param_set(P_MOD_LEVEL, v);
      }
      if (k == 4) {
        vibe_param_set(P_INTENSITY, v);
      }
      /* 0..127 = pattern A..H (a value that repeats is one cue) */
      if (k == 5 && (e->midi[2] >> 4) != (queued >= 0 ? queued : edit_pat)) {
        pattern_pick(e->midi[2] >> 4);
      }
    }
    break;

  case VIBE_EV_SYSEX:
    /* (its length is within the SDK's buffer, vibe.h: 4096) */
    if (is_number(e->value) && e->value >= 2.0f && e->value <= 4096.0f) {
      sysex_in(vibe_sysex_in(), (int)e->value);
    }
    break;

  case VIBE_EV_SAVE:
    bank_save();
    break;

  case VIBE_EV_PARAM:
    /* the host set a parameter: one of the mixer's is put where MIX has it */
    mix_from_host(e->id, e->value);
    break;

  case VIBE_EV_STAGE:
    stage_event(e);
    break;

  case VIBE_EV_ENCODER:
    if (e->id < VE_COUNT) {
      encoder(e->id, e->value);
    }
    break;

  case VIBE_EV_BUTTON:
    /* SHIFT, down and up (on SEQ, AUTO and PERFORM the keys' captions say what they do with it) */
    if (e->id == VB_SHIFT) {
      shift = e->down;
      relabel();
      break;
    }
    /* SEQ, REC while it plays: F4 is ERASE for as long as it is held */
    if (e->id == VB_F4) {
      if (!e->down) {
        f4_held = 0;
      } else if (page == PG_SEQ && rec && running) {
        f4_held = 1;
        erase_at = -1;
        erase_pat = -1;
        say("ERASE: HOLD F4");
        seq_poll();
        break;
      }
    }
    /* every other button counts when it is pressed */
    if (!e->down) {
      break;
    }
    if (e->id == VB_HOME) {
      help = !help;
    } else if (e->id == VB_RETURN) {
      if (help) {
        help = 0;
      } else {
        set_page(PG_OSC);
      }
    } else if (e->id == VB_PLAY) {
      seq_play(!running);
    } else if (e->id == VB_STOP) {
      seq_play(0);
    } else if (e->id == VB_REC) {
      /* SHIFT + REC: the metronome */
      if (shift) {
        metro = !metro;
        say(metro ? "CLICK ON" : "CLICK OFF");
      } else {
        seq_rec(!rec);
        /* PERFORM: lighting REC makes the pads the kit, as it does on SEQ (F1 makes them steps again; REC off leaves
           them as they are) */
        if (rec && page == PG_PERFORM) {
          perform_mode(PERF_SOUNDS);
        }
      }
    } else if (e->id == VB_MENU) {
      if (shift) {
        factory_reset();
      } else {
        tour_start();
      }
    } else if (e->id == VB_LEFT || e->id == VB_RIGHT) {
      arrow(e->id == VB_LEFT ? -1 : 1);
    } else if (e->id >= VB_F1 && e->id <= VB_F6) {
      /* F1-F6: by page */
      int f = e->id - VB_F1;
      if (page == PG_SEQ) {
        soft_seq(f);
      } else if (page == PG_SONG) {
        soft_song(f);
      } else if (page == PG_AUTO) {
        soft_auto(f);
      } else if (page == PG_PERFORM) {
        soft_perform(f);
      } else if (mix_page()) {
        soft_mix(f);
      } else {
        soft_sound(f);
      }
    } else if (kit_key) {
      /* SHIFT + ENTER: the other kit, on a sound page */
      kit_set(!kit);
      if (!sound_page()) {
        set_page(PG_OSC);
      }
      say(kit == KIT_SFX ? "SFX KIT" : "MUSIC KIT");
    } else if (e->id >= VB_K1 && e->id < VB_K1 + TAB_COUNT) {
      /* (K1-K6 are the pages with SHIFT held too. On the VIBE itself SHIFT + K1-K6 never get here: there the VIBE plays
         notes with them, SDK 28, and SHIFT + RETURN locks that. Nothing of ours is on either.) */
      tab_key(e->id - VB_K1);
    }
    break;

  default:
    break;
  }
}
