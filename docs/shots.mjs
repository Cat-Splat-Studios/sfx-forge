// Takes the screenshots the manual's pictures are made of, from the firmware itself.
//
//   node docs/shots.mjs [folder]          (after sh build.sh; the folder is out/doc unless another is named)
//   python docs/figures.py [folder]       then: the pictures, with their marks, into docs/img
//
// A scene is a script of key presses with the moments a picture is taken, as the tests' scripts are (tests/*.json).
// The device's display (480 x 320) is rendered by the SDK's wasmtest. The stage (960 x 540) wasmtest does not draw:
// for the scenes that want it the same script is played once more here, and the firmware's own stage is saved.
// Every scene starts from the factory state: nothing is read from a store, nothing is kept.
// Needs Node 18+, the VIBE FM SDK (.tools/vibe-fm-sdk, or VIBE_SDK) and build/sfx-forge.wasm.
import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { execFileSync } from 'child_process';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SDK = process.env.VIBE_SDK || path.join(ROOT, '.tools', 'vibe-fm-sdk');
const WASM = path.join(ROOT, 'build', 'sfx-forge.wasm');
const OUT = path.resolve(process.argv[2] || path.join(ROOT, 'out', 'doc'));
const BUTTON = { F1: 0, F2: 1, F3: 2, F4: 3, F5: 4, F6: 5, SHIFT: 6, K1: 10, K2: 11, K3: 12, K4: 13, K5: 14, K6: 15,
                 RETURN: 16, HOME: 17, LEFT: 18, MENU: 19, RIGHT: 20, REC: 21, STOP: 22, PLAY: 23 };
const ENCODER = { 1: 0, 2: 1, 3: 2, 4: 3, A: 4, B: 5, DATA: 6 };
const BAR = 2.0, STEP = BAR / 16;                    // the factory song: 120 BPM, 16 steps a bar

class Scene {
  constructor(name) { this.name = name; this.t = 0.1; this.steps = []; this.stages = 0; }
  ev(t, ...e) { this.steps.push({ t: +t.toFixed(4), ev: e }); }
  wait(s) { this.t += s; return this; }
  at(t) { this.t = t; return this; }                 // (a moment counted from the start, for what must fall on a beat)
  btn(name, gap = 0.12) { this.ev(this.t, 3, BUTTON[name], 1); this.ev(this.t + 0.04, 3, BUTTON[name], 0); this.t += gap; return this; }
  hold(name, seconds) { this.ev(this.t, 3, BUTTON[name], 1); this.ev(this.t + seconds, 3, BUTTON[name], 0); return this; }
  shift(name, gap = 0.2) {
    this.ev(this.t, 3, BUTTON.SHIFT, 1); this.ev(this.t + 0.03, 3, BUTTON[name], 1); this.ev(this.t + 0.06, 3, BUTTON[name], 0); this.ev(this.t + 0.09, 3, BUTTON.SHIFT, 0);
    this.t += gap; return this;
  }
  pad(n, gap = 0.2, vel = 0.9) { this.ev(this.t, 1, n - 1, 1, vel); this.ev(this.t + 0.04, 1, n - 1, 0, 0); this.t += gap; return this; }   // n: 1..16, as on the device
  turn(enc, detents, gap = 0.15) {                   // one detent at a time, as a hand turns it
    for (let k = 0; k < Math.abs(detents); k++) this.ev(this.t + k * 0.006, 4, ENCODER[enc], 0, Math.sign(detents));
    this.t += Math.abs(detents) * 0.006 + gap; return this;
  }
  padKey(n, note, gap = 0.3) {                       // a step's pad held, a key played: the step gets that note
    this.ev(this.t, 1, n - 1, 1, 0.9); this.ev(this.t + 0.05, 9, note, 1, 0.9); this.ev(this.t + 0.12, 9, note, 0, 0); this.ev(this.t + 0.16, 1, n - 1, 0, 0);
    this.t += gap; return this;
  }
  shot(name) { this.steps.push({ t: +this.t.toFixed(4), shot: name }); this.t += 0.02; return this; }
  stage(name) { this.steps.push({ t: +this.t.toFixed(4), stage: name }); this.stages++; this.t += 0.02; return this; }
  script() { return { seconds: +(this.t + 0.3).toFixed(2), steps: this.steps.slice().sort((a, b) => a.t - b.t) }; }
}

const scenes = [];
const scene = (name) => { const s = new Scene(name); scenes.push(s); return s; };

// ---- a sound: its pages, a knob in the hand ------------------------------------------------------------------------------
scene('sound').pad(9).wait(1.2).shot('sfx-sound')
  .btn('K2').wait(0.1).shot('env').btn('K3').wait(0.1).shot('lfo').btn('K4').wait(0.1).shot('fx')
  .btn('K1').btn('K1').btn('K1').turn(1, -8).wait(0.2).shot('touch');

// ---- the tutorial of a sound effect: a preset, a mutation, the filter, the envelope ---------------------------------------
scene('sfx').pad(9).wait(1.2).shot('t-sfx-1')
  .btn('F2').wait(0.4).shot('t-sfx-2').wait(2.4)
  .btn('K1').btn('K1').turn(1, -25).wait(1.6).shot('t-sfx-3')
  .btn('K2').turn(2, 30).wait(1.6).shot('t-sfx-4');

// ---- a wavetable sound's fourth page ---------------------------------------------------------------------------------------
scene('wave').shift('K1').turn('DATA', 15).btn('K1').btn('K1').btn('K1').wait(2.5).shot('music-wave');

// ---- automation: the knob turned last, two points, RAMP - on the device, on the stage, and the mark on the sound's page --------
scene('auto').shift('K1').turn('DATA', 6).btn('K1').turn(2, 2)
  .btn('K5').btn('K5').btn('K5')
  .pad(1).turn(2, -40).pad(16).turn(2, 45).btn('F3')
  .wait(2.6).shot('lanes-auto').stage('auto-stage')
  .btn('K1').btn('K1').wait(0.2).shot('lanes-sound');

// ---- the mixer (a sound muted, the song playing), the master effects, the link -----------------------------------------------
scene('mix').btn('K4').btn('K4').pad(9).turn('DATA', -2).btn('PLAY')
  .wait(BAR + 0.8).shot('mix').btn('K4').wait(0.2).shot('master').btn('K6').wait(0.2).shot('link');

// ---- SEQ while pattern A repeats: the page the manual explains, and the stage beside it --------------------------------------
{ const s = scene('seq').btn('K5').turn('DATA', 6).at(1.0).btn('PLAY');
  s.at(1.0 + BAR + 13 * STEP + 0.07).shot('seq-playing').stage('seq-playing-stage'); }

// ---- loop a pattern and play into it: LOOP in the top line, a pattern that waits, a take, F2 -----------------------------------
{ const s = scene('loop').btn('K5').turn('DATA', 2).wait(0.3).shot('loop-1');
  const t0 = 1.0;                                    // PLAY: pattern A repeats
  s.at(t0).btn('PLAY').at(t0 + 0.6);
  for (let k = 0; k < 6; k++) s.btn('RIGHT', 0.05);  // RIGHT six times: G, the first empty one, waits for the bar line
  s.at(t0 + 1.33).shot('loop-2');             // (at a moment the waiting pattern's tile is lit: it blinks)
  s.at(t0 + BAR + 0.3).btn('REC');                   // G plays now; REC: the pads are the kit
  for (const beat of [0, 1, 2, 3]) s.at(t0 + 2 * BAR + beat * 4 * STEP + 0.02).pad(3);              // a kick on every beat
  for (const beat of [1, 3]) s.at(t0 + 3 * BAR + beat * 4 * STEP + 0.02).pad(2);                    // the snare on 2 and 4
  s.at(t0 + 4 * BAR + 0.02).pad(3);                  // (the kick once more: the tiles show the sound played last)
  s.at(t0 + 4 * BAR + 2 * STEP + 0.05).shot('loop-3');
  s.at(t0 + 4 * BAR + 1.0).btn('F2').wait(0.2).shot('loop-4'); }

// ---- the tutorial of a beat: an empty pattern, FILL, the pads as steps, a pad held and a key, PLAY ------------------------------
{ const s = scene('beat').btn('K5');
  for (let k = 0; k < 6; k++) s.btn('RIGHT', 0.08);
  s.turn('DATA', 2).btn('F3').wait(0.3).shot('t-beat-1').wait(1.0)
    .turn('DATA', -1).pad(5, 0.5).pad(13, 0.5).wait(1.2).shot('t-beat-2')
    .turn('DATA', 11).btn('F3', 0.3).btn('F3').wait(0.3).shot('t-beat-3').wait(1.0)
    .turn('DATA', -6).padKey(1, 40).padKey(4, 40).padKey(7, 43).padKey(11, 45).padKey(14, 47).wait(2.4).shot('t-beat-4');
  const t0 = s.t + 0.1;
  s.at(t0).btn('PLAY').at(t0 + BAR + 6 * STEP + 0.06).shot('t-beat-5'); }

// ---- the SONG page while the arrangement plays its second bar -------------------------------------------------------------------
scene('song').btn('K5').btn('K5').turn('DATA', 6).at(1.0).btn('PLAY').at(1.0 + BAR + 0.8).shot('song');

// ---- the display, with the SDK's wasmtest ---------------------------------------------------------------------------------------
fs.mkdirSync(path.join(OUT, 'scripts'), { recursive: true });
if (!fs.existsSync(WASM)) { console.error('no build/sfx-forge.wasm: run sh build.sh first'); process.exit(1); }
const wasmtest = path.join(SDK, 'tools', 'wasmtest.mjs');
if (!fs.existsSync(wasmtest)) { console.error('VIBE FM SDK not found at ' + SDK); process.exit(1); }
let made = 0;
for (const s of scenes) {
  const file = path.join(OUT, 'scripts', s.name + '.json');
  fs.writeFileSync(file, JSON.stringify(s.script()));
  execFileSync(process.execPath, [wasmtest, WASM, file, OUT], { stdio: ['ignore', 'ignore', 'inherit'] });
  made += s.steps.filter((x) => x.shot).length;
  if (s.stages) made += stageShots(s);
}
console.log(`${made} screenshots written to ${path.relative(ROOT, OUT).split(path.sep).join('/')}`);

// ---- the stage: the same script once more, block by block as wasmtest plays it, and the firmware's stage saved -------------------
function stageShots(s) {
  const sr = 48000, block = 128, files = new Map();
  let inst;
  inst = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(WASM)), { env: {
    vibe_host_log: () => {}, vibe_host_request: () => {},
    vibe_host_store: (op, np, nl, dp, len) => {      // nothing stored: the factory state, as in wasmtest without "store"
      const name = Buffer.from(inst.exports.memory.buffer, np, nl).toString(), f = files.get(name);
      if (op === 0) { if (!f) return -1; const n = Math.min(len, f.length); new Uint8Array(inst.exports.memory.buffer, dp, n).set(f.subarray(0, n)); return n; }
      if (op === 1) { files.set(name, Buffer.from(new Uint8Array(inst.exports.memory.buffer, dp, len))); return 0; }
      if (op === 2) return f ? f.length : -1;
      return -1;
    } } });
  const e = inst.exports, script = s.script(), steps = script.steps.slice(), total = Math.ceil(script.seconds * sr / block);
  let n = 0;
  e.vibe_wasm_init(sr);
  e.vibe_wasm_event(19, 8, 0, 0, 0);                 // the stage is shown
  for (let b = 0; b < total; b++) {
    const due = [];
    while (steps.length && steps[0].t <= b * block / sr) {
      const x = steps.shift();
      if (x.ev) {
        if (e.vibe_wasm_time) { const vt = new Uint32Array(e.memory.buffer, e.vibe_wasm_time(), 8), f = b * block; vt[0] = f >>> 0; vt[1] = Math.floor(f / 4294967296); vt[2] = 0; vt[3] = 0; vt[4] = 0; }
        e.vibe_wasm_event(x.ev[0], x.ev[1], x.ev[2], x.ev[0] === 3 ? 0 : x.ev[3] ?? 0, x.ev[4] ?? 0);
      }
      if (x.stage) due.push(x.stage);
    }
    e.vibe_wasm_audio(block, 0);
    if (b % 12 === 0 || due.length) e.vibe_wasm_draw();
    for (const name of due) {
      const info = new DataView(e.memory.buffer, e.vibe_wasm_stage_info()), w = info.getUint16(0, true), h = info.getUint16(2, true);
      png(new Uint16Array(e.memory.buffer, e.vibe_wasm_stage_draw(), w * h), w, h, path.join(OUT, name + '.png')); n++;
    }
  }
  return n;
}

function png(px, w, h, file) {                       // RGB565, as the firmware draws, to an 8-bit RGB PNG
  const raw = Buffer.alloc(h * (w * 3 + 1));
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const v = px[y * w + x], o = y * (w * 3 + 1) + 1 + x * 3; raw[o] = (v >> 11) << 3; raw[o + 1] = ((v >> 5) & 63) << 2; raw[o + 2] = (v & 31) << 3; }
  const tab = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; tab[n] = c >>> 0; }
  const crc = (b) => { let r = -1; for (const x of b) r = tab[(r ^ x) & 255] ^ (r >>> 8); return (r ^ -1) >>> 0; };
  const chunk = (ty, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(ty), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}
