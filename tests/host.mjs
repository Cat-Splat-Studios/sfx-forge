// A small host for the checks that need what the SDK's wasmtest does not do: SysEx into the firmware, and stored files
// that are there before it starts. It loads the built .wasm, hands it SysEx the way the emulator does (the exports
// vibe_wasm_sysex_buf / vibe_wasm_sysex_in), runs audio blocks and draws, and reads the firmware's MIDI out queue as
// wasmtest does. Nothing of the firmware is copied or rebuilt.
//
//   import { load, HEAD } from './host.mjs';
//   const device = load('build/sfx-forge.wasm');
//   const d = device();                 // nothing stored
//   const d = device(files);            // files: a Map of name -> Buffer, read and written by the firmware (kept by
//                                       // the caller)
//   files.full = true;                  // from then on the store takes nothing more: a write fails, as on a full
//                                       // device
//   d.listen(); ... d.heard()           // the left channel of every block run since listen(), as samples
//
// What a device `d` can do is the object at the end of load(): sysex, event, pad, turn, button, shift, run, listen,
// heard, take, screen, song. The numbers in e.vibe_wasm_event(type, ...) are the SDK's event types: EVENT below.
//
// Two things a check relies on without saying so every time:
//   - K5 pressed again goes on to its next page: SEQ, SONG, AUTO, PERFORM, and SEQ again (K4: FX, MIX, MASTER).
//   - d.song() runs three seconds of blocks while it waits for the answer: the song plays on for that long.
import fs from 'fs';

// how every SysEx message of SFX FORGE begins
export const HEAD = [0xf0, 0x7d, 0x56, 0x42];
// a second, in blocks of 128 at 48 kHz
export const SECOND = 375;
// the SDK's event types (vibe.h), as vibe_wasm_event and d.event take them
export const EVENT = { PAD: 1, BUTTON: 3, ENCODER: 4 };
// the encoders, by their number: 0-3 are the four over the display
export const ENCODER = { A: 4, B: 5, DATA: 6 };
// the device's buttons, by the number the firmware knows them by
// prettier-ignore
export const BUTTON = {
  F1: 0, F2: 1, F3: 2, F4: 3, F5: 4, F6: 5,
  SHIFT: 6, ENTER: 9,
  K1: 10, K2: 11, K3: 12, K4: 13, K5: 14, K6: 15,
  RETURN: 16, HOME: 17, LEFT: 18, MENU: 19, RIGHT: 20,
  REC: 21, STOP: 22, PLAY: 23, DATA_PUSH: 24,
};

// Compiles the firmware once and gives a function that starts a device: device(files) -> d.
export function load(wasmFile) {
  const mod = new WebAssembly.Module(fs.readFileSync(wasmFile));
  return function device(files = new Map()) {
    // ---- the firmware, with the three functions it asks its host for
    let inst;
    inst = new WebAssembly.Instance(mod, {
      env: {
        vibe_host_log: () => {},
        vibe_host_request: () => {},
        // the store: a file's name and a place for its data in the firmware's memory. The answer is a length or 0,
        // or -1 for "no".
        vibe_host_store: (op, namePtr, nameLen, dataPtr, len) => {
          const name = Buffer.from(inst.exports.memory.buffer, namePtr, nameLen).toString();
          const file = files.get(name);
          // 0: read
          if (op === 0) {
            if (!file) {
              return -1;
            }
            const n = Math.min(len, file.length);
            new Uint8Array(inst.exports.memory.buffer, dataPtr, n).set(file.subarray(0, n));
            return n;
          }
          // 1: write
          if (op === 1) {
            if (files.full) {
              return -1;
            }
            files.set(name, Buffer.from(new Uint8Array(inst.exports.memory.buffer, dataPtr, len)));
            return 0;
          }
          // 2: the size of a file
          if (op === 2) {
            return file ? file.length : -1;
          }
          // 3: delete
          if (op === 3) {
            if (!file) {
              return -1;
            }
            files.delete(name);
            return 0;
          }
          return -1;
        },
      },
    });
    const e = inst.exports;
    e.vibe_wasm_init(48000);

    // the MIDI messages the firmware has sent, the audio blocks run so far, and the audio kept since listen()
    const out = [];
    let blocks = 0;
    let tape = null;

    // ---- moves what the firmware put in its MIDI out queue into `out`: a short message as its bytes, a SysEx whole
    // (the queue's layout: the SDK's vibe_sdk.c, "SDK 9: MIDI out")
    const drain = () => {
      const base = e.vibe_wasm_midi_out();
      const view = new DataView(e.memory.buffer);
      const writeIndex = view.getUint32(base, true);
      let readIndex = view.getUint32(base + 4, true);
      for (; readIndex !== writeIndex; readIndex = (readIndex + 1) >>> 0) {
        const entry = base + 16 + (readIndex % 256) * 6;
        const status = view.getUint8(entry + 2);
        const len = view.getUint8(entry + 5);
        if (!len && status === 0xf0) {
          // a SysEx: its bytes lie in a buffer of their own, and its length is set to 0 when it has been read
          const n = view.getUint32(base + 12, true);
          out.push([...new Uint8Array(e.memory.buffer, base + 1552, n)]);
          view.setUint32(base + 12, 0, true);
        } else {
          out.push([status, view.getUint8(entry + 3), view.getUint8(entry + 4)].slice(0, len));
        }
      }
      view.setUint32(base + 4, writeIndex, true);
    };

    // ---- the device, as a check drives it
    const d = {
      out,
      files,

      // a SysEx message into the firmware, as the emulator hands it over
      sysex(b) {
        new Uint8Array(e.memory.buffer, e.vibe_wasm_sysex_buf(), b.length).set(b);
        e.vibe_wasm_sysex_in(b.length);
      },

      // any event of the SDK, with no block run
      event(type, id, down, value = 0, midi = 0) {
        e.vibe_wasm_event(type, id, down, value, midi);
      },

      // pad 1..16, hit and let go
      pad(n) {
        e.vibe_wasm_event(EVENT.PAD, n - 1, 1, 0.9, 0);
        d.run(4);
        e.vibe_wasm_event(EVENT.PAD, n - 1, 0, 0, 0);
        d.run(4);
      },

      // an encoder: 0-3 the four, then ENCODER.A, ENCODER.B, ENCODER.DATA
      turn(id, clicks) {
        for (let i = 0; i < Math.abs(clicks); i++) {
          e.vibe_wasm_event(EVENT.ENCODER, id, 0, Math.sign(clicks), 0);
          d.run(1);
        }
      },

      // a button pressed and let go
      button(id) {
        e.vibe_wasm_event(EVENT.BUTTON, id, 1, 0, 0);
        d.run(4);
        e.vibe_wasm_event(EVENT.BUTTON, id, 0, 0, 0);
        d.run(4);
      },

      // SHIFT + a button
      shift(id) {
        e.vibe_wasm_event(EVENT.BUTTON, BUTTON.SHIFT, 1, 0, 0);
        d.run(2);
        d.button(id);
        e.vibe_wasm_event(EVENT.BUTTON, BUTTON.SHIFT, 0, 0, 0);
        d.run(2);
      },

      // n audio blocks (a draw every 12 blocks, as wasmtest)
      run(n) {
        for (let i = 0; i < n; i++) {
          e.vibe_wasm_audio(128, 0);
          drain();
          if (tape) {
            tape.push(new Float32Array(e.memory.buffer, e.vibe_wasm_io() + 1024, 128).slice());
          }
          if (++blocks % 12 === 0) {
            e.vibe_wasm_draw();
          }
        }
      },

      // audio blocks run so far
      get blocks() {
        return blocks;
      },

      // from here on the left channel is kept ...
      listen() {
        tape = [];
      },

      // ... and this is all of it
      heard() {
        const x = new Float32Array(tape.length * 128);
        tape.forEach((b, i) => x.set(b, i * 128));
        return x;
      },

      // the MIDI messages sent since the last take()
      take() {
        return out.splice(0);
      },

      // a hash of the display (480 x 320 pixels): two displays that show the same have the same hash
      screen() {
        const fb = new Uint16Array(e.memory.buffer, e.vibe_wasm_draw(), 480 * 320);
        let h = 2166136261;
        for (let i = 0; i < fb.length; i++) {
          h = Math.imul(h ^ fb[i], 16777619) >>> 0;
        }
        return h;
      },

      // REQUEST SONG: what comes back - the 16 sounds and the song's bytes (null when its pieces are not all there)
      song(blocksToWait = 3 * SECOND) {
        d.take();
        d.sysex([...HEAD, 0x06, 0xf7]);
        d.run(blocksToWait);
        const got = d.take();
        const pieces = got.filter((m) => m[4] === 0x05);
        const dumps = got.filter((m) => m[4] === 0x01);
        // the count every piece names, and whether the pieces are all there, in order
        const count = pieces.length ? pieces[0][8] | (pieces[0][9] << 7) : 0;
        const whole =
          pieces.length === count &&
          pieces.every((m, k) => (m[5] | (m[6] << 7)) === k && (m[8] | (m[9] << 7)) === count);
        return { dumps, pieces, bytes: whole ? pieces.flatMap((m) => m.slice(10, -1)) : null };
      },
    };
    return d;
  };
}

// a packed song as its messages, in the shape of firmware 1.6.0 (pieces of 240) and in the one before (39 pieces of
// 200: a format-5 song):
//   F0 7D 56 42 05 <piece lo> <piece hi> <format> <pieces lo> <pieces hi> <up to 240 bytes> F7
//   F0 7D 56 42 05 <piece> 39 <200 bytes> F7
export function pieces(song) {
  const K = Math.ceil(song.length / 240);
  const out = [];
  for (let k = 0; k < K; k++) {
    out.push([...HEAD, 0x05, k & 0x7f, k >> 7, song[0], K & 0x7f, K >> 7, ...song.slice(k * 240, k * 240 + 240), 0xf7]);
  }
  return out;
}

export function oldPieces(song) {
  const out = [];
  for (let k = 0; k < 39; k++) {
    out.push([...HEAD, 0x05, k, 39, ...song.slice(k * 200, k * 200 + 200), 0xf7]);
  }
  return out;
}
