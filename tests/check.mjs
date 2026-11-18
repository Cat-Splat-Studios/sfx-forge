// Checks the test performance (tests/tour.json) rendered by the SDK's wasmtest: every pad made a sound, telemetry moved
// the pitch of the held sound, and the MIDI the firmware sent follows the protocol in README.md.
import fs from 'fs';
const dir = process.argv[2] || 'out';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };

const b = fs.readFileSync(`${dir}/audio.wav`), sr = b.readUInt32LE(24), n = (b.length - 44) / 4;
const x = new Float32Array(n); for (let i = 0; i < n; i++) x[i] = b.readInt16LE(44 + i * 4) / 32768;
const rms = (t0, t1) => { let s = 0; const a = Math.floor(t0 * sr), z = Math.floor(t1 * sr); for (let i = a; i < z; i++) s += x[i] * x[i]; return Math.sqrt(s / (z - a)); };
const crossings = (t0, t1) => { let c = 0; for (let i = Math.floor(t0 * sr) + 1; i < Math.floor(t1 * sr); i++) if (x[i - 1] < 0 && x[i] >= 0) c++; return c / (t1 - t0); };
const silent = [];
for (let p = 0; p < 16; p++) if (rms(0.2 + 0.6 * p, 0.5 + 0.6 * p) < 0.01) silent.push(p + 1);
ok(!silent.length, silent.length ? `silent pads: ${silent.join(', ')}` : 'all 16 pads sound');
ok(rms(0, 0.19) < 1e-4, 'silence before the first pad');
const f0 = crossings(10.25, 10.4), f1 = crossings(12.4, 12.6);
ok(f1 > f0 * 2.5, `telemetry CC 10 raises the held sound's pitch (${f0.toFixed(0)} -> ${f1.toFixed(0)} Hz)`);
ok(rms(15.2, 15.5) < 0.01, 'the held sound ends when its pad is let go');

const midi = JSON.parse(fs.readFileSync(`${dir}/midi.json`, 'utf8')).map((m) => m.bytes);
const notes = midi.filter((m) => m[0] === 0x90).map((m) => m[1]);
ok([...Array(16)].every((_, i) => notes.includes(36 + i)), 'pads send notes 36-51 on channel 1');
ok(midi.some((m) => m[0] === 0xB0 && m[1] === 20 && m[2] === 69), 'LINK page: encoder 1 sends CC 20');
const sx = midi.find((m) => m[0] === 0xF0);
const head = sx && sx.slice(0, 5).join() === [0xF0, 0x7D, 0x56, 0x42, 0x01].join() && sx.length === 20 && sx[19] === 0xF7;
ok(head, 'EXPORT sends F0 7D 56 42 01 <slot> <13 bytes> F7');
if (head) {
  const p = sx.slice(6, 19);
  ok(p[0] === 1 && p[1] < 4 && p.every((v) => v < 128) && p[12] === (p.slice(0, 12).reduce((a, v) => a + v, 0) & 0x7F), `the patch is valid (slot ${sx[5]}, engine ${p[1]}, checksum ok)`);
}
console.log(failed ? `FAILED (${failed})` : 'tour PASSED');
process.exit(failed ? 1 : 0);
