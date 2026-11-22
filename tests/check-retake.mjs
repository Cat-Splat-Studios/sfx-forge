// What this check proves: a take stays one take whatever happens during it - a knob turned, a pattern chosen, REC
// switched off while a pad is held - and with F1 it goes along the song.
//
// The scene (tests/retake.json, rendered by the SDK's wasmtest into out/retake): takes, and what is around them. The
// factory song, 120 BPM: a bar is 2 s.
//   SEQ, REC, PLAY; pad 6, encoder 3 (the velocity), pad 6; STOP, F2     -> one take, one step back: the factory song
//   SEQ, PLAY (REC lit), RIGHT (the top line says A>B), STOP before the  -> B is shown: a step made now is in B
//   bar line; REC off, a step
//   REC lit, pad 16 held (a sound that holds), REC off, the pad let go   -> the sound ends
//   REC, F1 (TO SONG), PLAY; pad 7 in two bars                           -> the take goes along the song: two patterns
// The click runs under the takes: the output stays under full scale.
//
//   node tests/check-retake.mjs out/retake unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { wav, songs, steps, pattern, same } from './song-read.mjs';

const dir = process.argv[2] || 'out/retake';
const factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

const rms = wav(`${dir}/audio.wav`);
const sent = songs(`${dir}/midi.json`).map((s) => s.bytes);

// (the file is 16 sounds of 28 bytes, then the song)
const factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);
const all = [...Array(8)].map((_, p) => p);

// the bits of v that are set, as step numbers 1 to 16: "14 15 16", or "none"
const bits = (v) => {
  const numbers = [...Array(16)].map((_, i) => i + 1).filter((i) => (v >> (i - 1)) & 1);
  return numbers.join(' ') || 'none';
};

// ---- the three songs that were sent: after the first take and F2, after the step in B, after the take along the song
ok(sent.length === 3, `three songs went out (${sent.length})`);
if (sent.length === 3) {
  const [undone, picked, along] = sent;
  const A = 0;
  const B = 1;

  // pad 6 is sound 5 (counted from 0)
  ok(
    same(undone, factory),
    `a knob turned in the middle of a take does not cut it in two: F2 takes all of it back (pad 6 in A: ` +
      `${bits(steps(undone, A, 5) ^ steps(factory, A, 5))})`,
  );

  // the steps of sound 5 that differ from the factory song: step 16 in B, none in A
  const inB = steps(picked, B, 5) ^ steps(factory, B, 5);
  const inA = steps(picked, A, 5) ^ steps(factory, A, 5);
  ok(
    inB === 1 << 15 && inA === 0,
    `a pattern chosen during a take is the one shown after STOP: the step is in B (B: ${bits(inB)}, A: ${bits(inA)})`,
  );

  // pad 7 is sound 6: two patterns have changed, and in each it is that sound's steps
  const changed = all.filter((p) => !same(pattern(along, p), pattern(picked, p)));
  ok(
    changed.length === 2 && changed.every((p) => steps(along, p, 6) !== steps(picked, p, 6)),
    `REC with F1 on SONG: the take goes along the arrangement, a hit in each of two bars is in two patterns ` +
      `(${changed.map((p) => 'ABCDEFGH'[p]).join(' ') || 'none'})`,
  );
}

// ---- the held pad: it sounds at 10 s and is silent after it is let go
ok(rms(10.05, 10.35) > 0.01, `pad 16 sounds while it is held (${rms(10.05, 10.35).toFixed(4)})`);
ok(
  rms(12.3, 13.5) < 0.002,
  `and ends when it is let go, though REC went off in between (${rms(12.3, 13.5).toFixed(5)})`,
);

// ---- the largest sample of the whole file (float samples after the 44 bytes of the WAV's head)
const wavFile = fs.readFileSync(`${dir}/audio.wav`);
let peak = 0;
for (let i = 44; i + 4 <= wavFile.length; i += 4) {
  peak = Math.max(peak, Math.abs(wavFile.readFloatLE(i)));
}
ok(peak <= 1, `the click over a take stays under full scale (peak ${peak.toFixed(3)})`);

console.log(failed ? `retake FAILED (${failed})` : 'retake PASSED');
process.exit(failed ? 1 : 0);
