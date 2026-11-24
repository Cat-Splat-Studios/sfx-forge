// What this check proves: the beat and the time signature (firmware 1.7.0), on a small host of its own
// (tests/host.mjs). The bar's length is the song's (B on SONG and PERFORM, 1 to 16 sixteenths); the beat is the
// device's: SHIFT + B there chooses an eighth, three sixteenths, a quarter (as it comes) or a dotted quarter.
//   - FILL begins with the beat: every 3/8 with a dotted quarter, every 3/16 with three sixteenths, every 1/8 with an
//     eighth - then the eighth where the beat is made of eighths, the sixteenth, none.
//   - the metronome clicks on the beat: in a bar of 12 sixteenths on 1 5 9 with a quarter (3/4), on 1 7 with a dotted
//     quarter (6/8).
//   - the beat is not in the song: SHIFT + B changes no byte of it. B changes the bar's length and nothing else.
//   - SAVE keeps it in meter.bin (one byte) - and takes that file away again when the beat is a quarter. NEW leaves
//     it; the factory reset puts the quarter back.
//   - the display follows it: a bar of 12 is two different pictures with a quarter and with a dotted quarter.
// What it cannot see: what the display writes (the title's "BAR 6/8", the words in the top line) and how the grid is
// shaded - only that the picture changes; the stage; and what swing does to the click.
// Each case below is a block { ... } with a device of its own, started from the factory state.
//
//   node tests/check-meter.mjs build/sfx-forge.wasm
import { load, SECOND, BUTTON, EVENT, ENCODER } from './host.mjs';
import { stepList, same, empty } from './song-read.mjs';

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
// pattern H is empty in the factory song; pad 1's sound is selected when the device starts
const H = 7;
const FIRST = 0;
// a list of steps counted from 0, as text counted from 1
const list = (a) => a.map((i) => i + 1).join(' ') || 'none';

// a device on PERFORM, pattern H. files: what its store holds
const onH = (files) => {
  const d = device(files);
  d.run(SECOND);
  for (let i = 0; i < 4; i++) {
    d.button(BUTTON.K5);
  }
  d.button(BUTTON.LEFT);
  return d;
};
// SHIFT + B, so many clicks: the beat one further (+) or one back (-) for each click
const beat = (d, clicks) => {
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 1);
  d.run(2);
  d.turn(ENCODER.B, clicks);
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 0);
  d.run(2);
};
// F5 FILL, and the selected sound's steps in H after it
const fill = (d) => {
  d.button(BUTTON.F5);
  return stepList(d.song().bytes, H, FIRST);
};
// every n-th step of 16, from the first
const every = (n) => [...Array(16).keys()].filter((i) => i % n === 0);

// ---- FILL begins with the beat
{
  const d = onH();
  const quarter = fill(d);
  ok(same(quarter, every(4)), `the beat as it comes is a quarter: FILL is every 1/4 (${list(quarter)})`);
}
{
  const d = onH();
  beat(d, 1);
  const a = fill(d);
  const b = fill(d);
  const c = fill(d);
  const none = fill(d);
  ok(
    same(a, every(6)) && same(b, every(2)) && same(c, every(1)) && none.length === 0,
    `SHIFT + B one click up, a dotted quarter: FILL is every 3/8 (${list(a)}), then every 1/8, every 1/16, none`,
  );
  // further up there is no other beat: the row is empty, so FILL writes the beat again
  beat(d, 9);
  const top = fill(d);
  ok(same(top, every(6)), `further up it stays a dotted quarter (FILL: ${list(top)})`);
}
{
  const d = onH();
  beat(d, -1);
  const a = fill(d);
  const b = fill(d);
  const none = fill(d);
  ok(
    same(a, every(3)) && same(b, every(1)) && none.length === 0,
    `one click down, three sixteenths: FILL is every 3/16 (${list(a)}), then every 1/16, none`,
  );
  beat(d, -1);
  const e = fill(d);
  const f = fill(d);
  const none2 = fill(d);
  beat(d, -5);
  const low = fill(d);
  ok(
    same(e, every(2)) && same(f, every(1)) && none2.length === 0 && same(low, every(2)),
    `one more down, an eighth: FILL is every 1/8 (${list(e)}), then every 1/16, none; further down it stays an ` +
      `eighth (FILL: ${list(low)})`,
  );
}
{
  // a finer grid: 32 steps (SHIFT + F6), a dotted quarter - every 12 steps, then 4, 2, 1 (the thirty-second), none
  const d = onH();
  beat(d, 1);
  d.shift(BUTTON.F6);
  const got = [fill(d), fill(d), fill(d), fill(d), fill(d)];
  const each = (n) => [...Array(32).keys()].filter((i) => i % n === 0);
  ok(
    same(got[0], each(12)) &&
      same(got[1], each(4)) &&
      same(got[2], each(2)) &&
      same(got[3], each(1)) &&
      got[4].length === 0,
    `at 32 steps with a dotted quarter FILL is every 3/8 (steps ${list(got[0])}), 1/8, 1/16, 1/32, none`,
  );
}
{
  // SEQ: SHIFT + B is the beat there too (B alone is the scale)
  const d = device();
  d.run(SECOND);
  d.button(BUTTON.K5);
  d.button(BUTTON.LEFT);
  beat(d, 1);
  d.button(BUTTON.F3);
  const a = stepList(d.song().bytes, H, FIRST);
  ok(same(a, every(6)), `on SEQ, SHIFT + B is the beat too: F3 FILL is every 3/8 (${list(a)})`);
}

// ---- the beat is not in the song; B is the bar's length
{
  const d = onH();
  const was = d.song().bytes;
  beat(d, 1);
  const s = d.song().bytes;
  ok(same(s, was), "SHIFT + B changes no byte of the song: the beat is the device's");
  d.turn(ENCODER.B, -4);
  const t = d.song().bytes;
  ok(
    t[1] === 12 && same(t.slice(2), was.slice(2)) && t[0] === was[0],
    `B without SHIFT: the bar is ${t[1]} sixteenths, and nothing else in the song changed`,
  );
  // FILL in a bar of 12 with a dotted quarter: the two beats of 6/8
  const a = fill(d);
  ok(same(a, [0, 6]), `a bar of 12 with a dotted quarter (6/8): FILL is its two beats (${list(a)})`);
}

// ---- the click falls on the beat
{
  // REC and PLAY on the empty pattern H, a bar of 12 sixteenths: nothing sounds but the click. Gives, for the clicks
  // of four seconds, how many sixteenths lie between one and the next, and which of them are the louder ones (the
  // bar line's).
  const clicks = (up) => {
    const d = onH();
    d.turn(ENCODER.B, -4);
    if (up) {
      beat(d, 1);
    }
    d.button(BUTTON.REC);
    d.listen();
    d.button(BUTTON.PLAY);
    d.run(4 * SECOND);
    const x = d.heard();
    // a click begins where the sound rises after a tenth of a second of silence; its peak is taken over 5 ms
    const starts = [];
    let quiet = 4800;
    for (let i = 0; i < x.length; i++) {
      if (Math.abs(x[i]) > 0.01) {
        if (quiet >= 4800) {
          let peak = 0;
          for (let k = i; k < i + 240 && k < x.length; k++) {
            peak = Math.max(peak, Math.abs(x[k]));
          }
          starts.push({ at: i, peak });
        }
        quiet = 0;
      } else {
        quiet++;
      }
    }
    // 120 BPM at 48 kHz: a sixteenth is 6000 samples
    const gaps = starts.slice(1).map((c, i) => Math.round((c.at - starts[i].at) / 6000));
    const top = Math.max(0, ...starts.map((c) => c.peak));
    const lines = starts.flatMap((c, i) => (c.peak > top * 0.8 ? [i] : []));
    return { n: starts.length, gaps, lines };
  };
  const three = clicks(false);
  const six = clicks(true);
  ok(
    three.n >= 6 && three.gaps.every((g) => g === 4) && three.lines.every((i) => i % 3 === 0),
    `a bar of 12 with a quarter (3/4): ${three.n} clicks, a quarter apart (${[...new Set(three.gaps)]} sixteenths), ` +
      `every third the bar line's (${three.lines.join(' ')})`,
  );
  ok(
    six.n >= 4 && six.gaps.every((g) => g === 6) && six.lines.every((i) => i % 2 === 0),
    `the same bar with a dotted quarter (6/8): ${six.n} clicks, a dotted quarter apart ` +
      `(${[...new Set(six.gaps)]} sixteenths), every second the bar line's (${six.lines.join(' ')})`,
  );
}

// ---- the display follows it
{
  const a = onH();
  a.turn(ENCODER.B, -4);
  a.run(3 * SECOND);
  const b = onH();
  b.turn(ENCODER.B, -4);
  beat(b, 1);
  b.run(3 * SECOND);
  const c = onH();
  c.run(3 * SECOND);
  const d = onH();
  beat(d, 1);
  beat(d, -1);
  d.run(3 * SECOND);
  ok(
    a.screen() !== b.screen() && c.screen() === d.screen(),
    'the display of a bar of 12 differs with the beat (3/4, 6/8); the beat put back, it is as it was',
  );
}

// ---- kept in meter.bin; NEW and the factory reset put the quarter back
{
  const files = new Map();
  const d = onH(files);
  beat(d, 1);
  d.button(BUTTON.K5);
  d.button(BUTTON.F6);
  const kept = files.get('meter.bin');
  ok(kept && kept.length === 1 && kept[0] === 6, `SAVE keeps the beat in meter.bin: one byte, ${kept ? kept[0] : '-'}`);

  // the device switched on again with that store
  const again = onH(files);
  const a = fill(again);
  ok(same(a, every(6)), `switched on again, the beat is the dotted quarter: FILL is every 3/8 (${list(a)})`);

  // a store that holds another beat, or nonsense
  for (const [byte, want] of [
    [2, 2],
    [3, 3],
    [5, 4],
  ]) {
    const got = fill(onH(new Map([['meter.bin', Buffer.from([byte])]])));
    ok(same(got, every(want)), `meter.bin with ${byte}: the beat is ${want} sixteenths (FILL: ${list(got)})`);
  }

  // a quarter again, SAVE: no meter.bin
  beat(again, -1);
  again.button(BUTTON.K5);
  again.button(BUTTON.F6);
  ok(!files.has('meter.bin'), 'SAVE with a quarter takes meter.bin away');
}
{
  // NEW (SONG page, F4 twice) leaves the beat: it is the device's, not the song's
  const d = onH();
  beat(d, 1);
  d.button(BUTTON.K5);
  d.button(BUTTON.K5);
  d.button(BUTTON.F4);
  d.button(BUTTON.F4);
  d.button(BUTTON.K5);
  d.button(BUTTON.K5);
  // (NEW goes to pattern A: FILL fills it there)
  const s = d.song().bytes;
  d.button(BUTTON.F5);
  const a = stepList(d.song().bytes, 0, FIRST);
  ok(empty(s, 0) && same(a, every(6)), `NEW: an empty song, and the beat stays as it was (FILL: ${list(a)})`);
}
{
  // the factory reset (SHIFT + MENU twice) puts the quarter back
  const d = onH();
  beat(d, 1);
  d.shift(BUTTON.MENU);
  d.shift(BUTTON.MENU);
  d.run(SECOND);
  for (let i = 0; i < 4; i++) {
    d.button(BUTTON.K5);
  }
  d.button(BUTTON.LEFT);
  const a = fill(d);
  ok(same(a, every(4)), `the factory reset: the beat is a quarter again (FILL: ${list(a)})`);
}

console.log(failed ? `meter FAILED (${failed})` : 'meter PASSED');
process.exit(failed ? 1 : 0);
