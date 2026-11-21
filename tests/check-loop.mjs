// tests/loop.json: pattern H (empty) is chosen, K5 (the SEQ page: LOOP), the click off, REC, PLAY at 1.0 s – a bar is 2 s.
// The kick's pad is hit at 1.5 s (step 5): it is recorded into H, and H goes round – the arrangement does not walk on.
// F2 at 6.2 s takes the take back, F2 at 8.2 s brings it again; F4 held from 10.0 to 12.2 s erases it as the bar passes.
import { wav } from './song-read.mjs';
const dir = process.argv[2] || 'out/loop';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const rms = wav(`${dir}/audio.wav`), at = (t) => rms(t, t + 0.08), f = (v) => v.toFixed(3);
const hit = at(1.5), again = [3.5, 5.5].map(at), lines = [3.0, 5.0].map(at), undone = at(7.5), redone = at(9.5), erased = at(13.5);
const quiet = Math.max(...lines, undone, erased), loud = Math.min(hit, ...again, redone);
ok(hit > 0.05, `the pad sounds when it is hit (${f(hit)})`);
ok(Math.min(...again) > 0.05, `REC + PLAY records it: it plays again in the next two bars (${again.map(f).join(', ')})`);
ok(Math.max(...lines) < 0.01, `the pattern loops: nothing on the bar lines, where the arrangement's pattern A has its kick (${lines.map(f).join(', ')})`);
ok(undone < 0.01, `F2 takes the take back (${f(undone)})`);
ok(redone > 0.05, `F2 again brings it back (${f(redone)})`);
ok(erased < 0.01, `F4 held erases it as the bar passes (${f(erased)})`);
ok(loud > quiet * 5, `what sounds is at least five times what is silent (${f(loud)} / ${f(quiet)})`);
console.log(failed ? `loop FAILED (${failed})` : 'loop PASSED');
process.exit(failed ? 1 : 0);
