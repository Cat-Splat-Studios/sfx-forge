// What this check proves: a factory reset puts a sound that was changed and saved back as it came.
//
// The scene (tests/reset.json, rendered by the SDK's wasmtest into out/reset): the laser pad, then RANDOM and SAVE,
// the pad again, SHIFT + MENU twice (factory reset), the pad again.
//
//   node tests/check-reset.mjs out/reset
import fs from 'fs';

const dir = process.argv[2] || 'out/reset';

// ---- the rendered audio: a float stereo WAV; the level of its left channel between two moments, in seconds
const wavFile = fs.readFileSync(`${dir}/audio.wav`);
const sampleRate = wavFile.readUInt32LE(24);
const rms = (t0, t1) => {
  let sum = 0;
  let count = 0;
  for (let i = Math.floor(t0 * sampleRate); i < Math.floor(t1 * sampleRate); i++) {
    const v = wavFile.readFloatLE(44 + i * 8);
    sum += v * v;
    count++;
  }
  return Math.sqrt(sum / count);
};

// ---- the pad three times: as it came, after RANDOM and SAVE, after the reset
const made = rms(0.2, 0.7);
const changed = rms(3.0, 3.5);
const back = rms(6.0, 6.5);

// RANDOM made another sound of it (5 % off or more); the reset brought the first one back (within 1 %)
const good = Math.abs(changed - made) > made * 0.05 && Math.abs(back - made) < made * 0.01;

console.log(
  (good ? '  ok    ' : '  FAIL  ') +
    `SHIFT + MENU twice puts a changed, saved sound back as it came ` +
    `(${made.toFixed(4)} -> ${changed.toFixed(4)} -> ${back.toFixed(4)})`,
);
console.log(good ? 'reset PASSED' : 'reset FAILED');
process.exit(good ? 0 : 1);
