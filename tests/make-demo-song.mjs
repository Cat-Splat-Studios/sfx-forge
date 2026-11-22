// Turns what the firmware sends on SEND (SONG page) into the files the Unity projects keep of the factory song:
// DemoSong.bytes - 16 patches of 28 bytes, then the packed song (format 6: 2487 bytes) - and, beside each, DemoSong.asset:
// the song asset whose `data:` line holds the same packed song as hex, with the asset format 2 a format-6 song has.
// The factory sounds and the factory song, untouched. The assets are edited as text: Unity is not started.
//   node <sdk>/tools/wasmtest.mjs build/sfx-forge.wasm tests/song-export.json out/song
//   node tests/make-demo-song.mjs out/song/midi.json            (writes the three places below)
//   node tests/make-demo-song.mjs out/song/midi.json some.bytes (writes that one file only)
import fs from 'fs';
const [, , midiFile, outFile] = process.argv;
const PLACES = ['unity/Demo/Assets/SfxDemo', 'unity/Demo/Assets/Showcase/Song', 'tests/unity-inspectors/Assets/Data'];
const msgs = JSON.parse(fs.readFileSync(midiFile, 'utf8')).map((m) => m.bytes).filter((b) => b[0] === 0xF0 && b[1] === 0x7D && b[2] === 0x56 && b[3] === 0x42);
// the song message of firmware 1.6.0: F0 7D 56 42 05 <piece lo> <piece hi> <format> <pieces lo> <pieces hi> <up to 240 bytes> F7
const dumps = msgs.filter((b) => b[4] === 1), chunks = msgs.filter((b) => b[4] === 5 && b[6] <= 1 && b.length >= 12);
const count = chunks.length ? chunks[0][8] | chunks[0][9] << 7 : 0;
if (dumps.length !== 16 || !count || chunks.length !== count) { console.error(`expected 16 sounds and all of the song's pieces, got ${dumps.length} and ${chunks.length} of ${count}`); process.exit(1); }
const out = [], song = [];
const PATCH = 28;
for (let slot = 0; slot < 16; slot++) out.push(...dumps.find((d) => d[5] === slot).slice(6, 6 + PATCH));
for (let k = 0; k < count; k++) {
  const c = chunks.find((m) => (m[5] | m[6] << 7) === k);
  if (!c || c[7] !== 6 || (k < count - 1 && c.length !== 251)) { console.error(`piece ${k} of the song is missing, or not a piece of a format-6 song`); process.exit(1); }
  song.push(...c.slice(10, -1));
}
out.push(...song);
if (song.length !== 2487 || song[0] !== 6) { console.error(`the factory song is 2487 bytes of format 6: got ${song.length} of format ${song[0]}`); process.exit(1); }

if (outFile) { fs.writeFileSync(outFile, Buffer.from(out)); console.log(`wrote ${outFile} (${out.length} bytes)`); process.exit(0); }

const hex = song.map((v) => v.toString(16).padStart(2, '0')).join('');
for (const dir of PLACES) {
  fs.writeFileSync(`${dir}/DemoSong.bytes`, Buffer.from(out));
  const file = `${dir}/DemoSong.asset`, text = fs.readFileSync(file, 'utf8'), nl = text.includes('\r\n') ? '\r\n' : '\n';
  let lines = text.split(/\r?\n/);
  const data = lines.filter((l) => /^  data: /.test(l)).length;
  if (data !== 1) { console.error(`${file}: expected exactly one "data:" line, found ${data}`); process.exit(1); }
  lines = lines.map((l) => /^  data: /.test(l) ? '  data: ' + hex : l).filter((l) => !/^  (format|savedBy): /.test(l));
  const at = lines.findIndex((l) => /^  m_EditorClassIdentifier:/.test(l));
  if (at < 0) { console.error(`${file}: no m_EditorClassIdentifier line to put the format after`); process.exit(1); }
  lines.splice(at + 1, 0, '  format: 2', '  savedBy: 2');     // (a format-6 song: the asset says 2, so that an older package knows it is from a newer one)
  fs.writeFileSync(file, lines.join(nl));
  console.log(`wrote ${dir}/DemoSong.bytes (${out.length} bytes) and the data of DemoSong.asset (${song.length} bytes, asset format 2)`);
}
