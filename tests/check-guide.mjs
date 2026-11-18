// tests/guide.json: the laser pad, then MENU starts the tour (35.5 s), then the laser pad again.
// The tour must sound in every part, and leave the sound it reshaped and mutated exactly as it was.
import fs from 'fs';
const dir = process.argv[2] || 'out';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const b = fs.readFileSync(`${dir}/audio.wav`), sr = b.readUInt32LE(24), n = (b.length - 44) / 4;
const x = new Float32Array(n); for (let i = 0; i < n; i++) x[i] = b.readInt16LE(44 + i * 4) / 32768;
const rms = (t0, t1) => { let s = 0; const a = Math.floor(t0 * sr), z = Math.floor(t1 * sr); for (let i = a; i < z; i++) s += x[i] * x[i]; return Math.sqrt(s / (z - a)); };
const crossings = (t0, t1) => { let c = 0; for (let i = Math.floor(t0 * sr) + 1; i < Math.floor(t1 * sr); i++) if (x[i - 1] < 0 && x[i] >= 0) c++; return c; };
const T = 1.0;   // MENU
const parts = { 'the 16 sounds': [0.2, 4.0], 'shaping a sound': [4.4, 7.6], 'MUTATE': [8.0, 10.8], 'notes on the keys': [11.4, 13.6], 'the song at intensity 0': [14.0, 17.6], 'the layers': [22.0, 25.6], 'cued patterns': [26.0, 29.6], 'the link': [30.0, 32.8] };
const quiet = Object.entries(parts).filter(([, [a, z]]) => rms(T + a, T + z) < 0.02).map(([k]) => k);
ok(!quiet.length, quiet.length ? `silent parts of the tour: ${quiet.join(', ')}` : 'every part of the tour sounds');
ok(rms(T + 22, T + 25.6) > rms(T + 14, T + 17.6) * 1.5, `the tour's intensity ramp brings layers in (${rms(T + 14, T + 17.6).toFixed(3)} -> ${rms(T + 22, T + 25.6).toFixed(3)})`);
ok(rms(T + 35.8, T + 36.4) < 0.005, 'the tour ends in silence (the song is stopped)');
const r0 = rms(0.2, 0.7), r1 = rms(37.5, 38.0), c0 = crossings(0.2, 0.7), c1 = crossings(37.5, 38.0);
ok(Math.abs(r0 - r1) < r0 * 0.03 && Math.abs(c0 - c1) <= 3, `the sound the tour changed is back as it was (level ${r0.toFixed(4)} / ${r1.toFixed(4)}, ${c0} / ${c1} cycles)`);
console.log(failed ? `guide FAILED (${failed})` : 'guide PASSED');
process.exit(failed ? 1 : 0);
