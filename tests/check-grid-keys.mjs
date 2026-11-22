// The finer grid's keys where a scene of wasmtest cannot look (firmware 1.6.0), on a small host of its own (tests/host.mjs):
//   - SHIFT + F5 asks before it drops a step. The second press counts only while the number it named is still true:
//     a step set in between is asked for again, with the number that is true now.
//   - SHIFT + LEFT / RIGHT go through the windows that hold a step of the bar: with a bar of 12 sixteenths a pattern of
//     64 steps has three (steps 1-48), not four; with a bar of 4 it has one, and nothing moves.
//   node tests/check-grid-keys.mjs build/sfx-forge.wasm
import { load, SECOND, BUTTON } from './host.mjs';
import { grid, stepList, same } from './song-read.mjs';
const [, , wasmFile = 'build/sfx-forge.wasm'] = process.argv;
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const device = load(wasmFile);
const H = 7, SOUND = 0;                                    // (the sound selected when it starts: pad 1)
const list = (a) => a.map((i) => i + 1).join(' ') || 'none';
const onH = () => { const d = device(); d.run(SECOND); d.button(BUTTON.K5); d.button(BUTTON.LEFT); return d; };   // SEQ, pattern H (empty)
const bar = (d, sixteenths) => {                          // SONG, encoder B until the bar is that long, SEQ again
  d.button(BUTTON.K5);
  for (let i = 0; i < 40 && d.song().bytes[1] !== sixteenths; i++) d.turn(5, d.song().bytes[1] > sixteenths ? -1 : 1);
  d.button(BUTTON.K5); d.button(BUTTON.K5);
  return d.song().bytes[1];
};

// ---- the question and its number
{
  const d = onH();
  d.shift(BUTTON.F6);                                       // 1/32
  d.pad(2);                                                 // step 2: between two sixteenths
  d.shift(BUTTON.F5);                                       // the question: H DROPS 1
  const shown = d.screen();
  d.pad(4);                                                 // step 4, while the question stands
  d.shift(BUTTON.F5);                                       // not the second press of that question: it would drop 2 now
  const asks = d.screen() !== shown;                        // (the display changed: another number)
  const again = d.song().bytes;                             // (three seconds: the question is gone from the display)
  ok(grid(again, H) === 32 && same(stepList(again, H, SOUND), [1, 3]) && asks,
     `SHIFT + F5 asked for one step; a second step set, SHIFT + F5 again: nothing is dropped, it asks for two (H ${grid(again, H)}: ${list(stepList(again, H, SOUND))})`);
  d.shift(BUTTON.F5); d.shift(BUTTON.F5);                   // asked, and the second press of the question that is true
  const lowered = d.song().bytes;
  ok(grid(lowered, H) === 16 && same(stepList(lowered, H, SOUND), []), `asked again and pressed twice it is lowered: 16 steps, both gone (H ${grid(lowered, H)}: ${list(stepList(lowered, H, SOUND))})`);
  d.button(BUTTON.F2);
  const back = d.song().bytes;
  ok(grid(back, H) === 32 && same(stepList(back, H, SOUND), [1, 3]), `F2 brings both back (H ${grid(back, H)}: ${list(stepList(back, H, SOUND))})`);
}
{
  const d = onH();
  d.shift(BUTTON.F6); d.pad(2); d.shift(BUTTON.F5); d.shift(BUTTON.F5);
  const s = d.song().bytes;
  ok(grid(s, H) === 16 && same(stepList(s, H, SOUND), []), 'with nothing changed in between, the second press lowers it as before');
}

// ---- the windows of a short bar
{
  const d = onH();
  const len = bar(d, 12);
  d.shift(BUTTON.F6); d.shift(BUTTON.F6);                   // 1/64: 48 steps play, in three windows
  d.pad(1);                                                 // step 1
  d.shift(BUTTON.RIGHT); d.pad(1);                          // step 17
  d.shift(BUTTON.RIGHT); d.pad(1);                          // step 33
  d.shift(BUTTON.RIGHT); d.pad(2);                          // not 49-64, where nothing plays: round to 1-16, step 2
  d.shift(BUTTON.LEFT); d.pad(3);                           // back from the first: the last window that plays, step 35
  const s = d.song().bytes;
  ok(len === 12 && grid(s, H) === 64 && same(stepList(s, H, SOUND), [0, 1, 16, 32, 34]),
     `a bar of 12 sixteenths at 64 steps: SHIFT + RIGHT goes 1-16, 17-32, 33-48 and round again, never to 49-64 (H: ${list(stepList(s, H, SOUND))})`);
}
{
  const d = onH();
  const len = bar(d, 4);
  d.shift(BUTTON.F6); d.shift(BUTTON.F6);                   // 1/64: 16 steps play - one window
  const before = d.screen();
  d.shift(BUTTON.RIGHT);
  const said = d.screen() !== before;
  d.pad(5);
  const s = d.song().bytes;
  ok(len === 4 && said && same(stepList(s, H, SOUND), [4]), `a bar of 4 sixteenths at 64 steps is one window: SHIFT + RIGHT moves nothing and says where the bar ends (H: ${list(stepList(s, H, SOUND))})`);
}
{
  const d = onH();
  d.shift(BUTTON.F6);                                       // 1/32, a whole bar: two windows
  d.shift(BUTTON.RIGHT); d.pad(1); d.shift(BUTTON.RIGHT); d.pad(2);
  const s = d.song().bytes;
  ok(same(stepList(s, H, SOUND), [1, 16]), `a whole bar at 32 steps has its two windows as before (H: ${list(stepList(s, H, SOUND))})`);
}

console.log(failed ? `grid keys FAILED (${failed})` : 'grid keys PASSED');
process.exit(failed ? 1 : 0);
