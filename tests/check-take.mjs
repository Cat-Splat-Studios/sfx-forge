// tests/take.json: pattern H (empty) is chosen, K5 (the SEQ page), the click off, REC, PLAY at 1.0 s - a bar is 2 s.
// The kick's pad is hit once in each of three bars: 1.5 s (step 5), 3.25 s (step 3), 5.75 s (step 7). All three land in H,
// the pattern on the display, bar after bar. F4 is held from 7.6 to 8.2 s: steps 5 to 10 pass under it, step 3 does not.
// STOP, then SEND: the song that went out says what is in H now.
//   node tests/check-take.mjs out/take unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { wav, songs, steps, pattern, same } from './song-read.mjs';
const dir = process.argv[2] || 'out/take', factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const rms = wav(`${dir}/audio.wav`), at = (t) => rms(t, t + 0.08), f = (v) => v.toFixed(3);
const KICK = 2, H = 7;
const bar3 = [5.25, 5.5].map(at), bar4 = [7.25, 7.5, 7.75].map(at);
ok(Math.min(...bar3) > 0.05, `what was played in bars 1 and 2 plays in bar 3: steps 3 and 5 (${bar3.map(f).join(', ')})`);
ok(Math.min(...bar4) > 0.05, `and all three hits in bar 4: steps 3, 5 and 7 (${bar4.map(f).join(', ')})`);
ok(at(9.0) < 0.01 && at(11.0) < 0.01, `it is pattern H that goes round: nothing on the bar lines (${f(at(9.0))}, ${f(at(11.0))})`);
// after the erase the kick on step 3 still rings a little where steps 5 and 7 were: a kick STARTS where there is much more
// just after than just before
const starts = (t) => rms(t, t + 0.05) > 0.05 && rms(t, t + 0.05) > rms(t - 0.05, t) * 2;
const kept = [9.25, 11.25], gone = [9.5, 9.75, 11.5, 11.75];
ok(kept.every(starts) && !gone.some(starts), `F4 held erases the steps that pass under it, and no others: step 3 stays (${kept.map(at).map(f).join(', ')}), no kick starts on 5 and 7 (${gone.map(at).map(f).join(', ')}: the one on step 3 fading)`);
ok(Math.min(...kept.map(at)) > Math.max(...gone.map(at)) * 5, 'what sounds is at least five times what is left there');
const sent = songs(`${dir}/midi.json`);
ok(sent.length === 1, `SEND gave the song (${sent.length})`);
if (sent.length === 1) {
  const s = sent[0].bytes, factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);
  const others = [...Array(16)].map((_, t) => t).filter((t) => t !== KICK && steps(s, H, t));
  ok(steps(s, H, KICK) === 1 << 2 && !others.length, `pattern H holds the kick on step 3 and nothing else (kick ${steps(s, H, KICK).toString(2)}, other sounds: ${others.length})`);
  const changed = [...Array(7)].map((_, p) => p).filter((p) => !same(pattern(s, p), pattern(factory, p)));
  ok(!changed.length, `patterns A to G are the factory song's, untouched (${changed.length ? 'changed: ' + changed.map((p) => 'ABCDEFG'[p]).join(' ') : 'all seven'})`);
}
console.log(failed ? `take FAILED (${failed})` : 'take PASSED');
process.exit(failed ? 1 : 0);
