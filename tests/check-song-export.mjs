// What this check proves: what a game holds still is what the device plays.
//
// The scene (tests/song-export.json, rendered by the SDK's wasmtest into out/song): K5 twice (the SONG page), F5 SEND
// – the factory sounds and the factory song, as the device sends them. They must be, byte for byte, the file the
// Unity demo's song asset was built from (tests/make-demo-song.mjs).
//
//   node tests/check-song-export.mjs out/song unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';

const dir = process.argv[2] || 'out/song';
const file = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';

// ---- the SysEx messages of SFX FORGE that went out: the sounds (type 1) and the pieces of the song (type 5)
const msgs = JSON.parse(fs.readFileSync(`${dir}/midi.json`, 'utf8'))
  .map((m) => m.bytes)
  .filter((b) => b[0] === 0xf0 && b[1] === 0x7d && b[2] === 0x56 && b[3] === 0x42);
const dumps = msgs.filter((b) => b[4] === 1);
const chunks = msgs.filter((b) => b[4] === 5);

// ---- put together as the file is: the 16 sounds of 28 bytes in the order of their slots ...
const out = [];
for (let slot = 0; slot < 16; slot++) {
  const dump = dumps.find((d) => d[5] === slot) || [];
  out.push(...dump.slice(6, 6 + 28));
}

// ... then the song's pieces in order.
// the factory song in format 6: 2487 bytes, 11 pieces of up to 240 (7699 bytes in 39 pieces before firmware 1.6.0)
const PIECES = 11;
const SONG = 2487;
for (let k = 0; k < PIECES; k++) {
  const isPieceK = (c) => (c[5] | (c[6] << 7)) === k && (c[8] | (c[9] << 7)) === PIECES && c[7] === 6;
  const piece = chunks.find(isPieceK) || [];
  out.push(...piece.slice(10, -1));
}

// ---- against the file: the first byte that differs, or -1
const kept = [...fs.readFileSync(file)];
let first = -1;
for (let i = 0; i < Math.max(out.length, kept.length) && first < 0; i++) {
  if (out[i] !== kept[i]) {
    first = i;
  }
}

const good =
  dumps.length === 16 && chunks.length === PIECES && out.length === 16 * 28 + SONG && out[16 * 28] === 6 && first < 0;

console.log(
  (good ? '  ok    ' : '  FAIL  ') +
    `SEND gives the factory sounds and song byte for byte as the demo's song asset holds them ` +
    `(${dumps.length} sounds, ${chunks.length} pieces, ${out.length} bytes` +
    `${first < 0 ? '' : ', first difference at byte ' + first})`,
);
console.log(good ? 'song export PASSED' : 'song export FAILED');
process.exit(good ? 0 : 1);
