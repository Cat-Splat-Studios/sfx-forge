/* fw_draw_sound.h – the picture of the selected sound. A part of sfx-forge.c.
 *
 * Holds:  preview_render (the sound, rendered into a buffer whenever it has changed); path (a line through points);
 *         preview (the stage's picture: the whole sound left, what the page's knobs shape right);
 *         scene (a sound page of the display: the whole sound, and over it what this page's knobs shape).
 * Needs:  fw_draw_parts.h (colours, with, CY / CH), core/sfx_core.h (a voice: sfx_voice_trigger / render / release).
 * Read:   the picture is the real thing: preview_render plays the patch through a voice of its own (in the control
 *         flow, at 24 kHz, at most 1.5 s) and keeps the samples. preview and scene draw from that buffer - and draw
 *         the envelope, the filter and the LFO as simple shapes from the patch's values. The two are alike on
 *         purpose: the same four panes at two sizes.
 */

/* ---- the selected sound itself: rendered here whenever it changes, so every knob shows what it does ------------ */
#define PV_SR   24000.0f /* enough for a picture, and half the work */
#define PV_MAX  35968    /* 1.5 s, in blocks of 64 */
#define PV_COLS 256      /* the columns its whole shape is kept in */

/* the samples; the lowest and the highest sample of each column; the loudest sample of all */
static float pv_buf[PV_MAX];
static float pv_lo[PV_COLS];
static float pv_hi[PV_COLS];
static float pv_peak;
/* the patch that was rendered */
static sfx_patch pv_patch;
/* how many samples of pv_buf the picture shows (0: nothing was rendered yet) */
static int pv_n;

/* The sound of patch p into pv_buf - unless it is the patch that is there already. In this order: the voice plays it
   (a sustained sound is held 0.8 s, then let go); the loudest sample is found; the picture's end is where the sound
   has faded; the columns are filled. */
static void preview_render(const sfx_patch *p) {
  static sfx_voice v;
  sfx_mods none = {0, 0, 0, 1};

  /* the same patch as before: nothing to do */
  int same = pv_n && pv_patch.engine == p->engine;
  for (int i = 0; same && i < SFX_P_COUNT; i++) {
    same = pv_patch.p[i] == p->p[i];
  }
  if (same) {
    return;
  }
  pv_patch = *p;

  /* the voice plays it. n: the samples made. last: the last one that can be heard */
  int hold = sfx_patch_sustained(p);
  int n = 0;
  int last = 0;
  for (int i = 0; i < PV_MAX; i++) {
    pv_buf[i] = 0.0f;
  }
  /* (always the same noise: a steady picture) */
  sfx_voice_trigger(&v, p, 1.0f, 0.0f, 0, PV_SR, 12345u);
  while (n < PV_MAX && v.active) {
    /* a sustained sound: held 0.8 s, then let go */
    if (hold && n >= (int)(0.8f * PV_SR)) {
      sfx_voice_release(&v);
    }
    sfx_voice_render(&v, &none, pv_buf + n, 64);
    n += 64;
  }

  /* its loudest sample */
  pv_peak = 0.001f;
  for (int i = 0; i < n; i++) {
    float a = vd_absf(pv_buf[i]);
    if (a > pv_peak) {
      pv_peak = a;
    }
  }
  /* the picture ends where the sound has faded (10 ms after; at least 50 ms long) */
  for (int i = 0; i < n; i++) {
    if (vd_absf(pv_buf[i]) > pv_peak * 0.03f) {
      last = i;
    }
  }
  pv_n = last + 240 < 1200 ? 1200 : last + 240 > n ? n : last + 240;

  /* the columns: the lowest and the highest sample of each */
  for (int c = 0; c < PV_COLS; c++) {
    int a = c * pv_n / PV_COLS;
    int b = (c + 1) * pv_n / PV_COLS;
    float lo = 0.0f;
    float hi = 0.0f;
    for (int i = a; i < b; i++) {
      if (pv_buf[i] < lo) {
        lo = pv_buf[i];
      }
      if (pv_buf[i] > hi) {
        hi = pv_buf[i];
      }
    }
    pv_lo[c] = lo;
    pv_hi[c] = hi;
  }
}

/* a line through n points: xy holds x0, y0, x1, y1, ... */
static void path(const float *xy, int n, uint32_t rgb) {
  for (int i = 0; i + 1 < n; i++) {
    vg_line(xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], 2.0f, rgb);
  }
}

/* ---- the stage's picture --------------------------------------------------------------------------------------- */
/* left: the whole sound, start to end. Right: what the page's knobs shape – the wave (10 ms of it, where it is
   loudest), the envelope, the filter or the LFO, drawn from their values. pane: the page (PG_...) */
static void preview(int x, int y, int w, int h, const sfx_patch *p, uint32_t rgb, int pane) {
  char buf[16];
  preview_render(p);
  /* sw: the left part's width. ww: the right part's. mid: the zero line. from: where the 10 ms of wave begin */
  int sw = w * 3 / 5;
  int ww = w - sw - 8;
  int mid = y + h / 2;
  int from = 48;
  float gain = (float)(h / 2 - 4) / pv_peak;
  /* (a slow attack: not its silent start) */
  for (int i = 48; i < pv_n - 240; i += 16) {
    if (vd_absf(pv_buf[i]) > pv_peak * 0.7f) {
      from = i;
      break;
    }
  }

  /* left: the whole sound, a column a pixel - pale to its full height, the sound's colour to six tenths of it */
  vg_round(x, y, w, h, 6, PANEL);
  vg_rect(x + 2, mid, sw - 4, 1, LINE);
  vg_rect(x + sw, y + 6, 1, h - 12, LINE);
  for (int i = 0; i < sw - 6; i++) {
    int c = i * PV_COLS / (sw - 6);
    int y0 = mid - (int)(pv_hi[c] * gain);
    int y1 = mid - (int)(pv_lo[c] * gain);
    vg_rect(x + 3 + i, y0, 1, y1 - y0 + 1, vg_mix(PANEL, rgb, 0.42f));
    vg_rect(x + 3 + i, mid - (mid - y0) * 6 / 10, 1, (y1 - y0) * 6 / 10 + 1, rgb);
  }
  /* how long it is (where a sound has faded) */
  vg_text(with(vg_ftoa((float)pv_n / PV_SR, 2, buf), "s"), x + sw - 6, y + h - 18, VG_SMALL, DIM, VG_RIGHT);

  /* right: x0, pw: its left edge and width. top, bot: the room of the shape */
  float x0 = (float)(x + sw + 6);
  float pw = (float)(ww - 4);
  float top = (float)(y + 10);
  float bot = (float)(y + h - 24);
  const char *label = "WAVE  10 ms";
  if (pane == PG_ENV) {
    /* the envelope: up (attack), down to the sustain level (decay), held, down (release) */
    float a = (float)p->p[SFX_P_ATTACK] / 127.0f;
    float d = (float)p->p[SFX_P_AMP_DECAY] / 127.0f;
    float s = (float)p->p[SFX_P_SUSTAIN] / 127.0f;
    float r = (float)p->p[SFX_P_RELEASE] / 127.0f;
    int hold = p->p[SFX_P_SUSTAIN] > 0;
    /* seg: the width of the longest attack, decay or release. xa, xd, xs, xr: where each part ends. ys: the level */
    float seg = pw * 0.24f;
    float xa = x0 + 3.0f + a * seg;
    float xd = xa + 5.0f + d * seg;
    float xs = hold ? xd + pw * 0.16f : xd;
    float xr = hold ? xs + 5.0f + r * seg : xd;
    float ys = hold ? bot - (bot - top) * s : bot;
    float xy[10] = {x0, bot, xa, top, xd, ys, xs, ys, xr, bot};
    path(xy, 5, 0xFFFFFF);
    /* (the pad is let go here) */
    if (hold) {
      vg_rect((int)xs, (int)top, 1, (int)(bot - top), LINE);
    }
    label = hold ? "ENVELOPE  held" : "ENVELOPE  one-shot";
  } else if (pane == PG_FILTER) {
    /* the filter: flat, a peak at the cutoff (as high as the resonance), then down - FOLEY: a band, up and down */
    float cx = x0 + 12.0f + (float)p->p[SFX_P_FILTER_CUTOFF] / 127.0f * (pw - 36.0f);
    float base = top + (bot - top) * 0.45f;
    float peak = base - (float)p->p[SFX_P_FILTER_RES] / 127.0f * (bot - top) * 0.42f;
    if (p->engine == SFX_ENGINE_FOLEY_NOISE) {
      float xy[6] = {cx - 12.0f, bot, cx, peak, cx + 22.0f, bot};
      path(xy, 3, 0xFFFFFF);
      label = "BAND";
    } else {
      float xy[8] = {x0, base, cx - 10.0f > x0 ? cx - 10.0f : x0, base, cx, peak, cx + 22.0f, bot};
      path(xy, 4, 0xFFFFFF);
      label = "FILTER";
    }
  } else if (pane == PG_LFO) {
    /* the LFO: a sine - more cycles for a faster one, as high as the deepest of its three amounts */
    int depth = p->p[SFX_P_LFO_PITCH] > p->p[SFX_P_LFO_FILTER] ? p->p[SFX_P_LFO_PITCH] : p->p[SFX_P_LFO_FILTER];
    if (p->p[SFX_P_LFO_TIMBRE] > depth) {
      depth = p->p[SFX_P_LFO_TIMBRE];
    }
    float cyc = 1.0f + (float)p->p[SFX_P_LFO_RATE] / 127.0f * 5.0f;
    float amp = (bot - top) * 0.5f * (float)depth / 127.0f;
    float cy = (top + bot) * 0.5f;
    float py = cy;
    vg_rect((int)x0, (int)cy, (int)pw, 1, LINE);
    for (int i = 1; i < (int)pw; i++) {
      float ny = cy - vd_sin((float)i / pw * cyc) * amp;
      vg_line(x0 + (float)i - 1.0f, py, x0 + (float)i, ny, 2.0f, depth ? 0xFFFFFF : DIM);
      py = ny;
    }
    label = depth ? "LFO" : "LFO  off";
  } else {
    /* every other page: the wave itself, 240 samples (10 ms) from `from`, a column a pixel, joined to the one before */
    int prev = mid - (int)(pv_buf[from] * gain);
    vg_rect(x + sw + 2, mid, ww + 4, 1, LINE);
    for (int i = 0; i < ww; i++) {
      int now = mid - (int)(pv_buf[from + i * 240 / ww] * gain);
      int a = now < prev ? now : prev;
      int b = now < prev ? prev : now;
      vg_rect(x + sw + 4 + i, a, 1, b - a + 1, 0xFFFFFF);
      prev = now;
    }
  }
  /* what the right part shows, written in its corner */
  vg_rect(x + w - 10 - vg_text_width(label, VG_SMALL), y + h - 18, vg_text_width(label, VG_SMALL) + 8, 15, PANEL);
  vg_text(label, x + w - 6, y + h - 18, VG_SMALL, DIM, VG_RIGHT);
}

/* ---- the display's picture ------------------------------------------------------------------------------------- */
/* a sound page's picture: the sound's whole shape – and over it, in white, what this page's knobs shape.
   (The same shapes as in preview, at the display's size: a change to one of them belongs in both.) */
static void scene(const sfx_patch *p, uint32_t rgb, int pg) {
  char buf[16];
  preview_render(p);
  /* own: the page's knobs shape the whole sound, so it is drawn in full colour. from: where the 10 ms of wave begin */
  int x = 12;
  int y = CY;
  int w = 456;
  int h = CH;
  int mid = y + h / 2;
  int own = pg == PG_OSC || pg == PG_FX;
  int from = 48;
  /* x0, pw: the left edge and the width of the white shape. top, bot: its room */
  float gain = 58.0f / pv_peak;
  float x0 = (float)x + 44.0f;
  float pw = (float)w - 88.0f;
  float top = (float)mid - 58.0f;
  float bot = (float)mid + 58.0f;
  const char *what = "the whole sound";

  /* the whole sound, a column a pixel */
  vg_round(x, y, w, h, 8, PANEL);
  vg_rect(x + 4, mid, w - 8, 1, LINE);
  for (int i = 0; i < w - 8; i++) {
    int c = i * PV_COLS / (w - 8);
    int y0 = mid - (int)(pv_hi[c] * gain);
    int y1 = mid - (int)(pv_lo[c] * gain);
    vg_rect(x + 4 + i, y0, 1, y1 - y0 + 1, vg_mix(PANEL, rgb, own ? 0.42f : 0.18f));
    if (own) {
      vg_rect(x + 4 + i, mid - (mid - y0) * 6 / 10, 1, (y1 - y0) * 6 / 10 + 1, rgb);
    }
  }

  if (pg == PG_ENV) {
    /* the envelope */
    float a = (float)p->p[SFX_P_ATTACK] / 127.0f;
    float d = (float)p->p[SFX_P_AMP_DECAY] / 127.0f;
    float su = (float)p->p[SFX_P_SUSTAIN] / 127.0f;
    float r = (float)p->p[SFX_P_RELEASE] / 127.0f;
    int hold = p->p[SFX_P_SUSTAIN] > 0;
    float seg = pw * 0.24f;
    float xa = x0 + 6.0f + a * seg;
    float xd = xa + 10.0f + d * seg;
    float xs = hold ? xd + pw * 0.16f : xd;
    float xr = hold ? xs + 10.0f + r * seg : xd;
    float ys = hold ? bot - (bot - top) * su : bot;
    float xy[10] = {x0, bot, xa, top, xd, ys, xs, ys, xr, bot};
    path(xy, 5, 0xFFFFFF);
    /* (the pad is let go here) */
    if (hold) {
      vg_rect((int)xs, (int)top, 1, (int)(bot - top), LINE);
    }
    what = hold ? "held, then released" : "a one-shot";
  } else if (pg == PG_FILTER) {
    /* the filter, or FOLEY's band */
    float cx = x0 + 20.0f + (float)p->p[SFX_P_FILTER_CUTOFF] / 127.0f * (pw - 70.0f);
    float base = top + (bot - top) * 0.45f;
    float peak = base - (float)p->p[SFX_P_FILTER_RES] / 127.0f * (bot - top) * 0.42f;
    if (p->engine == SFX_ENGINE_FOLEY_NOISE) {
      float xy[6] = {cx - 26.0f, bot, cx, peak, cx + 44.0f, bot};
      path(xy, 3, 0xFFFFFF);
      what = "the noise's band";
    } else {
      float xy[8] = {x0, base, cx - 20.0f > x0 ? cx - 20.0f : x0, base, cx, peak, cx + 44.0f, bot};
      path(xy, 4, 0xFFFFFF);
      what = "the filter";
    }
  } else if (pg == PG_LFO) {
    /* the LFO */
    int depth = p->p[SFX_P_LFO_PITCH] > p->p[SFX_P_LFO_FILTER] ? p->p[SFX_P_LFO_PITCH] : p->p[SFX_P_LFO_FILTER];
    if (p->p[SFX_P_LFO_TIMBRE] > depth) {
      depth = p->p[SFX_P_LFO_TIMBRE];
    }
    float cyc = 1.0f + (float)p->p[SFX_P_LFO_RATE] / 127.0f * 7.0f;
    float amp = (bot - top) * 0.5f * (float)depth / 127.0f;
    float py = (float)mid;
    for (int i = 1; i < (int)pw; i++) {
      float ny = (float)mid - vd_sin((float)i / pw * cyc) * amp;
      vg_line(x0 + (float)i - 1.0f, py, x0 + (float)i, ny, 2.0f, depth ? 0xFFFFFF : DIM);
      py = ny;
    }
    what = depth ? "the slow wave" : "LFO: off";
  } else if (pg == PG_TIMBRE || pg == PG_WAVE) {
    /* the wave itself: 10 ms, where the sound is loudest */
    for (int i = 48; i < pv_n - 240; i += 16) {
      if (vd_absf(pv_buf[i]) > pv_peak * 0.7f) {
        from = i;
        break;
      }
    }
    int prev = mid - (int)(pv_buf[from] * gain);
    for (int i = 0; i < (int)pw; i++) {
      int now = mid - (int)(pv_buf[from + i * 240 / (int)pw] * gain);
      int a = now < prev ? now : prev;
      int b = now < prev ? prev : now;
      vg_rect((int)x0 + i, a, 2, b - a + 1, 0xFFFFFF);
      prev = now;
    }
    what = "its wave, 10 ms";
  }

  /* what is shown, at the top; how long the sound is, at the bottom */
  vg_text(what, 240, y + 12, VG_SMALL, DIM, VG_CENTER);
  vg_text(with(vg_ftoa((float)pv_n / PV_SR, 2, buf), "s long"), 240, y + h - 26, VG_SMALL, DIM, VG_CENTER);
}
