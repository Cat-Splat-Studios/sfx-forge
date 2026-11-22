// What this check proves: the song on the wire (firmware 1.6.0). It goes out and comes in as pieces of up to 240
// bytes with a 14-bit piece number and count and the song's format in every piece - and, still taken, in the shape of
// before: 39 pieces of 200 bytes of a format-5 song.
//
// The scene: a small host of its own (tests/host.mjs: wasmtest sends no SysEx). Each part below is a block { ... }
// with devices of its own.
//
//   node tests/check-wire.mjs build/sfx-forge.wasm tests/data/DemoSong-format5.bytes \
//        unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { load, HEAD, SECOND, pieces, oldPieces } from './host.mjs';
import { model, pattern, same, tempo } from './song-read.mjs';

// the built firmware, the factory song as firmware 1.5 packed it (format 5), and as 1.6 does (format 6)
const wasmFile = process.argv[2] ?? 'build/sfx-forge.wasm';
const oldFile = process.argv[3] ?? 'tests/data/DemoSong-format5.bytes';
const newFile = process.argv[4] ?? 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

const device = load(wasmFile);

// the two packed songs (each file is 16 sounds of 28 bytes, then the song)
const five = [...fs.readFileSync(oldFile)].slice(16 * 28);
const six = [...fs.readFileSync(newFile)].slice(16 * 28);

// a copy of a packed song with another tempo (bytes 2 and 3: 14 bits)
const withTempo = (s, bpm) => {
  const c = s.slice();
  c[2] = bpm >> 7;
  c[3] = bpm & 0x7f;
  return c;
};

const REQ_SONG = [...HEAD, 0x06, 0xf7];

// asks for the song and runs until its last piece is out: the bytes, the messages, the blocks it took
const requestSong = (d, limit = 20 * SECOND) => {
  d.take();
  d.sysex(REQ_SONG);

  // block by block, until as many pieces (messages of type 05) are there as the first of them says there are
  const got = [];
  let blocks = 0;
  let count = -1;
  while (blocks < limit) {
    d.run(1);
    blocks++;
    got.push(...d.take());
    const songPieces = got.filter((m) => m[4] === 0x05);
    if (songPieces.length) {
      count = songPieces[0][8] | (songPieces[0][9] << 7);
    }
    if (count > 0 && songPieces.length >= count) {
      break;
    }
  }

  // a quarter of a second more: nothing may follow
  d.run(SECOND / 4);
  got.push(...d.take());

  // whole: the pieces are numbered in order, name one format, and all but the last are full (251 bytes with the frame)
  const songPieces = got.filter((m) => m[4] === 0x05);
  const pieceIsRight = (m, k) =>
    (m[5] | (m[6] << 7)) === k && m[7] === songPieces[0][7] && m.length <= 251 && (k === count - 1 || m.length === 251);
  const whole = songPieces.length === count && songPieces.every(pieceIsRight);
  return {
    bytes: whole ? songPieces.flatMap((m) => m.slice(10, -1)) : null,
    messages: got.length,
    pieces: songPieces.length,
    blocks,
  };
};

// ---- the factory song goes out in the new shape
{
  const d = device();
  d.run(SECOND);
  const f = requestSong(d);
  ok(
    f.bytes && same(f.bytes, six) && f.pieces === 11 && f.messages === 27,
    `the factory song goes out in 16 + 11 messages ` +
      `(${f.messages}): 2487 bytes of format 6, ${f.blocks} blocks (${((f.blocks * 128) / 48).toFixed(0)} ms) ` +
      `from the request to its last piece`,
  );
}

// ---- the shape of before comes in: a format-5 song in 39 pieces of 200. It is taken, and comes back as format 6
// with the same content
{
  const d = device();
  d.run(SECOND);
  const song = withTempo(five, 97);
  ok(five.length === 7699 && five[0] === 5 && model(five) !== null, 'the fixture is a format-5 song of 7699 bytes');
  for (const m of oldPieces(song)) {
    d.sysex(m);
    d.run(1);
  }
  d.run(SECOND);
  const f = requestSong(d);
  // the eight patterns are alike, and so is the head of the song after its format byte
  const alike =
    f.bytes &&
    model(f.bytes) &&
    [0, 1, 2, 3, 4, 5, 6, 7].every((p) => same(pattern(f.bytes, p), pattern(song, p))) &&
    same(f.bytes.slice(1, 147), song.slice(1, 147));
  ok(
    f.bytes && f.bytes[0] === 6 && tempo(f.bytes) === 97 && alike && same(f.bytes, withTempo(six, 97)),
    `the old shape in (39 pieces of 200, format 5): taken, and it comes back as format 6 with the same head, ` +
      `steps, notes, velocities, lengths and points ` +
      `(${f.bytes ? f.bytes.length : 0} bytes, tempo ${f.bytes ? tempo(f.bytes) : 0})`,
  );
}

// ---- the new shape in, in reverse and every piece twice
{
  const d = device();
  d.run(SECOND);
  const song = withTempo(six, 131);
  const msgs = pieces(song);
  for (let k = msgs.length - 1; k >= 0; k--) {
    d.sysex(msgs[k]);
    d.sysex(msgs[k]);
    d.run(1);
  }
  d.run(SECOND);
  const f = requestSong(d);
  ok(
    f.bytes && same(f.bytes, song),
    `the new shape in, last piece first and every piece twice: taken (tempo ${f.bytes ? tempo(f.bytes) : 0})`,
  );
}

// ---- a second song soon after the first, that differs from it in a late piece only - or in the first only
{
  const first = withTempo(six, 131);
  const late = first.slice();
  const early = withTempo(late, 77);

  // the last point of pattern F's last lane: in the last piece
  late[late.length - 17] = late[late.length - 17] === 100 ? 101 : 100;

  // a song sent a piece a block, or every piece twice (as the emulator delivers them)
  const send = (d, song, twice) => {
    for (const m of pieces(song)) {
      d.sysex(m);
      if (twice) {
        d.sysex(m);
      }
      d.run(1);
    }
  };

  for (const twice of [false, true]) {
    // four songs in a row, the device's song asked for after each: a, b, c, e
    const d = device();
    d.run(SECOND);
    send(d, first, twice);
    d.run(SECOND / 2);
    const a = requestSong(d).bytes;
    d.run(SECOND);

    // (half a second, a second: well inside the two the receiver remembers)
    send(d, late, twice);
    d.run(SECOND / 2);
    const b = requestSong(d).bytes;
    d.run(SECOND);

    send(d, early, twice);
    d.run(SECOND / 2);
    const c = requestSong(d).bytes;
    d.run(SECOND);

    // the same song once more: nothing to take
    send(d, early, twice);
    d.run(SECOND / 2);
    const again = requestSong(d).bytes;

    ok(
      model(late) !== null &&
        a &&
        same(a, first) &&
        b &&
        same(b, late) &&
        !same(first, late) &&
        same(first.slice(0, 2400), late.slice(0, 2400)),
      `a song, and a second later one that differs in its last piece ` +
        `only${twice ? ', every piece twice' : ''}: the second is taken ` +
        `(byte ${late.length - 17}: ${b ? b[b.length - 17] : -1}, was ${first[first.length - 17]})`,
    );
    ok(
      c && same(c, early) && again && same(again, early),
      `then one that differs in its first piece ` +
        `only${twice ? ', every piece twice' : ''}, and that one again: taken (tempo ${c ? tempo(c) : 0})`,
    );
  }

  // the pieces of the song that was taken, come again late, begin nothing: half of them, then a whole other song
  const d = device();
  d.run(SECOND);
  send(d, first, false);
  d.run(SECOND / 2);
  for (const m of pieces(first).slice(0, 5)) {
    d.sysex(m);
    d.run(1);
  }
  d.run(SECOND / 4);
  send(d, late, false);
  d.run(SECOND / 2);
  const f = requestSong(d).bytes;
  ok(f && same(f, late), 'pieces of the song before that come again, and then another song: that one is taken whole');

  // every piece twice; the same song once more; then, with no pause at all, one that differs in its first and in its
  // last piece
  const both = withTempo(late, 66);
  const third = device();
  third.run(SECOND);
  send(third, first, true);
  send(third, first, true);
  send(third, both, true);
  third.run(SECOND / 2);
  const h = requestSong(third).bytes;
  // (byte 2400 is the first of the last piece: the last piece of `both` differs from `first` further on, in the
  // byte the check before changed - 17 from the end -, and its first piece in the tempo)
  ok(
    h && same(h, both) && both[2400] === first[2400] && both[both.length - 17] !== first[first.length - 17],
    `a song twice over, every piece twice, and straight after it one that differs in its first and its last ` +
      `piece: taken as it is (tempo ${h ? tempo(h) : 0})`,
  );
}

// ---- the format a piece names is the song's first byte
{
  const d = device();
  d.run(SECOND);

  // the pieces say 5, the song begins with 6
  const song = withTempo(six, 88);
  const msgs = pieces(song).map((m) => {
    const c = m.slice();
    c[7] = 5;
    return c;
  });
  for (const m of msgs) {
    d.sysex(m);
    d.run(1);
  }
  d.run(SECOND);
  const f = requestSong(d).bytes;
  ok(
    f && same(f, six),
    'a song whose pieces name format 5 while its first byte is 6 is not taken: the factory song stays',
  );
}

// ---- the longest song there is: every sound and every lane in all eight patterns at 64 steps, 38 355 bytes in 160
// pieces
{
  // built by hand in format 6 (core/sfx_song.h): the factory song's head of 147 bytes, then a block a pattern
  const big = six.slice(0, 147);
  for (let p = 0; p < 8; p++) {
    // the pattern's head: grid 2 (64 steps), all 16 sounds have a row, all 24 lanes are used
    big.push(2, 0x7f, 0x7f, 3, 0x7f, 0x7f, 0x7f, 7);
    for (let t = 0; t < 16; t++) {
      // the steps that are on: 63 bits in nine bytes, one in the tenth
      for (let b = 0; b < 10; b++) {
        big.push(b < 9 ? (p * 31 + t * 7 + b * 13) % 128 : (p + t) & 1);
      }
      // a note, a velocity and a length for each of the 64 steps
      for (let i = 0; i < 64; i++) {
        big.push(36 + ((p * 5 + t * 3 + i) % 60));
      }
      for (let i = 0; i < 64; i++) {
        big.push((p + t + i) % 8);
      }
      for (let i = 0; i < 64; i++) {
        big.push((p * 11 + t * 5 + i * 3) % 64);
      }
    }
    // the lanes: a point on every step
    for (let l = 0; l < 24; l++) {
      for (let i = 0; i < 64; i++) {
        big.push(1 + ((p * 17 + l * 9 + i * 5) % 127));
      }
    }
  }

  // in, a piece a block, and out again
  const d = device();
  d.run(SECOND);
  const msgs = pieces(big);
  let sentBlocks = 0;
  for (const m of msgs) {
    d.sysex(m);
    d.run(1);
    sentBlocks++;
  }
  d.run(SECOND);
  const f = requestSong(d, 60 * SECOND);
  ok(
    big.length === 38355 && msgs.length === 160 && msgs.every((m) => m.length <= 251),
    'the longest song is 38 355 bytes: 160 pieces, none over 251 bytes',
  );
  ok(
    f.bytes && same(f.bytes, big),
    `it goes in and comes out byte for byte: in over ` +
      `${sentBlocks} blocks (a piece a block), out in 16 + ${f.pieces} messages over ${f.blocks} blocks ` +
      `(${((f.blocks * 128) / 48000).toFixed(2)} s on this host, which takes one SysEx a block)`,
  );

  // a song whose last piece is one byte - the byte the song that just went out has at that place - but of another
  // count of pieces: taken, not held for the device's own echo (which is known by its count and its length too)
  const small = six.slice(0, 147);
  for (let p = 0; p < 8; p++) {
    // patterns B to H: empty
    if (p > 0) {
      small.push(0, 0, 0, 0, 0, 0, 0, 0);
      continue;
    }
    // pattern A: ten sounds with a row, 16 steps
    small.push(0, 0x7f, 0x07, 0, 0, 0, 0, 0);
    for (let t = 0; t < 10; t++) {
      small.push(1 + t, 0, 0);
      for (let i = 0; i < 16; i++) {
        small.push(i === 0 ? 48 + t : 0);
      }
      for (let i = 0; i < 16; i++) {
        small.push(5);
      }
      for (let i = 0; i < 16; i++) {
        small.push(0);
      }
    }
  }
  const sm = pieces(small);
  const last = sm[sm.length - 1];
  const echoDevice = device();
  echoDevice.run(SECOND);

  // the factory song goes out: its echo would come now
  echoDevice.take();
  echoDevice.sysex(REQ_SONG);
  const sentOut = [];
  for (let n = 0; n < 5 * SECOND && sentOut.filter((m) => m[4] === 0x05).length < 11; n++) {
    echoDevice.run(1);
    sentOut.push(...echoDevice.take());
  }
  const sentBytes = sentOut.filter((m) => m[4] === 0x05).flatMap((m) => m.slice(10, -1));

  // in the very block after its last piece
  for (const m of sm) {
    echoDevice.sysex(m);
  }
  echoDevice.run(SECOND);
  const afterEcho = requestSong(echoDevice);
  ok(
    small.length === 721 &&
      sm.length === 4 &&
      last.length === 12 &&
      last[10] === sentBytes[3 * 240] &&
      afterEcho.bytes &&
      same(afterEcho.bytes, small),
    `a song of 721 bytes right after that one went out - its last piece the one byte ` +
      `${last[10]}, as the song just sent has there - is taken (${afterEcho.bytes ? afterEcho.bytes.length : 0} bytes back)`,
  );
}

// ---- a piece of a later format: said on the display, nothing of it kept
{
  const a = device();
  const b = device();

  // (both have seen the other side: the LINK light is on in both)
  for (const d of [a, b]) {
    d.run(SECOND);
    d.sysex([...HEAD, 0x03, ...Buffer.from('HI'), 0xf7]);
    d.run(3 * SECOND);
  }

  // both have sent their song and show the same display
  const before = requestSong(a).bytes;
  requestSong(b);
  for (const d of [a, b]) {
    d.run(3 * SECOND);
  }
  const alike = a.screen() === b.screen();

  // the first piece of a song that says format 7 goes to a: its display differs now
  const later = withTempo(six, 150);
  later[0] = 7;
  a.sysex(pieces(later)[0]);
  a.run(24);
  b.run(24);
  const said = a.screen() !== b.screen();

  // the rest of it, and the song asked for
  for (const m of pieces(later).slice(1)) {
    a.sysex(m);
    a.run(1);
  }
  a.run(SECOND);
  const after = requestSong(a).bytes;
  ok(
    alike && said && before && after && same(before, after) && tempo(after) !== 150,
    `piece 1 of a song in format 7: the display says so (NEWER SONG: UPDATE), and the song asked for afterwards ` +
      `is the one before (tempo ${after ? tempo(after) : 0})`,
  );
}

console.log(failed ? `wire FAILED (${failed})` : 'wire PASSED');
process.exit(failed ? 1 : 0);
