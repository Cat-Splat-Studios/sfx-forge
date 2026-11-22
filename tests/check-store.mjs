// What this check proves: what the device stores (firmware 1.6.0). The song goes to song6.bin (format 6, and a check
// of the song.bin beside it); song.bin - the file of the firmwares up to 1.5.0, format 5 - is still read and never
// written. A song.bin that was written later by an older firmware wins.
//
// The scene: a small host of its own (tests/host.mjs); the stored files are a Map here. Each part below is a block
// { ... }; the Map `files` goes from one part to the next, as a device's store goes from one start to the next.
//
//   node tests/check-store.mjs build/sfx-forge.wasm tests/data/DemoSong-format5.bytes \
//        unity/Demo/Assets/SfxDemo/DemoSong.bytes
import fs from 'fs';
import { load, HEAD, SECOND, BUTTON, pieces } from './host.mjs';
import { same, tempo } from './song-read.mjs';

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

// a device with these stored files, one second after it was switched on
const start = (files) => {
  const d = device(files);
  d.run(SECOND);
  return d;
};

// the song the device has, asked for over SysEx
const songOf = (d) => d.song().bytes;

// F6 SAVE (the SOUND page, where it starts)
const save = (d) => {
  d.button(BUTTON.F6);
  d.run(SECOND / 4);
};

// a song sent to the device over SysEx, a piece a block
const put = (d, song) => {
  for (const m of pieces(song)) {
    d.sysex(m);
    d.run(1);
  }
  d.run(SECOND);
};

// the check of a file's bytes, as the firmware computes it (the firmware's store_check)
const storeCheck = (b) => {
  let h = 1;
  for (const v of b) {
    h = (h * 31 + v) % 16381;
  }
  return 1 + h;
};

// ---- an update keeps the song: a format-5 song.bin, as firmware 1.5.0 left it, is loaded
const files = new Map([['song.bin', Buffer.from(withTempo(five, 97))]]);
const kept = Buffer.from(files.get('song.bin'));
{
  const d = start(files);
  const s = songOf(d);
  ok(
    s && s[0] === 6 && tempo(s) === 97 && same(s, withTempo(six, 97)),
    `a song.bin of format 5 with another tempo is loaded (tempo ${s ? tempo(s) : 0})`,
  );

  save(d);
  const now = files.get('song6.bin');
  // the last two bytes of song6.bin: 14 bits
  const c = now ? now[now.length - 2] | (now[now.length - 1] << 7) : -1;
  ok(
    now && now.length === 2487 + 2 && same([...now.subarray(0, 2487)], withTempo(six, 97)) && c === storeCheck(kept),
    `SAVE writes song6.bin: the format-6 song and two bytes, the check of the song.bin beside it ` +
      `(${now ? now.length : 0} bytes, check ${c})`,
  );
  ok(files.get('song.bin').equals(kept), 'and leaves song.bin as it was, byte for byte');

  put(d, withTempo(six, 131));
  save(d);
  ok(files.get('song.bin').equals(kept), 'another song saved: song.bin is still not written');
}
// the next start
{
  const s = songOf(start(files));
  ok(
    s && tempo(s) === 131,
    `with both files there, song6.bin is the one that is loaded (tempo ${s ? tempo(s) : 0}; song.bin says 97)`,
  );
}

// ---- back to firmware 1.5 and forward again: 1.5 saves a song to song.bin - it is the later one, and it is loaded
files.set('song.bin', Buffer.from(withTempo(five, 111)));
{
  // it is said on the display for a moment (OLDER FIRMWARE'S SONG) - a device with that song.bin alone says nothing -
  // and the song of song6.bin is one F2 away
  const d = device(files);
  const alone = device(new Map([['song.bin', files.get('song.bin')]]));
  d.run(SECOND / 2);
  alone.run(SECOND / 2);
  const said = d.screen() !== alone.screen();
  d.run(3 * SECOND);
  alone.run(3 * SECOND);
  const gone = d.screen() === alone.screen();
  ok(
    said && gone,
    'the display says so for a moment: it differs from a device that has that song.bin alone, and is the same ' +
      'three seconds later',
  );

  // SEQ, F2 UNDO
  d.button(BUTTON.K5);
  d.button(BUTTON.F2);
  d.run(SECOND / 4);
  const other = songOf(d);
  d.button(BUTTON.F2);
  d.run(SECOND / 4);
  const again = songOf(d);
  ok(
    other && tempo(other) === 131 && again && tempo(again) === 111,
    `F2 brings the song of song6.bin, F2 again the older firmware's (tempo ` +
      `${other ? tempo(other) : 0}, then ${again ? tempo(again) : 0})`,
  );

  alone.button(BUTTON.K5);
  alone.button(BUTTON.F2);
  alone.run(SECOND / 4);
  const same111 = songOf(alone);
  ok(same111 && tempo(same111) === 111, 'a plain update (song.bin alone) leaves F2 nothing to take back');
}
// the next start, and a SAVE on 1.6
{
  const d = start(files);
  const s = songOf(d);
  ok(
    s && tempo(s) === 111,
    `a song.bin written after song6.bin (what a SAVE on 1.5 does) is loaded over it (tempo ${s ? tempo(s) : 0})`,
  );
  put(d, withTempo(six, 140));
  save(d);
}
// the start after that
{
  const s = songOf(start(files));
  ok(
    s && tempo(s) === 140 && tempo([...files.get('song.bin')]) === 111,
    `after the next SAVE song6.bin is the one again (tempo ${s ? tempo(s) : 0}; song.bin still says 111)`,
  );
}

// ---- files that are not a song
{
  const long = Buffer.concat([Buffer.from(withTempo(six, 99)), Buffer.alloc(38357 - 2487 + 1)]);
  const s = songOf(start(new Map([['song6.bin', long]])));
  ok(
    long.length === 38358 && s && same(s, six),
    'a song6.bin of 38 358 bytes - one more than the longest there can be - is not taken: the factory song',
  );

  const both = new Map([
    ['song6.bin', long],
    ['song.bin', Buffer.from(withTempo(five, 97))],
  ]);
  const t = songOf(start(both));
  ok(t && tempo(t) === 97, 'and with a song.bin beside it, that one is loaded');

  const cut = Buffer.from(withTempo(six, 99)).subarray(0, 2000);
  const u = songOf(start(new Map([['song6.bin', cut]])));
  ok(u && same(u, six), 'a song6.bin that is cut off is not taken either');
}

// ---- a store that takes nothing more: SAVE says that it was not saved, and what was stored stays
{
  const full = new Map([['song6.bin', Buffer.from([...withTempo(six, 99), 0, 0])]]);
  const room = new Map([['song6.bin', Buffer.from([...withTempo(six, 99), 0, 0])]]);
  const kept = Buffer.from(full.get('song6.bin'));

  // two devices get the same song and show the same display
  const a = start(full);
  const b = start(room);
  put(a, withTempo(six, 150));
  put(b, withTempo(six, 150));
  a.run(3 * SECOND);
  b.run(3 * SECOND);
  const alike = a.screen() === b.screen();

  // SAVE on both; the store of the first takes nothing more: NOT SAVED: STORE FULL beside SAVED
  full.full = true;
  save(a);
  save(b);
  const said = a.screen() !== b.screen();
  ok(
    alike && said && full.get('song6.bin').equals(kept) && tempo([...room.get('song6.bin')]) === 150,
    'a SAVE the store does not take: the display says another thing than after one that was kept (NOT SAVED: ' +
      'STORE FULL), and the file is as it was',
  );
}

// ---- a factory reset: SHIFT + MENU twice. What was saved is gone, the older firmware's song too
{
  const d = start(files);
  d.shift(BUTTON.MENU);
  d.shift(BUTTON.MENU);
  d.run(SECOND);
  const s = songOf(d);
  const now = files.get('song6.bin');
  ok(
    !files.has('song.bin') &&
      now &&
      same([...now.subarray(0, now.length - 2)], six) &&
      now[now.length - 2] === 0 &&
      now[now.length - 1] === 0 &&
      s &&
      same(s, six),
    'a factory reset deletes song.bin and saves the factory song to song6.bin (its check says: no song.bin)',
  );

  const again = songOf(start(files));
  ok(again && same(again, six), 'and the factory song is what the next start loads');
}

console.log(failed ? `store FAILED (${failed})` : 'store PASSED');
process.exit(failed ? 1 : 0);
