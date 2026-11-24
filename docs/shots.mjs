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
//
// How it reads, top to bottom: the Scene class (the words a scene is written in), the scenes themselves - this is
// the part to change when the manual wants another picture -, then the loop that hands every scene to wasmtest, and
// the two helpers stageShots() and png().
import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { execFileSync } from 'child_process';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SDK = process.env.VIBE_SDK || path.join(ROOT, '.tools', 'vibe-fm-sdk');
const WASM = path.join(ROOT, 'build', 'sfx-forge.wasm');
const OUT = path.resolve(process.argv[2] || path.join(ROOT, 'out', 'doc'));

// the device's buttons and encoders, by the number the firmware knows them by
// prettier-ignore
const BUTTON = {
  F1: 0, F2: 1, F3: 2, F4: 3, F5: 4, F6: 5,
  SHIFT: 6, ENTER: 9,
  K1: 10, K2: 11, K3: 12, K4: 13, K5: 14, K6: 15,
  RETURN: 16, HOME: 17, LEFT: 18, MENU: 19, RIGHT: 20,
  REC: 21, STOP: 22, PLAY: 23,
};
const ENCODER = { 1: 0, 2: 1, 3: 2, 4: 3, A: 4, B: 5, DATA: 6 };

// the factory song: 120 BPM, 16 sixteenths a bar (in seconds)
const BAR = 2.0;
const STEP = BAR / 16;

// A scene: a name, the time `t` the next press falls on, and the events and pictures so far. Every method adds its
// events at `t`, moves `t` on by its gap, and gives the scene back, so that a scene reads as one chain of presses.
// An event is what wasmtest hands the firmware: [type, id, down, value] - type 1 a pad, 3 a button, 4 an encoder,
// 9 a key of the keyboard.
class Scene {
  constructor(name) {
    this.name = name;
    this.t = 0.1;
    this.steps = [];
    this.stages = 0;
  }

  // one event at time t
  ev(t, ...e) {
    this.steps.push({ t: +t.toFixed(4), ev: e });
  }

  // time passes
  wait(s) {
    this.t += s;
    return this;
  }

  // (a moment counted from the start, for what must fall on a beat)
  at(t) {
    this.t = t;
    return this;
  }

  // a button pressed and let go
  btn(name, gap = 0.12) {
    this.ev(this.t, 3, BUTTON[name], 1);
    this.ev(this.t + 0.04, 3, BUTTON[name], 0);
    this.t += gap;
    return this;
  }

  // a button held for so many seconds (the time does not move on: what follows happens while it is held)
  hold(name, seconds) {
    this.ev(this.t, 3, BUTTON[name], 1);
    this.ev(this.t + seconds, 3, BUTTON[name], 0);
    return this;
  }

  // SHIFT + a button
  shift(name, gap = 0.2) {
    this.ev(this.t, 3, BUTTON.SHIFT, 1);
    this.ev(this.t + 0.03, 3, BUTTON[name], 1);
    this.ev(this.t + 0.06, 3, BUTTON[name], 0);
    this.ev(this.t + 0.09, 3, BUTTON.SHIFT, 0);
    this.t += gap;
    return this;
  }

  // a pad hit; n: 1..16, as on the device
  pad(n, gap = 0.2, vel = 0.9) {
    this.ev(this.t, 1, n - 1, 1, vel);
    this.ev(this.t + 0.04, 1, n - 1, 0, 0);
    this.t += gap;
    return this;
  }

  // an encoder turned: one detent at a time, as a hand turns it
  turn(enc, detents, gap = 0.15) {
    for (let k = 0; k < Math.abs(detents); k++) {
      this.ev(this.t + k * 0.006, 4, ENCODER[enc], 0, Math.sign(detents));
    }
    this.t += Math.abs(detents) * 0.006 + gap;
    return this;
  }

  // SHIFT held, an encoder turned
  shiftTurn(enc, detents, gap = 0.2) {
    const n = Math.abs(detents);
    this.ev(this.t, 3, BUTTON.SHIFT, 1);
    for (let k = 0; k < n; k++) {
      this.ev(this.t + 0.03 + k * 0.006, 4, ENCODER[enc], 0, Math.sign(detents));
    }
    this.ev(this.t + 0.06 + n * 0.006, 3, BUTTON.SHIFT, 0);
    this.t += n * 0.006 + gap;
    return this;
  }

  // a step's pad held, a key played: the step gets that note
  padKey(n, note, gap = 0.3) {
    this.ev(this.t, 1, n - 1, 1, 0.9);
    this.ev(this.t + 0.05, 9, note, 1, 0.9);
    this.ev(this.t + 0.12, 9, note, 0, 0);
    this.ev(this.t + 0.16, 1, n - 1, 0, 0);
    this.t += gap;
    return this;
  }

  // SHIFT + a pad; n: 1..16
  shiftPad(n, gap = 0.3) {
    this.ev(this.t, 3, BUTTON.SHIFT, 1);
    this.ev(this.t + 0.03, 1, n - 1, 1, 0.9);
    this.ev(this.t + 0.07, 1, n - 1, 0, 0);
    this.ev(this.t + 0.1, 3, BUTTON.SHIFT, 0);
    this.t += gap;
    return this;
  }

  // a picture of the display, taken by wasmtest
  shot(name) {
    this.steps.push({ t: +this.t.toFixed(4), shot: name });
    this.t += 0.02;
    return this;
  }

  // a picture of the stage, taken by stageShots() below
  stage(name) {
    this.steps.push({ t: +this.t.toFixed(4), stage: name });
    this.stages++;
    this.t += 0.02;
    return this;
  }

  // the scene as a script for wasmtest: its length and its steps in the order of their times
  script() {
    return { seconds: +(this.t + 0.3).toFixed(2), steps: this.steps.slice().sort((a, b) => a.t - b.t) };
  }
}

// all scenes, in the order they are written; scene(name) begins one
const scenes = [];
const scene = (name) => {
  const s = new Scene(name);
  scenes.push(s);
  return s;
};

// ==== the scenes ===================================================================================================
// One line of a chain is one stretch of key presses up to a picture: the lines are kept as they are written
// (prettier-ignore), a call a line would hide that.

// ---- a sound: its pages, a knob in the hand -----------------------------------------------------------------------
// prettier-ignore
scene('sound').pad(9).wait(1.2).shot('sfx-sound')
  .btn('K2').wait(0.1).shot('env').btn('K3').wait(0.1).shot('lfo').btn('K4').wait(0.1).shot('fx')
  .btn('K1').btn('K1').btn('K1').turn(1, -8).wait(0.2).shot('touch');

// ---- the tutorial of a sound effect: a preset, a mutation, the filter, the envelope -------------------------------
// prettier-ignore
scene('sfx').pad(9).wait(1.2).shot('t-sfx-1')
  .btn('F2').wait(0.4).shot('t-sfx-2').wait(2.4)
  .btn('K1').btn('K1').turn(1, -25).wait(1.6).shot('t-sfx-3')
  .btn('K2').turn(2, 30).wait(1.6).shot('t-sfx-4');

// ---- a wavetable sound's fourth page ------------------------------------------------------------------------------
scene('wave').shift('ENTER').turn('DATA', 15).btn('K1').btn('K1').btn('K1').wait(2.5).shot('music-wave');

// ---- automation: the knob turned last, two points, RAMP - on the device, on the stage, and the mark on the sound's
// page
// prettier-ignore
scene('auto').shift('ENTER').turn('DATA', 6).btn('K1').turn(2, 2)
  .btn('K5').btn('K5').btn('K5')
  .pad(1).turn(2, -40).pad(16).turn(2, 45).btn('F3')
  .wait(2.6).shot('lanes-auto').stage('auto-stage')
  .btn('K1').btn('K1').wait(0.2).shot('lanes-sound');

// ---- the mixer (a sound muted, the song playing), the master effects, the link ------------------------------------
// prettier-ignore
scene('mix').btn('K4').btn('K4').pad(9).turn('DATA', -2).btn('PLAY')
  .wait(BAR + 0.8).shot('mix').btn('K4').wait(0.2).shot('master').btn('K6').wait(0.2).shot('link');

// ---- SEQ while pattern A repeats: the page the manual explains, and the stage beside it ---------------------------
{
  const s = scene('seq').btn('K5').turn('DATA', 6).at(1.0).btn('PLAY');
  // prettier-ignore
  s.at(1.0 + BAR + 13 * STEP + 0.07).shot('seq-playing').stage('seq-playing-stage');
}

// ---- loop a pattern and play into it: LOOP in the top line, a pattern that waits, a take, F2 ----------------------
{
  const s = scene('loop').btn('K5').turn('DATA', 2).wait(0.3).shot('loop-1');

  // PLAY: pattern A repeats
  const t0 = 1.0;
  // prettier-ignore
  s.at(t0).btn('PLAY').at(t0 + 0.6);

  // RIGHT six times: G, the first empty one, waits for the bar line
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.05);
  }
  // (at a moment the waiting pattern's tile is lit: it blinks)
  s.at(t0 + 1.33).shot('loop-2');

  // G plays now; REC: the pads are the kit
  s.at(t0 + BAR + 0.3).btn('REC');

  // a kick on every beat
  for (const beat of [0, 1, 2, 3]) {
    s.at(t0 + 2 * BAR + beat * 4 * STEP + 0.02).pad(3);
  }
  // the snare on 2 and 4
  for (const beat of [1, 3]) {
    s.at(t0 + 3 * BAR + beat * 4 * STEP + 0.02).pad(2);
  }
  // (the kick once more: the tiles show the sound played last)
  s.at(t0 + 4 * BAR + 0.02).pad(3);
  s.at(t0 + 4 * BAR + 2 * STEP + 0.05).shot('loop-3');

  // prettier-ignore
  s.at(t0 + 4 * BAR + 1.0).btn('F2').wait(0.2).shot('loop-4');
}

// ---- the tutorial of a beat: an empty pattern, FILL, the pads as steps, a pad held and a key, PLAY ----------------
{
  const s = scene('beat').btn('K5');
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // prettier-ignore
  s.turn('DATA', 2).btn('F3').wait(0.3).shot('t-beat-1').wait(1.0)
    .turn('DATA', -1).pad(5, 0.5).pad(13, 0.5).wait(1.2).shot('t-beat-2')
    .turn('DATA', 11).btn('F3', 0.3).btn('F3').wait(0.3).shot('t-beat-3').wait(1.0)
    .turn('DATA', -6).padKey(1, 40).padKey(4, 40).padKey(7, 43).padKey(11, 45).padKey(14, 47)
    .wait(2.4).shot('t-beat-4');
  const t0 = s.t + 0.1;
  // prettier-ignore
  s.at(t0).btn('PLAY').at(t0 + BAR + 6 * STEP + 0.06).shot('t-beat-5');
}

// ---- the SONG page while the arrangement plays its second bar -----------------------------------------------------
// prettier-ignore
scene('song').btn('K5').btn('K5').turn('DATA', 6).at(1.0).btn('PLAY').at(1.0 + BAR + 0.8).shot('song');

// ---- a finer grid: hats on the eighths, 1/32, the second 16 steps and a roll in them, 1/64, the question on the way
// back
{
  const s = scene('grid').btn('K5');

  // G, the first empty pattern
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // prettier-ignore
  s.turn('DATA', 2).btn('F3', 0.3)                   // the kick on every quarter
    .turn('DATA', 10).btn('F3', 0.3).btn('F3', 0.3)  // the hi-hat on every eighth
    .shift('F6').wait(0.3).shot('grid-1').wait(2.6)  // SHIFT + F6: GRID 1/32. The pads are steps 1-16 of 32
    // SHIFT + RIGHT: steps 17-32
    .shift('RIGHT');

  // the last beat, every thirty-second: a roll
  for (const p of [10, 11, 12, 14, 15, 16]) {
    s.pad(p, 0.1);
  }
  // prettier-ignore
  s.wait(0.2).shot('grid-2').wait(2.6).stage('grid-stage')
    .shift('F6').wait(0.3).shot('grid-3').wait(2.6)  // 1/64: the roll is every other step of 49-64
    .shift('F5').wait(2.6)                           // back to 1/32: nothing stands between, nothing to ask
    .shift('F5').wait(0.3).shot('grid-4')            // to 1/16: four steps of the roll would go. It asks
    .shift('F5', 0.4).btn('F2').wait(2.6)            // again: done. F2: the roll is back, at 1/32
    // SONG: G has 32 in its corner
    .btn('K5').wait(0.2).shot('grid-song');
}

// ---- a take at 1/64: the tiles go with the bar, the bar strip stays, the hint is red ------------------------------
{
  const s = scene('grid-take').btn('K5');
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // the snare, 1/64, REC
  s.turn('DATA', 1).shift('F6').shift('F6').btn('REC');

  // PLAY at 3.0 s; S64 is one step of 64, in seconds
  const t0 = 3.0;
  const S64 = BAR / 64;
  s.at(t0).btn('PLAY');

  // a roll into the third beat, each hit a little harder than the one before
  for (const g of [16, 19, 22, 24, 26, 27, 28, 29, 30, 31]) {
    s.at(t0 + g * S64 + 0.004).pad(2, 0.02, 0.5 + (g - 16) * 0.03);
  }
  s.at(t0 + BAR + 27 * S64 + 0.01).shot('grid-take');
}

// ---- PERFORM on the demo song: as it opens, playing, two sounds muted, a pattern that waits, the kit with REC -----
{
  // K5 four times; DATA: the kick
  const s = scene('perform').btn('K5').btn('K5').btn('K5').btn('K5').turn('DATA', 2).wait(0.3).shot('perform-open');

  // PLAY: pattern A repeats. The pads are the kick's steps
  const t0 = 1.5;
  s.at(t0).btn('PLAY');
  s.at(t0 + BAR + 9 * STEP + 0.07).shot('perform');

  // F3 MUTES: pads 7 and 16, the bass and the strings
  // prettier-ignore
  s.at(t0 + 2 * BAR + 0.1).btn('F3').pad(7).pad(16);
  s.at(t0 + 2 * BAR + 13 * STEP + 0.07).shot('perform-mutes');

  // the two pads again: all are heard. F4 PATTERN, pad 2: B waits for the bar line
  // prettier-ignore
  s.at(t0 + 3 * BAR + 0.1).pad(7).pad(16).btn('F4').pad(2);
  // (at a moment the waiting pattern's letter is lit: it blinks)
  s.at(t0 + 3 * BAR + 11 * STEP + 0.05).shot('perform-pattern');

  // B plays. REC: the pads are the kit. The cowbell (pad 4) on the off-beats, for a bar
  s.at(t0 + 4 * BAR + 0.3).btn('REC');
  for (const beat of [0, 1, 2, 3]) {
    s.at(t0 + 5 * BAR + (beat * 4 + 2) * STEP + 0.02).pad(4);
  }
  s.at(t0 + 6 * BAR + 5 * STEP + 0.07).shot('perform-sounds');
}

// ---- PERFORM, a beat from nothing: FILL, SHIFT + a pad, PLAY, a mute, COPY, the bar line, a part played in -------
{
  const s = scene('perform-beat').btn('K5').btn('K5').btn('K5').btn('K5');

  // G, the first empty pattern
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // prettier-ignore
  s.turn('DATA', 2).btn('F5').wait(0.3).shot('t-perform-1').wait(2.6)       // the kick: F5 FILL, every quarter
    .shiftPad(2).pad(5, 0.5).pad(13, 0.5).wait(1.2).shot('t-perform-2')     // the snare: SHIFT + its pad; 5 and 13
    // the hi-hat: SHIFT + its pad; FILL twice
    .shiftPad(13).btn('F5', 0.3).btn('F5').wait(0.3).shot('t-perform-3').wait(2.6);

  // PLAY: G repeats
  const t0 = s.t + 0.1;
  // prettier-ignore
  s.at(t0).btn('PLAY').at(t0 + BAR + 6 * STEP + 0.06).shot('t-perform-4');

  // F3 MUTES: pad 13, the hi-hat, is out - and in again a bar later
  // prettier-ignore
  s.at(t0 + 2 * BAR + 0.1).btn('F3').pad(13);
  s.at(t0 + 2 * BAR + 10 * STEP + 0.06).shot('t-perform-5');
  s.at(t0 + 3 * BAR + 0.1).pad(13);

  // SHIFT + F4 COPY: H is a copy of G, and starts on the bar line. F1 STEPS, the kick, two steps more
  s.at(t0 + 3 * BAR + 8 * STEP).shift('F4');
  // prettier-ignore
  s.at(t0 + 4 * BAR + 0.2).btn('F1').shiftPad(3).pad(11, 0.3).pad(16, 0.3);
  s.at(t0 + 5 * BAR + 3 * STEP + 0.06).shot('t-perform-6');

  // F4 PATTERN, pad 7: G waits for the bar line
  // prettier-ignore
  s.at(t0 + 6 * BAR + 0.2).btn('F4').pad(7);
  s.at(t0 + 6 * BAR + 9 * STEP + 0.05).shot('t-perform-7');

  // G plays. REC: the pads are the kit; the cowbell played in, and heard on the next round
  s.at(t0 + 7 * BAR + 0.3).btn('REC');
  for (const g of [3, 6, 11, 14]) {
    s.at(t0 + 8 * BAR + g * STEP + 0.02).pad(4);
  }
  s.at(t0 + 9 * BAR + 12 * STEP + 0.07).shot('t-perform-8');
}

// ---- PERFORM at 1/32: the frame round the 16 steps the pads are, and a roll in the second 16 ---------------------
{
  const s = scene('perform-32').btn('K5').btn('K5').btn('K5').btn('K5');
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // prettier-ignore
  s.turn('DATA', 2).btn('F5', 0.3)                    // the kick on every quarter
    .shiftPad(13).btn('F5', 0.3).btn('F5', 0.3)       // the hi-hat on every eighth
    .shift('F6').wait(2.6)                            // SHIFT + F6: GRID 1/32
    // SHIFT + RIGHT: the pads are steps 17-32
    .shift('RIGHT');
  for (const p of [10, 11, 12, 14, 15, 16]) {
    s.pad(p, 0.1);
  }
  s.wait(0.3).shot('perform-32');
}

// ---- the bar and the beat on SEQ: a bar of 12 sixteenths is 3/4, a kick on its three beats - and SHIFT + B makes
// the same bar 6/8
{
  // B is the bar's length on SONG (on SEQ it is the scale): 12 sixteenths. Then K5 three times more, for SEQ
  const s = scene('meter-seq').btn('K5').btn('K5').turn('B', -4).btn('K5').btn('K5').btn('K5');

  // G, the first empty pattern
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // prettier-ignore
  s.wait(2.6).turn('DATA', 2).btn('F3').wait(0.3).shot('meter-34').wait(2.6)  // the kick: F3 FILL, every quarter
    .shiftTurn('B', 1).wait(0.3).shot('meter-beat'); // SHIFT + B: BEAT 3/8: BAR 6/8
}

// ---- PERFORM in 6/8 while it plays: 12 sixteenths, a beat of a dotted quarter, kick on the beats, hats on the
// eighths
{
  const s = scene('meter-68').btn('K5').btn('K5').btn('K5').btn('K5');
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // prettier-ignore
  s.turn('B', -4).shiftTurn('B', 1)                 // B: 12 sixteenths. SHIFT + B: a dotted quarter
    .turn('DATA', 2).btn('F5', 0.3)                 // the kick on every beat (EVERY 3/8)
    .shiftPad(13).btn('F5', 0.3).btn('F5', 0.3)     // the hi-hat on every eighth
    .wait(2.6);

  // PLAY: G repeats. A bar is 12 sixteenths now; the picture in the second beat of the second round
  const t0 = s.t + 0.1;
  // prettier-ignore
  s.at(t0).btn('PLAY').at(t0 + 12 * STEP + 8 * STEP + 0.06).shot('meter-68');
}

// ---- PERFORM in 7/8: a kick on every quarter, then B two clicks down. The last beat is a short one ----------------
{
  const s = scene('meter-78').btn('K5').btn('K5').btn('K5').btn('K5');
  for (let k = 0; k < 6; k++) {
    s.btn('RIGHT', 0.08);
  }
  // prettier-ignore
  s.turn('DATA', 2).btn('F5').wait(2.6).turn('B', -2).wait(0.3).shot('meter-78');
}

// ==== the pictures =================================================================================================
// ---- the display, with the SDK's wasmtest -------------------------------------------------------------------------
fs.mkdirSync(path.join(OUT, 'scripts'), { recursive: true });
if (!fs.existsSync(WASM)) {
  console.error('no build/sfx-forge.wasm: run sh build.sh first');
  process.exit(1);
}
const wasmtest = path.join(SDK, 'tools', 'wasmtest.mjs');
if (!fs.existsSync(wasmtest)) {
  console.error('VIBE FM SDK not found at ' + SDK);
  process.exit(1);
}

// every scene: its script written to out/.../scripts, played by wasmtest (which writes the pictures of the display),
// and played once more here when it has pictures of the stage
let made = 0;
for (const s of scenes) {
  const file = path.join(OUT, 'scripts', s.name + '.json');
  fs.writeFileSync(file, JSON.stringify(s.script()));
  execFileSync(process.execPath, [wasmtest, WASM, file, OUT], { stdio: ['ignore', 'ignore', 'inherit'] });
  made += s.steps.filter((x) => x.shot).length;
  if (s.stages) {
    made += stageShots(s);
  }
}
console.log(`${made} screenshots written to ${path.relative(ROOT, OUT).split(path.sep).join('/')}`);

// ---- the stage: the same script once more, block by block as wasmtest plays it, and the firmware's stage saved ----
// Gives the number of pictures it wrote.
function stageShots(s) {
  const sr = 48000;
  const block = 128;

  // the firmware, with the three functions it asks its host for (as in tests/host.mjs)
  const files = new Map();
  let inst;
  inst = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(WASM)), {
    env: {
      vibe_host_log: () => {},
      vibe_host_request: () => {},
      // nothing stored: the factory state, as in wasmtest without "store"
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
        return -1;
      },
    },
  });
  const e = inst.exports;
  const script = s.script();
  const steps = script.steps.slice();
  const total = Math.ceil((script.seconds * sr) / block);
  let n = 0;
  e.vibe_wasm_init(sr);
  // the stage is shown: event 19 is VIBE_EV_STAGE, id 8 is VIBE_STAGE_SHOWN (the SDK's vibe.h)
  e.vibe_wasm_event(19, 8, 0, 0, 0);

  // block by block: the events that are due, a block of audio, a draw every 12 blocks, the pictures that are due
  for (let b = 0; b < total; b++) {
    const due = [];
    while (steps.length && steps[0].t <= (b * block) / sr) {
      const x = steps.shift();
      if (x.ev) {
        // the SDK's clock, where the firmware reads the time of an event: the frame count as 64 bits, then zeros
        if (e.vibe_wasm_time) {
          const vt = new Uint32Array(e.memory.buffer, e.vibe_wasm_time(), 8);
          const f = b * block;
          vt[0] = f >>> 0;
          vt[1] = Math.floor(f / 4294967296);
          vt[2] = 0;
          vt[3] = 0;
          vt[4] = 0;
        }
        // (a button's value is 0 whatever the scene says, as in the SDK's wasmtest and every host)
        e.vibe_wasm_event(x.ev[0], x.ev[1], x.ev[2], x.ev[0] === 3 ? 0 : (x.ev[3] ?? 0), x.ev[4] ?? 0);
      }
      if (x.stage) {
        due.push(x.stage);
      }
    }
    e.vibe_wasm_audio(block, 0);
    if (b % 12 === 0 || due.length) {
      e.vibe_wasm_draw();
    }
    for (const name of due) {
      const info = new DataView(e.memory.buffer, e.vibe_wasm_stage_info());
      const w = info.getUint16(0, true);
      const h = info.getUint16(2, true);
      png(new Uint16Array(e.memory.buffer, e.vibe_wasm_stage_draw(), w * h), w, h, path.join(OUT, name + '.png'));
      n++;
    }
  }
  return n;
}

// Writes pixels in RGB565, as the firmware draws, as an 8-bit RGB PNG.
function png(px, w, h, file) {
  // the picture's rows: a filter byte (0) and three bytes a pixel
  const raw = Buffer.alloc(h * (w * 3 + 1));
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const v = px[y * w + x];
      const o = y * (w * 3 + 1) + 1 + x * 3;
      raw[o] = (v >> 11) << 3;
      raw[o + 1] = ((v >> 5) & 63) << 2;
      raw[o + 2] = (v & 31) << 3;
    }
  }

  // the CRC-32 every chunk of a PNG ends with: its table, and the sum of a buffer
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

  // a chunk: its length, its type and data, and the CRC of those two
  const chunk = (type, data) => {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const typeAndData = Buffer.concat([Buffer.from(type), data]);
    const sum = Buffer.alloc(4);
    sum.writeUInt32BE(crc(typeAndData));
    return Buffer.concat([length, typeAndData, sum]);
  };

  // the header: width, height, 8 bits a channel, colour type 2 (RGB)
  const header = Buffer.alloc(13);
  header.writeUInt32BE(w, 0);
  header.writeUInt32BE(h, 4);
  header[8] = 8;
  header[9] = 2;

  // the file: the PNG signature, the header, the rows deflated, the end
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
