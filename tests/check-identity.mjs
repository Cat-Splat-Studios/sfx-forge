// The identity (firmware 1.4.0): REQUEST IDENTITY `F0 7D 56 42 07 F7` is answered with `F0 7D 56 42 08 <18 bytes> F7`.
// The SDK's wasmtest sends events but no SysEx, so this check is a small host of its own: it loads the built .wasm,
// hands it SysEx the way the emulator does (the exports vibe_wasm_sysex_buf / vibe_wasm_sysex_in), runs audio blocks
// and draws, and reads the firmware's MIDI out queue as wasmtest does. Nothing of the firmware is copied or rebuilt.
//   node tests/check-identity.mjs build/sfx-forge.wasm sfx-forge
import fs from 'fs';
const [, , wasmFile = 'build/sfx-forge.wasm', fwDir = 'sfx-forge'] = process.argv;
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const hex = (b) => b.map((v) => v.toString(16).padStart(2, '0').toUpperCase()).join(' ');
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

// ---- the version: three defines in the firmware, one line in its manifest
const src = fs.readFileSync(`${fwDir}/sfx-forge.c`, 'utf8');
const def = (n) => { const m = src.match(new RegExp(`^#define ${n}\\s+(\\d+)`, 'm')); return m ? +m[1] : -1; };
const ver = [def('FW_MAJOR'), def('FW_MINOR'), def('FW_PATCH')];
const manifest = JSON.parse(fs.readFileSync(`${fwDir}/firmware.json`, 'utf8')).version;
ok(ver.join('.') === manifest, `the firmware's defines (${ver.join('.')}) and firmware.json ("${manifest}") say the same version`);

// ---- a host: the firmware with nothing stored, 48 kHz, blocks of 128
const mod = new WebAssembly.Module(fs.readFileSync(wasmFile));
function device() {
  const files = new Map(); let inst;
  inst = new WebAssembly.Instance(mod, { env: {
    vibe_host_log: () => {}, vibe_host_request: () => {},
    vibe_host_store: (op, np, nl, dp, len) => {
      const name = Buffer.from(inst.exports.memory.buffer, np, nl).toString(), f = files.get(name);
      if (op === 0) { if (!f) return -1; const n = Math.min(len, f.length); new Uint8Array(inst.exports.memory.buffer, dp, n).set(f.subarray(0, n)); return n; }
      if (op === 1) { files.set(name, Buffer.from(new Uint8Array(inst.exports.memory.buffer, dp, len))); return 0; }
      if (op === 2) return f ? f.length : -1;
      return -1;
    } } });
  const e = inst.exports, out = [];
  e.vibe_wasm_init(48000);
  let blocks = 0;
  const drain = () => {   // (the queue's layout: the SDK's vibe_sdk.c, "SDK 9: MIDI out")
    const base = e.vibe_wasm_midi_out(), dv = new DataView(e.memory.buffer), w = dv.getUint32(base, true);
    let r = dv.getUint32(base + 4, true);
    for (; r !== w; r = (r + 1) >>> 0) {
      const q = base + 16 + (r % 256) * 6, st = dv.getUint8(q + 2), len = dv.getUint8(q + 5);
      if (!len && st === 0xF0) { const n = dv.getUint32(base + 12, true); out.push([...new Uint8Array(e.memory.buffer, base + 1552, n)]); dv.setUint32(base + 12, 0, true); }
      else out.push([st, dv.getUint8(q + 3), dv.getUint8(q + 4)].slice(0, len));
    }
    dv.setUint32(base + 4, w, true);
  };
  const d = {
    out,
    sysex(b) { new Uint8Array(e.memory.buffer, e.vibe_wasm_sysex_buf(), b.length).set(b); e.vibe_wasm_sysex_in(b.length); },
    button(id) { e.vibe_wasm_event(3, id, 1, 0, 0); d.run(4); e.vibe_wasm_event(3, id, 0, 0, 0); d.run(4); },
    run(n) { for (let i = 0; i < n; i++) { e.vibe_wasm_audio(128, 0); drain(); if (++blocks % 12 === 0) e.vibe_wasm_draw(); } },   // (a draw every 12 blocks, as wasmtest)
    take() { return out.splice(0); },
    screen() { const fb = new Uint16Array(e.memory.buffer, e.vibe_wasm_draw(), 480 * 320); let h = 2166136261; for (let i = 0; i < fb.length; i++) h = Math.imul(h ^ fb[i], 16777619) >>> 0; return h; },
  };
  return d;
}
const HEAD = [0xF0, 0x7D, 0x56, 0x42];
const REQ = [...HEAD, 0x07, 0xF7];
// hardware, version, profile, patch format, song format, sounds, kits, patterns, steps, lanes, bars, voices, engines, wavetables, values, flags
const WANT = [...HEAD, 0x08, 1, ...ver, 1, 2, 5, 16, 2, 8, 16, 24, 32, 16, 5, 6, 25, 0x0F, 0xF7];
const SECOND = 375;   // blocks

// ---- the request and its answer
{
  const d = device(); d.run(SECOND); d.take();
  d.sysex(REQ); d.run(24);
  const got = d.take();
  ok(got.length === 1 && eq(got[0], WANT), `the request is answered once with the 24 bytes ${hex(WANT)}` + (got.length === 1 && !eq(got[0], WANT) ? ` (got ${hex(got[0])})` : got.length !== 1 ? ` (got ${got.length} messages)` : ''));
  ok(got.length === 1 && got[0].length === 24 && got[0].slice(5, 23).every((v) => v < 128), 'its payload is 18 bytes, each 0..127');
  d.run(SECOND);
  ok(d.take().length === 0, 'and nothing follows it');

  d.sysex(REQ); d.sysex(REQ); d.run(24);          // the emulator delivers every message twice: both before the next block
  let two = d.take();
  ok(two.length === 2 && two.every((m) => eq(m, WANT)), `a doubled request (both before the next block) gets two answers (${two.length})`);
  d.sysex(REQ); d.run(1); d.sysex(REQ); d.run(24);
  two = d.take();
  ok(two.length === 2 && two.every((m) => eq(m, WANT)), `two requests a block apart get two answers (${two.length})`);

  for (const bad of [[...HEAD, 0x07, 0x00, 0xF7], [...HEAD, 0x07, 0x00, 0x00, 0xF7], [...HEAD, 0x07, ...Array(18).fill(1), 0xF7]]) d.sysex(bad);
  d.run(SECOND);
  ok(d.take().length === 0, 'a request of any other length (7, 8, 24 bytes) is not answered');
}

// ---- a pad's dump asked for right after the identity (a game does that when it opens the link): the host takes one
// SysEx a block, so the dump has to wait for the identity, not vanish
{
  const DUMP = (slot) => [...HEAD, 0x04, slot, 0xF7];
  const kinds = (got) => got.map((m) => m[4] === 0x08 ? 'ID' : m[4] === 0x01 ? 'D' + m[5] : m[4] === 0x05 ? 'S' : '?').join(' ');
  const tryIt = (name, want, send) => {
    const d = device(); d.run(SECOND); d.take();
    send(d); d.run(SECOND);
    const got = kinds(d.take());
    ok(got === want, `${name}: ${want}` + (got === want ? '' : ` (got ${got || 'nothing'})`));
  };
  tryIt('a request, then a pad asked for in the same block', 'ID D0', (d) => { d.sysex(REQ); d.sysex(DUMP(0)); });
  tryIt('both doubled in the same block (the emulator)', 'ID ID D0', (d) => { d.sysex(REQ); d.sysex(REQ); d.sysex(DUMP(0)); d.sysex(DUMP(0)); });
  tryIt('a doubled request, two blocks, the pad asked for twice', 'ID ID D0', (d) => { d.sysex(REQ); d.sysex(REQ); d.run(2); d.sysex(DUMP(0)); d.sysex(DUMP(0)); });
  tryIt('a request, then three pads asked for in the same block', 'ID D2 D5 D9', (d) => { d.sysex(REQ); d.sysex(DUMP(5)); d.sysex(DUMP(9)); d.sysex(DUMP(2)); });
  tryIt('a pad asked for, then a request', 'D0 ID', (d) => { d.sysex(DUMP(0)); d.sysex(REQ); });
  // a pad asked for while the sounds and the song go out comes after them, not among the song's sounds
  const d = device(); d.run(SECOND); d.take();
  d.sysex([...HEAD, 0x06, 0xF7]); d.run(30); d.sysex(DUMP(7)); d.run(2 * SECOND);
  const got = d.take();
  ok(got.length === 56 && got.slice(0, 16).every((m, i) => m[4] === 0x01 && m[5] === i) && got.slice(16, 55).every((m, k) => m[4] === 0x05 && m[5] === k) && got[55][4] === 0x01 && got[55][5] === 7,
    `a pad asked for while a song goes out: the 16 sounds, the 39 pieces, then that pad (${got.length} messages)`);
}

// ---- an identity that comes in (the device's own, echoed) does nothing: the twin that never got one is the same
const patchOf = (m) => m.slice(6, 34);
const sum = (p) => p.slice(0, 27).reduce((a, v) => a + v, 0) & 0x7F;
{
  const a = device(), b = device(), c = device();
  for (const d of [a, b, c]) d.run(SECOND);
  ok(a.screen() === b.screen(), 'two devices that got the same show the same display');
  for (const m of [WANT, WANT, [...HEAD, 0x08, 0xF7], [...HEAD, 0x08, ...Array(40).fill(0), 0xF7]]) a.sysex(m);
  c.sysex(REQ);
  for (const d of [a, b, c]) d.run(SECOND);
  ok(a.take().length === 0, 'an identity sent to the device (18 bytes twice, none, 40) is not answered');
  ok(a.screen() === b.screen(), 'and leaves the display as it was: the LINK light stays off');
  ok(c.take().length === 1 && c.screen() !== b.screen(), 'while a request lights it (the display can tell)');

  // every other message, after that traffic (a) and without it (b): byte for byte the same
  const script = (d) => {
    const got = {};
    d.sysex([...HEAD, 0x04, 3, 0xF7]); d.run(SECOND); got.dump = d.take();
    const p = patchOf(got.dump[0] || []).slice(); p[2] = (p[2] + 9) & 0x7F; p[27] = sum(p);
    got.sent = p;
    d.sysex([...HEAD, 0x02, 5, ...p, 0xF7]); d.run(SECOND);
    d.sysex([...HEAD, 0x04, 5, 0xF7]); d.run(SECOND); got.loaded = d.take();
    d.sysex([...HEAD, 0x02, 16 + 2, ...p, 0xF7]); d.run(SECOND);
    d.sysex([...HEAD, 0x06, 0xF7]); d.run(2 * SECOND); got.song = d.take();
    return got;
  };
  const ga = script(a), gb = script(b);
  const same = (x, y) => x.length === y.length && x.every((m, i) => eq(m, y[i]));
  ok(ga.dump.length === 1 && ga.dump[0].length === 35 && eq(ga.dump[0].slice(0, 6), [...HEAD, 0x01, 3]) && patchOf(ga.dump[0])[27] === sum(patchOf(ga.dump[0])), 'REQUEST (04) is answered with that pad\'s dump, a valid patch');
  ok(ga.loaded.length === 1 && eq(patchOf(ga.loaded[0]), ga.sent) && ga.loaded[0][5] === 5, 'LOAD (02) puts a patch into a pad: its dump gives it back');
  const dumps = ga.song.filter((m) => m[4] === 0x01), pieces = ga.song.filter((m) => m[4] === 0x05);
  ok(ga.song.length === 55 && dumps.length === 16 && pieces.length === 39 && pieces.every((m, k) => m[5] === k && m[6] === 39), 'REQUEST SONG (06) is answered with 16 sounds and the song in 39 pieces');
  ok(dumps.length === 16 && eq(patchOf(dumps[2]), ga.sent), 'LOAD to slot 18 put the patch on pad 3 of the MUSIC kit');
  const song = pieces.flatMap((m) => m.slice(7, -1));
  ok(song.length === 7699 && song[0] === 5, `the song is 7699 bytes of format 5 (${song.length}, ${song[0]})`);
  ok(same(ga.dump, gb.dump) && same(ga.loaded, gb.loaded) && same(ga.song, gb.song), 'the device that was sent identities answers all of it byte for byte as the one that was not');

  // the song back in (05), changed: another tempo. Then asked for again.
  const changed = song.slice(); const bpm = (changed[2] << 7 | changed[3]) === 97 ? 131 : 97; changed[2] = bpm >> 7; changed[3] = bpm & 0x7F;
  for (let k = 38; k >= 0; k--) { a.sysex([...HEAD, 0x05, k, 39, ...changed.slice(k * 200, k * 200 + 200), 0xF7]); a.run(2); }   // (in reverse: any order)
  a.run(SECOND); a.take();
  a.sysex([...HEAD, 0x06, 0xF7]); a.run(2 * SECOND);
  const again = a.take().filter((m) => m[4] === 0x05).flatMap((m) => m.slice(7, -1));
  ok(eq(again, changed), `SONG (05) sent in is taken: asked for again it comes back with the new tempo (${bpm})`);

  // a request while the sounds and the song are going out: the answer comes, and all 55 messages too
  a.sysex([...HEAD, 0x06, 0xF7]); a.run(30); a.sysex(REQ); a.run(2 * SECOND);
  const mixed = a.take();
  ok(mixed.filter((m) => eq(m, WANT)).length === 1 && mixed.filter((m) => m[4] === 0x01).length === 16 && eq(mixed.filter((m) => m[4] === 0x05).flatMap((m) => m.slice(7, -1)), changed), 'a request while a song goes out: one answer, and the song whole');

  // TEXT (03): on the LINK page (K6) two devices that are both linked differ by the text alone
  const x = device(), y = device();
  for (const d of [x, y]) { d.run(SECOND); d.button(15); d.sysex(REQ); d.run(SECOND); }
  const before = x.screen() === y.screen();
  x.sysex([...HEAD, 0x03, ...Buffer.from('HELLO'), 0xF7]); x.run(SECOND); y.run(SECOND);
  ok(before && x.screen() !== y.screen(), 'TEXT (03) shows on the LINK page');
}

console.log(failed ? `identity FAILED (${failed})` : 'identity PASSED');
process.exit(failed ? 1 : 0);
