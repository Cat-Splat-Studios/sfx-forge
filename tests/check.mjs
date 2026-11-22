// What this check proves: every pad made a sound, telemetry moved the pitch of the held sound, the sequencer plays
// in time and follows INTENSITY and a cued pattern, and the MIDI the firmware sent follows the protocol in README.md.
//
// The scene: the test performance (tests/tour.json) rendered by the SDK's wasmtest into out/ (audio.wav, midi.json).
// This is the first check sh build.sh runs; read it first, then tests/host.mjs and tests/song-read.mjs, which the
// later checks share.
//
//   node tests/check.mjs out
import fs from 'fs';

const dir = process.argv[2] || 'out';

// one line for each thing that is checked: "ok" or "FAIL", and what was measured
let failed = 0;
const ok = (condition, text) => {
  console.log((condition ? '  ok    ' : '  FAIL  ') + text);
  if (!condition) {
    failed++;
  }
};

// ---- the rendered audio: a 16-bit stereo WAV, of which the left channel is read
const wavFile = fs.readFileSync(`${dir}/audio.wav`);
const sampleRate = wavFile.readUInt32LE(24);
const sampleCount = (wavFile.length - 44) / 4;
const left = new Float32Array(sampleCount);
for (let i = 0; i < sampleCount; i++) {
  left[i] = wavFile.readInt16LE(44 + i * 4) / 32768;
}

// the level (root mean square) between two moments, in seconds
const rms = (t0, t1) => {
  let sum = 0;
  const from = Math.floor(t0 * sampleRate);
  const to = Math.floor(t1 * sampleRate);
  for (let i = from; i < to; i++) {
    sum += left[i] * left[i];
  }
  return Math.sqrt(sum / (to - from));
};

// how often a second the wave goes up through zero between two moments: the pitch of a plain sound, in Hz
const crossings = (t0, t1) => {
  let count = 0;
  for (let i = Math.floor(t0 * sampleRate) + 1; i < Math.floor(t1 * sampleRate); i++) {
    if (left[i - 1] < 0 && left[i] >= 0) {
      count++;
    }
  }
  return count / (t1 - t0);
};

// ---- the pads: pad p is hit at 0.2 + 0.6 * p seconds
const silent = [];
for (let p = 0; p < 16; p++) {
  if (rms(0.2 + 0.6 * p, 0.5 + 0.6 * p) < 0.01) {
    silent.push(p + 1);
  }
}
ok(!silent.length, silent.length ? `silent pads: ${silent.join(', ')}` : 'all 16 pads sound');
ok(rms(0, 0.19) < 1e-4, 'silence before the first pad');

// ---- a pad held from 10 s on, and CC 10 (telemetry) sent to it: its pitch before and after
const pitchBefore = crossings(10.25, 10.4);
const pitchAfter = crossings(12.4, 12.6);
ok(
  pitchAfter > pitchBefore * 2.5,
  `telemetry CC 10 raises the held sound's pitch (${pitchBefore.toFixed(0)} -> ${pitchAfter.toFixed(0)} Hz)`,
);
ok(rms(15.2, 15.5) < 0.01, 'the held sound ends when its pad is let go');

// ---- the MIDI that went out: the pads' notes, a CC of the LINK page, a sound as SysEx (EXPORT)
const midi = JSON.parse(fs.readFileSync(`${dir}/midi.json`, 'utf8')).map((m) => m.bytes);
const notes = midi.filter((m) => m[0] === 0x90).map((m) => m[1]);
ok(
  [...Array(16)].every((_, i) => notes.includes(36 + i)),
  'pads send notes 36-51 on channel 1',
);
ok(
  midi.some((m) => m[0] === 0xb0 && m[1] === 20 && m[2] === 69),
  'LINK page: encoder 1 sends CC 20',
);

// the first SysEx message is the exported sound
const sysex = midi.find((m) => m[0] === 0xf0);
const PATCH = 28; // version, engine, 25 parameters, checksum
const head =
  sysex &&
  sysex.slice(0, 5).join() === [0xf0, 0x7d, 0x56, 0x42, 0x01].join() &&
  sysex.length === 7 + PATCH &&
  sysex[6 + PATCH] === 0xf7;
ok(head, 'EXPORT sends F0 7D 56 42 01 <slot> <28 bytes> F7');
if (head) {
  // the patch: version 2, an engine there is, 7-bit bytes, and the last byte the sum of the others (7 bits of it)
  const patch = sysex.slice(6, 6 + PATCH);
  const sum = patch.slice(0, PATCH - 1).reduce((a, v) => a + v, 0);
  ok(
    patch[0] === 2 && patch[1] < 5 && patch.every((v) => v < 128) && patch[PATCH - 1] === (sum & 0x7f),
    `the patch is valid (slot ${sysex[5]}, engine ${patch[1]}, checksum ok)`,
  );
}

// ---- the sequencer, PLAY at 16.0 s
// the song at intensity 0: only layers with threshold 0 play – the thud (kick) on steps 1, 5, 9, 13, every 0.5 s at
// 120 BPM
const kicks = [...Array(8)].map((_, k) => rms(16.0 + k * 0.5, 16.0 + k * 0.5 + 0.08));
const gaps = [...Array(8)].map((_, k) => rms(16.0 + k * 0.5 + 0.14, 16.0 + k * 0.5 + 0.24));
ok(
  Math.min(...kicks) > 0.08 && Math.max(...gaps) < Math.min(...kicks) * 0.6,
  `PLAY runs the pattern in time (kicks ${Math.min(...kicks).toFixed(2)}+, between ${Math.max(...gaps).toFixed(2)})`,
);
ok(rms(20.35, 20.6) < 0.01, 'STOP stops it');

// then pad 2 on the SEQ page put sound 4 on step 2: 0.125 s after the restart there is more than before
const before = rms(16.125, 16.24);
const after = rms(21.125, 21.24);
ok(
  after > before * 1.5 && after > 0.03,
  `a pad on the SEQ page sets a step of the selected sound (${before.toFixed(3)} -> ${after.toFixed(3)})`,
);

// full intensity brings the other layers in (bass, hat, snare, lead): much more between the kicks
const sparse = rms(16.26, 16.49);
const full = rms(23.76, 23.99);
ok(
  full > sparse * 3 && full > 0.05,
  `INTENSITY (CC 14) brings the layers in (${sparse.toFixed(3)} -> ${full.toFixed(3)} between two kicks)`,
);

// pattern H (empty) was cued by CC 15 at 24.0 s: it takes over on the bar line at 25.5 s, not before
ok(
  rms(24.6, 25.4) > 0.05 && rms(27.0, 27.4) < 0.01,
  `a cued pattern takes over on the next bar line ` +
    `(${rms(24.6, 25.4).toFixed(3)}, then ${rms(27.0, 27.4).toFixed(3)} once the echoes have faded)`,
);

// ---- SEND: the 16 sounds (with the EXPORT above: 17 dumps) and the song in pieces
const chunks = midi.filter((m) => m[0] === 0xf0 && m[4] === 0x05);
const dumps = midi.filter((m) => m[0] === 0xf0 && m[4] === 0x01);

// the song message of firmware 1.6.0:
//   F0 7D 56 42 05 <piece lo> <piece hi> <format> <pieces lo> <pieces hi> <up to 240 bytes> F7
const bytes = chunks.reduce((a, m) => a + m.length - 11, 0);
const count = Math.ceil(bytes / 240);

// piece i says its number, format 6 and the count, and every piece but the last is full (251 bytes with its frame)
const pieceIsRight = (m, i) =>
  (m[5] | (m[6] << 7)) === i &&
  m[7] === 6 &&
  (m[8] | (m[9] << 7)) === count &&
  m.length <= 251 &&
  (i === count - 1 || m.length === 251);
ok(
  chunks.length === count &&
    chunks.every(pieceIsRight) &&
    bytes >= 211 &&
    bytes <= 38355 &&
    chunks[0][10] === 6 &&
    dumps.length === 17,
  `SEND gives the 16 sounds and the song (format 6) in pieces of 240 bytes ` +
    `(${dumps.length - 1} dumps, ${chunks.length} pieces, ${bytes} bytes)`,
);

ok(rms(33.5, 34.0) < 0.01, 'silence at the end');

console.log(failed ? `FAILED (${failed})` : 'tour PASSED');
process.exit(failed ? 1 : 0);
