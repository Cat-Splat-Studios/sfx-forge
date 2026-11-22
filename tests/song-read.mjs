// What a test's midi.json holds of the song: every SEND (SONG page, F5) is 16 sounds and the packed song in pieces.
// songs(file) gives each song that went out whole, in order: { t, bytes } with the packed bytes of core/sfx_song.h.
// Both shapes of the song message are read (firmware 1.6.0: pieces of 240 with a 14-bit number and count; before: 39
// pieces of 200), and both kinds of packed song - format 6 (a block a pattern, only the rows that are used, a grid of
// 16, 32 or 64 steps) and format 5 (7699 bytes, every row, 16 steps) - into one model, so a check says the same of
// either.
//
// What it holds: songs() (the songs in a midi.json), model() (a packed song as arrays), the small questions the checks
// ask of a packed song (format, tempo, grid, steps, stepList, step, pattern, empty, same), and wav() / samples() for
// the audio. In all of them s is a packed song (an array of bytes), p a pattern (0-7: A-H), t a sound (0-15) and i a
// step counted from 0. The layout of a packed song is described in core/sfx_song.h.
import fs from 'fs';

// the bytes of a packed song before its patterns begin
export const HEAD = 147;
export const PATTERNS = 8;
export const TRACKS = 16;
export const LANES = 24;
export const SONG_BYTES_V5 = 7699;

export function songs(file) {
  // the song messages: F0 7D 56 42 05 ...
  const msgs = JSON.parse(fs.readFileSync(file, 'utf8')).filter(
    (m) => m.bytes[0] === 0xf0 && m.bytes[1] === 0x7d && m.bytes[2] === 0x56 && m.bytes[3] === 0x42 && m.bytes[4] === 5,
  );

  // put together piece by piece: cur is the song that is being collected (null: none)
  const out = [];
  let cur = null;
  for (const m of msgs) {
    // this piece's number k, the count of pieces it names, and its data - in the old shape or in the new
    const b = m.bytes;
    const old = b[6] === 39;
    const k = old ? b[5] : b[5] | (b[6] << 7);
    const count = old ? 39 : b[8] | (b[9] << 7);
    const data = old ? b.slice(7, -1) : b.slice(10, -1);

    // piece 0 begins a song; a piece out of order, or of another count, ends the one that was being collected
    if (k === 0) {
      cur = { t: m.t, bytes: [], next: 0, count };
    }
    if (!cur || k !== cur.next || count !== cur.count) {
      cur = null;
      continue;
    }
    cur.bytes.push(...data);
    cur.next++;

    // the last piece: the song is kept if it can be read
    if (cur.next === count) {
      if (model(cur.bytes)) {
        out.push({ t: cur.t, bytes: cur.bytes });
      }
      cur = null;
    }
  }
  return out;
}

// ---- the model: for each pattern its grid (16, 32, 64) and, for every step of that grid, what each sound and lane
// has there:
//   model(s)[p] = { grid, on[t][i], note[t][i], vel[t][i], len[t][i], aut[lane][i] }
// It is null for bytes that are not a song. A song is read once (the cache).
const cache = new WeakMap();
export function model(s) {
  if (cache.has(s)) {
    return cache.get(s);
  }
  let m = null;

  // an empty pattern of g steps
  const blank = (g) => ({
    grid: g,
    on: [...Array(TRACKS)].map(() => Array(g).fill(0)),
    note: [...Array(TRACKS)].map(() => Array(g).fill(0)),
    vel: [...Array(TRACKS)].map(() => Array(g).fill(5)),
    len: [...Array(TRACKS)].map(() => Array(g).fill(0)),
    aut: [...Array(LANES)].map(() => Array(g).fill(0)),
  });

  if (s[0] === 5 && s.length === SONG_BYTES_V5) {
    // ---- format 5: every sound of every pattern has 35 bytes (3 of step bits, 16 notes, 16 of velocity and
    // length), then every lane of every pattern has 16 points
    m = [];
    const AUT = HEAD + PATTERNS * TRACKS * 35;
    for (let p = 0; p < PATTERNS; p++) {
      const pt = blank(16);
      for (let t = 0; t < TRACKS; t++) {
        const o = HEAD + (p * TRACKS + t) * 35;
        const bits = s[o] | (s[o + 1] << 7) | (s[o + 2] << 14);
        for (let i = 0; i < 16; i++) {
          pt.on[t][i] = (bits >> i) & 1;
          pt.note[t][i] = s[o + 3 + i];
          pt.vel[t][i] = s[o + 19 + i] & 7;
          pt.len[t][i] = (s[o + 19 + i] >> 3) & 15;
        }
      }
      for (let l = 0; l < LANES; l++) {
        for (let i = 0; i < 16; i++) {
          pt.aut[l][i] = s[AUT + (p * LANES + l) * 16 + i];
        }
      }
      m.push(pt);
    }
  } else if (s[0] === 6 && s.length >= 211) {
    // ---- format 6: a block a pattern, read from offset o on. Its 8 bytes of head say the grid, which sounds have
    // a row (16 bits) and which lanes are used (24 bits); then only those rows and lanes follow.
    m = [];
    let o = HEAD;
    for (let p = 0; p < PATTERNS && m; p++) {
      if (o + 8 > s.length || s[o] > 2) {
        m = null;
        break;
      }
      // g: the steps of this pattern; nb: the bytes its step bits take (7 bits a byte)
      const g = 16 << s[o];
      const nb = Math.ceil(g / 7);
      const pt = blank(g);
      const rows = s[o + 1] | (s[o + 2] << 7) | (s[o + 3] << 14);
      const lanes = s[o + 4] | (s[o + 5] << 7) | (s[o + 6] << 14) | (s[o + 7] << 21);
      o += 8;

      // a row: the step bits, then a note, a velocity and a length for every step
      for (let t = 0; t < TRACKS; t++) {
        if (!((rows >> t) & 1)) {
          continue;
        }
        for (let i = 0; i < g; i++) {
          pt.on[t][i] = (s[o + Math.floor(i / 7)] >> i % 7) & 1;
          pt.note[t][i] = s[o + nb + i];
          pt.vel[t][i] = s[o + nb + g + i];
          pt.len[t][i] = s[o + nb + 2 * g + i];
        }
        o += nb + 3 * g;
      }

      // a lane: a point for every step
      for (let l = 0; l < LANES; l++) {
        if (!((lanes >> l) & 1)) {
          continue;
        }
        for (let i = 0; i < g; i++) {
          pt.aut[l][i] = s[o + i];
        }
        o += g;
      }
      m.push(pt);
    }
    // the song must end where its last pattern ends
    if (m && o !== s.length) {
      m = null;
    }
  }
  cache.set(s, m);
  return m;
}

// ---- what the checks ask of a packed song
// its head: the format, the chain, the tempo (14 bits), the arrangement's length in bars and its bars
export const format = (s) => s[0];
export const chain = (s) => s[6];
export const tempo = (s) => (s[2] << 7) | s[3];
export const arrLen = (s) => s[66];
export const arr = (s) => s.slice(67, 67 + arrLen(s));

// the grid of pattern p: its steps a full bar (16, 32, 64)
export const grid = (s, p) => model(s)[p].grid;

// the steps of sound t in pattern p: at 16 steps a bit a step (a number, as ever); stepList gives them at any grid,
// counted from 0
export const stepList = (s, p, t) => model(s)[p].on[t].flatMap((v, i) => (v ? [i] : []));
export const steps = (s, p, t) => {
  if (grid(s, p) !== 16) {
    throw new Error(`pattern ${p} has ${grid(s, p)} steps: use stepList`);
  }
  return model(s)[p].on[t].reduce((a, v, i) => a | (v << i), 0);
};

// what a step keeps: its note, its velocity (0..7), its length in steps of the pattern's grid
export const step = (s, p, t, i) => {
  const pt = model(s)[p];
  return { on: pt.on[t][i], note: pt.note[t][i], vel: pt.vel[t][i], len: pt.len[t][i] };
};

// all of pattern p as numbers, to compare two patterns (of two songs, of either format): its grid, steps, notes,
// velocities and lengths of the 16 sounds, then its automation points
export const pattern = (s, p) => {
  const pt = model(s)[p];
  return [pt.grid, ...pt.on.flat(), ...pt.note.flat(), ...pt.vel.flat(), ...pt.len.flat(), ...pt.aut.flat()];
};

// has pattern p no step and no automation point? Are two lists of numbers the same?
export const empty = (s, p) => {
  const pt = model(s)[p];
  return pt.on.flat().every((v) => !v) && pt.aut.flat().every((v) => !v);
};
export const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

// ---- the audio
// a float WAV of the SDK's wasmtest ("float": true): rms(t0, t1) of the left channel
export function wav(file) {
  const w = fs.readFileSync(file);
  const sr = w.readUInt32LE(24);
  const n = (w.length - 44) / 8;
  return (a, b) => {
    let s = 0;
    let c = 0;
    for (let i = Math.max(0, Math.floor(a * sr)); i < Math.min(n, Math.floor(b * sr)); i++) {
      const v = w.readFloatLE(44 + i * 8);
      s += v * v;
      c++;
    }
    return Math.sqrt(s / Math.max(1, c));
  };
}

// the same file as samples of the left channel, and its rate: for a check that looks for onsets or compares two
// stretches
export function samples(file) {
  const w = fs.readFileSync(file);
  const sr = w.readUInt32LE(24);
  const n = (w.length - 44) / 8;
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    x[i] = w.readFloatLE(44 + i * 8);
  }
  return { sr, x };
}
