// What this check proves: REC + PLAY clicks on the beats, the bar line included, and SHIFT + REC turns the click off.
//
// The scene (tests/metronome.json, rendered by the SDK's wasmtest into out/metronome): pattern H (empty) is cued, REC,
// PLAY at 0.5 s – then SHIFT + REC at 1.2 s: the metronome off (the display says CLICK OFF; REC had said REC ON:
// PATTERN LOOPS). PLAY starts at once: there is no count-in. The first bar is silent but for the click: on the bar
// line (higher) and on beat two; beats three and four have none.
//
//   node tests/check-metronome.mjs out/metronome
import fs from 'fs';

const dir = process.argv[2] || 'out/metronome';

// ---- the rendered audio (a float stereo WAV), the left channel through a high-pass at 1200 Hz: the click is high
const wavFile = fs.readFileSync(`${dir}/audio.wav`);
const sampleRate = wavFile.readUInt32LE(24);
const sampleCount = (wavFile.length - 44) / 8;
const smoothing = Math.exp((-2 * Math.PI * 1200) / sampleRate);
let lowPassed = 0;
const high = new Float32Array(sampleCount);
for (let i = 0; i < sampleCount; i++) {
  const v = wavFile.readFloatLE(44 + i * 8);
  lowPassed = v + (lowPassed - v) * smoothing;
  high[i] = v - lowPassed;
}

// the level of what is left between two moments, in seconds
const rms = (t0, t1) => {
  let sum = 0;
  let count = 0;
  for (let i = Math.floor(t0 * sampleRate); i < Math.floor(t1 * sampleRate); i++) {
    sum += high[i] * high[i];
    count++;
  }
  return Math.sqrt(sum / count);
};

// ---- the four beats of the first bar (120 BPM: every 0.5 s): the first two click, the last two do not
const beat = (t) => rms(t + 0.004, t + 0.03);
const on = [0.5, 1.0].map(beat);
const off = [1.5, 2.0].map(beat);
const bar = on[0];
const other = on[1];
const good = Math.min(...on) > 0.02 && Math.max(...off) < 0.001;

console.log(
  (good ? '  ok    ' : '  FAIL  ') +
    `REC + PLAY clicks on every beat ` +
    `(${Math.min(...on).toFixed(3)}+), SHIFT + REC turns it off (${Math.max(...off).toFixed(3)}); ` +
    `bar line ${bar.toFixed(3)}, beat ${other.toFixed(3)}`,
);
console.log(good ? 'metronome PASSED' : 'metronome FAILED');
process.exit(good ? 0 : 1);
