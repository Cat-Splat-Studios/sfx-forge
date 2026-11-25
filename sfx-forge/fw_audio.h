/* fw_audio.h – the audio flow: the queue that reaches it, and vibe_on_audio. A part of sfx-forge.c.
 *
 * Holds:  the messages the controls send to the audio (msg, send); vibe_on_audio, which the host calls for every block
 *         of samples.
 * Needs:  fw_state.h (P, SONG, WAY, the values the audio shows the controls), fw_sounds.h (P_...), song_way.h.
 * Read:   send() is the ONLY way anything in the control flow makes the player do something. vibe_on_audio is the only
 *         function of the firmware that runs in the audio flow: it must not wait, allocate or call the store, and it
 *         touches nothing of the controls but the queue, WAY and the host's parameters.
 */

/* ---- from the controls to the audio: one queue, one writer, one reader ----------------------------------------- */
enum {
  M_TRIGGER, /* a sound starts: slot, key (what M_RELEASE names it by), velocity, note (0: as designed), its patch */
  M_RELEASE, /* the key is let go */
  M_UPDATE,  /* a sound was edited: its patch, also for what sounds now */
  M_START,   /* PLAY, from pattern `slot` */
  M_STOP,    /* STOP */
  M_CUE,     /* pattern `slot` takes over (at once while it stands, else on the bar line) */
  M_QUIET,   /* every key let go */
  M_LOOP     /* what PLAY plays: slot 1 = LOOP, 0 = SONG */
};

/* one message */
typedef struct {
  uint8_t type; /* M_... */
  uint8_t slot; /* the sound 0-15; for M_START and M_CUE the pattern; for M_LOOP 1 or 0 */
  uint8_t note; /* the note to play, 0: as the sound was designed */
  uint8_t kit;  /* the kit `slot` is of */
  int16_t key;  /* what M_RELEASE names the note by */
  float vel;    /* the velocity, 0..1 */
  /* the put of the song it waits for (M_START, M_STOP, M_CUE and M_LOOP only: msg_song) */
  uint32_t need;
  /* the sound as it was when this was sent */
  sfx_patch patch;
} msg;

/* ---- what the host hands over, looked at before it is used -------------------------------------------------------
   A host may hand over a value that is no number (NaN) or without end (an infinity): a parameter, a velocity, a turn,
   the song position. One of them in the sound stays there for good, and as a position it reads past the song. The
   SDK's build uses fast maths, which lets the compiler drop a test such as v != v: so the bits are looked at. */
static int is_number(float v) {
  union {
    float f;
    uint32_t u;
  } b = {v};
  return (b.u & 0x7F800000u) != 0x7F800000u;
}
static int is_number_d(double v) {
  union {
    double f;
    uint64_t u;
  } b = {v};
  return (b.u & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}
/* v within lo..hi, or `else_` if it is no number */
static float number_in(float v, float lo, float hi, float else_) {
  return !is_number(v) ? else_ : v < lo ? lo : v > hi ? hi : v;
}
/* host parameter i, for the audio and for the controls: as it is, or where it starts if the host set it to no number
   (the SDK's own clamp lets such a value through) */
static float param_in(int i) {
  return number_in(vibe_param_get(i), PARAMS[i].min, PARAMS[i].max, PARAMS[i].def);
}

/* what starts or changes what plays: it must find the song as it was when this was sent */
static int msg_song(int type) {
  return type == M_START || type == M_STOP || type == M_CUE || type == M_LOOP;
}

/* the queue: a ring of QN messages. qw: how many were written (the controls), qr: how many were read (the audio) */
#define QN 64
static msg q[QN];
static volatile uint32_t qw;
static volatile uint32_t qr;

/* Control flow: a message to the audio. It carries the sound's patch as it is now, of the kit that is chosen. */
static void send(int type, int slot, int key, float vel, int note) {
  /* the song first, as it is now (whole, wherever one of these is sent): what starts, starts in it - the audio works
     this only with that song, or a later one */
  uint32_t need = msg_song(type) ? song_way_need(&WAY, &SONG) : 0;
  uint32_t w = qw;
  /* full: dropped */
  if (w - qr >= QN) {
    return;
  }
  msg *m = &q[w % QN];
  m->type = (uint8_t)type;
  m->slot = (uint8_t)slot;
  m->key = (int16_t)key;
  /* (a velocity is 0..1) */
  m->vel = number_in(vel, 0.0f, 1.0f, 0.8f);
  m->note = (uint8_t)note;
  m->need = need;
  m->patch = patches[slot & 15];
  m->kit = (uint8_t)kit;
  /* The message is written before the count. VIBE_RELEASE is the SDK's memory fence (vibe.h, "two flows"): with it
     the audio, in another thread, never sees qw ahead of the message it counts. */
  VIBE_RELEASE();
  qw = w + 1;
}

/* ---- the audio flow -------------------------------------------------------------------------------------------- */
/* The host calls this for every block. In this order:
     1  the song, should the controls have handed a new one (song_way_take), then the queue's messages
     2  the telemetry (the host's parameters), glided
     3  the player renders the block: the song's voices, the pads' and keys' voices, the effects
     4  what the controls may know: what sounds, the step and the pattern that play
     5  the metronome's click is decided
     6  the output: volume, a soft limit, the click on top; and the scope */
void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames) {
  (void)in;

  /* 1. the song and the queue */
  uint32_t w = qw;
  uint32_t k;
  /* the counterpart of the fence in send(): the count was read first, now the messages it counts may be read */
  VIBE_ACQUIRE();
  /* the song as the controls have made it since, whole - or none this block, should they be writing the next right
     now. A grid that changed under the playhead: the player finds its step again */
  song_way_take(&WAY, &P.song);
  for (k = qr; k != w; k++) {
    const msg *m = &q[k % QN];
    /* its song is not here yet (one of the two stepped back): it waits, and all behind it, in their order - a start
       never runs in the song from before the edit that came first */
    if (msg_song(m->type) && !song_way_has(&WAY, m->need)) {
      break;
    }
    switch (m->type) {
    case M_TRIGGER:
      /* (a sound of the SFX kit is slot 16-31 to the player: beside the song) */
      sfx_player_note_on(&P, m->slot + SLOTS * m->kit, m->key, m->vel, m->note, 0.0f, &m->patch);
      break;
    case M_RELEASE:
      sfx_player_note_off(&P, m->key);
      break;
    case M_UPDATE:
      /* a knob turned: also for what sounds now */
      sfx_player_set_patch(&P, m->slot + SLOTS * m->kit, &m->patch);
      break;
    case M_START:
      sfx_player_start(&P, m->slot);
      break;
    case M_STOP:
      sfx_player_stop(&P);
      break;
    case M_CUE:
      sfx_player_cue(&P, m->slot);
      break;
    case M_LOOP:
      /* (the controls never write P.loop) */
      sfx_player_set_loop(&P, m->slot);
      break;
    case M_QUIET:
      /* every key let go */
      for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
        sfx_voice_release(&P.voice[i]);
      }
      break;
    }
  }
  /* the same fence, the other way: the messages were read before the controls see their places as free */
  VIBE_RELEASE();
  qr = k;

  /* 2. the telemetry glides: 30 ms, no steps. The host's parameters are the second way from the controls to the
     audio beside the queue: the controls set them (vibe_param_set), the audio reads them here and in part 6. */
  float glide = vd_coef(0.03f, SR / (float)frames);
  mods.pitch_st = param_in(P_MOD_PITCH) + (mods.pitch_st - param_in(P_MOD_PITCH)) * glide;
  mods.cutoff_oct = param_in(P_MOD_CUTOFF) + (mods.cutoff_oct - param_in(P_MOD_CUTOFF)) * glide;
  mods.fm = param_in(P_MOD_FM) + (mods.fm - param_in(P_MOD_FM)) * glide;
  mods.level = param_in(P_MOD_LEVEL) + (mods.level - param_in(P_MOD_LEVEL)) * glide;
  P.mods = mods;
  P.intensity = param_in(P_INTENSITY);

  /* 3. the player renders. The DAW's song position while it plays, else the player's own clock */
  const vibe_transport_info *tp = vibe_transport();
  /* (a tempo and a position that are numbers, and not beyond what a song can be: else the player's own clock) */
  int host = tp->has && tp->playing && is_number(tp->bpm) && tp->bpm > 0.0f && tp->bpm <= 2000.0f &&
             is_number_d(tp->beat) && tp->beat > -1.0e9 && tp->beat < 1.0e9;
  for (int n = 0; n < frames; n++) {
    mixr[n] = 0.0f;
    mixl[n] = 0.0f;
  }
  sfx_player_render(&P, mixl, mixr, frames, host ? tp->beat * 4.0 : -1.0,
                    host ? (double)tp->bpm / 60.0 * 4.0 / (double)SR : 0.0);

  /* 4. for the controls: what sounds (a bit a sound, each kit), the step, the pattern, the bar of the arrangement */
  uint32_t music = 0;
  uint32_t effects = 0;
  for (int i = 0; i < SFX_PLAYER_VOICES; i++) {
    if (P.voice[i].active) {
      if (P.voice[i].slot < SLOTS) {
        music |= 1u << P.voice[i].slot;
      } else {
        effects |= 1u << (P.voice[i].slot - SLOTS);
      }
    }
  }
  play_k[0] = music;
  play_k[1] = effects;
  seq_step = P.running && P.step >= 0 ? (uint32_t)P.step : NO_STEP;
  seq_pattern = (uint32_t)P.pattern;
  seq_pos = P.running && P.step >= 0 ? (uint32_t)P.div << 28 | (uint32_t)(P.step & 0xFF) << 20 |
                                           (uint32_t)(vd_clamp(P.frac, 0.0f, 1.0f) * 1048575.0f)
                                     : NO_STEP;
  seq_arr = P.running && !P.loop && !P.hold && P.arr_pos >= 0 ? (uint32_t)P.arr_pos : NO_STEP;

  /* 5. the metronome. at: where the song is, in sixteenths, whatever the pattern's grid */
  int at = P.running && P.step >= 0 ? P.step / (P.div ? P.div : 1) : -1;
  if (at != click_step) {
    /* a new sixteenth: on a beat, the click starts */
    click_step = at;
    if (at >= 0 && at % beat() == 0 && metro && rec_on) {
      click_env = at == 0 ? 0.30f : 0.20f;
      click_f = (at == 0 ? 2200.0f : 1500.0f) / SR;
      click_ph = 0.0f;
    }
  }

  /* 6. the output, a frame at a time. vol_glide: the volume follows its knob in 10 ms. click_fade: the click dies
     away in 12 ms */
  float vol = param_in(P_VOLUME);
  float vol_glide = vd_coef(0.01f, SR);
  float click_fade = vd_coef(0.012f, SR);
  for (int n = 0; n < frames; n++) {
    vol_s = vol + (vol_s - vol) * vol_glide;
    float l = vd_tanh(mixl[n] * vol_s * 1.25f);
    float r = vd_tanh(mixr[n] * vol_s * 1.25f);
    float o = (l + r) * 0.5f;
    if (click_env > 0.0005f) {
      float click = vd_sin(click_ph) * click_env;
      click_ph += click_f;
      if (click_ph >= 1.0f) {
        click_ph -= 1.0f;
      }
      click_env *= click_fade;
      l += click;
      r += click;
    }
    /* never over full scale: the click on top of a full mix, and the limit itself, which rounds a hair over 1 for
       some inputs */
    out_l[n] = vd_clamp(l, -1.0f, 1.0f);
    out_r[n] = vd_clamp(r, -1.0f, 1.0f);

    /* the scope: the lowest and the highest of every 48 frames */
    if (o < sc_lo) {
      sc_lo = o;
    }
    if (o > sc_hi) {
      sc_hi = o;
    }
    if (++sc_n >= 48) {
      uint32_t s = scope_w;
      scope_lo[s % SCOPE] = sc_lo;
      scope_hi[s % SCOPE] = sc_hi;
      scope_w = s + 1;
      sc_hi = o;
      sc_lo = o;
      sc_n = 0;
    }
  }
}
