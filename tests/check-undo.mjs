// tests/undo.json: nothing plays. What F2 takes back, and what it leaves: after each case the song is sent (SONG page, F5).
//   0  SEQ: a step. SONG: two bars added, F2             the two bars are gone - and only they: the step stays
//   1  F2 again                                          the two bars are back (then F2 once more: gone again)
//   2  SEQ: a step, three pages on and back, a step,     F2 takes back the last step only: a run of edits ends with the
//      RIGHT (pattern B), a step, F2                     page and with the pattern
//   3  a step, SHIFT + MENU twice (factory reset),       the edit after the reset is F2's: the factory song, byte for byte
//      a step, F2
//   4  SEQ: F5 COPY (A > G), F2, a step                  G is empty again and A is shown: the step went into A
//   5  SONG: pattern H (empty), F3 CLR PAT, F2           nothing to clear, so F2 still holds the step before: it is gone
//   node tests/check-undo.mjs out/undo unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { songs, steps, pattern, empty, same, arrLen, arr } from './song-read.mjs';
const dir = process.argv[2] || 'out/undo', factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const sent = songs(`${dir}/midi.json`).map((s) => s.bytes), factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);
ok(sent.length === 6, `six songs went out (${sent.length})`);
if (sent.length === 6) {
  const [bars, redo, run, reset, copy, clear] = sent;
  const A = 0, B = 1, G = 6, all = [...Array(8)].map((_, p) => p);
  const flipped = (s, p, t = 0) => { const x = steps(s, p, t) ^ steps(factory, p, t); return [...Array(16)].map((_, i) => i + 1).filter((i) => x >> (i - 1) & 1).join(' ') || 'none'; };
  const others = (s, list) => all.filter((p) => !list.includes(p)).every((p) => same(pattern(s, p), pattern(factory, p)));
  ok(arrLen(factory) === 16 && arrLen(bars) === 16 && flipped(bars, A) === '16' && others(bars, [A]), `F2 after two bars added takes the bars back, and not the step made before them (${arrLen(bars)} bars, step changed in A: ${flipped(bars, A)})`);
  ok(arrLen(redo) === 18 && same(arr(redo).slice(16), [0, 0]) && flipped(redo, A) === '16', `F2 again brings the two bars back (${arrLen(redo)} bars)`);
  ok(arrLen(run) === 16 && flipped(run, A) === '14 15 16' && same(pattern(run, B), pattern(factory, B)) && others(run, [A]), `a run of edits ends with the page and the pattern: F2 takes back the step in B only (A: ${flipped(run, A)}, B: ${flipped(run, B)})`);
  ok(same(reset, factory), `after a factory reset the next edit is F2's (A: ${flipped(reset, A)})`);
  ok(empty(copy, G) && flipped(copy, A) === '16' && others(copy, [A]), `F2 after COPY: the copy is gone and A is shown again - the next step goes into A (A: ${flipped(copy, A)}, G ${empty(copy, G) ? 'empty' : 'not empty'})`);
  ok(same(clear, factory), `CLR PAT on an empty pattern takes nothing from F2: it still takes the step back (A: ${flipped(clear, A)})`);
}
console.log(failed ? `undo FAILED (${failed})` : 'undo PASSED');
process.exit(failed ? 1 : 0);
