// Turns what the firmware sends on SEND (SONG page) into the files the Unity projects keep of the factory song:
// DemoSong.bytes - 16 patches of 28 bytes, then the packed song (format 6: 2487 bytes) - and, beside each,
// DemoSong.asset: the song asset whose `data:` line holds the same packed song as hex, with the asset format 2 a
// format-6 song has. The factory sounds and the factory song, untouched. The assets are edited as text: Unity is not
// started.
//
//   node <sdk>/tools/wasmtest.mjs build/sfx-forge.wasm tests/song-export.json out/song
//   node tests/make-demo-song.mjs out/song/midi.json            (writes the three places below)
//   node tests/make-demo-song.mjs out/song/midi.json some.bytes (writes that one file only)
import fs from 'fs';

const midiFile = process.argv[2];
const outFile = process.argv[3];

// the folders that each hold a DemoSong.bytes and a DemoSong.asset
const PLACES = ['unity/Demo/Assets/SfxDemo', 'unity/Demo/Assets/Showcase/Song', 'tests/unity-inspectors/Assets/Data'];

// ---- the SysEx messages of SFX FORGE in the test's MIDI: the sounds (type 1) and the pieces of the song (type 5)
const msgs = JSON.parse(fs.readFileSync(midiFile, 'utf8'))
  .map((m) => m.bytes)
  .filter((b) => b[0] === 0xf0 && b[1] === 0x7d && b[2] === 0x56 && b[3] === 0x42);

// the song message of firmware 1.6.0:
//   F0 7D 56 42 05 <piece lo> <piece hi> <format> <pieces lo> <pieces hi> <up to 240 bytes> F7
const dumps = msgs.filter((b) => b[4] === 1);
// (b[6] <= 1 and 12 bytes or more: the new shape, whose byte 6 is the piece number's high bits. The old shape has
// its count of pieces there, 39.)
const chunks = msgs.filter((b) => b[4] === 5 && b[6] <= 1 && b.length >= 12);
const count = chunks.length ? chunks[0][8] | (chunks[0][9] << 7) : 0;
if (dumps.length !== 16 || !count || chunks.length !== count) {
  console.error(
    `expected 16 sounds and all of the song's pieces, got ${dumps.length} and ${chunks.length} of ${count}`,
  );
  process.exit(1);
}

// ---- the file's bytes: the 16 patches in the order of their slots, then the song's pieces in order
const out = [];
const song = [];
const PATCH = 28;
for (let slot = 0; slot < 16; slot++) {
  out.push(...dumps.find((d) => d[5] === slot).slice(6, 6 + PATCH));
}
for (let k = 0; k < count; k++) {
  const piece = chunks.find((m) => (m[5] | (m[6] << 7)) === k);
  if (!piece || piece[7] !== 6 || (k < count - 1 && piece.length !== 251)) {
    console.error(`piece ${k} of the song is missing, or not a piece of a format-6 song`);
    process.exit(1);
  }
  song.push(...piece.slice(10, -1));
}
out.push(...song);
if (song.length !== 2487 || song[0] !== 6) {
  console.error(`the factory song is 2487 bytes of format 6: got ${song.length} of format ${song[0]}`);
  process.exit(1);
}

// ---- one file only, when it was named
if (outFile) {
  fs.writeFileSync(outFile, Buffer.from(out));
  console.log(`wrote ${outFile} (${out.length} bytes)`);
  process.exit(0);
}

// ---- or the three places: DemoSong.bytes, and the song as hex into the "data:" line of DemoSong.asset
const hex = song.map((v) => v.toString(16).padStart(2, '0')).join('');
for (const dir of PLACES) {
  fs.writeFileSync(`${dir}/DemoSong.bytes`, Buffer.from(out));
  const file = `${dir}/DemoSong.asset`;
  const text = fs.readFileSync(file, 'utf8');
  // (the asset keeps the line ends it has)
  const nl = text.includes('\r\n') ? '\r\n' : '\n';
  let lines = text.split(/\r?\n/);
  const data = lines.filter((l) => /^  data: /.test(l)).length;
  if (data !== 1) {
    console.error(`${file}: expected exactly one "data:" line, found ${data}`);
    process.exit(1);
  }

  // the new data line; the lines "format:" and "savedBy:" are taken out, and put in again below
  lines = lines
    .map((l) => (/^  data: /.test(l) ? '  data: ' + hex : l))
    .filter((l) => !/^  (format|savedBy): /.test(l));
  const at = lines.findIndex((l) => /^  m_EditorClassIdentifier:/.test(l));
  if (at < 0) {
    console.error(`${file}: no m_EditorClassIdentifier line to put the format after`);
    process.exit(1);
  }

  // (a format-6 song: the asset says 2, so that an older package knows it is from a newer one)
  lines.splice(at + 1, 0, '  format: 2', '  savedBy: 2');
  fs.writeFileSync(file, lines.join(nl));
  console.log(
    `wrote ${dir}/DemoSong.bytes (${out.length} bytes) ` +
      `and the data of DemoSong.asset (${song.length} bytes, asset format 2)`,
  );
}
