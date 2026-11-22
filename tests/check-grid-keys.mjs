// What this check proves: the finer grid's keys where a scene of wasmtest cannot look (firmware 1.6.0), on a small
// host of its own (tests/host.mjs):
//   - SHIFT + F5 asks before it drops a step. The second press counts only while the number it named is still true:
//     a step set in between is asked for again, with the number that is true now.
//   - SHIFT + LEFT / RIGHT go through the windows that hold a step of the bar: with a bar of 12 sixteenths a pattern
//     of 64 steps has three (steps 1-48), not four; with a bar of 4 it has one, and nothing moves.
//   - and the key that changes kit, which is not a grid key but needs the same host: SHIFT + ENTER, a short press,
//     on every page and with FOCUS on. SHIFT + K1 is K1 and nothing more (on the VIBE itself SHIFT + K1-K6 play
//     notes, SDK 28: the VIBE does that and the firmware never gets them), ENTER held is FOCUS' and changes no kit.
//     With FOCUS' frame on F1-F6 it is SHIFT + that key; after SHIFT + DATA turned, ENTER is the SDK's (a soft key).
// Each case below is a block { ... } with a device of its own, started from the factory state.
//
//   node tests/check-grid-keys.mjs build/sfx-forge.wasm
import { load, SECOND, BUTTON, EVENT, ENCODER, HEAD } from './host.mjs';
import { grid, stepList, same } from './song-read.mjs';

const [, , wasmFile = 'build/sfx-forge.wasm'] = process.argv;

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

const device = load(wasmFile);
const H = 7;
// (the sound selected when it starts: pad 1)
const SOUND = 0;
// the DATA encoder
const DATA = ENCODER.DATA;

// a list of steps counted from 0, as text counted from 1
const list = (a) => a.map((i) => i + 1).join(' ') || 'none';

// a device on SEQ, pattern H (empty)
const onH = () => {
  const d = device();
  d.run(SECOND);
  d.button(BUTTON.K5);
  d.button(BUTTON.LEFT);
  return d;
};

// sets the length of the bar: SONG, encoder B until the bar is that long, SEQ again. Gives the length it reached.
const bar = (d, sixteenths) => {
  d.button(BUTTON.K5);
  for (let i = 0; i < 40 && d.song().bytes[1] !== sixteenths; i++) {
    d.turn(5, d.song().bytes[1] > sixteenths ? -1 : 1);
  }
  d.button(BUTTON.K5);
  d.button(BUTTON.K5);
  return d.song().bytes[1];
};

// ---- the question and its number
{
  const d = onH();
  d.shift(BUTTON.F6); // 1/32
  d.pad(2); // step 2: between two sixteenths
  d.shift(BUTTON.F5); // the question: H DROPS 1
  const shown = d.screen();
  d.pad(4); // step 4, while the question stands
  // not the second press of that question: it would drop 2 now
  d.shift(BUTTON.F5);
  const asks = d.screen() !== shown; // (the display changed: another number)
  const again = d.song().bytes; // (three seconds: the question is gone from the display)
  ok(
    grid(again, H) === 32 && same(stepList(again, H, SOUND), [1, 3]) && asks,
    `SHIFT + F5 asked for one step; a second step set, SHIFT + F5 again: nothing is dropped, it asks for two (H ` +
      `${grid(again, H)}: ${list(stepList(again, H, SOUND))})`,
  );

  // asked, and the second press of the question that is true
  d.shift(BUTTON.F5);
  d.shift(BUTTON.F5);
  const lowered = d.song().bytes;
  ok(
    grid(lowered, H) === 16 && same(stepList(lowered, H, SOUND), []),
    `asked again and pressed twice it is lowered: 16 steps, both gone (H ` +
      `${grid(lowered, H)}: ${list(stepList(lowered, H, SOUND))})`,
  );

  d.button(BUTTON.F2);
  const back = d.song().bytes;
  ok(
    grid(back, H) === 32 && same(stepList(back, H, SOUND), [1, 3]),
    `F2 brings both back (H ${grid(back, H)}: ${list(stepList(back, H, SOUND))})`,
  );
}
// the plain case: asked, and pressed again
{
  const d = onH();
  d.shift(BUTTON.F6);
  d.pad(2);
  d.shift(BUTTON.F5);
  d.shift(BUTTON.F5);
  const s = d.song().bytes;
  ok(
    grid(s, H) === 16 && same(stepList(s, H, SOUND), []),
    'with nothing changed in between, the second press lowers it as before',
  );
}

// ---- the windows of a short bar
{
  const d = onH();
  const len = bar(d, 12);
  // 1/64: 48 steps play, in three windows
  d.shift(BUTTON.F6);
  d.shift(BUTTON.F6);
  d.pad(1); // step 1
  d.shift(BUTTON.RIGHT);
  d.pad(1); // step 17
  d.shift(BUTTON.RIGHT);
  d.pad(1); // step 33
  d.shift(BUTTON.RIGHT);
  d.pad(2); // not 49-64, where nothing plays: round to 1-16, step 2
  d.shift(BUTTON.LEFT);
  d.pad(3); // back from the first: the last window that plays, step 35
  const s = d.song().bytes;
  ok(
    len === 12 && grid(s, H) === 64 && same(stepList(s, H, SOUND), [0, 1, 16, 32, 34]),
    `a bar of 12 sixteenths at 64 steps: SHIFT + RIGHT goes 1-16, 17-32, 33-48 and round again, never to 49-64 ` +
      `(H: ${list(stepList(s, H, SOUND))})`,
  );
}
{
  const d = onH();
  const len = bar(d, 4);
  // 1/64: 16 steps play - one window
  d.shift(BUTTON.F6);
  d.shift(BUTTON.F6);
  const before = d.screen();
  d.shift(BUTTON.RIGHT);
  const said = d.screen() !== before;
  d.pad(5);
  const s = d.song().bytes;
  ok(
    len === 4 && said && same(stepList(s, H, SOUND), [4]),
    `a bar of 4 sixteenths at 64 steps is one window: SHIFT + RIGHT moves nothing and says where the bar ends ` +
      `(H: ${list(stepList(s, H, SOUND))})`,
  );
}
{
  const d = onH();
  d.shift(BUTTON.F6); // 1/32, a whole bar: two windows
  d.shift(BUTTON.RIGHT);
  d.pad(1);
  d.shift(BUTTON.RIGHT);
  d.pad(2);
  const s = d.song().bytes;
  ok(
    same(stepList(s, H, SOUND), [1, 16]),
    `a whole bar at 32 steps has its two windows as before (H: ${list(stepList(s, H, SOUND))})`,
  );
}

// ---- the kit: SHIFT + ENTER. Pad 1's patch, asked for over SysEx, is the chosen kit's: that tells the two apart
{
  // the patch of pad n + 1 as the device answers a request for it: its bytes as text
  const patch = (d, n = 0) => {
    d.take();
    d.sysex([...HEAD, 0x04, n, 0xf7]);
    d.run(40);
    const m = d.take().find((x) => x[4] === 0x01 && x[5] === n);
    return m ? m.slice(6, -1).join(' ') : 'no answer';
  };

  // a button held for so many blocks
  const hold = (d, id, blocks) => {
    d.event(EVENT.BUTTON, id, 1);
    d.run(blocks);
    d.event(EVENT.BUTTON, id, 0);
    d.run(4);
  };

  // SHIFT + ENTER, and SHIFT + DATA pressed
  const d = device();
  d.run(SECOND);
  const sfx = patch(d);
  d.shift(BUTTON.ENTER);
  const music = patch(d);
  d.shift(BUTTON.ENTER);
  const back = patch(d);
  ok(
    sfx !== 'no answer' && music !== 'no answer' && music !== sfx && back === sfx,
    'on a sound page SHIFT + ENTER changes kit, and again changes it back (pad 1 is another sound, then the ' +
      'first again)',
  );
  d.shift(BUTTON.DATA_PUSH);
  const pushed = patch(d);
  d.shift(BUTTON.DATA_PUSH);
  ok(pushed === music && patch(d) === sfx, 'SHIFT + DATA pressed does the same as SHIFT + ENTER');

  // SHIFT + K1: the kit stays, and the display is what K1 alone gives (and not the page the device started on)
  d.shift(BUTTON.K1);
  const k1 = patch(d);
  const shifted = device();
  shifted.run(SECOND);
  shifted.shift(BUTTON.K1);
  shifted.run(SECOND);

  // (the same number of blocks)
  const plain = device();
  plain.run(SECOND);
  plain.button(BUTTON.K1);
  plain.run(SECOND + 4);

  // (the page it started on)
  const first = device();
  first.run(2 * SECOND + 12);
  ok(
    k1 === sfx && shifted.screen() === plain.screen() && plain.screen() !== first.screen(),
    'SHIFT + K1 no longer changes kit: it is K1, the next page of the sound, as without SHIFT',
  );

  // the other keys with SHIFT
  for (const k of [BUTTON.K2, BUTTON.K3, BUTTON.K4, BUTTON.K5, BUTTON.K6, BUTTON.RETURN]) {
    d.shift(k);
  }
  d.button(BUTTON.RETURN);
  ok(
    patch(d) === music,
    'SHIFT + K2-K6 are the pages (the song pages choose MUSIC, as without SHIFT), SHIFT + RETURN is RETURN: ' +
      'none of them is a kit key',
  );

  d.shift(BUTTON.ENTER);
  ok(patch(d) === sfx, 'SHIFT + ENTER after that: the SFX kit again');

  d.button(BUTTON.ENTER);
  d.button(BUTTON.DATA_PUSH);
  ok(patch(d) === sfx, 'ENTER and DATA pressed without SHIFT change no kit');

  // a long press: FOCUS on
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 1);
  d.run(2);
  hold(d, BUTTON.ENTER, SECOND);
  d.event(EVENT.BUTTON, BUTTON.SHIFT, 0);
  d.run(2);
  ok(patch(d) === sfx, 'SHIFT + ENTER held for a second changes no kit (ENTER held is FOCUS)');

  d.shift(BUTTON.ENTER);
  ok(patch(d) === music, 'with FOCUS on, SHIFT + ENTER still changes kit');

  // FOCUS off; SEQ (the MUSIC kit); the key
  d.button(BUTTON.RETURN);
  d.button(BUTTON.K5);
  d.shift(BUTTON.ENTER);
  const fromSeq = patch(d);
  const seqScreen = d.screen();
  const sound = device();
  sound.run(SECOND);
  sound.button(BUTTON.K5);
  sound.button(BUTTON.K1);
  ok(
    fromSeq === sfx && seqScreen !== sound.screen(),
    'on SEQ, SHIFT + ENTER goes to the first sound page, in the SFX kit',
  );

  // FOCUS with its frame on a soft key: ENTER presses that key, and SHIFT + ENTER is SHIFT + that key, not the kit key
  {
    // SEQ, pattern H: the MUSIC kit
    const f = onH();
    const was = patch(f);
    hold(f, BUTTON.ENTER, SECOND); // FOCUS on
    // the frame to the last control: F6
    f.turn(DATA, 40);

    // SHIFT + F6 on SEQ: a finer grid
    f.shift(BUTTON.ENTER);
    const finer = grid(f.song().bytes, H);
    f.shift(BUTTON.ENTER);
    f.shift(BUTTON.DATA_PUSH);
    ok(
      finer === 32 && grid(f.song().bytes, H) === 64 && patch(f) === was,
      `with the FOCUS frame on F6, SHIFT + ENTER is SHIFT + F6 (H: ` +
        `1/${finer}, then 1/${grid(f.song().bytes, H)}) and changes no kit`,
    );

    // the frame on K1, a page: the kit key again
    f.turn(DATA, -40);
    f.shift(BUTTON.ENTER);
    ok(patch(f) !== was, 'with the frame on a page key, SHIFT + ENTER is the kit key');
  }

  // SHIFT held and DATA turned is the SDK's frame over F1-F6 (every firmware): ENTER then presses the framed key
  {
    const s = device();
    s.run(SECOND);
    const one = patch(s, 0);
    const two = patch(s, 1);
    s.event(EVENT.BUTTON, BUTTON.SHIFT, 1);
    s.run(2);
    s.turn(DATA, -1); // the frame on F1 PRESET
    s.button(BUTTON.ENTER);
    const pressed = patch(s, 0);
    const kept = patch(s, 1);

    // SHIFT still held: the firmware was told it is up
    s.button(BUTTON.ENTER);
    const second = patch(s, 1);
    s.event(EVENT.BUTTON, BUTTON.SHIFT, 0);
    s.run(2);
    ok(
      pressed !== one && kept === two && second === two,
      'SHIFT held, DATA turned, ENTER: the framed soft key is pressed (F1: pad 1 has another sound), no kit ' +
        'changes, and ENTER again does nothing while SHIFT stays down',
    );
    s.shift(BUTTON.ENTER);
    ok(patch(s, 1) !== two, 'SHIFT let go and pressed again: SHIFT + ENTER changes kit');
  }
}

console.log(failed ? `grid keys FAILED (${failed})` : 'grid keys PASSED');
process.exit(failed ? 1 : 0);
