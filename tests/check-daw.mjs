// tests/daw.json: the sequencer was started on its own clock (120), then a DAW plays from 1.0 s at 140 BPM.
// From then on the kicks (every quarter note) must sit on the DAW's beats.
import fs from 'fs';
const dir = process.argv[2] || 'out';
const b = fs.readFileSync(`${dir}/audio.wav`), sr = b.readUInt32LE(24), n = (b.length - 44) / 4;
const x = new Float32Array(n); for (let i = 0; i < n; i++) x[i] = b.readInt16LE(44 + i * 4) / 32768;
const rms = (t0, t1) => { let s = 0; const a = Math.floor(t0 * sr), z = Math.floor(t1 * sr); for (let i = a; i < z; i++) s += x[i] * x[i]; return Math.sqrt(s / (z - a)); };
const beat = 60 / 140;
const on = [...Array(8)].map((_, k) => rms(1 + k * beat, 1 + k * beat + 0.06)), off = [...Array(8)].map((_, k) => rms(1 + k * beat + beat * 0.3, 1 + k * beat + beat * 0.45));
const good = Math.min(...on) > 0.08 && Math.max(...off) < Math.min(...on) * 0.6;
console.log((good ? '  ok    ' : '  FAIL  ') + `the sequencer follows the DAW's song position at 140 BPM (kicks ${Math.min(...on).toFixed(2)}+, between ${Math.max(...off).toFixed(2)})`);
console.log(good ? 'daw PASSED' : 'daw FAILED');
process.exit(good ? 0 : 1);
