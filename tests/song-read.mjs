// What a test's midi.json holds of the song: every SEND (SONG page, F5) is 16 sounds and the packed song in 39 pieces.
// songs(file) gives each song that went out whole, in order: { t, bytes } with the 7699 bytes of core/sfx_song.h.
import fs from 'fs';
export const SONG_BYTES = 7699, HEAD = 147, PATTERNS = 8, TRACKS = 16, STEPS = 16, LANES = 24, TRACK_BYTES = 3 + 2 * STEPS;
const AUT = HEAD + PATTERNS * TRACKS * TRACK_BYTES;

export function songs(file) {
  const msgs = JSON.parse(fs.readFileSync(file, 'utf8')).filter((m) => m.bytes[0] === 0xF0 && m.bytes[1] === 0x7D && m.bytes[2] === 0x56 && m.bytes[3] === 0x42 && m.bytes[4] === 5);
  const out = []; let cur = null;
  for (const m of msgs) {
    if (m.bytes[5] === 0) cur = { t: m.t, bytes: [], next: 0 };
    if (!cur || m.bytes[5] !== cur.next || m.bytes[6] !== 39) { cur = null; continue; }
    cur.bytes.push(...m.bytes.slice(7, -1)); cur.next++;
    if (cur.next === 39) { if (cur.bytes.length === SONG_BYTES) out.push({ t: cur.t, bytes: cur.bytes }); cur = null; }
  }
  return out;
}
export const chain = (s) => s[6];
export const tempo = (s) => s[2] << 7 | s[3];
export const arrLen = (s) => s[66];
export const arr = (s) => s.slice(67, 67 + arrLen(s));
// the steps of sound t in pattern p, a bit a step
export const steps = (s, p, t) => { const o = HEAD + (p * TRACKS + t) * TRACK_BYTES; return s[o] | s[o + 1] << 7 | s[o + 2] << 14; };
// all of pattern p: steps, notes, velocities and lengths of the 16 sounds, then its automation points
export const pattern = (s, p) => [...s.slice(HEAD + p * TRACKS * TRACK_BYTES, HEAD + (p + 1) * TRACKS * TRACK_BYTES), ...s.slice(AUT + p * LANES * STEPS, AUT + (p + 1) * LANES * STEPS)];
export const empty = (s, p) => { for (let t = 0; t < TRACKS; t++) if (steps(s, p, t)) return false; return s.slice(AUT + p * LANES * STEPS, AUT + (p + 1) * LANES * STEPS).every((v) => !v); };
export const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
// a float WAV of the SDK's wasmtest ("float": true): rms(t0, t1) of the left channel
export function wav(file) {
  const w = fs.readFileSync(file), sr = w.readUInt32LE(24), n = (w.length - 44) / 8;
  return (a, b) => { let s = 0, c = 0; for (let i = Math.max(0, Math.floor(a * sr)); i < Math.min(n, Math.floor(b * sr)); i++) { const v = w.readFloatLE(44 + i * 8); s += v * v; c++; } return Math.sqrt(s / Math.max(1, c)); };
}
