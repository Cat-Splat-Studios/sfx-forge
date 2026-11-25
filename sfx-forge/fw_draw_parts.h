/* fw_draw_parts.h – what every page of the display is drawn from. A part of sfx-forge.c.
 *
 * Holds:  the colours; a value as text (param_name, param_text, time_text, note_text, percent); layer_on;
 *         the pieces: arc, dial (an encoder's ring), hud (a value in a corner of a picture), row (a value in the
 *         column beside the tiles), footer, tile_at (where a pad's tile is), scope.
 * Needs:  fw_state.h, the SDK's drawing (vibe_gfx.h: vg_...), core/sfx_core.h (the values of a sound).
 * Read:   the display is 480 x 320 pixels; every vg_... call draws into it at once (on_draw, fw_draw.h, starts each
 *         frame with vg_clear). The numbers in the drawing code are pixels. The Unity package's inspectors copy this
 *         look (unity/com.catsplat.sfxforge/Editor/UI): the colours and the texts of param_name / param_text are
 *         repeated there.
 */

/* ---- the colours (0xRRGGBB) ------------------------------------------------------------------------------------ */
#define BG    0x180F0B /* the background */
#define PANEL 0x2B1C15 /* a panel on it */
#define LINE  0x4A3026 /* a line, an empty bar */
#define DIM   0x907A6F /* text that matters less */
#define TEXT  0xF4ECE8 /* text */
/* five that say something wherever they stand (the other colours written as numbers are shades for one state of one
   thing, beside the line that uses them) */
#define BLUE   0x36C5FF /* the pattern that is shown; LOOP; the song's own things */
#define YELLOW 0xFFD23F /* what waits for the bar line; what the game sends */
#define GREEN  0x6EE7A8 /* it runs: PLAY, the link */
#define ORANGE 0xFF8A3C /* look here: something is not as you may think; a knob that has a lane */
#define RED    0xFF4040 /* REC and ERASE */

/* ---- a value as text ------------------------------------------------------------------------------------------- */

/* what value k of a sound (SFX_P_...) is called. The engine's own two knobs, and FOLEY's cutoff, have the engine's
   name for them */
static const char *param_name(int k, int engine) {
  // clang-format off
  static const char *const N[SFX_P_COUNT] = {
    "FREQ",   "PITCH ENV", "ENV TIME", "FM RATIO",   "FM DEPTH",   "NOISE", "CUTOFF", "RESO", "DECAY", "CRUSH",
    "ATTACK", "SUSTAIN",   "RELEASE",  "FILTER ENV", "F.ENV TIME",
    "LFO RATE", "> PITCH", "> FILTER", "> TIMBRE",
    "SPREAD", "CHORUS",    "DELAY",    "REVERB",     "LEVEL",      "TABLE" };
  // clang-format on
  if (engine == SFX_ENGINE_UI_BLIP && k == SFX_P_FM_RATIO) {
    return "ARP STEP";
  }
  if (engine == SFX_ENGINE_UI_BLIP && k == SFX_P_FM_DEPTH) {
    return "ARP SPEED";
  }
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FM_RATIO) {
    return "GRAIN RATE";
  }
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FM_DEPTH) {
    return "GRAIN AMT";
  }
  if (engine == SFX_ENGINE_FOLEY_NOISE && k == SFX_P_FILTER_CUTOFF) {
    return "BAND";
  }
  if (engine == SFX_ENGINE_WAVETABLE && k == SFX_P_FM_RATIO) {
    return "POSITION";
  }
  if (engine == SFX_ENGINE_WAVETABLE && k == SFX_P_FM_DEPTH) {
    return "WARP";
  }
  return N[k];
}

/* the text in buf, with a unit behind it */
static const char *with(char *buf, const char *unit) {
  strcpy(buf + strlen(buf), unit);
  return buf;
}

/* seconds as "120ms", "1.4s" */
static const char *time_text(float s, char *buf) {
  if (s < 0.9995f) {
    return with(vg_itoa((int)(s * 1000.0f + 0.5f), buf), "ms");
  }
  return with(vg_ftoa(s, 1, buf), "s");
}

/* value k of a sound as the display writes it: in Hz, ms, %, a table's name ... The curves are the engine's
   (core/sfx_core.h computes the same); the Unity package shows the same texts (SfxUnits.cs, and a test compares). */
static const char *param_text(int k, const sfx_patch *p, char *buf) {
  /* v: the value, 0..127. u: the same, 0..1 */
  int v = p->p[k];
  float u = (float)v / 127.0f;
  switch (k) {
  case SFX_P_TABLE:
    return SFX_WT_NAME[v % SFX_WT_TABLES];
  case SFX_P_SUSTAIN:
    if (v == 0) {
      return "OFF";
    }
    break;
  case SFX_P_BITCRUSH:
    if (v == 0) {
      return "OFF";
    }
    break;
  case SFX_P_PITCH_ENV_AMT:
  case SFX_P_FILTER_ENV:
    if (v > 64) {
      buf[0] = '+';
      vg_itoa(v - 64, buf + 1);
      return buf;
    }
    return vg_itoa(v - 64, buf);
  case SFX_P_BASE_FREQ:
  case SFX_P_FILTER_CUTOFF: {
    float hz = k == SFX_P_BASE_FREQ ? 20.0f * vd_exp2(u * 9.5f) : 40.0f * vd_exp2(u * 8.6f);
    if (hz >= 1000.0f) {
      return with(vg_ftoa(hz / 1000.0f, 1, buf), "kHz");
    }
    return with(vg_itoa((int)hz, buf), "Hz");
  }
  case SFX_P_ATTACK:
    return time_text(0.0015f * vd_exp2(u * 11.4f), buf);
  case SFX_P_AMP_DECAY:
    return time_text(0.010f * vd_exp2(u * 8.6f), buf);
  case SFX_P_RELEASE:
    return time_text(0.005f * vd_exp2(u * 9.6f), buf);
  case SFX_P_PITCH_ENV_DEC:
  case SFX_P_FILTER_ENV_DEC:
    return time_text(0.005f * vd_exp2(u * 9.0f), buf);
  case SFX_P_LFO_RATE: {
    float hz = 0.1f * vd_exp2(u * 8.2f);
    return with(hz < 10.0f ? vg_ftoa(hz, 1, buf) : vg_itoa((int)hz, buf), "Hz");
  }
  case SFX_P_LEVEL:
  case SFX_P_FM_RATIO:
  case SFX_P_FM_DEPTH:
    return vg_itoa(v, buf);
  default:
    break;
  }
  /* everything else: per cent */
  return with(vg_itoa((v * 100 + 63) / 127, buf), "%");
}

/* a note as "A#3", or "--": the sound as designed */
static const char *note_text(int note, char *buf) {
  static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (note <= 0) {
    return "--";
  }
  strcpy(buf, N[note % 12]);
  vg_itoa(note / 12 - 1, buf + strlen(buf));
  return buf;
}

/* 0..127 as "0%".."100%" */
static const char *percent(int v127, char *buf) {
  return with(vg_itoa((v127 * 100 + 63) / 127, buf), "%");
}

/* the intensity is at or above this sound's layer: it plays in the song now */
static int layer_on(int slot) {
  return (int)(param_in(P_INTENSITY) * 127.0f + 0.5f) >= SONG.level[slot];
}

/* ---- the pieces a page is drawn from --------------------------------------------------------------------------- */

/* the six marks of K1-K6, in the line under the top line: where the first begins */
#define LOC_X 364

/* a piece of a circle: from a0 to a1 in turns, 0 = the top, clockwise. Drawn as n short lines.
   (vd_sin takes turns, not radians: sin(a + 1) is the sine, sin(a + 1.25) the cosine) */
static void arc(float cx, float cy, float r, float a0, float a1, float w, uint32_t rgb) {
  int n = (int)((a1 - a0) * 28.0f) + 1;
  float px = cx + r * vd_sin(a0 + 1.0f);
  float py = cy - r * vd_sin(a0 + 1.25f);
  for (int k = 1; k <= n; k++) {
    float a = a0 + (a1 - a0) * (float)k / (float)n;
    float x = cx + r * vd_sin(a + 1.0f);
    float y = cy - r * vd_sin(a + 1.25f);
    vg_line(px, py, x, y, w, rgb);
    px = x;
    py = y;
  }
}

/* A page is one of three kinds (the third, PERFORM's grid of a row a sound, is drawn in fw_draw_pages.h); the two
   that are drawn from the pieces here:
     a PICTURE of what the knobs shape, with their four values in its corners (the sound's pages, MASTER) – the knob
     you turn is written large across the middle for a moment;
     the 16 PADS as tiles, laid out as the pads are, with the four values in a column beside them (SEQ, SONG, AUTO,
     MIX).
   The display is 480 x 320: the top line, the line that says where you are, the page's own room, F1-F6. */
#define CY 58  /* a page's own room: from here … */
#define CH 228 /* … this high, down to the soft keys */

/* where each encoder's value is drawn: x, y, width, height (for FOCUS and the mouse; width 0: it has none) */
static int16_t enc_rect[4][4];

/* encoder i was turned within the last 1.3 s */
static int touched(int i) {
  return touch_i == i && vibe_frames() - touch_t < (uint64_t)(1.3f * SR);
}

/* a ring as the knob stands, its number inside. bar: 0..1 (below 0: no value). centre: the value grows from the
   middle (a pan, an envelope amount) */
static void dial(float cx, float cy, float r, int i, float bar, int centre, uint32_t rgb) {
  static const char *const NUM[4] = {"1", "2", "3", "4"};
  float to = -0.375f + 0.75f * vd_clamp(bar, 0.0f, 1.0f);
  arc(cx, cy, r, -0.375f, 0.375f, 3.0f, LINE);
  if (bar >= 0.0f) {
    if (!centre) {
      arc(cx, cy, r, -0.375f, to, 3.0f, rgb);
    } else if (to > 0.0f) {
      arc(cx, cy, r, 0.0f, to, 3.0f, rgb);
    } else {
      arc(cx, cy, r, to, 0.0f, 3.0f, rgb);
    }
    vg_circle(cx + r * vd_sin(to + 1.0f), cy - r * vd_sin(to + 1.25f), 3.5f, TEXT);
  }
  vg_text(NUM[i], (int)cx, (int)cy - 7, VG_SMALL, DIM, VG_CENTER);
}

/* a value in a corner of the picture: 0 top left, 1 top right, 2 bottom left, 3 bottom right. i: its encoder */
static void hud(int corner, int i, const char *name, const char *value, float bar, int centre, uint32_t rgb) {
  int right = corner & 1;
  int w = 150;
  int x = right ? 462 - w : 18;
  int y = (corner >> 1) ? CY + CH - 52 : CY + 6;
  float cx = (float)(right ? x + w - 20 : x + 20);
  /* (the host shows the same name and value over the encoder; an empty name: this encoder has nothing here) */
  vibe_link_encoder(VE_1 + i, name, value);
  if (!name[0]) {
    return;
  }
  enc_rect[i][0] = (int16_t)x;
  enc_rect[i][1] = (int16_t)y;
  enc_rect[i][2] = (int16_t)w;
  enc_rect[i][3] = 46;
  vg_rect_a(x, y, w, 46, BG, 0.66f);
  dial(cx, (float)y + 23.0f, 15.0f, i, bar, centre, rgb);
  vg_text(name, right ? x + w - 42 : x + 42, y + 4, VG_SMALL, touched(i) ? rgb : DIM, right ? VG_RIGHT : VG_LEFT);
  vg_text(value, right ? x + w - 42 : x + 42, y + 20, VG_BIG, TEXT, right ? VG_RIGHT : VG_LEFT);
  /* the knob in the hand: large, in the middle */
  if (touched(i)) {
    vg_rect_a(120, CY + CH / 2 - 40, 240, 80, BG, 0.82f);
    vg_text(name, 240, CY + CH / 2 - 34, VG_BOLD, rgb, VG_CENTER);
    vg_text(value, 240, CY + CH / 2 - 14, VG_BIG | VG_X2, TEXT, VG_CENTER);
  }
}

/* a value in the column beside the tiles */
static void row(int i, const char *name, const char *value, float bar, int centre, uint32_t rgb) {
  int x = 256;
  int y = CY + i * 47;
  vibe_link_encoder(VE_1 + i, name, value);
  if (!name[0]) {
    return;
  }
  enc_rect[i][0] = (int16_t)x;
  enc_rect[i][1] = (int16_t)y;
  enc_rect[i][2] = 212;
  enc_rect[i][3] = 43;
  vg_round(x, y, 212, 43, 8, touched(i) ? vg_mix(PANEL, rgb, 0.22f) : PANEL);
  dial((float)x + 24.0f, (float)y + 22.0f, 14.0f, i, bar, centre, rgb);
  vg_text(name, x + 48, y + 3, VG_SMALL, touched(i) ? TEXT : DIM, VG_LEFT);
  vg_text(value, x + 48, y + 18, VG_BIG, TEXT, VG_LEFT);
}

/* two lines under the column: what else there is to know here */
static void footer(const char *a, uint32_t rgb, const char *b) {
  vg_text(a, 262, CY + 191, VG_BOLD, rgb, VG_LEFT);
  vg_text(b, 262, CY + 209, VG_SMALL, DIM, VG_LEFT);
}

/* the tile of pad s: 55 x 54 */
static void tile_at(int s, int *x, int *y) {
  *x = 12 + (s % 4) * 59;
  *y = CY + (s / 4) * 58;
}

/* what sounds now: the audio's last SCOPE columns (fw_audio.h), the oldest on the left */
static void scope(int x, int y, int w, int h, uint32_t rgb) {
  vg_round(x, y, w, h, 6, PANEL);
  vg_rect(x + 2, y + h / 2, w - 4, 1, LINE);
  uint32_t at = scope_w;
  float half = (float)(h / 2 - 3);
  float dx = (float)(w - 4) / (float)SCOPE;
  for (int i = 0; i < SCOPE; i++) {
    float lo = scope_lo[(at + (uint32_t)i) % SCOPE];
    float hi = scope_hi[(at + (uint32_t)i) % SCOPE];
    int y0 = y + h / 2 - (int)(hi * half);
    int y1 = y + h / 2 - (int)(lo * half);
    if (y1 > y0) {
      vg_rect(x + 2 + (int)((float)i * dx), y0, (int)dx + 1, y1 - y0 + 1, rgb);
    }
  }
}
