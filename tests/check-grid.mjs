// What this check proves: a pattern has a grid of its own (16, 32 or 64 steps), the pads show 16 steps of it, and
// FILL follows the grid.
//
// The scene (tests/grid.json, written by tests/make-grid-scenes.mjs, rendered by the SDK's wasmtest into out/grid):
// pattern H (empty), K5 (SEQ), the hi-hat selected, swing 0. SHIFT + F6: 32 steps. Pads 1 and 4 (steps 1 and 4),
// SHIFT + RIGHT (the pads are steps 17-32 now), pads 2 and 7 (steps 18 and 23). SEND. PLAY at 4.0 s for two bars: the
// hat sounds on those thirty-seconds. SHIFT + F6 again: 64 steps. F3 FILL five times, a SEND after each: every 1/4,
// 1/8, 1/16, 1/32, none. Then pattern G (16 steps): pad 5, SHIFT + LEFT (nothing to move to), pad 6. SEND.
//
//   node tests/check-grid.mjs out/grid unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { songs, samples, grid, stepList, pattern, same, format } from './song-read.mjs';

const dir = process.argv[2] || 'out/grid';
const factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

const HAT = 12;
const G = 6;
const H = 7;
const sent = songs(`${dir}/midi.json`).map((s) => s.bytes);
// (the file is 16 sounds of 28 bytes, then the song)
const factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);

// ---- the seven songs: after the four pads, after each of the five FILLs, after the pads on G
ok(sent.length === 7 && sent.every((s) => format(s) === 6), `seven songs went out, all in format 6 (${sent.length})`);
if (sent.length === 7) {
  const [first, ...rest] = sent;
  const fills = rest.slice(0, 5);
  const last = rest[5];

  // the sounds other than the hat that have a step in pattern p
  const others = (s, p) => [...Array(16)].map((_, t) => t).filter((t) => t !== HAT && stepList(s, p, t).length);
  // a list of steps counted from 0, as text counted from 1
  const list = (a) => a.map((i) => i + 1).join(' ') || 'none';
  ok(
    grid(first, H) === 32 && same(stepList(first, H, HAT), [0, 3, 17, 22]) && !others(first, H).length,
    `SHIFT + F6 gives pattern H 32 steps; pads 1 and 4, then SHIFT + RIGHT and pads 2 and 7, set steps 1, 4, 18 ` +
      `and 23 (${grid(first, H)} steps: ${list(stepList(first, H, HAT))})`,
  );
  ok(
    [0, 1, 2, 3, 4, 5, 6].every((p) => grid(first, p) === 16 && same(pattern(first, p), pattern(factory, p))),
    "the grid belongs to the pattern: A to G still have 16 steps, and are the factory song's",
  );

  // FILL at 64 steps: every 16th step (a quarter note), every 8th, 4th, 2nd, then none
  const want = [16, 8, 4, 2].map((every) => [...Array(64 / every)].map((_, i) => i * every));
  const got = fills.map((s) => stepList(s, H, HAT));
  ok(
    fills.every((s) => grid(s, H) === 64) && want.every((w, k) => same(got[k], w)) && got[4].length === 0,
    `at 64 steps FILL goes every 1/4, 1/8, 1/16, 1/32, then none: ` +
      `${got.map((g) => g.length).join(', ')} steps in the bar`,
  );
  ok(
    same(stepList(fills[0], H, HAT), [0, 16, 32, 48]) && same(stepList(fills[1], H, HAT).slice(0, 3), [0, 8, 16]),
    'and they stand where those notes stand: every 16th step, every 8th',
  );
  ok(
    grid(last, G) === 16 && same(stepList(last, G, HAT), [4, 5]) && grid(last, H) === 64,
    `at 16 steps SHIFT + LEFT moves nothing: pads 5 and 6 are steps 5 and 6 before and after it ` +
      `(${list(stepList(last, G, HAT))})`,
  );
}

// ---- the audio: between 4.0 and 8.0 s the hat alone, on steps 1, 4, 18 and 23 of 32 - at 120 BPM a thirty-second
// is 62.5 ms
const { sr: sampleRate, x: left } = samples(`${dir}/audio.wav`);

// the level between two samples
const rms = (from, to) => {
  let sum = 0;
  for (let i = from; i < to; i++) {
    sum += left[i] * left[i];
  }
  return Math.sqrt(sum / Math.max(1, to - from));
};

// the moments a sound starts: 2 ms that are loud, and four times what the 9 ms before them were. After one is found
// the next 30 ms are passed over.
const onsets = [];
for (let i = Math.floor(3.95 * sampleRate); i < Math.floor(7.98 * sampleRate); i += 12) {
  const now = rms(i, i + 96);
  const before = rms(i - 480, i - 48);
  if (now > 0.01 && now > before * 4) {
    onsets.push(i / sampleRate);
    i += Math.floor(0.03 * sampleRate);
  }
}

// two bars: the four steps, twice, in ms after the first
const wantMs = [0, 3, 17, 22, 32, 35, 49, 54].map((n) => n * 62.5);
const gotMs = onsets.map((t) => (t - onsets[0]) * 1000);
const worst = gotMs.length === wantMs.length ? Math.max(...gotMs.map((v, i) => Math.abs(v - wantMs[i]))) : -1;
ok(
  onsets.length === 8 && worst < 3 && Math.abs(onsets[0] - 4.0) < 0.02,
  `played, the hat starts on those thirty-seconds: ` +
    `${gotMs.map((v) => v.toFixed(1)).join(', ')} ms after the first ` +
    `(wanted ${wantMs.join(', ')}; furthest off ${worst.toFixed(2)} ms)`,
);

console.log(failed ? `grid FAILED (${failed})` : 'grid PASSED');
process.exit(failed ? 1 : 0);
