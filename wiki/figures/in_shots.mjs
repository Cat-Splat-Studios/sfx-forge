// The one device screenshot the internals pages of the wiki need: the SEQ page while the factory song plays.
//
//   node wiki/figures/in_shots.mjs            (from the repository's root, after sh build.sh)
//   python wiki/figures/in_figures.py         then: the pictures with their marks, into wiki/img
//
// It works as docs/shots.mjs does: a scene (a script of key presses and the moment of a picture) is handed to the
// SDK's wasmtest, which plays it into build/sfx-forge.wasm and draws the display (480 x 320). What wasmtest writes
// beside the picture (audio.wav, midi.json) goes to a folder in the system's temp directory and is removed; only
// wiki/img/in-shot-seq.png is kept.
// Needs Node 18+, the VIBE FM SDK (.tools/vibe-fm-sdk, or VIBE_SDK) and the built firmware.
import fs from 'fs';
import os from 'os';
import path from 'path';
import { execFileSync } from 'child_process';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const SDK = process.env.VIBE_SDK || path.join(ROOT, '.tools', 'vibe-fm-sdk');
const WASM = path.join(ROOT, 'build', 'sfx-forge.wasm');
const OUT = path.join(ROOT, 'wiki', 'img');

// the buttons and the DATA encoder, by the number the firmware knows them by (tests/host.mjs has the whole list)
const K5 = 14;
const PLAY = 23;
const DATA = 6;
// the factory song: 120 BPM, 16 sixteenths a bar (in seconds)
const BAR = 2.0;
const STEP = BAR / 16;

// an event is [type, id, down, value]: type 3 a button, 4 an encoder
const press = (t, id) => [
  { t, ev: [3, id, 1] },
  { t: t + 0.04, ev: [3, id, 0] },
];
const steps = [
  // K5: the SEQ page. DATA two detents: the third sound, the kick
  ...press(0.1, K5),
  { t: 0.3, ev: [4, DATA, 0, 1] },
  { t: 0.31, ev: [4, DATA, 0, 1] },
  // PLAY, and a picture in the second bar, a little after its sixth step began
  ...press(1.0, PLAY),
  { t: 1.0 + BAR + 5 * STEP + 0.07, shot: 'in-shot-seq' },
];

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'sfxforge-wiki-'));
try {
  const scene = path.join(work, 'scene.json');
  fs.writeFileSync(scene, JSON.stringify({ seconds: 4.5, float: true, steps }));
  execFileSync('node', [path.join(SDK, 'tools', 'wasmtest.mjs'), WASM, scene, work], { stdio: 'ignore' });
  fs.mkdirSync(OUT, { recursive: true });
  fs.copyFileSync(path.join(work, 'in-shot-seq.png'), path.join(OUT, 'in-shot-seq.png'));
  console.log('wiki/img/in-shot-seq.png');
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}
