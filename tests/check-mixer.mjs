// What this check proves: the mixer as the host's parameters (firmware 1.7.1), on a small host of its own
// (tests/host.mjs). The SDK's mixer convention (SDK 11, CONTROLLERS.md): a firmware with tracks declares
// mix_<n>_level, _pan, _mute, _solo and mix_select, so that a mixer controller and a DAW work them.
//   - the list: the six parameters of before, unchanged and first; then four for each of the 16 sounds and
//     mix_select, with the convention's ids, ranges and steps.
//   - at the start the host is told all of them, as the song has them.
//   - from the host: a level, a pan, a mute, a solo and the selected sound land where MIX has them - the next turn of
//     an encoder goes on from there - and what the host set is not said back to it.
//   - to the host: a fader and a pan turned on MIX, a pad that mutes, SHIFT + pad for solo, DATA, ALL ON and NEW are
//     each reported, and nothing else is.
//   - it is heard: every sound muted from the host, the song is silent; unmuted, it sounds.
//   - what is not taken: a value that is no number, and the selected sound while the tour runs - the host is told
//     what the value really is. The tour changes no sound for good, whatever is selected while it runs.
// What it cannot see: a controller's faders and lights, and the plugin's Editor view - those are the host's.
// Each case below is a block { ... } with a device of its own, started from the factory state.
//
//   node tests/check-mixer.mjs build/sfx-forge.wasm
import { load, SECOND, BUTTON, EVENT, ENCODER } from './host.mjs';

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
// where the mixer begins in the list, and the four of a sound (n: 1-16)
const MIX = 6;
const LEVEL = (n) => MIX + 4 * (n - 1);
const PAN = (n) => LEVEL(n) + 1;
const MUTE = (n) => LEVEL(n) + 2;
const SOLO = (n) => LEVEL(n) + 3;
const SELECT = MIX + 64;
const near = (a, b) => Math.abs(a - b) < 1e-6;
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

// a device on the MIX page (K4 twice), with what the start reported forgotten
const onMix = () => {
  const d = device();
  d.run(SECOND);
  d.button(BUTTON.K4);
  d.button(BUTTON.K4);
  d.changed();
  return d;
};
// SHIFT + pad n
const shiftPad = (d, n) => {
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 1);
  d.run(2);
  d.pad(n);
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 0);
  d.run(2);
};

// ---- the list
{
  const d = device();
  const p = d.params();
  ok(
    same(
      p.slice(0, 6).map((x) => x.id),
      ['volume', 'mod_pitch', 'mod_cutoff', 'mod_fm', 'mod_level', 'intensity'],
    ),
    'the six parameters of before are the first six, in their order',
  );
  let good = p.length === 71;
  for (let n = 1; n <= 16; n++) {
    const [level, pan, mute, solo] = [p[LEVEL(n)], p[PAN(n)], p[MUTE(n)], p[SOLO(n)]];
    good =
      good &&
      level.id === `mix_${n}_level` &&
      level.min === 0 &&
      level.max === 1 &&
      level.steps === 0 &&
      pan.id === `mix_${n}_pan` &&
      pan.min === -1 &&
      pan.max === 1 &&
      pan.steps === 0 &&
      mute.id === `mix_${n}_mute` &&
      mute.min === 0 &&
      mute.max === 1 &&
      mute.steps === 2 &&
      solo.id === `mix_${n}_solo` &&
      solo.min === 0 &&
      solo.max === 1 &&
      solo.steps === 2 &&
      [level, pan, mute, solo].every((x) => x.group === `MIX ${n}`);
  }
  const s = p[SELECT] || {};
  ok(
    good && s.id === 'mix_select' && s.min === 1 && s.max === 16 && s.steps === 16,
    `${p.length} parameters: mix_<n>_level, _pan, _mute, _solo for 16 sounds and mix_select, as the convention has them`,
  );
  ok(new Set(p.map((x) => x.id)).size === p.length, 'no id is there twice');

  // the start: all 65 told, in their ranges; the factory song's kick is quieter than a new fader, nothing is muted
  const told = d.changed();
  const mix = told.filter((i) => i >= MIX);
  ok(mix.length === 65, `at the start the host is told all of the mixer (${mix.length} parameters)`);
  const levels = [...Array(16).keys()].map((i) => d.param(LEVEL(i + 1)));
  ok(
    levels.every((v) => v >= 0 && v <= 1) && new Set(levels.map((v) => v.toFixed(4))).size > 3 && d.param(SELECT) === 1,
    `the levels are the factory song's (${levels
      .slice(0, 4)
      .map((v) => Math.round(v * 127))
      .join(' ')} ... of 127), pad 1's sound is selected`,
  );
  ok(
    [...Array(16).keys()].every((i) => d.param(MUTE(i + 1)) === 0 && d.param(SOLO(i + 1)) === 0),
    'no sound is muted or solo',
  );
  d.run(SECOND);
  ok(d.changed().length === 0, 'a second later, nothing touched: nothing more is told');
}

// ---- from the host
{
  const d = onMix();
  // pad 3 mutes on MIX and selects: from here encoders 1 and 2 are sound 3's fader and pan
  d.pad(3);
  d.pad(3);
  d.changed();

  d.setParam(LEVEL(3), 0.5);
  d.run(4);
  ok(d.changed().length === 0, 'the host sets mix_3_level: it is not said back to the host');
  d.turn(0, 1);
  ok(
    near(d.param(LEVEL(3)), 65 / 127) && same(d.changed(), [LEVEL(3)]),
    `then encoder 1 one click up goes on from there: 64 -> 65 of 127 (${(d.param(LEVEL(3)) * 127).toFixed(2)})`,
  );

  d.setParam(PAN(3), -1);
  d.turn(1, 1);
  const left = d.param(PAN(3));
  d.setParam(PAN(3), 1);
  d.turn(1, -1);
  const right = d.param(PAN(3));
  d.setParam(PAN(3), 0);
  d.turn(1, 1);
  const mid = d.param(PAN(3));
  ok(
    near(left, -63 / 64) && near(right, 62 / 63) && near(mid, 1 / 63),
    `a pan from the host: all left, all right and the middle are the song's 0, 127 and 64 (a click from each: ` +
      `${left.toFixed(3)}, ${right.toFixed(3)}, ${mid.toFixed(3)})`,
  );

  // out of range: the SDK clamps to the parameter's range
  d.setParam(LEVEL(3), 9);
  d.turn(0, -1);
  ok(near(d.param(LEVEL(3)), 126 / 127), 'a level beyond the range is the top of the fader');

  // the selected sound
  d.changed();
  d.setParam(SELECT, 5);
  d.run(4);
  const quiet = d.changed().length === 0;
  d.turn(ENCODER.DATA, 1);
  ok(
    quiet && d.param(SELECT) === 6 && same(d.changed(), [SELECT]),
    `mix_select 5 from the host, then DATA one click: sound ${d.param(SELECT)} is selected, and that is told`,
  );
  d.setParam(LEVEL(6), 0.25);
  d.turn(0, 1);
  ok(near(d.param(LEVEL(6)), 33 / 127), "encoder 1 is now sound 6's fader");
}
{
  // mute and solo from the host are what the pads of MIX toggle
  const d = onMix();
  d.setParam(MUTE(4), 1);
  d.setParam(SOLO(7), 1);
  d.run(4);
  const quiet = d.changed().length === 0;
  d.pad(4);
  const unmuted = d.param(MUTE(4));
  shiftPad(d, 7);
  ok(
    quiet && unmuted === 0 && d.param(SOLO(7)) === 0,
    'mute 4 and solo 7 from the host: pad 4 unmutes it, SHIFT + pad 7 takes the solo away',
  );
  ok(
    [...Array(16).keys()].every((i) => d.param(MUTE(i + 1)) === 0 && d.param(SOLO(i + 1)) === 0),
    'and no other mute or solo moved',
  );
}

// ---- to the host
{
  const d = onMix();
  d.pad(2);
  ok(
    d.param(MUTE(2)) === 1 && same(d.changed(), [MUTE(2), SELECT]) && d.param(SELECT) === 2,
    'pad 2 on MIX: mix_2_mute is 1 and mix_select is 2, and only these two are told',
  );
  shiftPad(d, 9);
  ok(d.param(SOLO(9)) === 1 && same(d.changed(), [SOLO(9), SELECT]), 'SHIFT + pad 9: mix_9_solo is 1');
  const before = d.param(LEVEL(9));
  d.turn(0, -3);
  d.turn(1, 5);
  ok(
    near(d.param(LEVEL(9)), before - 3 / 127) && same(d.changed(), [LEVEL(9), PAN(9)]),
    `encoders 1 and 2: mix_9_level three of 127 down, mix_9_pan moved (${d.param(PAN(9)).toFixed(3)})`,
  );
  // F4 on MIX: ALL ON
  d.button(BUTTON.F4);
  ok(
    d.param(MUTE(2)) === 0 && d.param(SOLO(9)) === 0 && same(d.changed(), [MUTE(2), SOLO(9)]),
    'F4 ALL ON: the mute and the solo are 0 again, and both are told',
  );
}
{
  // NEW (SONG page, F4 twice): every fader where a new song has it
  const d = onMix();
  d.button(BUTTON.K5);
  d.button(BUTTON.K5);
  d.button(BUTTON.F4);
  d.button(BUTTON.F4);
  const levels = [...Array(16).keys()].map((i) => d.param(LEVEL(i + 1)));
  const pans = [...Array(16).keys()].map((i) => d.param(PAN(i + 1)));
  const told = d.changed();
  ok(
    levels.every((v) => near(v, 100 / 127)) && pans.every((v) => v === 0) && told.length > 3,
    `NEW: every level is 100 of 127 and every pan the middle; ${told.length} parameters that moved are told`,
  );
  // F2 UNDO brings the song back, and its mix
  d.button(BUTTON.F2);
  const back = [...Array(16).keys()].map((i) => d.param(LEVEL(i + 1)));
  ok(
    new Set(back.map((v) => v.toFixed(4))).size > 3 && same(d.changed(), told),
    'F2 UNDO: the levels of the song before are back, and the same parameters are told',
  );
}

// ---- it is heard
{
  const loud = (d) => {
    d.listen();
    d.button(BUTTON.PLAY);
    d.run(3 * SECOND);
    const x = d.heard();
    d.button(BUTTON.STOP);
    d.run(2 * SECOND);
    return Math.sqrt(x.reduce((sum, v) => sum + v * v, 0) / x.length);
  };
  const d = onMix();
  const open = loud(d);
  for (let n = 1; n <= 16; n++) {
    d.setParam(MUTE(n), 1);
  }
  const muted = loud(d);
  for (let n = 1; n <= 16; n++) {
    d.setParam(MUTE(n), 0);
  }
  const again = loud(d);
  ok(
    open > 0.01 && muted < open / 100 && again > open / 2,
    `the song plays (${open.toFixed(4)}); every sound muted from the host it is silent (${muted.toFixed(6)}); ` +
      `unmuted it sounds again (${again.toFixed(4)})`,
  );
  for (let n = 1; n <= 16; n++) {
    d.setParam(LEVEL(n), 0);
  }
  const down = loud(d);
  ok(down < open / 100, `every fader down from the host: silent (${down.toFixed(6)})`);
}

// ---- what is not taken
{
  const d = onMix();
  d.pad(3);
  d.pad(3);
  d.setParam(LEVEL(3), 0.5);
  d.setParam(PAN(3), 0.5);
  d.changed();
  d.setParam(LEVEL(3), NaN);
  d.setParam(PAN(3), NaN);
  d.setParam(MUTE(3), NaN);
  d.setParam(SELECT, NaN);
  d.run(12);
  ok(
    near(d.param(LEVEL(3)), 64 / 127) &&
      near(d.param(PAN(3)), 32 / 63) &&
      d.param(MUTE(3)) === 0 &&
      d.param(SELECT) === 3 &&
      same(d.changed(), [LEVEL(3), PAN(3), MUTE(3), SELECT]),
    'a value that is no number is not taken: level, pan, mute and the selected sound stay, and the host is told them',
  );
}
{
  // the 16 sounds of the kit that is chosen, as the device sends them when asked (REQUEST SONG)
  const sounds = (d) => JSON.stringify(d.song().dumps);
  const tour = (meddle) => {
    const d = device();
    d.run(SECOND);
    const before = sounds(d);
    d.button(BUTTON.MENU);
    // four seconds in the tour has selected a sound of its own and is changing it
    d.run(4 * SECOND);
    if (meddle) {
      d.setParam(SELECT, 3);
      d.run(SECOND);
      d.turn(ENCODER.DATA, 2);
    }
    // (a minute: the tour is over)
    d.run(60 * SECOND);
    d.changed();
    return { d, same: sounds(d) === before };
  };
  const plain = tour(false);
  const meddled = tour(true);
  ok(plain.same, 'the tour, left alone: every sound is as it was after it');
  ok(
    meddled.same,
    'the tour with mix_select from the host and DATA turned while it runs: every sound is as it was after it',
  );
  ok(
    meddled.d.param(SELECT) === 1 && meddled.d.changed().length === 0,
    'after it the sound of before is selected, the host has that, and nothing more is told',
  );
}

console.log(failed ? `mixer FAILED (${failed})` : 'mixer PASSED');
process.exit(failed ? 1 : 0);
