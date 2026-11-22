// What a test's midi.json holds of the song: every SEND (SONG page, F5) is 16 sounds and the packed song in pieces.
// songs(file) gives each song that went out whole, in order: { t, bytes } with the packed bytes of core/sfx_song.h.
// Both shapes of the song message are read (firmware 1.6.0: pieces of 240 with a 14-bit number and count; before: 39
// pieces of 200), and both kinds of packed song - format 6 (a block a pattern, only the rows that are used, a grid of
// 16, 32 or 64 steps) and format 5 (7699 bytes, every row, 16 steps) - into one model, so a check says the same of either.
import fs from 'fs';
export const HEAD = 147, PATTERNS = 8, TRACKS = 16, LANES = 24, SONG_BYTES_V5 = 7699;

export function songs(file) {
  const msgs = JSON.parse(fs.readFileSync(file, 'utf8')).filter((m) => m.bytes[0] === 0xF0 && m.bytes[1] === 0x7D && m.bytes[2] === 0x56 && m.bytes[3] === 0x42 && m.bytes[4] === 5);
  const out = []; let cur = null;
  for (const m of msgs) {
    const b = m.bytes, old = b[6] === 39;
    const k = old ? b[5] : b[5] | b[6] << 7, K = old ? 39 : b[8] | b[9] << 7, data = old ? b.slice(7, -1) : b.slice(10, -1);
    if (k === 0) cur = { t: m.t, bytes: [], next: 0, K };
    if (!cur || k !== cur.next || K !== cur.K) { cur = null; continue; }
    cur.bytes.push(...data); cur.next++;
    if (cur.next === K) { if (model(cur.bytes)) out.push({ t: cur.t, bytes: cur.bytes }); cur = null; }
  }
  return out;
}

// ---- the model: for each pattern its grid (16, 32, 64) and, for every step of that grid, what each sound and lane has there
const cache = new WeakMap();
export function model(s) {
  if (cache.has(s)) return cache.get(s);
  let m = null;
  const blank = (g) => ({ grid: g, on: [...Array(TRACKS)].map(() => Array(g).fill(0)), note: [...Array(TRACKS)].map(() => Array(g).fill(0)),
                          vel: [...Array(TRACKS)].map(() => Array(g).fill(5)), len: [...Array(TRACKS)].map(() => Array(g).fill(0)), aut: [...Array(LANES)].map(() => Array(g).fill(0)) });
  if (s[0] === 5 && s.length === SONG_BYTES_V5) {
    m = [];
    const AUT = HEAD + PATTERNS * TRACKS * 35;
    for (let p = 0; p < PATTERNS; p++) {
      const pt = blank(16);
      for (let t = 0; t < TRACKS; t++) {
        const o = HEAD + (p * TRACKS + t) * 35, bits = s[o] | s[o + 1] << 7 | s[o + 2] << 14;
        for (let i = 0; i < 16; i++) { pt.on[t][i] = bits >> i & 1; pt.note[t][i] = s[o + 3 + i]; pt.vel[t][i] = s[o + 19 + i] & 7; pt.len[t][i] = s[o + 19 + i] >> 3 & 15; }
      }
      for (let l = 0; l < LANES; l++) for (let i = 0; i < 16; i++) pt.aut[l][i] = s[AUT + (p * LANES + l) * 16 + i];
      m.push(pt);
    }
  } else if (s[0] === 6 && s.length >= 211) {
    m = []; let o = HEAD;
    for (let p = 0; p < PATTERNS && m; p++) {
      if (o + 8 > s.length || s[o] > 2) { m = null; break; }
      const g = 16 << s[o], nb = Math.ceil(g / 7), pt = blank(g);
      const rows = s[o + 1] | s[o + 2] << 7 | s[o + 3] << 14, lanes = s[o + 4] | s[o + 5] << 7 | s[o + 6] << 14 | s[o + 7] << 21;
      o += 8;
      for (let t = 0; t < TRACKS; t++) {
        if (!(rows >> t & 1)) continue;
        for (let i = 0; i < g; i++) { pt.on[t][i] = s[o + Math.floor(i / 7)] >> (i % 7) & 1; pt.note[t][i] = s[o + nb + i]; pt.vel[t][i] = s[o + nb + g + i]; pt.len[t][i] = s[o + nb + 2 * g + i]; }
        o += nb + 3 * g;
      }
      for (let l = 0; l < LANES; l++) { if (!(lanes >> l & 1)) continue; for (let i = 0; i < g; i++) pt.aut[l][i] = s[o + i]; o += g; }
      m.push(pt);
    }
    if (m && o !== s.length) m = null;
  }
  cache.set(s, m);
  return m;
}
export const format = (s) => s[0];
export const chain = (s) => s[6];
export const tempo = (s) => s[2] << 7 | s[3];
export const arrLen = (s) => s[66];
export const arr = (s) => s.slice(67, 67 + arrLen(s));
// the grid of pattern p: its steps a full bar (16, 32, 64)
export const grid = (s, p) => model(s)[p].grid;
// the steps of sound t in pattern p: at 16 steps a bit a step (a number, as ever); stepList gives them at any grid, counted from 0
export const stepList = (s, p, t) => model(s)[p].on[t].flatMap((v, i) => v ? [i] : []);
export const steps = (s, p, t) => { if (grid(s, p) !== 16) throw new Error(`pattern ${p} has ${grid(s, p)} steps: use stepList`); return model(s)[p].on[t].reduce((a, v, i) => a | v << i, 0); };
// what a step keeps: its note, its velocity (0..7), its length in steps of the pattern's grid
export const step = (s, p, t, i) => { const pt = model(s)[p]; return { on: pt.on[t][i], note: pt.note[t][i], vel: pt.vel[t][i], len: pt.len[t][i] }; };
// all of pattern p as numbers, to compare two patterns (of two songs, of either format): its grid, steps, notes, velocities and lengths of the 16 sounds, then its automation points
export const pattern = (s, p) => { const pt = model(s)[p]; return [pt.grid, ...pt.on.flat(), ...pt.note.flat(), ...pt.vel.flat(), ...pt.len.flat(), ...pt.aut.flat()]; };
export const empty = (s, p) => { const pt = model(s)[p]; return pt.on.flat().every((v) => !v) && pt.aut.flat().every((v) => !v); };
export const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
// a float WAV of the SDK's wasmtest ("float": true): rms(t0, t1) of the left channel
export function wav(file) {
  const w = fs.readFileSync(file), sr = w.readUInt32LE(24), n = (w.length - 44) / 8;
  return (a, b) => { let s = 0, c = 0; for (let i = Math.max(0, Math.floor(a * sr)); i < Math.min(n, Math.floor(b * sr)); i++) { const v = w.readFloatLE(44 + i * 8); s += v * v; c++; } return Math.sqrt(s / Math.max(1, c)); };
}
// the same file as samples of the left channel, and its rate: for a check that looks for onsets or compares two stretches
export function samples(file) {
  const w = fs.readFileSync(file), sr = w.readUInt32LE(24), n = (w.length - 44) / 8, x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = w.readFloatLE(44 + i * 8);
  return { sr, x };
}
