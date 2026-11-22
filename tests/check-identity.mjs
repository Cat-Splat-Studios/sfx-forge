// What this check proves: the identity (firmware 1.4.0). REQUEST IDENTITY `F0 7D 56 42 07 F7` is answered with
// `F0 7D 56 42 08 <18 bytes> F7` - once, whatever else is on the wire - and the other messages of the protocol
// (REQUEST 04, LOAD 02, REQUEST SONG 06, SONG 05, TEXT 03) work as before beside it.
//
// The scene: the SDK's wasmtest sends events but no SysEx, so this check uses a small host of its own
// (tests/host.mjs). Each part below is a block { ... } with devices of its own.
//
//   node tests/check-identity.mjs build/sfx-forge.wasm sfx-forge
import fs from 'fs';
import { load, HEAD, SECOND, BUTTON } from './host.mjs';

const [, , wasmFile = 'build/sfx-forge.wasm', fwDir = 'sfx-forge'] = process.argv;

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

// bytes as text ("F0 7D ..."), and whether two lists of bytes are the same
const hex = (b) => b.map((v) => v.toString(16).padStart(2, '0').toUpperCase()).join(' ');
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

// ---- the version: three defines in the firmware, one line in its manifest
const src = fs.readFileSync(`${fwDir}/sfx-forge.c`, 'utf8');
// the number a "#define NAME 123" line gives, or -1
const def = (n) => {
  const m = src.match(new RegExp(`^#define ${n}\\s+(\\d+)`, 'm'));
  return m ? +m[1] : -1;
};
const ver = [def('FW_MAJOR'), def('FW_MINOR'), def('FW_PATCH')];
const manifest = JSON.parse(fs.readFileSync(`${fwDir}/firmware.json`, 'utf8')).version;
ok(
  ver.join('.') === manifest,
  `the firmware's defines (${ver.join('.')}) and firmware.json ("${manifest}") say the same version`,
);

// ---- a host: the firmware with nothing stored, 48 kHz, blocks of 128
const device = load(wasmFile);
const REQ = [...HEAD, 0x07, 0xf7];

// the answer that is wanted. Its 18 bytes:
// hardware, version, profile, patch format, song format, sounds, kits, patterns, the most steps a pattern holds,
// lanes, bars, voices, engines, wavetables, values, flags
const WANT = [...HEAD, 0x08, 1, ...ver, 1, 2, 6, 16, 2, 8, 64, 24, 32, 16, 5, 6, 25, 0x0f, 0xf7];
ok(
  hex(WANT) === 'F0 7D 56 42 08 01 01 06 00 01 02 06 10 02 08 40 18 20 10 05 06 19 0F F7',
  `firmware 1.6.0 says: ${hex(WANT)}`,
);

// the factory song in format 6: its pieces on the wire, its bytes
const PIECES = 11;
const SONG = 2487;

// ---- the request and its answer
{
  const d = device();
  d.run(SECOND);
  d.take();
  d.sysex(REQ);
  d.run(24);
  const got = d.take();
  ok(
    got.length === 1 && eq(got[0], WANT),
    `the request is answered once with the 24 bytes ${hex(WANT)}` +
      (got.length === 1 && !eq(got[0], WANT)
        ? ` (got ${hex(got[0])})`
        : got.length !== 1
          ? ` (got ${got.length} messages)`
          : ''),
  );
  ok(
    got.length === 1 && got[0].length === 24 && got[0].slice(5, 23).every((v) => v < 128),
    'its payload is 18 bytes, each 0..127',
  );

  d.run(SECOND);
  ok(d.take().length === 0, 'and nothing follows it');

  // the emulator delivers every message twice: both before the next block
  d.sysex(REQ);
  d.sysex(REQ);
  d.run(24);
  let two = d.take();
  ok(
    two.length === 2 && two.every((m) => eq(m, WANT)),
    `a doubled request (both before the next block) gets two answers (${two.length})`,
  );

  d.sysex(REQ);
  d.run(1);
  d.sysex(REQ);
  d.run(24);
  two = d.take();
  ok(two.length === 2 && two.every((m) => eq(m, WANT)), `two requests a block apart get two answers (${two.length})`);

  // requests with one, two and eighteen bytes too many
  for (const bad of [
    [...HEAD, 0x07, 0x00, 0xf7],
    [...HEAD, 0x07, 0x00, 0x00, 0xf7],
    [...HEAD, 0x07, ...Array(18).fill(1), 0xf7],
  ]) {
    d.sysex(bad);
  }
  d.run(SECOND);
  ok(d.take().length === 0, 'a request of any other length (7, 8, 24 bytes) is not answered');
}

// ---- a pad's dump asked for right after the identity (a game does that when it opens the link): the host takes one
// SysEx a block, so the dump has to wait for the identity, not vanish
{
  // the request for a pad's dump
  const DUMP = (slot) => [...HEAD, 0x04, slot, 0xf7];

  // what came back, as a word a message: ID (the identity), D3 (the dump of slot 3), S (a piece of the song)
  const kinds = (got) =>
    got.map((m) => (m[4] === 0x08 ? 'ID' : m[4] === 0x01 ? 'D' + m[5] : m[4] === 0x05 ? 'S' : '?')).join(' ');

  // one "ok" line: a fresh device, what `send` sends, and what comes back within a second
  const tryIt = (name, want, send) => {
    const d = device();
    d.run(SECOND);
    d.take();
    send(d);
    d.run(SECOND);
    const got = kinds(d.take());
    ok(got === want, `${name}: ${want}` + (got === want ? '' : ` (got ${got || 'nothing'})`));
  };

  tryIt('a request, then a pad asked for in the same block', 'ID D0', (d) => {
    d.sysex(REQ);
    d.sysex(DUMP(0));
  });
  tryIt('both doubled in the same block (the emulator)', 'ID ID D0', (d) => {
    d.sysex(REQ);
    d.sysex(REQ);
    d.sysex(DUMP(0));
    d.sysex(DUMP(0));
  });
  tryIt('a doubled request, two blocks, the pad asked for twice', 'ID ID D0', (d) => {
    d.sysex(REQ);
    d.sysex(REQ);
    d.run(2);
    d.sysex(DUMP(0));
    d.sysex(DUMP(0));
  });
  tryIt('a request, then three pads asked for in the same block', 'ID D2 D5 D9', (d) => {
    d.sysex(REQ);
    d.sysex(DUMP(5));
    d.sysex(DUMP(9));
    d.sysex(DUMP(2));
  });
  tryIt('a pad asked for, then a request', 'D0 ID', (d) => {
    d.sysex(DUMP(0));
    d.sysex(REQ);
  });

  // a pad asked for while the sounds and the song go out comes after them, not among the song's sounds
  const d = device();
  d.run(SECOND);
  d.take();
  d.sysex([...HEAD, 0x06, 0xf7]);
  d.run(30);
  d.sysex(DUMP(7));
  d.run(2 * SECOND);
  const got = d.take();
  ok(
    got.length === 16 + PIECES + 1 &&
      got.slice(0, 16).every((m, i) => m[4] === 0x01 && m[5] === i) &&
      got.slice(16, 16 + PIECES).every((m, k) => m[4] === 0x05 && m[5] === k) &&
      got[16 + PIECES][4] === 0x01 &&
      got[16 + PIECES][5] === 7,
    `a pad asked for while a song goes out: the 16 sounds, the ` +
      `${PIECES} pieces, then that pad (${got.length} messages)`,
  );
}

// ---- an identity that comes in (the device's own, echoed) does nothing: the twin that never got one is the same
// the 28 bytes of the patch in a dump or LOAD message, and the checksum such a patch must end with
const patchOf = (m) => m.slice(6, 34);
const sum = (p) => p.slice(0, 27).reduce((a, v) => a + v, 0) & 0x7f;
{
  const a = device();
  const b = device();
  const c = device();
  for (const d of [a, b, c]) {
    d.run(SECOND);
  }
  ok(a.screen() === b.screen(), 'two devices that got the same show the same display');

  // a gets identities (two that are right, one with no payload, one with 40 bytes), b nothing, c a request
  for (const m of [WANT, WANT, [...HEAD, 0x08, 0xf7], [...HEAD, 0x08, ...Array(40).fill(0), 0xf7]]) {
    a.sysex(m);
  }
  c.sysex(REQ);
  for (const d of [a, b, c]) {
    d.run(SECOND);
  }
  ok(a.take().length === 0, 'an identity sent to the device (18 bytes twice, none, 40) is not answered');
  ok(a.screen() === b.screen(), 'and leaves the display as it was: the LINK light stays off');
  ok(c.take().length === 1 && c.screen() !== b.screen(), 'while a request lights it (the display can tell)');

  // every other message, after that traffic (a) and without it (b): byte for byte the same
  const script = (d) => {
    const got = {};

    // REQUEST (04): the dump of pad 4
    d.sysex([...HEAD, 0x04, 3, 0xf7]);
    d.run(SECOND);
    got.dump = d.take();

    // that patch with one parameter changed and its checksum made right again
    const p = patchOf(got.dump[0] || []).slice();
    p[2] = (p[2] + 9) & 0x7f;
    p[27] = sum(p);
    got.sent = p;

    // LOAD (02) into pad 6, and its dump asked for
    d.sysex([...HEAD, 0x02, 5, ...p, 0xf7]);
    d.run(SECOND);
    d.sysex([...HEAD, 0x04, 5, 0xf7]);
    d.run(SECOND);
    got.loaded = d.take();

    // LOAD into slot 18 (pad 3 of the MUSIC kit), then REQUEST SONG (06)
    d.sysex([...HEAD, 0x02, 16 + 2, ...p, 0xf7]);
    d.run(SECOND);
    d.sysex([...HEAD, 0x06, 0xf7]);
    d.run(2 * SECOND);
    got.song = d.take();
    return got;
  };
  const ga = script(a);
  const gb = script(b);

  // are two lists of messages the same, byte for byte?
  const same = (x, y) => x.length === y.length && x.every((m, i) => eq(m, y[i]));
  ok(
    ga.dump.length === 1 &&
      ga.dump[0].length === 35 &&
      eq(ga.dump[0].slice(0, 6), [...HEAD, 0x01, 3]) &&
      patchOf(ga.dump[0])[27] === sum(patchOf(ga.dump[0])),
    "REQUEST (04) is answered with that pad's dump, a valid patch",
  );
  ok(
    ga.loaded.length === 1 && eq(patchOf(ga.loaded[0]), ga.sent) && ga.loaded[0][5] === 5,
    'LOAD (02) puts a patch into a pad: its dump gives it back',
  );

  const dumps = ga.song.filter((m) => m[4] === 0x01);
  const pieces = ga.song.filter((m) => m[4] === 0x05);
  ok(
    ga.song.length === 16 + PIECES &&
      dumps.length === 16 &&
      pieces.length === PIECES &&
      pieces.every((m, k) => m[5] === k && m[6] === 0 && m[7] === 6 && m[8] === PIECES && m[9] === 0),
    `REQUEST SONG (06) is answered with 16 sounds and the song in ` +
      `${PIECES} pieces: piece number, format 6 and count in each`,
  );
  ok(dumps.length === 16 && eq(patchOf(dumps[2]), ga.sent), 'LOAD to slot 18 put the patch on pad 3 of the MUSIC kit');

  const song = pieces.flatMap((m) => m.slice(10, -1));
  ok(song.length === SONG && song[0] === 6, `the song is ${SONG} bytes of format 6 (${song.length}, ${song[0]})`);
  ok(
    same(ga.dump, gb.dump) && same(ga.loaded, gb.loaded) && same(ga.song, gb.song),
    'the device that was sent identities answers all of it byte for byte as the one that was not',
  );

  // the song back in (05), changed: another tempo. Then asked for again.
  const changed = song.slice();
  const bpm = ((changed[2] << 7) | changed[3]) === 97 ? 131 : 97;
  changed[2] = bpm >> 7;
  changed[3] = bpm & 0x7f;

  // (in reverse: any order)
  for (let k = PIECES - 1; k >= 0; k--) {
    a.sysex([...HEAD, 0x05, k, 0, 6, PIECES, 0, ...changed.slice(k * 240, k * 240 + 240), 0xf7]);
    a.run(2);
  }
  a.run(SECOND);
  a.take();
  a.sysex([...HEAD, 0x06, 0xf7]);
  a.run(2 * SECOND);
  const again = a
    .take()
    .filter((m) => m[4] === 0x05)
    .flatMap((m) => m.slice(10, -1));
  ok(eq(again, changed), `SONG (05) sent in is taken: asked for again it comes back with the new tempo (${bpm})`);

  // a request while the sounds and the song are going out: the answer comes, and all the song's messages too
  a.sysex([...HEAD, 0x06, 0xf7]);
  a.run(30);
  a.sysex(REQ);
  a.run(2 * SECOND);
  const mixed = a.take();
  ok(
    mixed.filter((m) => eq(m, WANT)).length === 1 &&
      mixed.filter((m) => m[4] === 0x01).length === 16 &&
      eq(
        mixed.filter((m) => m[4] === 0x05).flatMap((m) => m.slice(10, -1)),
        changed,
      ),
    'a request while a song goes out: one answer, and the song whole',
  );

  // TEXT (03): on the LINK page (K6) two devices that are both linked differ by the text alone
  const x = device();
  const y = device();
  for (const d of [x, y]) {
    d.run(SECOND);
    d.button(BUTTON.K6);
    d.sysex(REQ);
    d.run(SECOND);
  }
  const before = x.screen() === y.screen();
  x.sysex([...HEAD, 0x03, ...Buffer.from('HELLO'), 0xf7]);
  x.run(SECOND);
  y.run(SECOND);
  ok(before && x.screen() !== y.screen(), 'TEXT (03) shows on the LINK page');
}

console.log(failed ? `identity FAILED (${failed})` : 'identity PASSED');
process.exit(failed ? 1 : 0);
