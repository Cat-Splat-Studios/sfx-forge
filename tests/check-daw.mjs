// What this check proves: the sequencer follows a DAW's clock and song position.
//
// The scene (tests/daw.json, rendered by the SDK's wasmtest into out/daw): the sequencer was started on its own clock
// (120), then a DAW plays from 1.0 s at 140 BPM. From then on the kicks (every quarter note) must sit on the DAW's
// beats.
//
//   node tests/check-daw.mjs out/daw
import fs from 'fs';

const dir = process.argv[2] || 'out';

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

// ---- eight beats of the DAW from 1.0 s on: a kick on each, and much less between two of them
const beat = 60 / 140;
const on = [...Array(8)].map((_, k) => rms(1 + k * beat, 1 + k * beat + 0.06));
const off = [...Array(8)].map((_, k) => rms(1 + k * beat + beat * 0.3, 1 + k * beat + beat * 0.45));
const good = Math.min(...on) > 0.08 && Math.max(...off) < Math.min(...on) * 0.6;

console.log(
  (good ? '  ok    ' : '  FAIL  ') +
    `the sequencer follows the DAW's song position at 140 BPM (kicks ` +
    `${Math.min(...on).toFixed(2)}+, between ${Math.max(...off).toFixed(2)})`,
);
console.log(good ? 'daw PASSED' : 'daw FAILED');
process.exit(good ? 0 : 1);
