// A whole pattern or song rewritten while it plays (firmware 1.6.0): the player has the song before the edit or the song
// after it, from one block on - never half of one. The controls edit a song of their own; after every event and draw
// what differs goes to the player, which takes it at the start of a block (sfx-forge/song_way.h).
//
// What this can show and what it cannot: this host (tests/host.mjs), like the SDK's wasmtest, calls the firmware in ONE
// flow - an event, then a block, never both at once. So the race itself (the audio reading a pattern while the controls
// move its cells) cannot happen here, before the change or after it. That the two flows keep off each other is shown
// with two real threads by tests/stress-song-way.c (sh tests/stress-song-way.sh). Here: every such edit goes through
// the new way and arrives - whole, in time, with the playhead where it was.
//   Two devices play pattern A of the factory song, block for block. One is left alone; on the other, while it plays:
//   the grid finer and coarser, twice finer within one block, F2 back and forth, COPY, a song over SysEx. None of that
//   changes what the pattern sounds like - so every sample must be the same as on the device left alone. Then a step
//   that only a pattern of 64 steps has: it must sound at its moment, to the sample. Then CLR PAT and F2.
//   The same changes of grid taken in the very block a step begins on: that step sounds, once.
//   Stopped, and with no block between the edits: a burst of edits to two patterns and the tempo, then PLAY.
//   node tests/check-atomic.mjs build/sfx-forge.wasm
import { load, SECOND, BUTTON, pieces } from './host.mjs';
import { grid, stepList, pattern, tempo, same, empty } from './song-read.mjs';
const [, , wasmFile = 'build/sfx-forge.wasm'] = process.argv;
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const device = load(wasmFile);
const A = 0, G = 6, TIMPANI = 0, BAR = 96000, BLOCK = 128;   // (the sound selected when it starts: pad 1, which the factory song does not use; a bar at 120: 2 s)
const list = (a) => a.map((i) => i + 1).join(' ') || 'none';
const press = (d, id) => { d.event(3, id, 1); d.event(3, id, 0); };                               // a key, with no block before, in or after it
const shifted = (d, id, times = 1) => { d.event(3, BUTTON.SHIFT, 1); for (let i = 0; i < times; i++) press(d, id); d.event(3, BUTTON.SHIFT, 0); };
const tap = (d, n) => { d.event(1, n - 1, 1, 0.9); d.event(1, n - 1, 0, 0); };                    // a pad, likewise
const firstDiff = (x, y, from, to) => { for (let i = from; i < to; i++) if (x[i] !== y[i]) return i; return -1; };
const rms = (x, a, b) => { let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return Math.sqrt(s / Math.max(1, b - a)); };
const peak = (x, a, b) => { let m = 0; for (let i = a; i < b; i++) m = Math.max(m, Math.abs(x[i])); return m; };

// ---- while it plays ------------------------------------------------------------------------------------------------------
{
  const still = device(), d = device();
  for (const x of [still, d]) { x.run(SECOND); x.button(BUTTON.K5); x.listen(); x.button(BUTTON.PLAY); }   // SEQ: PLAY loops pattern A, from the first sample kept
  const t0 = d.blocks;
  const now = () => (d.blocks - t0) * BLOCK;                                // samples since PLAY
  const along = () => still.run(d.blocks - still.blocks);                   // the device left alone: as many blocks
  let from = 0;
  const alike = (what) => {                                                 // every sample since the last look
    along();
    const x = still.heard(), y = d.heard(), at = firstDiff(x, y, from, y.length), loud = peak(x, from, y.length);
    ok(at < 0 && loud > 0.05, `${what}: ${((y.length - from) / 48000).toFixed(1)} s of it, sample for sample what the device left alone plays` + (at < 0 ? '' : ` - NOT from ${(at / 48000).toFixed(4)} s on`));
    from = y.length;
  };
  const factory = still.song().bytes; d.run(still.blocks - d.blocks);

  d.run(431);                                                               // somewhere in the first bar, between two steps
  d.shift(BUTTON.F6); d.run(97); d.shift(BUTTON.F6); d.run(2 * SECOND);     // 1/32, 1/64
  let s = d.song().bytes;
  ok(grid(s, A) === 64, `SHIFT + F6 twice while A plays: A has ${grid(s, A)} steps`);
  alike('the grid made finer while it plays');
  d.run(211); d.shift(BUTTON.F5); d.run(53); d.shift(BUTTON.F5); d.run(2 * SECOND);   // nothing stands between: nothing is asked
  s = d.song().bytes;
  ok(same(s, factory), 'SHIFT + F5 twice: the factory song again, byte for byte');
  alike('the grid made coarser while it plays');

  d.run(77); shifted(d, BUTTON.F6, 2); d.run(2 * SECOND);                   // 16 to 64 with no block between the two changes
  s = d.song().bytes;
  ok(grid(s, A) === 64, `two changes of grid within one block: A has ${grid(s, A)} steps`);
  alike('two changes of grid within one block');

  d.run(303); d.button(BUTTON.F2); d.run(SECOND);
  const back = grid(d.song().bytes, A);
  d.run(140); d.button(BUTTON.F2); d.run(SECOND);
  const forth = grid(d.song().bytes, A);
  ok(back === 32 && forth === 64, `F2 while it plays takes the last change of grid back, F2 again brings it: ${back} steps, then ${forth}`);
  alike('UNDO and REDO while it plays');

  d.run(190); d.button(BUTTON.F5); d.run(3 * SECOND);                       // COPY: A into G, and on to G with the next bar line
  s = d.song().bytes;
  ok(same(pattern(s, G), pattern(s, A)) && !empty(s, G), 'COPY while it plays: G is A, grid and all');
  alike('COPY while it plays, and the copy from the bar line on');
  d.button(BUTTON.K5); d.pad(1); d.button(BUTTON.K5); d.button(BUTTON.K5);  // SONG: pad 1 is pattern A; SEQ again
  d.run(3 * SECOND);
  alike('back on A with the next bar line');

  d.shift(BUTTON.F5); d.shift(BUTTON.F5); d.run(SECOND);                    // A at 16 steps again - and another device's song, with A at 64 and G empty
  const other = device(); other.run(SECOND); other.button(BUTTON.K5); other.shift(BUTTON.F6); other.shift(BUTTON.F6);
  const theirs = other.song().bytes;
  d.take();
  for (const m of pieces(theirs)) { d.sysex(m); d.run(1); }                 // (a piece a block: it is taken with the last, in the middle of a bar)
  d.run(2 * SECOND);
  s = d.song().bytes;
  ok(same(s, theirs) && grid(s, A) === 64 && empty(s, G), `a song over SysEx while it plays is the song (${pieces(theirs).length} pieces; A ${grid(s, A)} steps, G empty)`);
  alike('a song that comes in while it plays');

  // a step only 64 steps have: step 3, half a sixteenth after the bar line - with the factory's swing of 20 % that is
  // 0.55 of a sixteenth of 6000 samples: 3300 samples after every bar line
  d.run(SECOND + 100);
  const pressed = now();
  d.pad(3); d.run(3 * SECOND); along();
  {
    const x = still.heard(), y = d.heard(), at = firstDiff(x, y, from, y.length), into = at % BAR;
    ok(at >= pressed && into >= 3300 && into <= 3300 + 32, `step 3 of 64 set while it plays sounds ${into} samples after a bar line (3300: its moment, to the sample; nothing else differs before)`);
    s = d.song().bytes;
    ok(same(stepList(s, A, TIMPANI), [2]), `and it is in the song (A: ${list(stepList(s, A, TIMPANI))})`);
  }
  d.pad(3);                                                                 // off again
  d.run(6 * SECOND);

  // CLR PAT (SONG page, F3), then F2: the pattern is silent, then it is back, on the same bar lines
  const keep = d.song().bytes;
  d.button(BUTTON.K5); d.run(60); d.button(BUTTON.F3);
  d.run(11 * SECOND); along();
  const quiet = now();
  {
    const x = still.heard(), y = d.heard(), a = rms(x, quiet - 48000, quiet), b = rms(y, quiet - 48000, quiet);
    s = d.song().bytes;
    ok(empty(s, A) && grid(s, A) === 64 && b < a * 0.02, `CLR PAT while it plays: A is empty (its grid stays) and falls silent (${b.toExponential(1)} against ${a.toFixed(3)} of the device left alone)`);
  }
  d.run(250); d.button(BUTTON.F2);
  d.run(7 * SECOND); along();
  {
    const x = still.heard(), y = d.heard(), end = y.length, start = end - 2 * BAR, W = 240;   // the last two bars, in pieces of 5 ms
    let ab = 0, aa = 0, bb = 0;
    for (let i = start; i + W <= end; i += W) { const u = rms(x, i, i + W), v = rms(y, i, i + W); ab += u * v; aa += u * u; bb += v * v; }
    const c = ab / Math.sqrt(aa * bb + 1e-12);
    s = d.song().bytes;
    ok(same(s, keep) && c > 0.98, `F2 while it plays brings the pattern back, and it plays on the same steps as the device left alone (the loudness of two bars in pieces of 5 ms: correlation ${c.toFixed(4)})`);
  }
}

// ---- the change in the very block a step begins on -------------------------------------------------------------------------
// At 120 a sixteenth is 6000 samples: every eighth one begins on the first sample of a block (48000 = 375 blocks after
// PLAY), at 150 (4800 samples, with the swing) others do. A change of grid taken in that block meets a step that has not
// sounded yet: it must sound, once. So: the change right before that block and the ones beside it, every way a grid
// changes without dropping anything - all as on a device left alone.
{
  const finer = (n) => (d) => { for (let i = 0; i < n; i++) d.shift(BUTTON.F6); };
  const CASES = [
    ['16 to 32', null, (d) => shifted(d, BUTTON.F6)],
    ['16 to 64', null, (d) => shifted(d, BUTTON.F6, 2)],
    ['64 to 32', finer(2), (d) => shifted(d, BUTTON.F5)],
    ['64 to 16', finer(2), (d) => shifted(d, BUTTON.F5, 2)],
    ['32 to 16', finer(1), (d) => shifted(d, BUTTON.F5)],
    ['F2 (64 back to 32)', finer(2), (d) => press(d, BUTTON.F2)],
  ];
  for (const [clicks, at] of [[0, [374, 375, 376]], [30, [150, 375, 450, 525]]]) {
    const start = (pre) => { const d = device(); d.run(375); d.button(BUTTON.K5); d.turn(4, clicks); if (pre) pre(d); d.run(375); d.listen(); press(d, BUTTON.PLAY); return d; };
    const bad = []; let loud = 1;
    for (const [name, pre, change] of CASES) {
      const still = start(pre); still.run(1200); const want = still.heard();
      if (peak(want, 0, want.length) < 0.05) loud = 0;
      for (const n of at) {
        const d = start(pre); d.run(n); change(d); d.run(1200 - n);
        const i = firstDiff(want, d.heard(), 0, want.length);
        if (i >= 0) bad.push(`${name} before block ${n}: from sample ${i}`);
      }
    }
    ok(loud && !bad.length, `tempo ${120 + clicks}: the grid changed in the block a step begins on (before block ${at.join(', ')} after PLAY; ${CASES.map((c) => c[0]).join(', ')}): every sample as on the device left alone` + (bad.length ? ` - NOT: ${bad.join('; ')}` : ''));
  }
}

// ---- stopped, and no block between the edits ------------------------------------------------------------------------------
{
  const burst = device(), spaced = device();
  for (const x of [burst, spaced]) { x.run(SECOND); x.button(BUTTON.K5); }
  // the same edits on both, none of which lets a sound be heard (a pad would: then the two devices' voices have another
  // past): FILL twice (the timpani on every eighth of A), A at 32 steps, FILL (every sixteenth), COPY (to G, and on to
  // it), the tempo up by 7, G at 64 steps, FILL (every thirty-second)
  { const d = burst; press(d, BUTTON.F3); press(d, BUTTON.F3); shifted(d, BUTTON.F6); press(d, BUTTON.F3); press(d, BUTTON.F5); for (let i = 0; i < 7; i++) d.event(4, 4, 0, 1); shifted(d, BUTTON.F6); press(d, BUTTON.F3); }
  { const d = spaced; d.button(BUTTON.F3); d.button(BUTTON.F3); d.shift(BUTTON.F6); d.button(BUTTON.F3); d.button(BUTTON.F5); d.turn(4, 7); d.shift(BUTTON.F6); d.button(BUTTON.F3); }
  const a = burst.song().bytes, b = spaced.song().bytes, every = (n, k) => [...Array(n / k)].map((_, i) => i * k);
  ok(same(a, b) && grid(a, A) === 32 && same(stepList(a, A, TIMPANI), every(32, 2)) && grid(a, G) === 64 && same(stepList(a, G, TIMPANI), every(64, 2)) && tempo(a) === 127,
     `eight edits with no block between them are all in the song, as when blocks run after each (A ${grid(a, A)} steps, the timpani on ${stepList(a, A, TIMPANI).length}; G ${grid(a, G)} steps, on ${stepList(a, G, TIMPANI).length}; tempo ${tempo(a)})`);
  const heardAlike = (what, blocks) => {
    for (const x of [burst, spaced]) x.run(blocks);
    const x = burst.heard(), y = spaced.heard();
    let worst = 0; for (let i = 0; i < x.length; i++) worst = Math.max(worst, Math.abs(x[i] - y[i]));
    ok(x.length === y.length && peak(x, 0, x.length) > 0.05 && worst < 1e-6, `${what} (${(x.length / 48000).toFixed(1)} s, peak ${peak(x, 0, x.length).toFixed(2)}, the largest difference of a sample ${worst.toExponential(1)})`);
  };
  for (const x of [burst, spaced]) { x.run(9 * SECOND); x.listen(); press(x, BUTTON.PLAY); }   // (what the pads let hear has rung out)
  heardAlike('and the player has them all: G with its timpani on every thirty-second, at the new tempo, sounds as on the device that had blocks after each edit', 4 * SECOND);
  for (const x of [burst, spaced]) { x.button(BUTTON.STOP); x.run(9 * SECOND); }
  // edits and PLAY in one go: G at 32 steps (nothing stands between), the tempo down, PLAY - before any block
  { const d = burst; d.listen(); shifted(d, BUTTON.F5); for (let i = 0; i < 12; i++) d.event(4, 4, 0, -1); press(d, BUTTON.PLAY); }
  { const d = spaced; d.shift(BUTTON.F5); d.turn(4, -12); d.listen(); press(d, BUTTON.PLAY); }
  heardAlike('edits and PLAY with no block between them: it starts in the song as it is then', 4 * SECOND);
  const c = burst.song().bytes;
  ok(grid(c, G) === 32 && tempo(c) === 115 && same(c, spaced.song().bytes), `(G ${grid(c, G)} steps, tempo ${tempo(c)})`);
}

console.log(failed ? `atomic FAILED (${failed})` : 'atomic PASSED');
process.exit(failed ? 1 : 0);
