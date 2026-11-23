// What this check proves: the PERFORM page (firmware 1.7.0), the fourth page of K5, on a small host of its own
// (tests/host.mjs). The page shows the whole pattern as a grid, and F1-F4 say what the 16 pads are:
//   - STEPS (F1, as it opens): a pad is a step of the selected sound, on and off. SHIFT + a pad selects that pad's
//     sound and sets no step. A pad held and a key played: the step gets the key's note.
//   - SOUNDS (F2): a pad plays and selects its sound, and sets no step - unless REC is lit and the song runs: then it
//     is recorded. REC pressed on this page makes the pads SOUNDS by itself.
//   - MUTES (F3): a pad mutes its sound, and again lets it be heard.
//   - PATTERN (F4): pads 1-8 choose the pattern, pads 9-16 the intensity.
//   - F5 FILL and F6 UNDO; with SHIFT: F1 what PLAY plays, F2 CLEAR, F3 ALL ON, F4 COPY, F5 and F6 the grid.
//   - encoders 1-4 are VOLUME, PAN, INTENSITY and LAYER; A the tempo, B the bar's length; LEFT and RIGHT the
//     pattern, with SHIFT the 16 steps the pads are.
//   - K5 once more is SEQ again, where F3 is FILL and mutes nothing.
// What it cannot see: the display itself (the manual's pictures of the page are taken from the firmware: docs/shots.mjs),
// the stage's REC button, and solo, which a song does not keep.
// Each case below is a block { ... } with a device of its own, started from the factory state.
//
//   node tests/check-perform.mjs build/sfx-forge.wasm
import { load, SECOND, BUTTON, EVENT, ENCODER } from './host.mjs';
import { grid, stepList, step, pattern, empty, same, tempo } from './song-read.mjs';

const [, , wasmFile = 'build/sfx-forge.wasm'] = process.argv;

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

const device = load(wasmFile);
// patterns A, G and H (G and H are empty in the factory song)
const A = 0;
const G = 6;
const H = 7;
// the sounds by their place in the kit, counted from 0: pad 1 is selected when the device starts, pad 3 is the kick
const FIRST = 0;
const KICK = 2;
// where the song's head keeps the layers, the faders, the pans and the mutes (core/sfx_song.h, "FORMAT 6")
const LAYER = 7;
const FADER = 23;
const PAN = 39;
const MUTE = 55;
// a key of the keyboard, as the SDK's hosts hand it over: event type 9
const KEY = 9;

// a list of steps counted from 0, as text counted from 1
const list = (a) => a.map((i) => i + 1).join(' ') || 'none';
// the mutes of a song as one number, a bit a sound
const mutes = (s) => s[MUTE] | (s[MUTE + 1] << 7) | (s[MUTE + 2] << 14);
// how loud a stretch of audio is
const rms = (x) => Math.sqrt(x.reduce((a, v) => a + v * v, 0) / Math.max(1, x.length));

// a device on PERFORM: K5 four times. onH: pattern H (LEFT from A), which is empty
const perform = () => {
  const d = device();
  d.run(SECOND);
  for (let i = 0; i < 4; i++) {
    d.button(BUTTON.K5);
  }
  return d;
};
const onH = () => {
  const d = perform();
  d.button(BUTTON.LEFT);
  return d;
};

// ---- STEPS: the pads are the steps of the selected sound
{
  const d = onH();
  for (const p of [1, 5, 9, 13]) {
    d.pad(p);
  }
  let s = d.song().bytes;
  ok(
    same(stepList(s, H, FIRST), [0, 4, 8, 12]) && empty(s, A) === false && empty(s, G),
    `STEPS, as the page opens: pads 1 5 9 13 are steps of the selected sound in H (${list(stepList(s, H, FIRST))})`,
  );
  d.pad(5);
  s = d.song().bytes;
  ok(same(stepList(s, H, FIRST), [0, 8, 12]), `the pad again: the step is off (${list(stepList(s, H, FIRST))})`);

  // SHIFT + pad 3: the kick is selected, and no step is set by it
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 1);
  d.run(2);
  d.pad(3);
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 0);
  d.run(2);
  const before = d.song().bytes;
  d.pad(2);
  s = d.song().bytes;
  ok(
    same(stepList(before, H, KICK), []) && same(stepList(s, H, KICK), [1]) && same(stepList(s, H, FIRST), [0, 8, 12]),
    `SHIFT + pad 3 selects the kick and sets no step; pad 2 is then the kick's step (${list(stepList(s, H, KICK))})`,
  );

  // pad 7 held, a key (note 60) played, the pad let go: step 7 of the kick has that note
  d.event(EVENT.PAD, 6, 1, 0.9);
  d.run(4);
  d.event(KEY, 60, 1, 0.8);
  d.run(4);
  d.event(KEY, 60, 0, 0);
  d.event(EVENT.PAD, 6, 0, 0);
  d.run(4);
  s = d.song().bytes;
  ok(
    step(s, H, KICK, 6).on === 1 && step(s, H, KICK, 6).note === 60,
    `a pad held and a key played: the step has the key's note (step 7: note ${step(s, H, KICK, 6).note})`,
  );
}

// ---- SOUNDS: the pads play the kit; with REC, while it runs, they are recorded
{
  const d = onH();
  d.button(BUTTON.F2);
  d.pad(4);
  let s = d.song().bytes;
  ok(empty(s, H), 'SOUNDS (F2): a pad sets no step');
  d.button(BUTTON.F1);
  d.pad(2);
  s = d.song().bytes;
  ok(
    same(stepList(s, H, 3), [1]),
    `and it selected its sound: back on STEPS (F1), pad 2 is a step of pad 4's sound (${list(stepList(s, H, 3))})`,
  );
  d.pad(2);

  // REC on PERFORM: the pads are the kit without F2. PLAY, the kick hit twice in the bar, STOP
  d.button(BUTTON.REC);
  d.button(BUTTON.PLAY);
  d.run(Math.round(0.3 * SECOND));
  d.pad(3);
  d.run(Math.round(0.6 * SECOND));
  d.pad(3);
  d.run(SECOND);
  d.button(BUTTON.STOP);
  d.button(BUTTON.REC);
  s = d.song().bytes;
  const took = stepList(s, H, KICK);
  ok(
    took.length === 2 && took[1] > took[0],
    `REC lit on PERFORM, PLAY: the pads are the kit and what is played is recorded (the kick on ${list(took)})`,
  );
  d.button(BUTTON.F6);
  s = d.song().bytes;
  ok(empty(s, H), 'F6 UNDO takes the take back');
}
{
  // REC lit before the page is opened (on SEQ), then K5 three times: the pads are the kit on PERFORM too
  const d = device();
  d.run(SECOND);
  d.button(BUTTON.K5);
  d.button(BUTTON.LEFT);
  d.button(BUTTON.REC);
  for (let i = 0; i < 3; i++) {
    d.button(BUTTON.K5);
  }
  d.pad(4);
  let s = d.song().bytes;
  const none = empty(s, H);
  // F1: steps again, REC still lit
  d.button(BUTTON.F1);
  d.pad(2);
  s = d.song().bytes;
  ok(
    none && same(stepList(s, H, 3), [1]),
    'PERFORM opened with REC lit: a pad sets no step (the pads are the kit); F1 makes them steps again',
  );
}

// ---- MUTES
{
  const d = perform();
  d.button(BUTTON.F3);
  d.pad(3);
  d.pad(5);
  let s = d.song().bytes;
  const fresh = device();
  fresh.run(SECOND);
  const factory = fresh.song().bytes;
  ok(
    mutes(s) === ((1 << 2) | (1 << 4)) && same(pattern(s, A), pattern(factory, A)),
    `MUTES (F3): pads 3 and 5 mute their sounds and set no step (mutes: ${mutes(s).toString(2)})`,
  );
  d.pad(3);
  s = d.song().bytes;
  ok(mutes(s) === 1 << 4, `the pad again: the sound is heard again (mutes: ${mutes(s).toString(2)})`);
  d.shift(BUTTON.F3);
  s = d.song().bytes;
  ok(mutes(s) === 0, 'SHIFT + F3 ALL ON: nothing is muted');
}

// ---- PATTERN: pads 1-8 the pattern, pads 9-16 the intensity
{
  const d = perform();
  d.button(BUTTON.F4);
  d.pad(7);
  d.button(BUTTON.F1);
  d.pad(1);
  const s = d.song().bytes;
  ok(
    same(stepList(s, G, FIRST), [0]) && empty(s, H),
    'PATTERN (F4): pad 7 chooses pattern G - the step set after it (F1, pad 1) is in G',
  );

  // the intensity: pattern A for two bars with pad 9 (12 %) and with pad 16 (100 %)
  const loud = (pad) => {
    const x = perform();
    x.button(BUTTON.F4);
    x.pad(pad);
    x.button(BUTTON.PLAY);
    x.listen();
    x.run(4 * SECOND);
    return rms(x.heard());
  };
  const low = loud(9);
  const high = loud(16);
  ok(
    low > 0.001 && high > low * 1.5,
    `pads 9-16 are the intensity: pattern A at pad 9 is thinner than at pad 16 (rms ${low.toFixed(3)} and ` +
      `${high.toFixed(3)})`,
  );
}

// ---- F5 FILL, F6 UNDO, and the keys with SHIFT
{
  const d = onH();
  d.button(BUTTON.F5);
  let s = d.song().bytes;
  ok(same(stepList(s, H, FIRST), [0, 4, 8, 12]), `F5 FILL: every quarter (${list(stepList(s, H, FIRST))})`);
  d.button(BUTTON.F6);
  s = d.song().bytes;
  const undone = empty(s, H);
  d.button(BUTTON.F6);
  s = d.song().bytes;
  ok(undone && same(stepList(s, H, FIRST), [0, 4, 8, 12]), 'F6 UNDO takes it back, and pressed again brings it back');
  d.shift(BUTTON.F2);
  s = d.song().bytes;
  ok(empty(s, H), 'SHIFT + F2 CLEAR: the selected sound has no steps');

  // the grid: SHIFT + F6 finer; SHIFT + RIGHT, and pad 1 is step 17; SHIFT + F5 coarser (it asks: twice)
  d.shift(BUTTON.F6);
  d.shift(BUTTON.RIGHT);
  d.pad(1);
  s = d.song().bytes;
  ok(
    grid(s, H) === 32 && same(stepList(s, H, FIRST), [16]),
    `SHIFT + F6: ${grid(s, H)} steps. SHIFT + RIGHT: the pads are steps 17-32 (pad 1 set step ` +
      `${list(stepList(s, H, FIRST))})`,
  );
  d.shift(BUTTON.F5);
  d.shift(BUTTON.F5);
  s = d.song().bytes;
  ok(grid(s, H) === 16, `SHIFT + F5: ${grid(s, H)} steps again`);
}
{
  // COPY: pattern A into G, the first empty one
  const d = perform();
  d.shift(BUTTON.F4);
  const s = d.song().bytes;
  ok(same(pattern(s, G), pattern(s, A)) && !empty(s, G) && empty(s, H), 'SHIFT + F4 COPY: G is A');
}
{
  // what PLAY plays. On H, which is empty: LOOP repeats H and nothing sounds; SHIFT + F1 makes it SONG, and the
  // arrangement plays
  const d = onH();
  d.button(BUTTON.PLAY);
  d.listen();
  d.run(2 * SECOND);
  const looped = rms(d.heard());
  d.button(BUTTON.STOP);
  d.shift(BUTTON.F1);
  d.button(BUTTON.PLAY);
  d.run(Math.round(0.5 * SECOND));
  d.listen();
  d.run(4 * SECOND);
  const song = rms(d.heard());
  ok(
    looped < 0.0005 && song > 0.01,
    `the page opens on LOOP (empty H repeats: rms ${looped.toFixed(4)}); SHIFT + F1: SONG (rms ${song.toFixed(3)})`,
  );
}

// ---- the encoders
{
  const d = perform();
  const was = d.song().bytes;
  d.turn(0, 3);
  d.turn(1, -4);
  d.turn(3, 5);
  d.turn(ENCODER.A, 7);
  d.turn(ENCODER.B, -4);
  const s = d.song().bytes;
  ok(
    s[FADER + FIRST] === was[FADER + FIRST] + 3 && s[PAN + FIRST] === was[PAN + FIRST] - 4,
    `encoders 1 and 2: the selected sound's VOLUME (${was[FADER + FIRST]} to ${s[FADER + FIRST]}) and PAN ` +
      `(${was[PAN + FIRST]} to ${s[PAN + FIRST]})`,
  );
  ok(
    s[LAYER + FIRST] === was[LAYER + FIRST] + 10,
    `encoder 4: its LAYER (${was[LAYER + FIRST]} to ${s[LAYER + FIRST]})`,
  );
  ok(
    tempo(s) === tempo(was) + 7 && s[1] === was[1] - 4,
    `A: the tempo (${tempo(was)} to ${tempo(s)}). B: the bar (${was[1]} to ${s[1]} sixteenths)`,
  );

  // encoder 3 is the intensity: turned down, pattern A is thinner
  const loud = (clicks) => {
    const x = perform();
    x.turn(2, clicks);
    x.button(BUTTON.PLAY);
    x.listen();
    x.run(4 * SECOND);
    return rms(x.heard());
  };
  const full = loud(0);
  const thin = loud(-45);
  ok(
    thin > 0.001 && full > thin * 1.5,
    `encoder 3: the INTENSITY (pattern A, turned down: rms ${thin.toFixed(3)}; as it was: ${full.toFixed(3)})`,
  );
}

// ---- K5 once more: SEQ, where F3 is FILL
{
  const d = onH();
  d.button(BUTTON.K5);
  d.button(BUTTON.F3);
  const s = d.song().bytes;
  ok(
    same(stepList(s, H, FIRST), [0, 4, 8, 12]) && mutes(s) === 0,
    'K5 a fifth time is SEQ again: F3 fills the selected sound and mutes nothing',
  );
}

console.log(failed ? `perform FAILED (${failed})` : 'perform PASSED');
process.exit(failed ? 1 : 0);
