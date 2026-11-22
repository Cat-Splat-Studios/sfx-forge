// What this check proves: LOOP repeats the pattern shown, a cued pattern starts on the bar line and stays, F1 goes on
// in the arrangement from that pattern's bar, and PLAY on the SONG page plays the arrangement.
//
// The scene (tests/modes.json, rendered by the SDK's wasmtest into out/modes): the factory song at intensity 0 – only
// the kick plays, and its steps tell the patterns apart: A and B have a kick on step 8 (and 5, 13), C and D on step
// 11 instead of 8, E on steps 1 and 9 only. 120 BPM: a bar is 2 s.
//   K5 (the SEQ page: LOOP), PLAY at 1.0 s      -> A goes round, bar after bar (the arrangement would be on C in bar 5)
//   pattern E cued at 11.4 s (CC 15)            -> it starts on the bar line at 13.0 s, not before, and stays
//   F1 (TO SONG) at 18.6 s, while it plays      -> on in the arrangement from its first bar with E: E, then A
//   STOP; the SONG page, pattern A, PLAY at 26 s -> the arrangement as ever: A B A B, then C in bar 5
//
//   node tests/check-modes.mjs out/modes
import { wav } from './song-read.mjs';

const dir = process.argv[2] || 'out/modes';

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

const rms = wav(`${dir}/audio.wav`);

// a kick starts at t: much more just after than just before (the kick before it still rings a little)
const rise = (t) => {
  const after = rms(t, t + 0.05);
  const before = rms(t - 0.05, t);
  return { after, before, kick: after > 0.03 && after > before * 2 };
};

// the moment of step n (counted from 0) of the bar that begins at `bar` seconds
// (swing 20: the off-beat steps wait a tenth of a step)
const step = (bar, n) => bar + n * 0.125 + (n & 1 ? 0.0125 : 0);

// a bar as a word: x where a kick starts on one of the listed steps, - where none does
const kicks = (bar, list) => list.map((n) => (rise(step(bar, n)).kick ? 'x' : '-')).join('');
const told = (bar) => kicks(bar, [0, 4, 7, 8, 10, 12]); // steps 1, 5, 8, 9, 11, 13
const A = 'xxxx-x';
const C = 'xx-xxx';
const E = 'x--x--';

// ---- LOOP from 1.0 s: five bars
const loop = [1, 3, 5, 7, 9].map(told);
ok(
  loop.every((b) => b === A),
  `LOOP: pattern A plays in each of the first five bars (${loop.join(' ')}; the arrangement has C in the fifth: ${C})`,
);

// ---- pattern E cued in the middle of the bar at 11 s: that bar is still A, the next three are E
ok(told(11) === A, `a pattern cued in the middle of a bar does not start before the bar line (${told(11)})`);
const held = [13, 15, 17].map(told);
ok(
  held.every((b) => b === E),
  `it starts on the bar line and stays, bar after bar (${held.join(' ')}; E is ${E})`,
);

// ---- F1 (TO SONG) at 18.6 s: the bar at 19 s is still E, then the arrangement goes on with A
const on = [19, 21].map(told);
ok(
  on[0] === E && on[1] === A,
  `F1 while it plays: SONG goes on in the arrangement from that pattern's bar - E, then A (${on.join(' ')})`,
);

// ---- STOP, then PLAY on the SONG page at 26 s: five bars of the arrangement
ok(rms(25.2, 25.9) < 0.001, `STOP stops it (${rms(25.2, 25.9).toFixed(4)})`);
const song = [26, 28, 30, 32, 34].map(told);
ok(
  song.slice(0, 4).every((b) => b === A) && song[4] === C,
  `SONG: PLAY on the SONG page plays the arrangement - A B A B, then C (${song.join(' ')})`,
);

console.log(failed ? `modes FAILED (${failed})` : 'modes PASSED');
process.exit(failed ? 1 : 0);
