// The song on the wire (firmware 1.6.0): pieces of up to 240 bytes with a 14-bit piece number and count and the song's
// format in every piece - and, still taken, the shape of before: 39 pieces of 200 bytes of a format-5 song. A small
// host of its own (tests/host.mjs: wasmtest sends no SysEx).
//   node tests/check-wire.mjs build/sfx-forge.wasm tests/data/DemoSong-format5.bytes unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { load, HEAD, SECOND, pieces, oldPieces } from './host.mjs';
import { model, pattern, same, tempo } from './song-read.mjs';
const [, , wasmFile = 'build/sfx-forge.wasm', oldFile = 'tests/data/DemoSong-format5.bytes', newFile = 'unity/Demo/Assets/SfxDemo/DemoSong.bytes'] = process.argv;
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const device = load(wasmFile);
const five = [...fs.readFileSync(oldFile)].slice(16 * 28), six = [...fs.readFileSync(newFile)].slice(16 * 28);
const withTempo = (s, bpm) => { const c = s.slice(); c[2] = bpm >> 7; c[3] = bpm & 0x7F; return c; };
const REQ_SONG = [...HEAD, 0x06, 0xF7];
// asks for the song and runs until its last piece is out: the bytes, the messages, the blocks it took
const fetch = (d, limit = 20 * SECOND) => {
  d.take(); d.sysex(REQ_SONG);
  const got = []; let blocks = 0, K = -1;
  while (blocks < limit) {
    d.run(1); blocks++;
    got.push(...d.take());
    const p = got.filter((m) => m[4] === 0x05);
    if (p.length) K = p[0][8] | p[0][9] << 7;
    if (K > 0 && p.length >= K) break;
  }
  d.run(SECOND / 4); got.push(...d.take());
  const p = got.filter((m) => m[4] === 0x05);
  const whole = p.length === K && p.every((m, k) => (m[5] | m[6] << 7) === k && m[7] === p[0][7] && m.length <= 251 && (k === K - 1 || m.length === 251));
  return { bytes: whole ? p.flatMap((m) => m.slice(10, -1)) : null, messages: got.length, pieces: p.length, blocks };
};

// ---- the factory song goes out in the new shape
{
  const d = device(); d.run(SECOND);
  const f = fetch(d);
  ok(f.bytes && same(f.bytes, six) && f.pieces === 11 && f.messages === 27, `the factory song goes out in 16 + 11 messages (${f.messages}): 2487 bytes of format 6, ${f.blocks} blocks (${(f.blocks * 128 / 48).toFixed(0)} ms) from the request to its last piece`);
}

// ---- the shape of before comes in: a format-5 song in 39 pieces of 200. It is taken, and comes back as format 6 with the same content
{
  const d = device(); d.run(SECOND);
  const song = withTempo(five, 97);
  ok(five.length === 7699 && five[0] === 5 && model(five) !== null, 'the fixture is a format-5 song of 7699 bytes');
  for (const m of oldPieces(song)) { d.sysex(m); d.run(1); }
  d.run(SECOND);
  const f = fetch(d);
  const alike = f.bytes && model(f.bytes) && [0, 1, 2, 3, 4, 5, 6, 7].every((p) => same(pattern(f.bytes, p), pattern(song, p))) && same(f.bytes.slice(1, 147), song.slice(1, 147));
  ok(f.bytes && f.bytes[0] === 6 && tempo(f.bytes) === 97 && alike && same(f.bytes, withTempo(six, 97)),
     `the old shape in (39 pieces of 200, format 5): taken, and it comes back as format 6 with the same head, steps, notes, velocities, lengths and points (${f.bytes ? f.bytes.length : 0} bytes, tempo ${f.bytes ? tempo(f.bytes) : 0})`);
}

// ---- the new shape in, in reverse and every piece twice
{
  const d = device(); d.run(SECOND);
  const song = withTempo(six, 131), msgs = pieces(song);
  for (let k = msgs.length - 1; k >= 0; k--) { d.sysex(msgs[k]); d.sysex(msgs[k]); d.run(1); }
  d.run(SECOND);
  const f = fetch(d);
  ok(f.bytes && same(f.bytes, song), `the new shape in, last piece first and every piece twice: taken (tempo ${f.bytes ? tempo(f.bytes) : 0})`);
}

// ---- a second song soon after the first, that differs from it in a late piece only - or in the first only
{
  const first = withTempo(six, 131), late = first.slice(), early = withTempo(late, 77);
  late[late.length - 17] = late[late.length - 17] === 100 ? 101 : 100;       // the last point of pattern F's last lane: in the last piece
  const send = (d, song, twice) => { for (const m of pieces(song)) { d.sysex(m); if (twice) d.sysex(m); d.run(1); } };
  for (const twice of [false, true]) {
    const d = device(); d.run(SECOND);
    send(d, first, twice); d.run(SECOND / 2);
    const a = fetch(d).bytes;
    d.run(SECOND);
    send(d, late, twice); d.run(SECOND / 2);                                // (half a second, a second: well inside the two the receiver remembers)
    const b = fetch(d).bytes;
    d.run(SECOND);
    send(d, early, twice); d.run(SECOND / 2);
    const c = fetch(d).bytes;
    d.run(SECOND);
    send(d, early, twice); d.run(SECOND / 2);                               // the same song once more: nothing to take
    const e = fetch(d).bytes;
    ok(model(late) !== null && a && same(a, first) && b && same(b, late) && !same(first, late) && same(first.slice(0, 2400), late.slice(0, 2400)),
       `a song, and a second later one that differs in its last piece only${twice ? ', every piece twice' : ''}: the second is taken (byte ${late.length - 17}: ${b ? b[b.length - 17] : -1}, was ${first[first.length - 17]})`);
    ok(c && same(c, early) && e && same(e, early), `then one that differs in its first piece only${twice ? ', every piece twice' : ''}, and that one again: taken (tempo ${c ? tempo(c) : 0})`);
  }
  // the pieces of the song that was taken, come again late, begin nothing: half of them, then a whole other song
  const d = device(); d.run(SECOND);
  send(d, first, false); d.run(SECOND / 2);
  for (const m of pieces(first).slice(0, 5)) { d.sysex(m); d.run(1); }
  d.run(SECOND / 4);
  send(d, late, false); d.run(SECOND / 2);
  const f = fetch(d).bytes;
  ok(f && same(f, late), 'pieces of the song before that come again, and then another song: that one is taken whole');
  // every piece twice; the same song once more; then, with no pause at all, one that differs in its first and in its last piece
  const both = withTempo(late, 66), g = device(); g.run(SECOND);
  send(g, first, true); send(g, first, true); send(g, both, true); g.run(SECOND / 2);
  const h = fetch(g).bytes;
  ok(h && same(h, both) && both[2400] === first[2400] && both[both.length - 17] !== first[first.length - 17], `a song twice over, every piece twice, and straight after it one that differs in its first and its last piece: taken as it is (tempo ${h ? tempo(h) : 0})`);
}

// ---- the format a piece names is the song's first byte
{
  const d = device(); d.run(SECOND);
  const song = withTempo(six, 88), msgs = pieces(song).map((m) => { const c = m.slice(); c[7] = 5; return c; });   // the pieces say 5, the song begins with 6
  for (const m of msgs) { d.sysex(m); d.run(1); }
  d.run(SECOND);
  const f = fetch(d).bytes;
  ok(f && same(f, six), 'a song whose pieces name format 5 while its first byte is 6 is not taken: the factory song stays');
}

// ---- the longest song there is: every sound and every lane in all eight patterns at 64 steps, 38 355 bytes in 160 pieces
{
  const big = six.slice(0, 147);
  for (let p = 0; p < 8; p++) {
    big.push(2, 0x7F, 0x7F, 3, 0x7F, 0x7F, 0x7F, 7);
    for (let t = 0; t < 16; t++) {
      for (let b = 0; b < 10; b++) big.push(b < 9 ? (p * 31 + t * 7 + b * 13) % 128 : (p + t) & 1);       // the steps that are on: 63 bits in nine bytes, one in the tenth
      for (let i = 0; i < 64; i++) big.push(36 + (p * 5 + t * 3 + i) % 60);
      for (let i = 0; i < 64; i++) big.push((p + t + i) % 8);
      for (let i = 0; i < 64; i++) big.push((p * 11 + t * 5 + i * 3) % 64);
    }
    for (let l = 0; l < 24; l++) for (let i = 0; i < 64; i++) big.push(1 + (p * 17 + l * 9 + i * 5) % 127);
  }
  const d = device(); d.run(SECOND);
  const msgs = pieces(big);
  let sentBlocks = 0;
  for (const m of msgs) { d.sysex(m); d.run(1); sentBlocks++; }
  d.run(SECOND);
  const f = fetch(d, 60 * SECOND);
  ok(big.length === 38355 && msgs.length === 160 && msgs.every((m) => m.length <= 251), 'the longest song is 38 355 bytes: 160 pieces, none over 251 bytes');
  ok(f.bytes && same(f.bytes, big), `it goes in and comes out byte for byte: in over ${sentBlocks} blocks (a piece a block), out in 16 + ${f.pieces} messages over ${f.blocks} blocks (${(f.blocks * 128 / 48000).toFixed(2)} s on this host, which takes one SysEx a block)`);

  // a song whose last piece is one byte - the byte the song that just went out has at that place - but of another count
  // of pieces: taken, not held for the device's own echo (which is known by its count and its length too)
  const small = six.slice(0, 147);
  for (let p = 0; p < 8; p++) {
    if (p > 0) { small.push(0, 0, 0, 0, 0, 0, 0, 0); continue; }
    small.push(0, 0x7F, 0x07, 0, 0, 0, 0, 0);                              // pattern A: ten sounds with a row, 16 steps
    for (let t = 0; t < 10; t++) { small.push(1 + t, 0, 0); for (let i = 0; i < 16; i++) small.push(i === 0 ? 48 + t : 0); for (let i = 0; i < 16; i++) small.push(5); for (let i = 0; i < 16; i++) small.push(0); }
  }
  const sm = pieces(small), last = sm[sm.length - 1];
  const e = device(); e.run(SECOND);
  e.take(); e.sysex(REQ_SONG);                                             // the factory song goes out: its echo would come now
  const sentOut = [];
  for (let n = 0; n < 5 * SECOND && sentOut.filter((m) => m[4] === 0x05).length < 11; n++) { e.run(1); sentOut.push(...e.take()); }
  const out = { bytes: sentOut.filter((m) => m[4] === 0x05).flatMap((m) => m.slice(10, -1)) };
  for (const m of sm) e.sysex(m);                                          // in the very block after its last piece                                        
  e.run(SECOND);
  const g = fetch(e);
  ok(small.length === 721 && sm.length === 4 && last.length === 12 && out.bytes && last[10] === out.bytes[3 * 240] && g.bytes && same(g.bytes, small),
     `a song of 721 bytes right after that one went out - its last piece the one byte ${last[10]}, as the song just sent has there - is taken (${g.bytes ? g.bytes.length : 0} bytes back)`);
}

// ---- a piece of a later format: said on the display, nothing of it kept
{
  const a = device(), b = device();
  for (const d of [a, b]) { d.run(SECOND); d.sysex([...HEAD, 0x03, ...Buffer.from('HI'), 0xF7]); d.run(3 * SECOND); }   // (both have seen the other side: the LINK light is on in both)
  const before = fetch(a).bytes; fetch(b);
  for (const d of [a, b]) d.run(3 * SECOND);
  const alike = a.screen() === b.screen();
  const later = withTempo(six, 150); later[0] = 7;
  a.sysex(pieces(later)[0]); a.run(24); b.run(24);
  const said = a.screen() !== b.screen();
  for (const m of pieces(later).slice(1)) { a.sysex(m); a.run(1); }
  a.run(SECOND);
  const after = fetch(a).bytes;
  ok(alike && said && before && after && same(before, after) && tempo(after) !== 150, `piece 1 of a song in format 7: the display says so (NEWER SONG: UPDATE), and the song asked for afterwards is the one before (tempo ${after ? tempo(after) : 0})`);
}

console.log(failed ? `wire FAILED (${failed})` : 'wire PASSED');
process.exit(failed ? 1 : 0);
