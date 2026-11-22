// tests/grid.json: a pattern's grid, and the 16 steps the pads show of it. Pattern H (empty), K5 (SEQ), the hi-hat selected,
// swing 0. SHIFT + F6: 32 steps. Pads 1 and 4 (steps 1 and 4), SHIFT + RIGHT (the pads are steps 17-32 now), pads 2 and 7
// (steps 18 and 23). SEND. PLAY at 4.0 s for two bars: the hat sounds on those thirty-seconds. SHIFT + F6 again: 64 steps.
// F3 FILL five times, a SEND after each: every 1/4, 1/8, 1/16, 1/32, none. Then pattern G (16 steps): pad 5, SHIFT + LEFT
// (nothing to move to), pad 6. SEND.
//   node tests/check-grid.mjs out/grid unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { songs, samples, grid, stepList, pattern, same, format } from './song-read.mjs';
const dir = process.argv[2] || 'out/grid', factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const HAT = 12, G = 6, H = 7;
const sent = songs(`${dir}/midi.json`).map((s) => s.bytes), factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);
ok(sent.length === 7 && sent.every((s) => format(s) === 6), `seven songs went out, all in format 6 (${sent.length})`);
if (sent.length === 7) {
  const [first, ...rest] = sent, fills = rest.slice(0, 5), last = rest[5];
  const others = (s, p) => [...Array(16)].map((_, t) => t).filter((t) => t !== HAT && stepList(s, p, t).length);
  const list = (a) => a.map((i) => i + 1).join(' ') || 'none';
  ok(grid(first, H) === 32 && same(stepList(first, H, HAT), [0, 3, 17, 22]) && !others(first, H).length,
     `SHIFT + F6 gives pattern H 32 steps; pads 1 and 4, then SHIFT + RIGHT and pads 2 and 7, set steps 1, 4, 18 and 23 (${grid(first, H)} steps: ${list(stepList(first, H, HAT))})`);
  ok([0, 1, 2, 3, 4, 5, 6].every((p) => grid(first, p) === 16 && same(pattern(first, p), pattern(factory, p))), 'the grid belongs to the pattern: A to G still have 16 steps, and are the factory song\'s');
  const want = [16, 8, 4, 2].map((every) => [...Array(64 / every)].map((_, i) => i * every));
  const got = fills.map((s) => stepList(s, H, HAT));
  ok(fills.every((s) => grid(s, H) === 64) && want.every((w, k) => same(got[k], w)) && got[4].length === 0,
     `at 64 steps FILL goes every 1/4, 1/8, 1/16, 1/32, then none: ${got.map((g) => g.length).join(', ')} steps in the bar`);
  ok(same(stepList(fills[0], H, HAT), [0, 16, 32, 48]) && same(stepList(fills[1], H, HAT).slice(0, 3), [0, 8, 16]), 'and they stand where those notes stand: every 16th step, every 8th');
  ok(grid(last, G) === 16 && same(stepList(last, G, HAT), [4, 5]) && grid(last, H) === 64,
     `at 16 steps SHIFT + LEFT moves nothing: pads 5 and 6 are steps 5 and 6 before and after it (${list(stepList(last, G, HAT))})`);
}

// the audio: between 4.0 and 8.0 s the hat alone, on steps 1, 4, 18 and 23 of 32 - at 120 BPM a thirty-second is 62.5 ms
const { sr, x } = samples(`${dir}/audio.wav`);
const rms = (a, b) => { let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return Math.sqrt(s / Math.max(1, b - a)); };
const onsets = [];
for (let i = Math.floor(3.95 * sr); i < Math.floor(7.98 * sr); i += 12) {
  const now = rms(i, i + 96), before = rms(i - 480, i - 48);
  if (now > 0.01 && now > before * 4) { onsets.push(i / sr); i += Math.floor(0.03 * sr); }
}
const wantMs = [0, 3, 17, 22, 32, 35, 49, 54].map((n) => n * 62.5), gotMs = onsets.map((t) => (t - onsets[0]) * 1000);
const worst = gotMs.length === wantMs.length ? Math.max(...gotMs.map((v, i) => Math.abs(v - wantMs[i]))) : -1;
ok(onsets.length === 8 && worst < 3 && Math.abs(onsets[0] - 4.0) < 0.02,
   `played, the hat starts on those thirty-seconds: ${gotMs.map((v) => v.toFixed(1)).join(', ')} ms after the first (wanted ${wantMs.join(', ')}; furthest off ${worst.toFixed(2)} ms)`);
console.log(failed ? `grid FAILED (${failed})` : 'grid PASSED');
process.exit(failed ? 1 : 0);
