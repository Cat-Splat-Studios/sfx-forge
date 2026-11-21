// tests/edit.json: nothing plays. The keys that ask twice, COPY, CLEAR and UNDO are pressed, and after each the song is
// sent (SONG page, F5): the ten songs that went out say what each key did.
//   0  F4 NEW once, and the question runs out          the factory song, untouched
//   1  F4 NEW twice                                     an empty song
//   2  F2 UNDO                                          the factory song again, byte for byte
//   3  SEQ, F5 COPY on pattern A                        A is in G, the first empty pattern; the chain is left alone
//   4  F5 COPY on G, then once more on H                G is in H; then no pattern is empty and nothing happens
//   5  SEQ, the kick selected, F4 CLEAR                 the kick's steps are gone from H, the other sounds stay
//   6  F2 UNDO                                          they are back
//   7  SHIFT + pad 16 once, the question runs out       the arrangement stays
//   8  SHIFT + pad 16 twice                             no arrangement; the patterns stay
//   9  F2 UNDO                                          the arrangement is back
import { songs, steps, pattern, empty, same, chain, tempo, arrLen } from './song-read.mjs';
const dir = process.argv[2] || 'out/edit';
let failed = 0;
const ok = (c, t) => { console.log((c ? '  ok    ' : '  FAIL  ') + t); if (!c) failed++; };
const sent = songs(`${dir}/midi.json`).map((s) => s.bytes);
ok(sent.length === 10, `ten songs went out (${sent.length})`);
if (sent.length === 10) {
  const [was, fresh, undone, copied, twice, cleared, uncleared, asked, noArr, arrBack] = sent;
  const A = 0, G = 6, H = 7, KICK = 2, SNARE = 1, all = [...Array(8)].map((_, p) => p);
  const patterns = (a, b, list = all) => list.every((p) => same(pattern(a, p), pattern(b, p)));
  ok(arrLen(was) === 16 && chain(was) === 6 && !empty(was, A) && empty(was, G) && empty(was, H), `NEW pressed once does nothing: the factory song (16 bars, chain ${chain(was)}, G and H empty)`);
  ok(all.every((p) => empty(fresh, p)) && arrLen(fresh) === 0 && chain(fresh) === 1 && tempo(fresh) === 120, `NEW pressed twice: an empty song (no bars, chain ${chain(fresh)}, ${tempo(fresh)} BPM)`);
  ok(same(undone, was), 'F2 UNDO brings the whole song back, byte for byte');
  ok(same(pattern(copied, G), pattern(was, A)) && empty(copied, H) && patterns(copied, was, [0, 1, 2, 3, 4, 5]) && chain(copied) === chain(was), `COPY puts pattern A into G, the next empty one, and leaves the chain alone (${chain(copied)})`);
  ok(same(pattern(twice, H), pattern(was, A)) && patterns(twice, copied, [0, 1, 2, 3, 4, 5, 6]), 'COPY on G goes to H; with no empty pattern left, COPY changes nothing');
  ok(steps(twice, H, KICK) !== 0 && steps(cleared, H, KICK) === 0 && steps(cleared, H, SNARE) === steps(twice, H, SNARE) && patterns(cleared, twice, [0, 1, 2, 3, 4, 5, 6]), `CLEAR takes the selected sound's steps out of the pattern shown, and nothing else (kick ${steps(twice, H, KICK).toString(2)} -> ${steps(cleared, H, KICK).toString(2)})`);
  ok(same(uncleared, twice), 'F2 UNDO brings the cleared steps back');
  ok(same(asked, twice), `SHIFT + pad 16 pressed once does nothing (${arrLen(asked)} bars)`);
  ok(arrLen(noArr) === 0 && patterns(noArr, twice), `pressed twice it clears the arrangement, and only that (${arrLen(noArr)} bars)`);
  ok(same(arrBack, twice), `F2 UNDO brings the arrangement back (${arrLen(arrBack)} bars)`);
}
console.log(failed ? `edit FAILED (${failed})` : 'edit PASSED');
process.exit(failed ? 1 : 0);
