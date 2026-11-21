// tests/retake.json: takes, and what is around them. The factory song, 120 BPM: a bar is 2 s.
//   SEQ, REC, PLAY; pad 6, encoder 3 (the velocity), pad 6; STOP, F2      -> one take, one step back: the factory song
//   SEQ, PLAY (REC lit), RIGHT (the top line says A>B), STOP before the    -> B is shown: a step made now is in B
//   bar line; REC off, a step
//   REC lit, pad 16 held (a sound that holds), REC off, the pad let go     -> the sound ends
//   REC, F1 (TO SONG), PLAY; pad 7 in two bars                             -> the take goes along the song: two patterns
// The click runs under the takes: the output stays under full scale.
//   node tests/check-retake.mjs out/retake unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { wav, songs, steps, pattern, same } from './song-read.mjs';
const dir = process.argv[2] || 'out/retake', factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const rms = wav(`${dir}/audio.wav`);
const sent = songs(`${dir}/midi.json`).map((s) => s.bytes), factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);
const all = [...Array(8)].map((_, p) => p), bits = (v) => [...Array(16)].map((_, i) => i + 1).filter((i) => v >> (i - 1) & 1).join(' ') || 'none';
ok(sent.length === 3, `three songs went out (${sent.length})`);
if (sent.length === 3) {
  const [undone, picked, along] = sent, A = 0, B = 1;
  ok(same(undone, factory), `a knob turned in the middle of a take does not cut it in two: F2 takes all of it back (pad 6 in A: ${bits(steps(undone, A, 5) ^ steps(factory, A, 5))})`);
  const inB = steps(picked, B, 5) ^ steps(factory, B, 5), inA = steps(picked, A, 5) ^ steps(factory, A, 5);
  ok(inB === 1 << 15 && inA === 0, `a pattern chosen during a take is the one shown after STOP: the step is in B (B: ${bits(inB)}, A: ${bits(inA)})`);
  const changed = all.filter((p) => !same(pattern(along, p), pattern(picked, p)));
  ok(changed.length === 2 && changed.every((p) => steps(along, p, 6) !== steps(picked, p, 6)), `REC with F1 on SONG: the take goes along the arrangement, a hit in each of two bars is in two patterns (${changed.map((p) => 'ABCDEFGH'[p]).join(' ') || 'none'})`);
}
ok(rms(10.05, 10.35) > 0.01, `pad 16 sounds while it is held (${rms(10.05, 10.35).toFixed(4)})`);
ok(rms(12.3, 13.5) < 0.002, `and ends when it is let go, though REC went off in between (${rms(12.3, 13.5).toFixed(5)})`);
const w = fs.readFileSync(`${dir}/audio.wav`); let peak = 0;
for (let i = 44; i + 4 <= w.length; i += 4) peak = Math.max(peak, Math.abs(w.readFloatLE(i)));
ok(peak <= 1, `the click over a take stays under full scale (peak ${peak.toFixed(3)})`);
console.log(failed ? `retake FAILED (${failed})` : 'retake PASSED');
process.exit(failed ? 1 : 0);
