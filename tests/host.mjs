// A small host for the checks that need what the SDK's wasmtest does not do: SysEx into the firmware, and stored files
// that are there before it starts. It loads the built .wasm, hands it SysEx the way the emulator does (the exports
// vibe_wasm_sysex_buf / vibe_wasm_sysex_in), runs audio blocks and draws, and reads the firmware's MIDI out queue as
// wasmtest does. Nothing of the firmware is copied or rebuilt.
//   import { load, HEAD } from './host.mjs';
//   const device = load('build/sfx-forge.wasm');
//   const d = device();                 // nothing stored
//   const d = device(files);            // files: a Map of name -> Buffer, read and written by the firmware (kept by the caller)
//   files.full = true;                  // from then on the store takes nothing more: a write fails, as on a full device
//   d.listen(); ... d.heard()           // the left channel of every block run since listen(), as samples
import fs from 'fs';

export const HEAD = [0xF0, 0x7D, 0x56, 0x42];
export const SECOND = 375;   // blocks of 128 at 48 kHz
export const BUTTON = { F1: 0, F2: 1, F3: 2, F4: 3, F5: 4, F6: 5, SHIFT: 6, ENTER: 9, K1: 10, K2: 11, K3: 12, K4: 13, K5: 14, K6: 15,
                        RETURN: 16, HOME: 17, LEFT: 18, MENU: 19, RIGHT: 20, REC: 21, STOP: 22, PLAY: 23, DATA_PUSH: 24 };

export function load(wasmFile) {
  const mod = new WebAssembly.Module(fs.readFileSync(wasmFile));
  return function device(files = new Map()) {
    let inst;
    inst = new WebAssembly.Instance(mod, { env: {
      vibe_host_log: () => {}, vibe_host_request: () => {},
      vibe_host_store: (op, np, nl, dp, len) => {
        const name = Buffer.from(inst.exports.memory.buffer, np, nl).toString(), f = files.get(name);
        if (op === 0) { if (!f) return -1; const n = Math.min(len, f.length); new Uint8Array(inst.exports.memory.buffer, dp, n).set(f.subarray(0, n)); return n; }
        if (op === 1) { if (files.full) return -1; files.set(name, Buffer.from(new Uint8Array(inst.exports.memory.buffer, dp, len))); return 0; }
        if (op === 2) return f ? f.length : -1;
        if (op === 3) { if (!f) return -1; files.delete(name); return 0; }
        return -1;
      } } });
    const e = inst.exports, out = [];
    e.vibe_wasm_init(48000);
    let blocks = 0, tape = null;
    const drain = () => {   // (the queue's layout: the SDK's vibe_sdk.c, "SDK 9: MIDI out")
      const base = e.vibe_wasm_midi_out(), dv = new DataView(e.memory.buffer), w = dv.getUint32(base, true);
      let r = dv.getUint32(base + 4, true);
      for (; r !== w; r = (r + 1) >>> 0) {
        const q = base + 16 + (r % 256) * 6, st = dv.getUint8(q + 2), len = dv.getUint8(q + 5);
        if (!len && st === 0xF0) { const n = dv.getUint32(base + 12, true); out.push([...new Uint8Array(e.memory.buffer, base + 1552, n)]); dv.setUint32(base + 12, 0, true); }
        else out.push([st, dv.getUint8(q + 3), dv.getUint8(q + 4)].slice(0, len));
      }
      dv.setUint32(base + 4, w, true);
    };
    const d = {
      out, files,
      sysex(b) { new Uint8Array(e.memory.buffer, e.vibe_wasm_sysex_buf(), b.length).set(b); e.vibe_wasm_sysex_in(b.length); },
      event(type, id, down, value = 0, midi = 0) { e.vibe_wasm_event(type, id, down, value, midi); },
      pad(n) { e.vibe_wasm_event(1, n - 1, 1, 0.9, 0); d.run(4); e.vibe_wasm_event(1, n - 1, 0, 0, 0); d.run(4); },   // pad 1..16
      turn(id, clicks) { for (let i = 0; i < Math.abs(clicks); i++) { e.vibe_wasm_event(4, id, 0, Math.sign(clicks), 0); d.run(1); } },   // an encoder: 0-3 the four, 4 A, 5 B
      button(id) { e.vibe_wasm_event(3, id, 1, 0, 0); d.run(4); e.vibe_wasm_event(3, id, 0, 0, 0); d.run(4); },
      shift(id) { e.vibe_wasm_event(3, BUTTON.SHIFT, 1, 0, 0); d.run(2); d.button(id); e.vibe_wasm_event(3, BUTTON.SHIFT, 0, 0, 0); d.run(2); },
      run(n) { for (let i = 0; i < n; i++) { e.vibe_wasm_audio(128, 0); drain(); if (tape) tape.push(new Float32Array(e.memory.buffer, e.vibe_wasm_io() + 1024, 128).slice()); if (++blocks % 12 === 0) e.vibe_wasm_draw(); } },   // (a draw every 12 blocks, as wasmtest)
      get blocks() { return blocks; },                    // audio blocks run so far
      listen() { tape = []; },                            // from here on the left channel is kept ...
      heard() { const x = new Float32Array(tape.length * 128); tape.forEach((b, i) => x.set(b, i * 128)); return x; },   // ... and this is all of it
      take() { return out.splice(0); },
      screen() { const fb = new Uint16Array(e.memory.buffer, e.vibe_wasm_draw(), 480 * 320); let h = 2166136261; for (let i = 0; i < fb.length; i++) h = Math.imul(h ^ fb[i], 16777619) >>> 0; return h; },
      // REQUEST SONG: what comes back - the 16 sounds and the song's bytes (null when its pieces are not all there)
      song(blocksToWait = 3 * SECOND) {
        d.take(); d.sysex([...HEAD, 0x06, 0xF7]); d.run(blocksToWait);
        const got = d.take(), pieces = got.filter((m) => m[4] === 0x05), dumps = got.filter((m) => m[4] === 0x01);
        const K = pieces.length ? pieces[0][8] | pieces[0][9] << 7 : 0;
        const whole = pieces.length === K && pieces.every((m, k) => (m[5] | m[6] << 7) === k && (m[8] | m[9] << 7) === K);
        return { dumps, pieces, bytes: whole ? pieces.flatMap((m) => m.slice(10, -1)) : null };
      },
    };
    return d;
  };
}

// a packed song as its messages, in the shape of firmware 1.6.0 (pieces of 240) and in the one before (39 pieces of 200: a format-5 song)
export function pieces(song) {
  const K = Math.ceil(song.length / 240), out = [];
  for (let k = 0; k < K; k++) out.push([...HEAD, 0x05, k & 0x7F, k >> 7, song[0], K & 0x7F, K >> 7, ...song.slice(k * 240, k * 240 + 240), 0xF7]);
  return out;
}
export function oldPieces(song) {
  const out = [];
  for (let k = 0; k < 39; k++) out.push([...HEAD, 0x05, k, 39, ...song.slice(k * 200, k * 200 + 200), 0xF7]);
  return out;
}
