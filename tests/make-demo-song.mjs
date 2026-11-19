// Turns what the firmware sends on SEND (SONG page) into the file the Unity demo's song asset is built from:
// 16 patches of 28 bytes, then the packed song (2498 bytes). The factory sounds and the factory song, untouched.
//   node <sdk>/tools/wasmtest.mjs build/sfx-forge.wasm tests/song-export.json out/song
//   node tests/make-demo-song.mjs out/song/midi.json unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
const [, , midiFile, outFile] = process.argv;
const msgs = JSON.parse(fs.readFileSync(midiFile, 'utf8')).map((m) => m.bytes).filter((b) => b[0] === 0xF0 && b[1] === 0x7D && b[2] === 0x56 && b[3] === 0x42);
const dumps = msgs.filter((b) => b[4] === 1), chunks = msgs.filter((b) => b[4] === 5);
if (dumps.length !== 16 || chunks.length !== 13) { console.error(`expected 16 sounds and 13 song pieces, got ${dumps.length} and ${chunks.length}`); process.exit(1); }
const out = [];
const PATCH = 28;
for (let slot = 0; slot < 16; slot++) out.push(...dumps.find((d) => d[5] === slot).slice(6, 6 + PATCH));
for (let k = 0; k < 13; k++) out.push(...chunks.find((c) => c[5] === k).slice(7, -1));
if (out.length !== 16 * PATCH + 2498) { console.error(`wrong size: ${out.length}`); process.exit(1); }
fs.writeFileSync(outFile, Buffer.from(out));
console.log(`wrote ${outFile} (${out.length} bytes)`);
