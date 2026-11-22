// What this check proves: a take over several bars lands in the pattern on the display, and F4 held erases the steps
// that pass under it and no others.
//
// The scene (tests/take.json, rendered by the SDK's wasmtest into out/take): pattern H (empty) is chosen, K5 (the SEQ
// page), the click off, REC, PLAY at 1.0 s - a bar is 2 s. The kick's pad is hit once in each of three bars: 1.5 s
// (step 5), 3.25 s (step 3), 5.75 s (step 7). All three land in H, the pattern on the display, bar after bar. F4 is
// held from 7.6 to 8.2 s: steps 5 to 10 pass under it, step 3 does not. STOP, then SEND: the song that went out says
// what is in H now.
//
//   node tests/check-take.mjs out/take unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { wav, songs, steps, pattern, same } from './song-read.mjs';

const dir = process.argv[2] || 'out/take';
const factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

// the level of the first 80 ms after a moment: is there a kick at t?
const rms = wav(`${dir}/audio.wav`);
const at = (t) => rms(t, t + 0.08);
const f = (v) => v.toFixed(3);
const KICK = 2;
const H = 7;

// ---- what is heard: the bars at 5.0 s and 7.0 s (bars 3 and 4 of the take), then the bar lines at 9.0 and 11.0 s
const bar3 = [5.25, 5.5].map(at);
const bar4 = [7.25, 7.5, 7.75].map(at);
ok(
  Math.min(...bar3) > 0.05,
  `what was played in bars 1 and 2 plays in bar 3: steps 3 and 5 (${bar3.map(f).join(', ')})`,
);
ok(Math.min(...bar4) > 0.05, `and all three hits in bar 4: steps 3, 5 and 7 (${bar4.map(f).join(', ')})`);
ok(
  at(9.0) < 0.01 && at(11.0) < 0.01,
  `it is pattern H that goes round: nothing on the bar lines (${f(at(9.0))}, ${f(at(11.0))})`,
);

// after the erase the kick on step 3 still rings a little where steps 5 and 7 were: a kick STARTS where there is much
// more just after than just before
const starts = (t) => rms(t, t + 0.05) > 0.05 && rms(t, t + 0.05) > rms(t - 0.05, t) * 2;
const kept = [9.25, 11.25];
const gone = [9.5, 9.75, 11.5, 11.75];
ok(
  kept.every(starts) && !gone.some(starts),
  `F4 held erases the steps that pass under it, and no others: step 3 stays ` +
    `(${kept.map(at).map(f).join(', ')}), no kick starts on 5 and 7 ` +
    `(${gone.map(at).map(f).join(', ')}: the one on step 3 fading)`,
);
ok(Math.min(...kept.map(at)) > Math.max(...gone.map(at)) * 5, 'what sounds is at least five times what is left there');

// ---- what the song that was sent holds
const sent = songs(`${dir}/midi.json`);
ok(sent.length === 1, `SEND gave the song (${sent.length})`);
if (sent.length === 1) {
  const s = sent[0].bytes;

  // (the file is 16 sounds of 28 bytes, then the song)
  const factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);

  // the sounds other than the kick that have a step in H
  const others = [...Array(16)].map((_, t) => t).filter((t) => t !== KICK && steps(s, H, t));
  ok(
    steps(s, H, KICK) === 1 << 2 && !others.length,
    `pattern H holds the kick on step 3 and nothing else (kick ` +
      `${steps(s, H, KICK).toString(2)}, other sounds: ${others.length})`,
  );

  // the patterns A to G that are not what the factory song has
  const changed = [...Array(7)].map((_, p) => p).filter((p) => !same(pattern(s, p), pattern(factory, p)));
  ok(
    !changed.length,
    `patterns A to G are the factory song's, untouched ` +
      `(${changed.length ? 'changed: ' + changed.map((p) => 'ABCDEFG'[p]).join(' ') : 'all seven'})`,
  );
}

console.log(failed ? `take FAILED (${failed})` : 'take PASSED');
process.exit(failed ? 1 : 0);
