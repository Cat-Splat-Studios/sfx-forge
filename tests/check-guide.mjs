// What this check proves: the built-in tour sounds in every one of its parts, and leaves the sound it reshaped and
// mutated exactly as it was.
//
// The scene (tests/guide.json, rendered by the SDK's wasmtest into out/guide): the laser pad, then MENU starts the
// tour (40.9 s), then the laser pad again.
//
//   node tests/check-guide.mjs out/guide
import fs from 'fs';

const dir = process.argv[2] || 'out';

let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

// ---- the rendered audio: a 16-bit stereo WAV, of which the left channel is read
const wavFile = fs.readFileSync(`${dir}/audio.wav`);
const sampleRate = wavFile.readUInt32LE(24);
const sampleCount = (wavFile.length - 44) / 4;
const left = new Float32Array(sampleCount);
for (let i = 0; i < sampleCount; i++) {
  left[i] = wavFile.readInt16LE(44 + i * 4) / 32768;
}

// the level (root mean square) between two moments, in seconds
const rms = (t0, t1) => {
  let sum = 0;
  const from = Math.floor(t0 * sampleRate);
  const to = Math.floor(t1 * sampleRate);
  for (let i = from; i < to; i++) {
    sum += left[i] * left[i];
  }
  return Math.sqrt(sum / (to - from));
};

// how often the wave goes up through zero between two moments: the cycles of the sound
const crossings = (t0, t1) => {
  let count = 0;
  for (let i = Math.floor(t0 * sampleRate) + 1; i < Math.floor(t1 * sampleRate); i++) {
    if (left[i - 1] < 0 && left[i] >= 0) {
      count++;
    }
  }
  return count;
};

// ---- the parts of the tour: the seconds, counted from MENU, in which each must be heard
const MENU_AT = 1.0; // MENU is pressed
// the later parts come after the instruments and the wavetable
const LATER_PARTS = 5.4;
const parts = {
  'the 16 sounds': [0.2, 4.0],
  'shaping a sound': [4.4, 7.6],
  'MUTATE': [8.0, 10.8],
  'notes on the keys': [11.4, 13.6],
  'the strings': [14.3, 15.7],
  'the wavetable': [16.6, 18.4],
  'the song at intensity 0': [LATER_PARTS + 14.0, LATER_PARTS + 17.6],
  'the layers': [LATER_PARTS + 22.0, LATER_PARTS + 25.6],
  'cued patterns': [LATER_PARTS + 26.0, LATER_PARTS + 29.6],
  'the link': [LATER_PARTS + 30.0, LATER_PARTS + 32.8],
};

// every part sounds
const quiet = Object.entries(parts)
  .filter(([, [from, to]]) => rms(MENU_AT + from, MENU_AT + to) < 0.02)
  .map(([name]) => name);
ok(!quiet.length, quiet.length ? `silent parts of the tour: ${quiet.join(', ')}` : 'every part of the tour sounds');

// "the layers" is louder than "the song at intensity 0": the tour turned the intensity up
const atIntensity0 = rms(MENU_AT + LATER_PARTS + 14, MENU_AT + LATER_PARTS + 17.6);
const withLayers = rms(MENU_AT + LATER_PARTS + 22, MENU_AT + LATER_PARTS + 25.6);
ok(
  withLayers > atIntensity0 * 1.5,
  `the tour's intensity ramp brings layers in (${atIntensity0.toFixed(3)} -> ${withLayers.toFixed(3)})`,
);

// the end of the tour
ok(
  rms(MENU_AT + LATER_PARTS + 36.2, MENU_AT + LATER_PARTS + 37.2) < 0.005,
  'the tour ends in silence (the song is stopped, the echoes have faded)',
);

// the laser pad before the tour (0.2 s) and after it (44.0 s): the same level, the same number of cycles
const levelBefore = rms(0.2, 0.7);
const levelAfter = rms(44.0, 44.5);
const cyclesBefore = crossings(0.2, 0.7);
const cyclesAfter = crossings(44.0, 44.5);
ok(
  Math.abs(levelBefore - levelAfter) < levelBefore * 0.03 && Math.abs(cyclesBefore - cyclesAfter) <= 3,
  `the sound the tour changed is back as it was (level ${levelBefore.toFixed(4)} / ${levelAfter.toFixed(4)}, ` +
    `${cyclesBefore} / ${cyclesAfter} cycles)`,
);

console.log(failed ? `guide FAILED (${failed})` : 'guide PASSED');
process.exit(failed ? 1 : 0);
