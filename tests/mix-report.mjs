// Measures the demo song's mix, since a test cannot listen: plays the whole arrangement once with every instrument
// soloed in turn (MIX page, SHIFT + pad), and once all together, and prints for each its level, its peak and how much
// of it is high (above about 4 kHz). Not part of sh build.sh: run it by hand when the factory song's mix is changed.
//
//   node tests/mix-report.mjs            (after sh build.sh; needs the SDK in .tools/vibe-fm-sdk)
import fs from 'fs';
import path from 'path';
import { execFileSync } from 'child_process';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sdk = process.env.VIBE_SDK || path.join(root, '.tools/vibe-fm-sdk');

// the instruments of the demo song: the sound's number (pad - 1) and its name in the report
const NAMES = {
  2: 'KICK',
  1: 'SNARE',
  12: 'HI-HAT',
  6: 'FM BASS',
  7: 'FM KEYS',
  5: 'BRASS',
  13: 'BELLS',
  15: 'STRINGS',
  14: 'CHOIR',
};
const SECONDS = 34; // 16 bars at 120 BPM, and the tail

// Plays the song with sound `solo` soloed (-1: all together) and measures what comes out:
// { rms, peak, high, side } - the level of the loudest 2 seconds, the largest sample, the share of that level above
// 4 kHz, and the right channel against the left in dB.
function run(solo) {
  // ---- the script for wasmtest
  const dir = path.join(root, 'out', 'mix', solo < 0 ? 'all' : String(solo));
  fs.mkdirSync(dir, { recursive: true });
  // a button pressed and let go
  const b = (t, id) => [
    { t, ev: [3, id, 1] },
    { t: t + 0.03, ev: [3, id, 0] },
  ];
  // K4 twice: the MIX page
  const steps = [...b(0.1, 13), ...b(0.2, 13)];
  // SHIFT + the sound's pad: solo
  if (solo >= 0) {
    steps.push(
      { t: 0.3, ev: [3, 6, 1] },
      { t: 0.35, ev: [1, solo, 1, 0.9] },
      { t: 0.4, ev: [1, solo, 0, 0] },
      { t: 0.45, ev: [3, 6, 0] },
    );
  }
  steps.push(...b(0.6, 23)); // PLAY
  const script = path.join(dir, 'script.json');
  fs.writeFileSync(script, JSON.stringify({ seconds: SECONDS, float: true, steps }));
  execFileSync('node', [path.join(sdk, 'tools/wasmtest.mjs'), path.join(root, 'build/sfx-forge.wasm'), script, dir], {
    stdio: 'pipe',
  });

  // ---- the audio it rendered: a float stereo WAV, as the left and the right channel
  const w = fs.readFileSync(path.join(dir, 'audio.wav'));
  const sr = w.readUInt32LE(24);
  const n = (w.length - 44) / 8;
  const l = new Float32Array(n);
  const r = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    l[i] = w.readFloatLE(44 + i * 8);
    r[i] = w.readFloatLE(48 + i * 8);
  }

  // ---- the level: the loudest 2 seconds. The highs: what a one-pole high-pass at 4 kHz leaves.
  // sq and hq: the square of every sample of the mono sum, and of its highs
  const k = Math.exp((-2 * Math.PI * 4000) / sr);
  let lp = 0;
  let peak = 0;
  const win = 2 * sr;
  const sq = new Float64Array(n);
  const hq = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const m = (l[i] + r[i]) * 0.5;
    lp = m + (lp - m) * k;
    const h = m - lp;
    sq[i] = m * m;
    hq[i] = h * h;
    peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
  }

  // a window of 2 seconds moved over them: s and h are the sums inside it, best and bestH those of the loudest window
  let s = 0;
  let h = 0;
  let best = 0;
  let bestH = 0;
  let all = 0;
  let allH = 0;
  for (let i = 0; i < n; i++) {
    s += sq[i];
    h += hq[i];
    all += sq[i];
    allH += hq[i];
    if (i >= win) {
      s -= sq[i - win];
      h -= hq[i - win];
    }
    if (s > best) {
      best = s;
      bestH = h;
    }
  }

  // ---- the energy of the left and of the right channel
  let sl = 0;
  let sr2 = 0;
  for (let i = 0; i < n; i++) {
    sl += l[i] * l[i];
    sr2 += r[i] * r[i];
  }

  return {
    rms: Math.sqrt(best / win),
    peak,
    high: Math.sqrt(bestH / Math.max(best, 1e-12)),
    side: 10 * Math.log10((sr2 + 1e-12) / (sl + 1e-12)),
  };
}

// ---- the report: a line an instrument, then all together
const db = (x) => (20 * Math.log10(Math.max(x, 1e-6))).toFixed(1).padStart(6);
console.log('                level    peak   highs   right-left');
const rows = Object.keys(NAMES)
  .map(Number)
  .map((t) => [NAMES[t], run(t)]);
rows.push(['ALL TOGETHER', run(-1)]);
for (const [name, m] of rows) {
  console.log(
    `${name.padEnd(13)} ${db(m.rms)} dB ${db(m.peak)} dB ${(m.high * 100).toFixed(0).padStart(4)} %   ` +
      `${m.side.toFixed(1).padStart(5)} dB`,
  );
}
