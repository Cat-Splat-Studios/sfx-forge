// tests/take64.json: a take at 64 steps keeps the player's timing. Pattern H (empty), K5 (SEQ), the kick selected, swing 0,
// SHIFT + F6 twice (64 steps), REC, PLAY at 1.0 s - at 120 BPM a bar is 2 s, a step 31.25 ms. The kick's pad is hit 100,
// 290 and 1010 ms after the bar line at 3.0 s. STOP at 6.2 s (1.2 s into a bar: the pads show steps 33-48), REC off, SEND
// (song 1). Pad 1 - the tile of step 33 - switches that step off. SEND (song 2). REC, PLAY at 10.0 s, F4 held from 10.2 to
// 10.4 s, STOP, SEND (song 3). The first bar of the take is empty: only the metronome is heard in it.
//   node tests/check-take64.mjs out/take64 unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { songs, samples, grid, stepList, pattern, same } from './song-read.mjs';
const dir = process.argv[2] || 'out/take64', factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const KICK = 2, H = 7, STEP_MS = 31.25;
const sent = songs(`${dir}/midi.json`).map((s) => s.bytes), factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);
ok(sent.length === 3, `three songs went out (${sent.length})`);
if (sent.length === 3) {
  const [take, off, erased] = sent;
  const got = stepList(take, H, KICK), hits = [100, 290, 1010], want = hits.map((ms) => Math.round(ms / STEP_MS));
  const others = [...Array(16)].map((_, t) => t).filter((t) => t !== KICK && stepList(take, H, t).length);
  // (what was measured: the step each hit landed on, counted from 0, and how far that step's start is from the hit)
  const said = got.map((g, i) => `${hits[i] ?? '?'} ms -> ${g} (${(g * STEP_MS - (hits[i] ?? 0)).toFixed(1)} ms)`).join(', ');
  ok(grid(take, H) === 64 && got.length === 3 && got.every((g, i) => Math.abs(g - want[i]) <= 1), `each hit lands within one step of where it was played: ${said}; nearest steps ${want.join(', ')}`);
  ok(same(got, [3, 9, 32]), `on steps 3, 9 and 32 exactly, counted from 0 (${got.join(', ')})`);
  ok(!others.length && [0, 1, 2, 3, 4, 5, 6].every((p) => same(pattern(take, p), pattern(factory, p))), 'nothing else is set: no other sound in H, and A to G are the factory song\'s');
  ok(same(stepList(off, H, KICK), [3, 9]), `after STOP the pads still show the steps the take ended on: pad 1 is step 33 and switches it off (${stepList(off, H, KICK).join(', ')} left)`);
  ok(same(stepList(erased, H, KICK), [3]), `F4 held from 200 to 400 ms erases the steps that pass under it - step 9 - and no others (${stepList(erased, H, KICK).join(', ')} left)`);
}

// the click: bar 1 of the take (1.0 to 3.0 s) holds nothing, so what is heard there is the metronome - on the four beats
const { sr, x } = samples(`${dir}/audio.wav`);
const k = Math.exp(-2 * Math.PI * 1200 / sr); let lp = 0;
const hi = new Float32Array(x.length);
for (let i = 0; i < x.length; i++) { lp = x[i] + (lp - x[i]) * k; hi[i] = x[i] - lp; }
const rms = (a, b) => { let s = 0, c = 0; for (let i = Math.floor(a * sr); i < Math.floor(b * sr); i++) { s += hi[i] * hi[i]; c++; } return Math.sqrt(s / c); };
const beats = [1.0, 1.5, 2.0, 2.5].map((t) => rms(t + 0.004, t + 0.03)), between = [1.25, 1.75, 2.25, 2.75].map((t) => rms(t + 0.004, t + 0.03));
ok(Math.min(...beats) > 0.02 && Math.max(...between) < 0.002, `the metronome clicks four times a bar at 64 steps as at 16: on the beats ${beats.map((v) => v.toFixed(3)).join(', ')}, between them ${Math.max(...between).toFixed(4)}`);
console.log(failed ? `take64 FAILED (${failed})` : 'take64 PASSED');
process.exit(failed ? 1 : 0);
