// tests/regrid.json: a pattern put in a finer grid sounds the same; a coarser one drops what stands between, asks first,
// and F2 brings it back.
//   Intensity 0 (the kick alone), K5 (SEQ). PLAY at 1.0 s: pattern A for two bars at 16 steps. SHIFT + F6 twice: 64 steps.
//   PLAY at 6.0 s: two bars again. SEND (song 1). SHIFT + F5 twice - nothing stands between the sixteenths, so nothing is
//   asked: 16 steps again. SEND (song 2).
//   Pattern H: 32 steps, the hat on step 2. Pattern G: 32 steps, the hat on steps 1 and 2. On G, SHIFT + F5: the question.
//   RIGHT (pattern H), SHIFT + F5: H's own question - G's does not count there. LEFT (G), SHIFT + F5 twice: lowered.
//   SEND (song 3). F2, SEND (song 4). FILL twice: the hat on every eighth. PLAY at 21.0 s, SHIFT + F6 at 23.6 s - while it
//   plays, between two eighths of the second bar. STOP at 27 s. SEND (song 5).
//   node tests/check-regrid.mjs out/regrid unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { songs, samples, grid, stepList, step, pattern, same } from './song-read.mjs';
const dir = process.argv[2] || 'out/regrid', factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const HAT = 12, A = 0, G = 6, H = 7;
const { sr, x } = samples(`${dir}/audio.wav`);

// ---- the same sound: the two bars from 1.0 s and the two from 6.0 s, sample against sample (both begin on a block's first sample)
{
  const a = Math.round(1.0 * sr / 128) * 128, b = Math.round(6.0 * sr / 128) * 128, n = Math.floor(3.9 * sr);
  let best = 0, off = 0, peak = 0;
  for (let d = -256; d <= 256; d += 128) {
    let ab = 0, aa = 0, bb = 0;
    for (let i = 0; i < n; i++) { const u = x[a + i], v = x[b + d + i]; ab += u * v; aa += u * u; bb += v * v; }
    const c = ab / Math.sqrt(aa * bb + 1e-12);
    if (c > best) { best = c; off = d; }
  }
  let differ = 0;
  for (let i = 0; i < n; i++) { peak = Math.max(peak, Math.abs(x[a + i])); differ = Math.max(differ, Math.abs(x[a + i] - x[b + off + i])); }
  // (on the device the two stretches are not the same samples: the second starts on the tails of the first. That a finer
  // grid changes no sample at all is shown where nothing else differs: tests/dotnet, "raising changes no sample")
  ok(best > 0.999 && peak > 0.1 && differ < 0.03, `pattern A at 16 steps and at 64 sounds alike on the device: correlation ${best.toFixed(5)} over two bars (peak ${peak.toFixed(2)}, the largest difference of a sample ${differ.toExponential(1)}, under 0.03)`);
}

const sent = songs(`${dir}/midi.json`).map((s) => s.bytes), factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);
ok(sent.length === 5, `five songs went out (${sent.length})`);
if (sent.length === 5) {
  const [fine, coarse, lowered, undone, raised] = sent;
  const list = (a) => a.map((i) => i + 1).join(' ') || 'none';
  let moved = grid(fine, A) === 64;
  for (let t = 0; t < 16 && moved; t++) for (let i = 0; i < 16; i++) {
    const f = step(factory, A, t, i), g = step(fine, A, t, i * 4);
    moved = moved && f.on === g.on && f.note === g.note && f.vel === g.vel && g.len === f.len * 4;
    for (let j = 1; j < 4; j++) moved = moved && !step(fine, A, t, i * 4 + j).on;
  }
  ok(moved, 'raised to 64 every step of A stands on the step that begins at the same moment, its length four times as many steps, nothing between');
  ok(same(coarse, factory), 'SHIFT + F5 twice asks nothing where nothing would be dropped, and the song is the factory song again, byte for byte');
  ok(grid(lowered, G) === 16 && same(stepList(lowered, G, HAT), [0]) && grid(lowered, H) === 32 && same(stepList(lowered, H, HAT), [1]),
     `the question for G does not lower H: H keeps 32 steps and its step 2; G, asked twice, has 16 and lost its step 2 (G ${grid(lowered, G)}: ${list(stepList(lowered, G, HAT))}; H ${grid(lowered, H)}: ${list(stepList(lowered, H, HAT))})`);
  ok(grid(undone, G) === 32 && same(stepList(undone, G, HAT), [0, 1]) && same(pattern(undone, H), pattern(lowered, H)),
     `F2 brings the grid and the step back (G ${grid(undone, G)}: ${list(stepList(undone, G, HAT))})`);
  ok(grid(raised, G) === 64 && same(stepList(raised, G, HAT), [0, 8, 16, 24, 32, 40, 48, 56]), `raised while it played, G has 64 steps and the hat on every eighth (${list(stepList(raised, G, HAT))})`);
}

// ---- raised while it plays: the hat on every eighth, 250 ms apart, from 21.0 s to 27.0 s - 24 of them, none twice
{
  const rms = (a, b) => { let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return Math.sqrt(s / Math.max(1, b - a)); };
  const onsets = [];
  for (let i = Math.floor(20.95 * sr); i < Math.floor(26.99 * sr); i += 12) {
    const now = rms(i, i + 96), before = rms(i - 480, i - 48);
    if (now > 0.01 && now > before * 4) { onsets.push(i / sr); i += Math.floor(0.02 * sr); }
  }
  const gaps = onsets.slice(1).map((t, i) => (t - onsets[i]) * 1000), worst = gaps.length ? Math.max(...gaps.map((g) => Math.abs(g - 250))) : -1;
  ok(onsets.length === 24 && worst < 3, `SHIFT + F6 while it plays: every eighth sounds once, before and after (${onsets.length} onsets in three bars, the gaps within ${worst.toFixed(2)} ms of 250)`);
}
console.log(failed ? `regrid FAILED (${failed})` : 'regrid PASSED');
process.exit(failed ? 1 : 0);
