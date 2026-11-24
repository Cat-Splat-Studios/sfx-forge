// Takes the device pictures the wiki's Device pages add to those of docs/img, from the firmware itself.
//
//   node wiki/figures/dv_shots.mjs        (from the repository root, after sh build.sh)
//
// It writes wiki/img/dv-*.png and nothing else. A scene is a list of key presses with the moments a picture is
// taken, as in docs/shots.mjs; here the firmware is run in this process, so no script file and no folder of raw
// shots is needed. Every scene starts from the factory state: nothing is read from a store, nothing is kept.
// Needs Node 18+ and build/sfx-forge.wasm.
import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const WASM = path.join(ROOT, 'build', 'sfx-forge.wasm');
const OUT = path.join(ROOT, 'wiki', 'img');

// the device's buttons and encoders, by the number the firmware knows them by (as in docs/shots.mjs)
// prettier-ignore
const BUTTON = {
  F1: 0, F2: 1, F3: 2, F4: 3, F5: 4, F6: 5,
  SHIFT: 6, ENTER: 9,
  K1: 10, K2: 11, K3: 12, K4: 13, K5: 14, K6: 15,
  RETURN: 16, HOME: 17, LEFT: 18, MENU: 19, RIGHT: 20,
  REC: 21, STOP: 22, PLAY: 23,
};
const ENCODER = { 1: 0, 2: 1, 3: 2, 4: 3, A: 4, B: 5, DATA: 6 };

// A scene: the time the next press falls on, and the events and pictures so far. An event is what the host hands the
// firmware: [type, id, down, value] - type 1 a pad, 3 a button, 4 an encoder.
class Scene {
  constructor() {
    this.t = 0.1;
    this.steps = [];
  }
  ev(t, ...e) {
    this.steps.push({ t, ev: e });
  }
  wait(s) {
    this.t += s;
    return this;
  }
  btn(name, gap = 0.12) {
    this.ev(this.t, 3, BUTTON[name], 1);
    this.ev(this.t + 0.04, 3, BUTTON[name], 0);
    this.t += gap;
    return this;
  }
  shift(name, gap = 0.2) {
    this.ev(this.t, 3, BUTTON.SHIFT, 1);
    this.ev(this.t + 0.03, 3, BUTTON[name], 1);
    this.ev(this.t + 0.06, 3, BUTTON[name], 0);
    this.ev(this.t + 0.09, 3, BUTTON.SHIFT, 0);
    this.t += gap;
    return this;
  }
  pad(n, gap = 0.2) {
    this.ev(this.t, 1, n - 1, 1, 0.9);
    this.ev(this.t + 0.04, 1, n - 1, 0, 0);
    this.t += gap;
    return this;
  }
  turn(enc, detents, gap = 0.15) {
    for (let k = 0; k < Math.abs(detents); k++) {
      this.ev(this.t + k * 0.006, 4, ENCODER[enc], 0, Math.sign(detents));
    }
    this.t += Math.abs(detents) * 0.006 + gap;
    return this;
  }
  shot(name) {
    this.steps.push({ t: this.t, shot: name });
    this.t += 0.02;
    return this;
  }
}

const scenes = [];
const scene = () => {
  const s = new Scene();
  scenes.push(s);
  return s;
};

// ==== the scenes ===================================================================================================

// HOME: the controls of the page that is open - on a sound page and on SEQ
scene().pad(9).wait(1.2).btn('HOME').wait(0.3).shot('dv-home-sound');
scene().btn('K5').wait(0.3).btn('HOME').wait(0.3).shot('dv-home-seq');

// MENU: the tour, a second after it began
scene().btn('MENU').wait(1.0).shot('dv-tour');

// the five engines: the TONE page (K1 once: a scene starts on OSC) of one sound of each, where knobs A and B have the engine's names.
// SFX kit: pad 1 EXPLOSION, 5 COIN, 9 LASER, 13 FOOTSTEP. MUSIC kit (SHIFT + ENTER): pad 16 STRINGS
scene().btn('K1').pad(1).wait(2.6).shot('dv-engine-impact');
scene().btn('K1').pad(5).wait(2.6).shot('dv-engine-blip');
scene().btn('K1').pad(9).wait(2.6).shot('dv-engine-fm');
scene().btn('K1').pad(13).wait(2.6).shot('dv-engine-foley');
scene().shift('ENTER').btn('K1').pad(16).wait(2.6).shot('dv-engine-wavetable');

// layers: PERFORM while pattern A repeats, at the intensity the device comes with (100 %) and 28 clicks lower (44 %)
scene().btn('K5').btn('K5').btn('K5').btn('K5').turn('DATA', 2).btn('PLAY').wait(2.6).shot('dv-layers-100')
  .turn(3, -28).wait(2.5).shot('dv-layers-44');

// the same on the SONG page: pad 12 is the fourth of the eight intensity pads (50 %)
scene().btn('K5').btn('K5').pad(12).wait(2.5).shot('dv-song-intensity');

// the factory reset asks first; SAVE says what it did
scene().shift('MENU').wait(0.3).shot('dv-reset-ask');
scene().btn('F6').wait(0.3).shot('dv-saved');

// ==== run them =====================================================================================================

const SR = 48000;
const BLOCK = 128;
const W = 480;
const H = 320;

function run(s) {
  // the firmware, with the three functions it asks its host for; its store is a map in memory
  const files = new Map();
  let inst;
  inst = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(WASM)), {
    env: {
      vibe_host_log: () => {},
      vibe_host_request: () => {},
      vibe_host_store: (op, np, nl, dp, len) => {
        const name = Buffer.from(inst.exports.memory.buffer, np, nl).toString();
        const f = files.get(name);
        if (op === 0) {
          if (!f) {
            return -1;
          }
          const n = Math.min(len, f.length);
          new Uint8Array(inst.exports.memory.buffer, dp, n).set(f.subarray(0, n));
          return n;
        }
        if (op === 1) {
          files.set(name, Buffer.from(new Uint8Array(inst.exports.memory.buffer, dp, len)));
          return 0;
        }
        if (op === 2) {
          return f ? f.length : -1;
        }
        if (op === 3) {
          return files.delete(name) ? 0 : -1;
        }
        return -1;
      },
    },
  });
  const e = inst.exports;
  const steps = s.steps.slice().sort((a, b) => a.t - b.t);
  const total = Math.ceil(((s.t + 0.3) * SR) / BLOCK);
  let n = 0;
  e.vibe_wasm_init(SR);

  // block by block: the events that are due, a block of audio, a draw every 12 blocks, the pictures that are due
  for (let b = 0; b < total; b++) {
    const due = [];
    while (steps.length && steps[0].t <= (b * BLOCK) / SR) {
      const x = steps.shift();
      if (x.ev) {
        // the SDK's clock, where the firmware reads the time of an event: the frame count as 64 bits, then zeros
        if (e.vibe_wasm_time) {
          const vt = new Uint32Array(e.memory.buffer, e.vibe_wasm_time(), 8);
          const f = b * BLOCK;
          vt[0] = f >>> 0;
          vt[1] = Math.floor(f / 4294967296);
          vt[2] = 0;
          vt[3] = 0;
          vt[4] = 0;
        }
        // (a button's value is 0, as in every host)
        e.vibe_wasm_event(x.ev[0], x.ev[1], x.ev[2], x.ev[0] === 3 ? 0 : (x.ev[3] ?? 0), 0);
      }
      if (x.shot) {
        due.push(x.shot);
      }
    }
    e.vibe_wasm_audio(BLOCK, 0);
    if (b % 12 === 0 || due.length) {
      const fb = e.vibe_wasm_draw();
      for (const name of due) {
        png(new Uint16Array(e.memory.buffer, fb, W * H), path.join(OUT, name + '.png'));
        n++;
      }
    }
  }
  return n;
}

// Writes the display's pixels (RGB565, 480 x 320) as an 8-bit RGB PNG twice as large, each pixel as four: the size
// of the pictures in docs/img.
function png(px, file) {
  const S = 2;
  const raw = Buffer.alloc(H * S * (W * S * 3 + 1));
  for (let y = 0; y < H * S; y++) {
    for (let x = 0; x < W * S; x++) {
      const v = px[Math.floor(y / S) * W + Math.floor(x / S)];
      const o = y * (W * S * 3 + 1) + 1 + x * 3;
      raw[o] = (v >> 11) << 3;
      raw[o + 1] = ((v >> 5) & 63) << 2;
      raw[o + 2] = (v & 31) << 3;
    }
  }
  // the CRC-32 every chunk of a PNG ends with
  const tab = [];
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) {
      c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    tab[n] = c >>> 0;
  }
  const crc = (b) => {
    let r = -1;
    for (const x of b) {
      r = tab[(r ^ x) & 255] ^ (r >>> 8);
    }
    return (r ^ -1) >>> 0;
  };
  const chunk = (type, data) => {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const typeAndData = Buffer.concat([Buffer.from(type), data]);
    const sum = Buffer.alloc(4);
    sum.writeUInt32BE(crc(typeAndData));
    return Buffer.concat([length, typeAndData, sum]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(W * S, 0);
  header.writeUInt32BE(H * S, 4);
  header[8] = 8;
  header[9] = 2;
  fs.writeFileSync(
    file,
    Buffer.concat([
      Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
      chunk('IHDR', header),
      chunk('IDAT', zlib.deflateSync(raw)),
      chunk('IEND', Buffer.alloc(0)),
    ]),
  );
}

fs.mkdirSync(OUT, { recursive: true });
let made = 0;
for (const s of scenes) {
  made += run(s);
}
console.log(`${made} pictures written to wiki/img`);
