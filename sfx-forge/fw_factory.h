/* fw_factory.h – what a new device holds: its 32 sounds and its song. A part of sfx-forge.c.
 *
 * Holds:  preset_load (a named sound onto a pad), step_set (a step of a pattern, on, with its note),
 *         demo_song (the song a device starts with, written out as tables), factory (both kits and that song).
 * Needs:  fw_state.h (SONG, kits), fw_sounds.h (PRESETS), core/sfx_song.h (sfx_song_init, the song's fields).
 * Read:   the bytes demo_song makes are checked against the Unity demo's song asset (tests/check-song-export.mjs):
 *         a note changed here changes that test's expected file too.
 *         step_set is here only because demo_song needs it first; the sequencer uses it for every step it sets
 *         (recording, the NOTE encoder, a key played while a step's pad is held).
 */

/* named sound k (a row of PRESETS) onto a pad of the chosen kit */
static void preset_load(int slot, int k) {
  patches[slot].version = SFX_PATCH_VERSION;
  patches[slot].engine = PRESETS[k].engine;
  for (int i = 0; i < SFX_P_COUNT; i++) {
    patches[slot].p[i] = PRESETS[k].p[i];
  }
  pad_preset[slot] = (int8_t)k;
}

/* a step of pattern p is on for a sound, with its note (0: the sound as designed) */
static void step_set(sfx_pattern *p, int step, int slot, int note) {
  p->on[step] |= (uint16_t)(1u << slot);
  p->note[slot][step] = (uint8_t)note;
}

/* a song to start from, our own, played by the MUSIC kit: funk in E minor the way a 16-bit console's FM chip played it.
   Drums, an FM bass that jumps octaves on the off-beats, FM keys with the tune, brass stabs, bells, strings, a choir.
   Six patterns – two of the verse, two of the chorus, a breakdown, a turnaround – arranged over 16 bars. Only the kick
   plays at intensity 0; the game's intensity brings the band in.

   In this order: the tables (what each sound plays in each of the six patterns); the drums, bass and keys of every
   pattern, step by step; the strings; brass, bells and the choir; chain, swing and arrangement; two automation lanes;
   pans, faders and layers. */
static void demo_song(void) {
  /* the pads of the MUSIC kit that play (0-based), and N: the patterns */
  enum {
    SNARE = 1,
    KICK = 2,
    BRASS = 5,
    BASS = 6,
    KEYS = 7,
    HAT = 12,
    BELLS = 13,
    CHOIR = 14,
    STRINGS = 15,
    N = 6
  };

  // clang-format off
  /* the bass: its note on each sixteenth of each pattern (MIDI note numbers, 0 = no note) */
  static const uint8_t BASS_N[N][SFX_BAR] = {
    { 28,  0,  0, 28,  0, 40,  0, 28,   31,  0, 33,  0,  0, 35, 38,  0 },      /* Em */
    { 28,  0,  0, 28,  0, 40,  0, 28,   33,  0, 33, 45,  0, 31, 35,  0 },      /* Em, A */
    { 36,  0,  0, 36,  0, 48,  0, 36,   38,  0,  0, 38,  0, 50, 38,  0 },      /* C, D */
    { 36,  0,  0, 36,  0, 48,  0, 36,   38,  0, 38,  0, 40,  0, 40, 35 },      /* C, D, back to E */
    { 33,  0,  0,  0, 33,  0,  0, 45,   35,  0,  0,  0, 35,  0, 47,  0 },      /* Am, Bm: the breakdown */
    { 28,  0, 40, 28,  0, 40,  0, 31,   33,  0, 45, 33, 35,  0, 38, 40 } };    /* the turnaround */
  /* the keys: the tune, in the same way */
  static const uint8_t KEYS_N[N][SFX_BAR] = {
    {  0,  0, 64, 67,  0,  0, 69,  0,    0, 67,  0, 64,  0,  0, 62,  0 },
    { 71,  0,  0, 69,  0,  0, 67,  0,   64,  0, 67,  0, 69,  0, 71, 74 },
    { 67,  0,  0, 64,  0, 67,  0,  0,   69,  0,  0, 66,  0, 69,  0, 71 },
    { 72,  0, 71,  0, 67,  0, 64,  0,   66,  0, 69,  0, 71,  0, 74,  0 },
    {  0,  0,  0,  0, 64,  0,  0, 60,    0,  0,  0,  0, 66,  0, 62,  0 },
    { 64,  0, 67,  0, 69,  0, 71,  0,   74,  0, 71,  0, 69, 67, 64,  0 } };
  /* the strings: the two notes of each pattern, on steps 1 and 9 */
  static const uint8_t STRINGS_N[N][2] = { { 52, 50 }, { 55, 57 }, { 60, 62 }, { 60, 62 }, { 57, 59 }, { 52, 47 } };
  /* the drums: a bit a step (bit 0 = the first sixteenth), a word a pattern */
  static const uint16_t KICKS[N]  = { 0x1191, 0x1191, 0x1511, 0x1511, 0x0101, 0x5111 };
  static const uint16_t SNARES[N] = { 0x1010, 0x1410, 0x9010, 0x9090, 0x1000, 0xF010 };
  static const uint16_t HATS[N]   = { 0xD555, 0xD555, 0xFFFF, 0xFFFF, 0x4444, 0xF555 };
  /* the brass: pattern, step, note, steps it holds */
  static const uint8_t STABS[][4] = {
    { 1, 14, 71, 2 },
    { 2, 3, 67, 1 }, { 2, 6, 67, 2 }, { 2, 11, 69, 1 }, { 2, 14, 69, 2 },
    { 3, 0, 72, 3 }, { 3, 8, 74, 2 }, { 3, 12, 76, 3 },
    { 5, 0, 64, 1 }, { 5, 3, 64, 1 }, { 5, 6, 67, 2 }, { 5, 12, 67, 1 }, { 5, 13, 69, 1 }, { 5, 14, 71, 1 },
    { 5, 15, 74, 1 } };
  /* the bells: pattern, step, note */
  static const uint8_t CHIMES[][3] = {
    { 2, 2, 76 }, { 2, 10, 78 },
    { 3, 2, 74 }, { 3, 10, 78 },
    { 4, 0, 76 }, { 4, 6, 79 }, { 4, 8, 74 }, { 4, 14, 78 } };
  /* the arrangement: the pattern of each of its 16 bars */
  static const uint8_t BARS[16] = { 0, 1, 0, 1,  2, 3, 2, 3,  4, 4, 0, 1,  2, 3, 2, 5 };
  // clang-format on

  sfx_song_init(&SONG);

  /* drums, bass and keys: every pattern, every sixteenth; then the strings */
  for (int b = 0; b < N; b++) {
    sfx_pattern *p = &SONG.pat[b];
    for (int i = 0; i < SFX_BAR; i++) {
      /* the kick. The pushes between the beats: softer */
      if ((KICKS[b] >> i) & 1) {
        step_set(p, i, KICK, 0);
        p->vel[KICK][i] = (uint8_t)(i % 4 ? 3 : 5);
      }
      /* the snare: 2 and 4, ghosts, a fill that grows */
      if ((SNARES[b] >> i) & 1) {
        step_set(p, i, SNARE, 0);
        p->vel[SNARE][i] = (uint8_t)(i % 8 == 4 ? 6 : b == 5 && i > 12 ? i - 9 : 2);
      }
      /* the hat: the off-beat leads */
      if ((HATS[b] >> i) & 1) {
        step_set(p, i, HAT, 0);
        p->vel[HAT][i] = (uint8_t)(i & 1 ? 2 : i % 4 ? 5 : 3);
      }
      /* the bass: the octave pops */
      if (BASS_N[b][i]) {
        step_set(p, i, BASS, BASS_N[b][i]);
        p->vel[BASS][i] = (uint8_t)(BASS_N[b][i] >= 40 ? 7 : 5);
      }
      if (KEYS_N[b][i]) {
        step_set(p, i, KEYS, KEYS_N[b][i]);
        p->vel[KEYS][i] = (uint8_t)(i & 1 ? 4 : 6);
      }
    }
    /* strings: two long notes a bar, each let go before the next */
    step_set(p, 0, STRINGS, STRINGS_N[b][0]);
    step_set(p, 8, STRINGS, STRINGS_N[b][1]);
    p->vel[STRINGS][8] = 4;
    p->vel[STRINGS][0] = 4;
    p->len[STRINGS][8] = 7;
    p->len[STRINGS][0] = 7;
  }

  /* the brass and the bells, from their lists */
  for (unsigned k = 0; k < sizeof STABS / sizeof STABS[0]; k++) {
    step_set(&SONG.pat[STABS[k][0]], STABS[k][1], BRASS, STABS[k][2]);
    SONG.pat[STABS[k][0]].vel[BRASS][STABS[k][1]] = 6;
    SONG.pat[STABS[k][0]].len[BRASS][STABS[k][1]] = STABS[k][3];
  }
  for (unsigned k = 0; k < sizeof CHIMES / sizeof CHIMES[0]; k++) {
    step_set(&SONG.pat[CHIMES[k][0]], CHIMES[k][1], BELLS, CHIMES[k][2]);
    SONG.pat[CHIMES[k][0]].vel[BELLS][CHIMES[k][1]] = 4;
  }
  /* the breakdown: a choir holds through it */
  step_set(&SONG.pat[4], 0, CHOIR, 64);
  SONG.pat[4].vel[CHOIR][0] = 4;
  SONG.pat[4].len[CHOIR][0] = 15;

  /* the chain, the swing, the arrangement */
  SONG.chain = N;
  SONG.swing = 20;
  for (int i = 0; i < 16; i++) {
    SONG.arr[i] = BARS[i];
  }
  SONG.arr_len = 16;

  /* automation: the bass opens through the turnaround … */
  SONG.lane[0].track = BASS + 1;
  SONG.lane[0].to = SFX_AT_PATCH + SFX_P_FILTER_CUTOFF;
  /* … and the strings swell through the breakdown */
  SONG.lane[1].track = STRINGS + 1;
  SONG.lane[1].to = SFX_AT_VOL;
  for (int i = 0; i < SFX_BAR; i++) {
    SONG.pat[5].aut[0][i] = (uint8_t)(62 + i * 3);
    if (i % 2 == 0) {
      SONG.pat[4].aut[1][i] = (uint8_t)(19 + i * 3);
    }
  }

  /* the mix: pans (64 = the middle) */
  SONG.pan[HAT] = 84;
  SONG.pan[KEYS] = 76;
  SONG.pan[BRASS] = 48;
  SONG.pan[BELLS] = 36;
  SONG.pan[CHOIR] = 70;
  /* the faders. Measured: tests/mix-report.mjs */
  SONG.vol[KICK] = 68;
  SONG.vol[SNARE] = 100;
  SONG.vol[HAT] = 50;
  SONG.vol[BASS] = 52;
  SONG.vol[KEYS] = 24;
  SONG.vol[BRASS] = 54;
  SONG.vol[STRINGS] = 46;
  SONG.vol[BELLS] = 56;
  SONG.vol[CHOIR] = 52;
  /* the layers: the intensity (0..127) from which each sound plays. The kick's stays 0 */
  SONG.level[HAT] = 20;
  SONG.level[BASS] = 35;
  SONG.level[SNARE] = 50;
  SONG.level[STRINGS] = 60;
  SONG.level[CHOIR] = 60;
  SONG.level[KEYS] = 72;
  SONG.level[BRASS] = 90;
  SONG.level[BELLS] = 100;
}

/* both kits as they came (the first 16 named sounds on SFX, the next 16 on MUSIC), and the song to start from */
static void factory(void) {
  int was = kit;
  for (int s = 0; s < SLOTS; s++) {
    kit = KIT_SFX;
    preset_load(s, s);
    kit = KIT_MUSIC;
    preset_load(s, SFX_PRESETS + s);
  }
  kit = was;
  demo_song();
}
