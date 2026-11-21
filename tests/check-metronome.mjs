// tests/metronome.json: pattern H (empty) is cued, REC, PLAY at 0.5 s – then SHIFT + REC at 1.2 s: the metronome off
// (the display says CLICK OFF; REC had said REC ON: PATTERN LOOPS). PLAY starts at once: there is no count-in.
// The first bar is silent but for the click: on the bar line (higher) and on beat two; beats three and four have none.
import fs from 'fs';
const dir = process.argv[2] || 'out/metronome';
const w = fs.readFileSync(`${dir}/audio.wav`), sr = w.readUInt32LE(24), n = (w.length - 44) / 8;
const k = Math.exp(-2 * Math.PI * 1200 / sr); let lp = 0;
const hi = new Float32Array(n);
for (let i = 0; i < n; i++) { const v = w.readFloatLE(44 + i * 8); lp = v + (lp - v) * k; hi[i] = v - lp; }
const rms = (a, b) => { let s = 0, c = 0; for (let i = Math.floor(a * sr); i < Math.floor(b * sr); i++) { s += hi[i] * hi[i]; c++; } return Math.sqrt(s / c); };
const beat = (t) => rms(t + 0.004, t + 0.03);
const on = [0.5, 1.0].map(beat), off = [1.5, 2.0].map(beat);
const bar = on[0], other = on[1];
const good = Math.min(...on) > 0.02 && Math.max(...off) < 0.001;
console.log((good ? '  ok    ' : '  FAIL  ') + `REC + PLAY clicks on every beat (${Math.min(...on).toFixed(3)}+), SHIFT + REC turns it off (${Math.max(...off).toFixed(3)}); bar line ${bar.toFixed(3)}, beat ${other.toFixed(3)}`);
console.log(good ? 'metronome PASSED' : 'metronome FAILED');
process.exit(good ? 0 : 1);
