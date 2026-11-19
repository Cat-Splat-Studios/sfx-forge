// tests/reset.json: the laser pad, then RANDOM and SAVE, the pad again, SHIFT + MENU twice (factory reset), the pad again.
import fs from 'fs';
const dir = process.argv[2] || 'out/reset';
const w = fs.readFileSync(`${dir}/audio.wav`), sr = w.readUInt32LE(24);
const rms = (a, b) => { let s = 0, n = 0; for (let i = Math.floor(a * sr); i < Math.floor(b * sr); i++) { const v = w.readFloatLE(44 + i * 8); s += v * v; n++; } return Math.sqrt(s / n); };
const made = rms(0.2, 0.7), changed = rms(3.0, 3.5), back = rms(6.0, 6.5);
const good = Math.abs(changed - made) > made * 0.05 && Math.abs(back - made) < made * 0.01;
console.log((good ? '  ok    ' : '  FAIL  ') + `SHIFT + MENU twice puts a changed, saved sound back as it came (${made.toFixed(4)} -> ${changed.toFixed(4)} -> ${back.toFixed(4)})`);
console.log(good ? 'reset PASSED' : 'reset FAILED');
process.exit(good ? 0 : 1);
