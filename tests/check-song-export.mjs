// tests/song-export.json: K5 twice (the SONG page), F5 SEND – the factory sounds and the factory song, as the device sends
// them. They must be, byte for byte, the file the Unity demo's song asset was built from (tests/make-demo-song.mjs): what
// a game holds still is what the device plays.
//   node tests/check-song-export.mjs out/song unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
const dir = process.argv[2] || 'out/song', file = process.argv[3] || 'unity/Demo/Assets/SfxDemo/DemoSong.bytes';
const msgs = JSON.parse(fs.readFileSync(`${dir}/midi.json`, 'utf8')).map((m) => m.bytes).filter((b) => b[0] === 0xF0 && b[1] === 0x7D && b[2] === 0x56 && b[3] === 0x42);
const dumps = msgs.filter((b) => b[4] === 1), chunks = msgs.filter((b) => b[4] === 5), out = [];
for (let slot = 0; slot < 16; slot++) out.push(...(dumps.find((d) => d[5] === slot) || []).slice(6, 6 + 28));
const PIECES = 11, SONG = 2487;   // the factory song in format 6: 2487 bytes, 11 pieces of up to 240 (7699 bytes in 39 pieces before firmware 1.6.0)
for (let k = 0; k < PIECES; k++) out.push(...(chunks.find((c) => (c[5] | c[6] << 7) === k && (c[8] | c[9] << 7) === PIECES && c[7] === 6) || []).slice(10, -1));
const kept = [...fs.readFileSync(file)];
let first = -1;
for (let i = 0; i < Math.max(out.length, kept.length) && first < 0; i++) if (out[i] !== kept[i]) first = i;
const good = dumps.length === 16 && chunks.length === PIECES && out.length === 16 * 28 + SONG && out[16 * 28] === 6 && first < 0;
console.log((good ? '  ok    ' : '  FAIL  ') + `SEND gives the factory sounds and song byte for byte as the demo's song asset holds them (${dumps.length} sounds, ${chunks.length} pieces, ${out.length} bytes${first < 0 ? '' : ', first difference at byte ' + first})`);
console.log(good ? 'song export PASSED' : 'song export FAILED');
process.exit(good ? 0 : 1);
