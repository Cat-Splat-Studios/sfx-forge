// What this check proves: what F2 (UNDO) takes back, and what it leaves.
//
// The scene (tests/undo.json, rendered by the SDK's wasmtest into out/undo): nothing plays. After each case the song
// is sent (SONG page, F5).
//   0  SEQ: a step. SONG: two bars added, F2            the two bars are gone - and only they: the step stays
//   1  F2 again                                         the two bars are back (then F2 once more: gone again)
//   2  SEQ: a step, three pages on and back, a step,    F2 takes back the last step only: a run of edits ends with
//      RIGHT (pattern B), a step, F2                    the page and with the pattern
//   3  a step, SHIFT + MENU twice (factory reset),      the edit after the reset is F2's: the factory song, byte for
//      a step, F2                                       byte
//   4  SEQ: F5 COPY (A > G), F2, a step                 G is empty again and A is shown: the step went into A
//   5  SONG: pattern H (empty), F3 CLR PAT, F2          nothing to clear, so F2 still holds the step before: it is
//                                                       gone
//
//   node tests/check-undo.mjs out/undo unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { songs, steps, pattern, empty, same, arrLen, arr } from './song-read.mjs';

const dir = process.argv[2] || 'out/undo';
const factoryFile = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

const sent = songs(`${dir}/midi.json`).map((s) => s.bytes);

// (the file is 16 sounds of 28 bytes, then the song)
const factory = [...fs.readFileSync(factoryFile)].slice(16 * 28);

ok(sent.length === 6, `six songs went out (${sent.length})`);
if (sent.length === 6) {
  // the six songs, named after their cases (0 to 5 above)
  const [bars, redo, run, reset, copy, clear] = sent;
  const A = 0;
  const B = 1;
  const G = 6;
  const all = [...Array(8)].map((_, p) => p);

  // the steps (1 to 16) of sound t in pattern p that are not as in the factory song, as text: "14 15 16", or "none"
  const flipped = (s, p, t = 0) => {
    const differing = steps(s, p, t) ^ steps(factory, p, t);
    const numbers = [...Array(16)].map((_, i) => i + 1).filter((i) => (differing >> (i - 1)) & 1);
    return numbers.join(' ') || 'none';
  };

  // are all patterns but the listed ones still the factory song's?
  const others = (s, list) =>
    all.filter((p) => !list.includes(p)).every((p) => same(pattern(s, p), pattern(factory, p)));

  // case 0
  ok(
    arrLen(factory) === 16 && arrLen(bars) === 16 && flipped(bars, A) === '16' && others(bars, [A]),
    `F2 after two bars added takes the bars back, and not the step made before them ` +
      `(${arrLen(bars)} bars, step changed in A: ${flipped(bars, A)})`,
  );

  // case 1
  ok(
    arrLen(redo) === 18 && same(arr(redo).slice(16), [0, 0]) && flipped(redo, A) === '16',
    `F2 again brings the two bars back (${arrLen(redo)} bars)`,
  );

  // case 2
  ok(
    arrLen(run) === 16 &&
      flipped(run, A) === '14 15 16' &&
      same(pattern(run, B), pattern(factory, B)) &&
      others(run, [A]),
    `a run of edits ends with the page and the pattern: F2 takes back the step in B only (A: ` +
      `${flipped(run, A)}, B: ${flipped(run, B)})`,
  );

  // case 3
  ok(same(reset, factory), `after a factory reset the next edit is F2's (A: ${flipped(reset, A)})`);

  // case 4
  ok(
    empty(copy, G) && flipped(copy, A) === '16' && others(copy, [A]),
    `F2 after COPY: the copy is gone and A is shown again - the next step goes into A (A: ` +
      `${flipped(copy, A)}, G ${empty(copy, G) ? 'empty' : 'not empty'})`,
  );

  // case 5
  ok(
    same(clear, factory),
    `CLR PAT on an empty pattern takes nothing from F2: it still takes the step back (A: ${flipped(clear, A)})`,
  );
}

console.log(failed ? `undo FAILED (${failed})` : 'undo PASSED');
process.exit(failed ? 1 : 0);
