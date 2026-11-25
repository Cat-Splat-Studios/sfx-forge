// What this proves: hard and wrong use does not break the built firmware - or it shows where it does. A seeded fuzzer
// runs the .wasm as a host would (one flow: events, then an audio block, now and then a draw) and throws at it what a
// host, a controller, a game and a broken store can send:
//   - every event type of vibe.h, ids and values in range and out of it (pads, keys, buttons, SHIFT held, encoders,
//     notes from every source, raw MIDI with the telemetry CCs on channel 2, pedal, clock, transport, fader, save,
//     the stage with its modifiers, touch, types that do not exist), and bursts of hundreds with no block between
//   - all parameters through vibe_wasm_param_set: in range, at the ends, far outside, NaN, the infinities
//   - the host's transport table: playing on and off, tempo 1..999 and 0 and NaN, the beat jumping both ways
//   - SysEx in: the firmware's own messages (patches, a song in pieces, requests, text, the identity) whole, with
//     wrong bytes, cut short, with a wrong checksum, pieces out of order - and plain garbage of every length
//   - the store: the files written before, or random bytes, or cut files, or "no"; a full store
//   - restarts: a new instance with the store the last one left, as a power cycle; sometimes every file spoiled
//   - blocks of 1..256 frames at 22050 .. 192000 Hz
// After every block it checks: no trap, every sample of both outputs written, finite and within -1..+1, the memory
// did not grow, the MIDI out queue holds what a host can read. A trap, a NaN, a frame not written or a memory that
// grew ends the seed there. A finite sample over full scale is a FAIL too, but the seed runs on: such samples are
// counted, and the first is reported with what led to it. The time of every block is measured against its
// real-time budget (frames / sample rate): told as information, a FAIL only when a seed's blocks take longer than
// their budget on average. Something that never comes back (a loop without end) is caught by a watchdog: each seed
// runs in a thread of its own, and one that stands still for --hang seconds is stopped and reported.
//
// What it cannot see:
//   - two flows. The plugin runs the audio beside the events and the draws; here they take turns. A race between
//     the two is not found here (tests/stress-song-way.sh is for the one hand-over that could have one).
//   - memory errors that stay inside the firmware's own memory: WebAssembly traps only outside it. An index that
//     runs into the neighbouring array shows here only by what it does later (native/ built with a sanitizer does).
//   - whether it sounds right. Silence passes, and so does the wrong note.
//   - a host that breaks the protocol in ways not tried here: a store that answers more bytes than were asked for,
//     blocks of more than 256 frames, calls from two threads at once.
//   - the plugin itself: this is Node's WebAssembly, not the plugin's runtime, and no DAW.
//   - time on a real machine: the budget is measured on this computer, with other seeds running beside it (--jobs).
//
// The same seed gives the same run: every decision comes from one small PRNG (mulberry32) seeded with the seed; the
// clock is read only to measure. --minutes only says where a run ends, so a failure of a 2-minute run is there again
// in a longer one.
//
// --level says how wrong the host may be (the SysEx is rough at every level: it comes from outside the host):
//   0  a correct host: ids and values as vibe.h says, an honest store
//   1  + ids and values out of range (finite, up to a million), types that do not exist, a store that lies, spoiled
//        files, the Editor's calls
//   2  + NaN, the infinities and numbers too big for an int in every value; events only the SDK makes (a SysEx or
//        Editor event with a length of its own)                                                     (the default)
//
//   node tests/stress-fuzz.mjs build/sfx-forge.wasm                         20 seeds, 2 device-minutes each
//   node tests/stress-fuzz.mjs build/sfx-forge.wasm --seeds 40 --jobs 8     seeds 1..40, eight at a time
//   node tests/stress-fuzz.mjs build/sfx-forge.wasm --seed 7 --minutes 30   one seed, long
//   node tests/stress-fuzz.mjs build/sfx-forge.wasm --seed 7 --trace        all of the last second before a failure,
//                                                                           each line a call that can be pasted
//   other switches: --level 0|1|2, --first N (the first seed of --seeds), --hang S (seconds of standstill, 30),
//   --sdk-loop (DATA turns of more than a million detents as they are: with SHIFT held the SDK's own soft-key
//   gesture then never comes back - a loop in the SDK's runtime, before the firmware sees the event)
//
// A failure prints the seed, the block, what happened, the last 40 events and the line that runs it again.
import fs from 'fs';
import { Worker, isMainThread, parentPort, workerData } from 'worker_threads';

const NAME = 'stress-fuzz';
const RATES = [22050, 44100, 48000, 88200, 96000, 192000];
// the SDK's most frames a block
const MAX_BLOCK = 256;
// the SDK's event types (vibe.h)
// prettier-ignore
const EV = {
  PAD: 1, KEY: 2, BUTTON: 3, ENCODER: 4, MIDI: 5, PEDAL: 6, CLOCK: 7, TRANSPORT: 8, NOTE: 9, PARAM: 10, AREA: 11,
  MEDIA: 12, EDITOR: 13, SYSEX: 14, EDITOR_DATA: 15, TOUCH: 16, FADER: 17, SAVE: 18, STAGE: 19,
};
const EV_NAME = Object.fromEntries(Object.entries(EV).map(([k, v]) => [v, k]));
const BUTTON_SHIFT = 6;
// how every SysEx message of SFX FORGE begins (sfx-forge/fw_protocol.h)
const HEAD = [0xf0, 0x7d, 0x56, 0x42];
// a packed patch: version, engine, 25 values, checksum (core/sfx_core.h)
const PATCH_BYTES = 28;
const PATCH_VERSION = 2;
const ENGINES = 5;
// a song's pieces: now, and up to firmware 1.5.0 (fw_protocol.h)
const PIECE = 240;
const OLD_PIECE = 200;
const OLD_PIECES = 39;
const OLD_SONG_BYTES = 7699;
// what the watchdog reads: a counter that moves with every call, the block, and the last calls as text
const RING = 40;
const RING_SLOT = 256;
const SHARED_BYTES = 16 + RING * RING_SLOT;

// ---- a small PRNG: the same seed, the same numbers
const mulberry32 = (seed) => {
  let a = seed | 0;
  return () => {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
};

// a number as JavaScript reads it again
const num = (v) => (Object.is(v, -0) ? '-0' : String(v));
const hex = (b) => '0x' + b.toString(16).padStart(2, '0');

// what went wrong: kind is trap, bad-audio, memory or wrong-state
class Failure extends Error {
  constructor(kind, text) {
    super(text);
    this.kind = kind;
  }
}

// ================================================================================================================
// one seed (in a thread of its own)
// ================================================================================================================
function fuzz(job) {
  const { wasmFile, seed, minutes, level, trace, shared, sdkLoop } = job;
  const mod = new WebAssembly.Module(fs.readFileSync(wasmFile));
  // a format-5 song (the 16 sounds come first in the file), should the repository's test data be there
  let oldSong = null;
  try {
    const file = fs.readFileSync(new URL('./data/DemoSong-format5.bytes', import.meta.url));
    oldSong = [...file].slice(16 * PATCH_BYTES);
    if (oldSong.length !== OLD_SONG_BYTES) {
      oldSong = null;
    }
  } catch {
    oldSong = null;
  }

  // ---- the decisions: R for what is done to the firmware, S for what the store answers
  const R = mulberry32(seed);
  const S = mulberry32(seed ^ 0x5f0e57a1);
  const rnd = () => R();
  const int = (n) => Math.floor(R() * n);
  const chance = (p) => R() < p;
  const pick = (list) => list[int(list.length)];

  const rate = pick(RATES);
  // how busy this seed's player is, how often the host draws (0: the plugin's window is closed), how often it restarts
  const actionsPerSecond = pick([10, 40, 120, 400]);
  const fps = pick([0, 10, 30, 60]);
  const restartEvery = pick([5, 15, 40]);
  const endFrames = Math.round(minutes * 60 * rate);

  // ---- what the run counts
  const st = {
    blocks: 0,
    frames: 0,
    events: 0,
    restarts: 0,
    draws: 0,
    storeCalls: 0,
    midiOut: 0,
    sysexOut: 0,
    dropped: 0,
    peak: 0,
    audioMs: 0,
    worstRatio: 0,
    worstRatioBlock: 0,
    worstRatioFrames: 0,
    worstMs: 0,
    worstMsBlock: 0,
    worstMsFrames: 0,
    callMaxMs: 0,
    callMaxText: '',
    drawMaxMs: 0,
    types: new Set(),
    params: new Set(),
  };

  // ---- the log: every call into the firmware as a line, the latest also where the watchdog can read them
  const sharedInts = new Int32Array(shared, 0, 4);
  const sharedBytes = new Uint8Array(shared, 16);
  const log = [];
  const note = (text, isEvent = true) => {
    log.push({ frames: st.frames, block: st.blocks + 1, text, isEvent });
    if (log.length > 20000) {
      log.splice(0, 10000);
    }
    const slot = (Atomics.add(sharedInts, 2, 1) % RING) * RING_SLOT;
    const n = Math.min(text.length, RING_SLOT - 2);
    sharedBytes[slot] = n & 255;
    sharedBytes[slot + 1] = n >> 8;
    for (let i = 0; i < n; i++) {
      sharedBytes[slot + 2 + i] = text.charCodeAt(i) & 127;
    }
    Atomics.add(sharedInts, 0, 1);
  };
  const fail = (kind, text) => {
    throw new Failure(kind, text);
  };

  // ---- the device: the instance, its exports, where its tables are
  let inst = null;
  let e = null;
  let memBytes = 0;
  const at = {};
  let params = [];
  // the stored files: they stay from one instance to the next
  const files = new Map();
  let storeMode = 'honest';
  let deviceMode = 'base';
  // the host's transport as it stands
  const tp = {
    has: 0,
    playing: 0,
    bpm: 120,
    sigNum: 4,
    sigDen: 4,
    beat: 0,
    barStart: 0,
    loop: 0,
    loopStart: 0,
    loopEnd: 0,
  };
  let hasInput = 0;
  let stageShown = false;
  let lastFrames = 128;
  // what the firmware sent: the song it exported last, its last SysEx messages (to hand them back as an echo)
  let baseSong = null;
  let songParts = new Map();
  const echoes = [];
  // SysEx messages that wait to go in, some each gap; and what is to happen at a later block (a key let go)
  const sysexQueue = [];
  let sysexPerGap = 1;
  const later = [];

  const memory = () => e.memory.buffer;
  const text = (ptr, len) => {
    const b = new Uint8Array(memory(), ptr, Math.max(0, Math.min(len, 200)));
    return String.fromCharCode(...b).replace(/[^\x20-\x7e]/g, '?');
  };

  // ---- the store (vibe_host_store): op 0 read, 1 write, 2 size, 3 delete, 4 list, 5 read at an offset, 14 the device
  const store = (op, namePtr, nameLen, dataPtr, len) => {
    st.storeCalls++;
    const size = inst.exports.memory.buffer.byteLength;
    const name = nameLen > 0 && namePtr > 0 && namePtr + nameLen <= size ? text(namePtr, nameLen) : '';
    const answer = (r) => {
      note(`// store op ${op} "${name}" (${len} bytes) -> ${r}   [${storeMode}]`, false);
      return r;
    };
    if (len < 0 || dataPtr < 0 || dataPtr + len > size) {
      return answer(-1);
    }
    const data = new Uint8Array(inst.exports.memory.buffer, dataPtr, len);
    const file = files.get(name);
    if (op === 14) {
      // the device (vibe_device_info, 52 bytes): none said, a bigger one, or nonsense
      if (deviceMode === 'base' || len < 52) {
        return answer(-1);
      }
      if (deviceMode === 'big') {
        data.set([61, 64, 8, 16, 16, 1, 8, 8]);
        new DataView(inst.exports.memory.buffer, dataPtr, len).setUint16(16, 800, true);
        new DataView(inst.exports.memory.buffer, dataPtr, len).setUint16(18, 480, true);
      } else {
        for (let i = 0; i < 52; i++) {
          data[i] = Math.floor(S() * 256);
        }
      }
      return answer(1);
    }
    if (storeMode === 'none') {
      return answer(-1);
    }
    if (op === 0 || op === 5) {
      let from = 0;
      if (op === 5) {
        if (len < 4) {
          return answer(-1);
        }
        from = new DataView(inst.exports.memory.buffer, dataPtr, 4).getInt32(0, true);
      }
      if (storeMode === 'liar') {
        // bytes of its own, as many as it likes (never more than asked for) - also for a file that is not there
        const n = Math.floor(S() * (len + 1));
        for (let i = 0; i < n; i++) {
          data[i] = Math.floor(S() * 256);
        }
        return answer(S() < 0.2 ? -1 : n);
      }
      if (!file || from < 0) {
        return answer(-1);
      }
      let n = Math.max(0, Math.min(len, file.length - from));
      if (storeMode === 'short') {
        n = Math.floor(S() * (n + 1));
      }
      data.set(file.subarray(from, from + n));
      if (storeMode === 'noise') {
        for (let i = 0; i < n; i++) {
          data[i] = Math.floor(S() * 256);
        }
      }
      return answer(n);
    }
    if (op === 1) {
      if (storeMode === 'full' || !name) {
        return answer(-1);
      }
      files.set(name, Buffer.from(data));
      return answer(0);
    }
    if (op === 2) {
      if (storeMode === 'liar') {
        return answer(S() < 0.3 ? -1 : Math.floor(S() * pick2(S, [2, 100, 1000, 8000, 40000, 1000000])));
      }
      return answer(file ? file.length : -1);
    }
    if (op === 3) {
      if (!file || storeMode === 'full') {
        return answer(-1);
      }
      files.delete(name);
      return answer(0);
    }
    if (op === 4) {
      // the names, one a line, as many whole ones as fit
      let n = 0;
      for (const k of files.keys()) {
        if (n + k.length + 1 > len) {
          break;
        }
        data.set(Buffer.from(k + '\n'), n);
        n += k.length + 1;
      }
      return answer(n);
    }
    // media and all the rest: this host has none
    return answer(-1);
  };

  // ---- a new instance: the firmware starts, as after a power cycle
  const boot = () => {
    storeMode = level === 0 ? 'honest' : pick(['honest', 'honest', 'honest', 'full', 'none', 'short', 'noise', 'liar']);
    deviceMode = level === 0 ? pick(['base', 'base', 'big']) : pick(['base', 'base', 'big', 'junk']);
    inst = new WebAssembly.Instance(mod, {
      env: { vibe_host_log: () => {}, vibe_host_request: () => {}, vibe_host_store: store },
    });
    e = inst.exports;
    note(`// a new instance: store ${storeMode}, device ${deviceMode}, ${files.size} files`, false);
    note(`e.vibe_wasm_init(${rate});`);
    e.vibe_wasm_init(rate);
    memBytes = memory().byteLength;
    at.io = e.vibe_wasm_io();
    at.midi = e.vibe_wasm_midi_out();
    at.transport = e.vibe_wasm_transport();
    at.time = e.vibe_wasm_time();
    at.sysex = e.vibe_wasm_sysex_buf();
    at.ports = e.vibe_wasm_ports ? e.vibe_wasm_ports() : 0;
    at.tabbuf = e.vibe_wasm_editor_tabbuf ? e.vibe_wasm_editor_tabbuf() : 0;
    at.inbox = e.vibe_wasm_editor_inbox ? e.vibe_wasm_editor_inbox() : 0;
    // the parameters as the firmware declared them (vibe_param: id, name, min, max, def, unit, steps, choices)
    const view = new DataView(memory());
    params = [];
    for (let i = 0; i < e.vibe_wasm_param_count(); i++) {
      const p = e.vibe_wasm_param_info(i);
      params.push({
        min: view.getFloat32(p + 8, true),
        max: view.getFloat32(p + 12, true),
        steps: view.getInt32(p + 24, true),
      });
    }
    stageShown = false;
    songParts = new Map();
  };

  // ---- values and ids: mostly what vibe.h says, and by level what it does not
  const FAR = [1e6, -1e6, 32768.5, -32769, 65536, 1e-40, -0, 255, 256, -1, 1000, -1000];
  const WILD = [
    NaN,
    Infinity,
    -Infinity,
    3.4e38,
    -3.4e38,
    1e30,
    -1e30,
    2147483648,
    -2147483904,
    4294967296,
    1e10,
    -1e10,
  ];
  // a value for something that goes from lo to hi
  const val = (lo, hi) => {
    const k = int(100);
    const inside = lo + rnd() * (hi - lo);
    if (k < 56) {
      return inside;
    }
    if (k < 64) {
      return lo;
    }
    if (k < 72) {
      return hi;
    }
    if (k < 76) {
      return (lo + hi) / 2;
    }
    if (level === 0) {
      return inside;
    }
    if (k < 82) {
      return hi + (hi - lo) * (0.001 + rnd() * 10);
    }
    if (k < 88) {
      return lo - (hi - lo) * (0.001 + rnd() * 10);
    }
    if (k < 93 || level === 1) {
      return pick(FAR);
    }
    return pick(WILD);
  };
  // an id of something there are n of
  const idOf = (n) => {
    if (level === 0 || chance(0.85)) {
      return int(n);
    }
    return pick([n, n + 1, 127, 128, 255, 256, 256 + int(n), -1, int(256), int(256), 31, 32, 63, 64]);
  };
  // a byte of a MIDI message's data
  const data7 = () => (level > 0 && chance(0.05) ? 128 + int(128) : int(128));

  // ---- calls into the firmware. Each is written down first, as a line that can be pasted, then made and timed
  const call = (line, fn) => {
    note(line);
    st.events++;
    const t = performance.now();
    fn();
    const ms = performance.now() - t;
    if (ms > st.callMaxMs) {
      st.callMaxMs = ms;
      st.callMaxText = line.length > 100 ? line.slice(0, 100) + ' ...' : line;
    }
  };
  const ev = (type, id, down, value = 0, midi = 0) => {
    // DATA turned by more than 16 million detents, or by no number, with SHIFT held: the SDK's own gesture (vibe_sdk.c,
    // sk_take) counts such a turn down by ones and never comes back, before the firmware sees the event. Nothing in
    // the firmware can change that, and every seed would end there: so such a turn is cut to a million, unless
    // --sdk-loop asks for it as it is.
    if (type === 4 && id === 6 && !sdkLoop && !(Math.abs(value) <= 1e6)) {
      value = value > 0 ? 1e6 : value < 0 ? -1e6 : 0;
    }
    st.types.add(type);
    call(
      `e.vibe_wasm_event(${type}, ${id}, ${down}, ${num(value)}, 0x${(midi >>> 0).toString(16)}); // ${EV_NAME[type] ?? 'no such type'}`,
      () => e.vibe_wasm_event(type, id, down, value, midi),
    );
  };
  const setParam = (i, value) => {
    if (i >= 0 && i < params.length) {
      st.params.add(i);
    }
    call(`e.vibe_wasm_param_set(${i}, ${num(value)});`, () => e.vibe_wasm_param_set(i, value));
  };
  // a SysEx message in: its bytes into the SDK's buffer (4096 at most), then the length - the true one, or `says`
  const sysex = (bytes, says = bytes.length) => {
    const shown = trace || bytes.length <= 48 ? bytes : bytes.slice(0, 48);
    const more = shown.length < bytes.length ? ` /* + ${bytes.length - shown.length} more */` : '';
    call(`sysex([${shown.map(hex).join(', ')}${more}], ${says});`, () => {
      new Uint8Array(memory(), at.sysex, 4096).set(bytes.slice(0, 4096));
      e.vibe_wasm_sysex_in(says);
    });
  };
  const after = (blocks, fn) => later.push({ block: st.blocks + blocks, fn });

  // ---- the events, one kind each
  // something pressed: now and let go later, or only one of the two
  const press = (type, id, value, heldBlocks) => {
    const k = int(10);
    if (k < 7) {
      ev(type, id, 1, value);
      after(heldBlocks, () => ev(type, id, 0, 0));
    } else if (k < 8) {
      ev(type, id, 1, value);
    } else if (k < 9) {
      ev(type, id, 0, level > 0 ? value : 0);
    } else {
      ev(type, id, level > 0 ? pick([2, 255, 256, -1]) : 1, value);
    }
  };
  const doPad = () => press(EV.PAD, idOf(16), val(0, 1), 1 + int(60));
  const doKey = () => press(EV.KEY, idOf(25), val(0, 1), 1 + int(200));
  const doButton = () => {
    let id = int(25);
    if (level > 0 && chance(0.1)) {
      id = pick([25, 26, 63, 64 + int(16), 80, 127, 255, 256 + int(25), int(256)]);
    }
    // (a button's value is always 0; held up to a second and more: ENTER held is FOCUS)
    press(EV.BUTTON, id, level > 0 && chance(0.05) ? val(0, 1) : 0, 1 + int(chance(0.2) ? 600 : 30));
  };
  const doShift = () => {
    ev(EV.BUTTON, BUTTON_SHIFT, 1, 0);
    after(1 + int(400), () => ev(EV.BUTTON, BUTTON_SHIFT, 0, 0));
  };
  const doEncoder = () => {
    let id = idOf(7);
    if (level > 0 && chance(0.05)) {
      id = 16 + int(8);
    }
    const k = int(10);
    const turn = k < 5 ? pick([1, -1]) : k < 7 ? rnd() * 2 - 1 : k < 9 ? (2 + int(50)) * pick([1, -1]) : val(-4, 4);
    ev(EV.ENCODER, id, 0, turn);
  };
  const doNote = () => {
    const note7 = level > 0 && chance(0.05) ? int(256) : int(128);
    const channel = level > 0 && chance(0.05) ? int(256) : int(16);
    const source = level > 0 && chance(0.05) ? int(256) : int(4);
    press(EV.NOTE, note7, val(0, 1), 1 + int(200));
    // (press() sends no midi bytes: one more with the channel and the source)
    ev(EV.NOTE, note7, int(2), val(0, 1), channel | (source << 8));
  };
  const doMidi = () => {
    const k = int(10);
    let status;
    if (k < 3) {
      status = pick([0x90, 0x80]) | int(16);
    } else if (k < 5) {
      status = 0xb0 | int(16);
    } else if (k < 8) {
      status =
        pick([0xa0, 0xc0, 0xd0, 0xe0, 0xf8, 0xfa, 0xfb, 0xfc, 0xf2, 0xf3, 0xfe, 0xff, 0xf0, 0xf7]) |
        (chance(0.5) ? 0 : int(16));
    } else {
      status = level > 0 ? int(256) : 0x80 + int(128);
    }
    ev(EV.MIDI, 0, 0, 0, (status & 255) | (data7() << 8) | (data7() << 16));
  };
  // the game's telemetry: CC 10-17 on channel 2
  const doTelemetry = () => ev(EV.MIDI, 0, 0, 0, 0xb1 | ((10 + int(8)) << 8) | (data7() << 16));
  const doPedal = () => ev(EV.PEDAL, idOf(2), int(2), val(0, 1));
  const doClock = () => ev(EV.CLOCK, 0, 1, val(0, 2));
  const doTransportEvent = () => {
    if (chance(0.5)) {
      ev(EV.TRANSPORT, 0, int(2), 0);
    } else {
      ev(EV.TRANSPORT, 1, 0, chance(0.2) ? 0 : val(1, 999));
    }
  };
  const doFader = () => ev(EV.FADER, idOf(16), 0, val(0, 1));
  const doSave = () => ev(EV.SAVE, 0, 0, 0);
  const doOther = () => {
    const type = pick([EV.AREA, EV.MEDIA, EV.EDITOR]);
    ev(type, type === EV.AREA ? 0 : idOf(4), int(2), type === EV.AREA ? pick([-1, 0, 1, val(-1, 1)]) : val(0, 8));
  };
  const doUnknown = () =>
    ev(pick([0, 20, 21, 31, 64, 127, 128, 200, 255, 256 + int(20), -1]), int(256), int(3), val(0, 1), int(0x1000000));
  // a parameter set by the host itself, as the event the SDK makes of it - and events only the SDK makes
  const doParamEvent = () => ev(EV.PARAM, idOf(params.length || 1), 0, val(-1, 16));
  const doInner = () => {
    if (chance(0.5)) {
      // a SysEx event with a length of its own (the SDK's own are 2..4096)
      const bytes = someSysex();
      new Uint8Array(memory(), at.sysex, 4096).set(bytes.slice(0, 4096));
      note(
        `new Uint8Array(e.memory.buffer, e.vibe_wasm_sysex_buf(), 4096).set([${bytes.slice(0, 48).map(hex).join(', ')}]);`,
        false,
      );
      ev(EV.SYSEX, 0, 1, level > 1 ? val(0, 4096) : 2 + int(4095));
    } else {
      ev(EV.EDITOR_DATA, 0, 1, level > 1 ? val(0, 4096) : 1 + int(4096));
    }
  };

  // the stage (960 x 540) and the display (480 x 320): a pointer goes down, moves and comes up
  const point = (w, h, row) => {
    let x = int(w);
    let y = int(h);
    if (level > 0 && chance(0.15)) {
      x = pick([w, w + 1, row - 1, int(row)]);
    }
    if (level > 0 && chance(0.15)) {
      y = pick([h, h + 1, 4 * h, int(4096)]);
    }
    const v = y * row + x;
    return level > 0 && chance(0.04) ? val(0, v + 1) : v;
  };
  const doStage = () => {
    const pointer = level > 0 && chance(0.1) ? int(256) : int(10);
    const mods = level > 0 && chance(0.1) ? int(256) : int(8);
    const midi = pointer | (mods << 8);
    const k = int(10);
    if (k < 1) {
      stageShown = !stageShown;
      ev(EV.STAGE, stageShown ? 8 : 9, 0, 0);
    } else if (k < 3) {
      ev(EV.STAGE, 3, 0, chance(0.7) ? pick([1, -1, 3, -3]) : val(-5, 5), midi);
    } else if (k < 8) {
      // a drag
      ev(EV.STAGE, 0, 1, point(960, 540, 4096), midi);
      const moves = int(12);
      for (let i = 1; i <= moves; i++) {
        after(i * (1 + int(3)), () => ev(EV.STAGE, 1, 1, point(960, 540, 4096), midi));
      }
      after(moves * 4 + 1 + int(20), () => ev(EV.STAGE, 2, 0, point(960, 540, 4096), midi));
    } else {
      ev(EV.STAGE, level > 0 ? idOf(4) : int(4), int(2), point(960, 540, 4096), midi);
    }
  };
  const doTouch = () => {
    if (chance(0.7)) {
      ev(EV.TOUCH, 0, 1, point(480, 320, 512));
      const moves = int(8);
      for (let i = 1; i <= moves; i++) {
        after(i * (1 + int(3)), () => ev(EV.TOUCH, 1, 1, point(480, 320, 512)));
      }
      after(moves * 4 + 1 + int(20), () => ev(EV.TOUCH, 2, 0, point(480, 320, 512)));
    } else {
      ev(EV.TOUCH, idOf(3), int(2), point(480, 320, 512));
    }
  };

  // the parameters: one, or all of them at once
  const paramValue = (p) => {
    if (p.steps > 1 && chance(0.5)) {
      return p.min + (int(p.steps) * (p.max - p.min)) / (p.steps - 1);
    }
    return val(p.min, p.max);
  };
  const doParam = () => {
    if (!params.length) {
      return;
    }
    if (level > 0 && chance(0.05)) {
      setParam(pick([-1, params.length, 255, 256, 1000000, -2147483648]), val(0, 1));
      return;
    }
    const i = int(params.length);
    setParam(i, paramValue(params[i]));
  };
  const doParamsAll = () => {
    for (let i = 0; i < params.length; i++) {
      setParam(i, paramValue(params[i]));
    }
  };

  // the host's transport table, as it stands from the next block on
  const doTransport = () => {
    const k = int(12);
    if (k < 2) {
      tp.has = level > 0 && chance(0.1) ? pick([2, -1, 255]) : 1 - (tp.has ? 1 : 0);
    } else if (k < 5) {
      tp.playing = level > 0 && chance(0.1) ? pick([2, -1, 255]) : 1 - (tp.playing ? 1 : 0);
      tp.has = chance(0.8) ? 1 : tp.has;
      // (a host says it as an event too)
      if (chance(0.5)) {
        ev(EV.TRANSPORT, 0, tp.playing ? 1 : 0, 0);
      }
    } else if (k < 8) {
      tp.bpm = pick([120, 1, 999, 0, 1 + rnd() * 998, 1 + rnd() * 998, val(1, 999)]);
      if (chance(0.5)) {
        ev(EV.TRANSPORT, 1, 0, tp.bpm);
      }
    } else if (k < 11) {
      // the beat jumps: forward, back, to the start, far away
      const far =
        level === 0
          ? 100000
          : level === 1
            ? pick([1e6, 1e9, -1e6, -3])
            : pick([1e9, 1e15, 1e300, -1e300, NaN, Infinity, -Infinity, -1e9]);
      tp.beat = pick([0, tp.beat + rnd() * 64, tp.beat - rnd() * 64, rnd() * 4000, tp.beat + 1e-9, far]);
      if (level === 0 && !(tp.beat >= 0)) {
        tp.beat = 0;
      }
      tp.barStart = chance(0.8) ? Math.floor(tp.beat / 4) * 4 : val(0, 4000);
    } else {
      const sig =
        level > 0 && chance(0.3)
          ? [int(256) - 100, int(256) - 100]
          : pick([
              [4, 4],
              [3, 4],
              [6, 8],
              [7, 8],
              [5, 4],
              [12, 8],
              [2, 2],
            ]);
      tp.sigNum = sig[0];
      tp.sigDen = sig[1];
      tp.loop = int(2);
      tp.loopStart = val(0, 64);
      tp.loopEnd = tp.loopStart + val(0, 64);
    }
    note(
      `// the transport table: has ${tp.has} playing ${tp.playing} bpm ${num(tp.bpm)} beat ${num(tp.beat)} ${tp.sigNum}/${tp.sigDen}`,
      false,
    );
  };
  const writeTransport = () => {
    // vibe_transport_info on wasm32: has @0, playing @4, bpm @8, sig_num @12, sig_den @16, beat @24, bar_start @32,
    // loop @40, loop_start @48, loop_end @56
    const view = new DataView(memory(), at.transport, 64);
    view.setInt32(0, tp.has, true);
    view.setInt32(4, tp.playing, true);
    view.setFloat32(8, tp.bpm, true);
    view.setInt32(12, tp.sigNum, true);
    view.setInt32(16, tp.sigDen, true);
    view.setFloat64(24, tp.beat, true);
    view.setFloat64(32, tp.barStart, true);
    view.setInt32(40, tp.loop, true);
    view.setFloat64(48, tp.loopStart, true);
    view.setFloat64(56, tp.loopEnd, true);
  };

  // the other tables a host fills: the time an event was heard, the inputs and outputs it has
  const doTables = () => {
    const view = new DataView(memory());
    const k = int(3);
    if (k === 0) {
      // (vibe_wasm_time: heard lo @0, hi @4, latency out @8, in @12, flags @16)
      const heard =
        level === 0 || chance(0.6)
          ? Math.max(0, st.frames - int(4096))
          : pick([0, st.frames + 100000, 4294967295, 2 ** 40, 2 ** 53]);
      view.setUint32(at.time, heard >>> 0, true);
      view.setUint32(at.time + 4, Math.floor(heard / 4294967296) >>> 0, true);
      view.setUint32(at.time + 8, pick([0, 64, 512, 4096, level > 0 ? 4294967295 : 256]), true);
      view.setUint32(at.time + 12, pick([0, 64, 512, 4096, level > 0 ? 4294967295 : 256]), true);
      view.setUint32(at.time + 16, level > 0 && chance(0.1) ? int(256) : int(2), true);
      note(`// the time table: an event was heard at frame ${heard}`, false);
    } else if (k === 1 && at.ports) {
      // (vibe_wasm_ports: host_in @4, host_out @8, the names @16 and @272)
      const count = () => (level > 0 && chance(0.2) ? pick([0, 1, 17, 255, -1, 1000000]) : 2 + int(15));
      view.setInt32(at.ports + 4, count(), true);
      view.setInt32(at.ports + 8, count(), true);
      const names = new Uint8Array(memory(), at.ports + 16, 512);
      for (let i = 0; i < 512; i++) {
        names[i] = chance(0.1) ? 0 : level > 0 ? 1 + int(255) : 65 + int(26);
      }
      note('// the ports table: other counts and names', false);
    } else {
      // which inputs carry sound, and the sound (io: in @0, out_l @1024, out_r @2048, in2 @3072)
      hasInput = level === 0 ? int(4) : chance(0.7) ? int(4) : int(65536) | (chance(0.3) ? 0x80000000 : 0);
      const io = new Float32Array(memory(), at.io, 4 * MAX_BLOCK);
      for (let i = 0; i < MAX_BLOCK; i++) {
        io[i] = rnd() * 2 - 1;
        io[3 * MAX_BLOCK + i] = rnd() * 2 - 1;
      }
      note(`// inputs: has_input 0x${(hasInput >>> 0).toString(16)}`, false);
    }
  };

  // the Editor's calls and the worker jobs' own: the SDK answers them, the firmware has little of it
  const doEditor = () => {
    const k = int(5);
    if (k === 0 && e.vibe_wasm_editor_curve) {
      const i = int(20) - 2;
      call(`e.vibe_wasm_editor_curve(${i}, e.vibe_wasm_editor_tabbuf());`, () =>
        e.vibe_wasm_editor_curve(i, at.tabbuf),
      );
    } else if (k === 1 && e.vibe_wasm_editor_press) {
      const i = int(20) - 2;
      call(`e.vibe_wasm_editor_press(${i}, e.vibe_wasm_editor_tabbuf());`, () =>
        e.vibe_wasm_editor_press(i, at.tabbuf),
      );
    } else if (k === 2 && e.vibe_wasm_editor_data_in && at.inbox) {
      const n = pick([0, 1, 2, 100, 4096, 4097, -1, int(4097)]);
      const inbox = new Uint8Array(memory(), at.inbox, 4096);
      for (let i = 0; i < 4096; i++) {
        inbox[i] = int(256);
      }
      call(`e.vibe_wasm_editor_data_in(${n}); // random bytes in the inbox`, () => e.vibe_wasm_editor_data_in(n));
    } else if (k === 3 && e.vibe_wasm_editor_curve_draw) {
      const a = [int(20) - 2, int(70) - 2, int(70), val(0, 1)];
      call(`e.vibe_wasm_editor_curve_draw(${a[0]}, e.vibe_wasm_editor_tabbuf(), ${a[1]}, ${a[2]}, ${num(a[3])});`, () =>
        e.vibe_wasm_editor_curve_draw(a[0], at.tabbuf, a[1], a[2], a[3]),
      );
    } else if (e.vibe_wasm_job) {
      const a = [int(6) - 1, pick([0, 1, 128, 256, 257, -1])];
      call(`e.vibe_wasm_job(${a[0]}, ${a[1]});`, () => e.vibe_wasm_job(a[0], a[1]));
    }
  };

  // ---- SysEx
  const patchBytes = () => {
    const b = [PATCH_VERSION, int(ENGINES)];
    for (let i = 0; i < PATCH_BYTES - 3; i++) {
      b.push(chance(0.2) ? pick([0, 127, 64]) : int(128));
    }
    b.push(b.reduce((s, v) => s + v, 0) & 0x7f);
    return b;
  };
  // a message of the firmware's own protocol, as it should be
  const ownMessage = () => {
    const k = int(12);
    if (k < 4) {
      return [...HEAD, 0x02, int(32), ...patchBytes(), 0xf7];
    }
    if (k < 5) {
      return [...HEAD, 0x01, int(16), ...patchBytes(), 0xf7];
    }
    if (k < 7) {
      return [...HEAD, 0x04, int(16), 0xf7];
    }
    if (k < 8) {
      return [...HEAD, 0x06, 0xf7];
    }
    if (k < 9) {
      return [...HEAD, 0x07, 0xf7];
    }
    if (k < 10) {
      return [...HEAD, 0x08, ...Array.from({ length: 18 }, () => int(128)), 0xf7];
    }
    return [
      ...HEAD,
      0x03,
      ...Array.from({ length: int(chance(0.8) ? 40 : 4000) }, () => (chance(0.9) ? 32 + int(95) : int(128))),
      0xf7,
    ];
  };
  // ... and the same made wrong in one way
  const spoilMessage = (m) => {
    const b = m.slice();
    const k = int(10);
    if (k === 0 && b.length > 1) {
      b[int(b.length)] = int(256);
    } else if (k === 1 && b.length > 6) {
      // (a patch's checksum is its last byte before F7)
      b[b.length - 2] = (b[b.length - 2] + 1 + int(126)) & 0x7f;
    } else if (k === 2) {
      b.length = int(b.length + 1);
    } else if (k === 3) {
      b.splice(b.length - 1, 0, ...Array.from({ length: 1 + int(40) }, () => int(128)));
    } else if (k === 4 && b.length > 6) {
      b[5] = pick([16, 31, 32, 64, 127, 128, 255]);
    } else if (k === 5) {
      b.pop();
    } else if (k === 6) {
      b.unshift(0xf0);
    } else if (k === 7 && b.length > 4) {
      b[4] = int(256);
    } else if (k === 8 && b.length > 8) {
      // a byte of the body with its high bit set
      b[6 + int(b.length - 7)] = 128 + int(128);
    } else {
      b[int(4)] = int(256);
    }
    return b;
  };
  // plain garbage of any length the buffer takes, sometimes dressed as one of ours
  const garbage = () => {
    const n = pick([
      0,
      1,
      2,
      3,
      5,
      6,
      7,
      8,
      11,
      12,
      34,
      35,
      36,
      250,
      251,
      252,
      4095,
      4096,
      int(64),
      int(600),
      int(4097),
      int(4097),
    ]);
    const k = int(3);
    const same = int(256);
    const b = Array.from({ length: n }, () => (k === 0 ? int(256) : k === 1 ? int(128) : same));
    if (n > 0 && chance(0.7)) {
      b[0] = 0xf0;
    }
    if (n > 4 && chance(0.6)) {
      b[1] = 0x7d;
      b[2] = 0x56;
      b[3] = 0x42;
      b[4] = chance(0.8) ? 1 + int(8) : int(256);
    }
    if (n > 6 && chance(0.4)) {
      // (a song's piece: the bytes that say which and of how many)
      b[4] = 0x05;
      b[5] = int(chance(0.5) ? 4 : 128);
      b[6] = pick([0, 0, 1, OLD_PIECES, int(128)]);
      if (n > 9) {
        b[7] = pick([6, 5, 1, 0, 7, int(128)]);
        b[8] = int(chance(0.5) ? 4 : 128);
        b[9] = pick([0, 0, 1, int(128)]);
      }
    }
    if (n > 1 && chance(0.7)) {
      b[n - 1] = 0xf7;
    }
    return b;
  };
  const someSysex = () => (chance(0.4) ? garbage() : chance(0.5) ? ownMessage() : spoilMessage(ownMessage()));
  const doSysex = () => sysex(chance(0.35) ? spoilMessage(ownMessage()) : ownMessage());
  const doGarbage = () => {
    const b = garbage();
    // the length said: the true one, or (the bytes of the message before still lie in the buffer) another
    sysex(b, level > 0 && chance(0.1) ? pick([0, 1, 2, 6, 4096, 4097, 5000, -1, 1000000000, int(4097)]) : b.length);
  };
  // what the firmware sent last, straight back - as a virtual MIDI cable does
  const doEcho = () => {
    if (echoes.length) {
      sysex(pick(echoes));
    } else {
      sysex([...HEAD, pick([0x06, 0x04, 0x07]), ...(chance(0.5) ? [] : [int(16)]), 0xf7]);
    }
  };
  // a song as its messages, in the shape of now and in the one before (tests/host.mjs has the same two)
  const pieces = (song, says = Math.ceil(song.length / PIECE)) => {
    const out = [];
    const K = Math.ceil(song.length / PIECE);
    for (let k = 0; k < K; k++) {
      out.push([
        ...HEAD,
        0x05,
        k & 0x7f,
        (k >> 7) & 0x7f,
        song[0] & 0xff,
        says & 0x7f,
        (says >> 7) & 0x7f,
        ...song.slice(k * PIECE, k * PIECE + PIECE),
        0xf7,
      ]);
    }
    return out;
  };
  const oldPieces = (song) => {
    const out = [];
    for (let k = 0; k < OLD_PIECES; k++) {
      out.push([...HEAD, 0x05, k, OLD_PIECES, ...song.slice(k * OLD_PIECE, k * OLD_PIECE + OLD_PIECE), 0xf7]);
    }
    return out;
  };
  // a whole song goes in: the one the firmware exported, the format-5 one, or bytes of no song - as it is or with
  // some bytes changed, cut or made longer; its pieces in order, backwards, shuffled, twice, one missing
  const doSong = () => {
    if (!baseSong && chance(0.7)) {
      // (none exported yet: ask for it, and the next time there is one)
      sysex([...HEAD, 0x06, 0xf7]);
      return;
    }
    const k = int(10);
    let song;
    let old = false;
    if (k < 6 && baseSong) {
      song = baseSong.slice();
    } else if (k < 8 && oldSong) {
      song = oldSong.slice();
      old = true;
    } else {
      song = Array.from({ length: pick([1, 211, 212, 1000, OLD_SONG_BYTES, 38355, 38400, 1 + int(38400)]) }, () =>
        int(128),
      );
      song[0] = pick([6, 6, 5, 4, 3, 2, 1, 7, 0]);
      old = song.length === OLD_SONG_BYTES && chance(0.5);
    }
    // bytes changed: none, one, a few, many - in the head (where the sizes are) more often than not
    const changes = pick([0, 0, 1, 1, 2, 5, 20, 200]);
    for (let i = 0; i < changes; i++) {
      const where = chance(0.5) ? int(Math.min(song.length, 160)) : int(song.length);
      song[where] = chance(0.9) ? int(128) : pick([0, 1, 127, 128, 255]);
    }
    if (!old && chance(0.15)) {
      song.length = Math.max(1, chance(0.5) ? int(song.length) : song.length - 1 - int(4));
    } else if (!old && chance(0.1)) {
      song.push(...Array.from({ length: 1 + int(500) }, () => int(128)));
    }
    if (song.length > 160 * PIECE) {
      song.length = 160 * PIECE;
    }
    let list = old
      ? oldPieces(song)
      : pieces(song, chance(0.1) ? Math.max(1, Math.ceil(song.length / PIECE) + pick([-1, 1, 2])) : undefined);
    const order = int(8);
    if (order === 1) {
      list.reverse();
    } else if (order === 2) {
      // (shuffled)
      for (let i = list.length - 1; i > 0; i--) {
        const j = int(i + 1);
        [list[i], list[j]] = [list[j], list[i]];
      }
    } else if (order === 3) {
      list = list.flatMap((m) => [m, m]);
    } else if (order === 4 && list.length > 1) {
      list.splice(int(list.length), 1);
    } else if (order === 5) {
      list = [...list, ...list];
    } else if (order === 6 && list.length > 1) {
      const i = int(list.length);
      list[i] = spoilMessage(list[i]);
    }
    note(`// a song of ${song.length} bytes goes in: ${list.length} messages${old ? ', the old shape' : ''}`, false);
    sysexQueue.push(...list);
    sysexPerGap = pick([1, 1, 2, 5, 1000]);
  };

  // the store answers in another way from now on
  const doStoreMode = () => {
    storeMode = pick(['honest', 'full', 'none', 'short', 'noise', 'liar']);
    note(`// the store is now: ${storeMode}`, false);
  };

  // ---- what happens between two blocks, by weight
  const ACTIONS = [
    [12, doPad, 0],
    [8, doKey, 0],
    [14, doButton, 0],
    [3, doShift, 0],
    [12, doEncoder, 0],
    [5, doNote, 0],
    [5, doMidi, 0],
    [5, doTelemetry, 0],
    [1, doPedal, 0],
    [1, doClock, 0],
    [2, doTransportEvent, 0],
    [4, doTransport, 0],
    [9, doParam, 0],
    [0.5, doParamsAll, 0],
    [1, doFader, 0],
    [1, doSave, 0],
    [1, doOther, 0],
    [8, doStage, 0],
    [4, doTouch, 0],
    [1, doTables, 0],
    [5, doSysex, 0],
    [3, doGarbage, 0],
    [1, doEcho, 0],
    [1.5, doSong, 0],
    [2, doParamEvent, 1],
    [1, doUnknown, 1],
    [1, doEditor, 1],
    [0.3, doStoreMode, 1],
    [1, doInner, 1],
  ].filter((a) => a[2] <= level);
  const WEIGHT = ACTIONS.reduce((s, a) => s + a[0], 0);
  const action = () => {
    let k = rnd() * WEIGHT;
    for (const a of ACTIONS) {
      k -= a[0];
      if (k < 0) {
        a[1]();
        return;
      }
    }
  };
  // hundreds of events with no block between them: all kinds, or one kind again and again
  const burst = () => {
    const n = 100 + int(700);
    note(`// a burst of ${n}`, false);
    if (chance(0.5)) {
      for (let i = 0; i < n; i++) {
        action();
      }
      return;
    }
    const one = pick([
      doPad,
      doKey,
      doButton,
      doEncoder,
      doNote,
      doMidi,
      doTelemetry,
      doParam,
      doStage,
      doTouch,
      doSysex,
      doGarbage,
      doTransportEvent,
      doSave,
      () => ev(EV.BUTTON, pick([21, 22, 23]), 1, 0),
      () => ev(EV.BUTTON, int(6), 1, 0),
      () => ev(EV.BUTTON, 10 + int(6), 1, 0),
      () => sysex([...HEAD, 0x06, 0xf7]),
      () => sysex([...HEAD, 0x07, 0xf7]),
      () => sysex([...HEAD, 0x04, int(16), 0xf7]),
      () => ev(EV.NOTE, int(128), 1, 0.9, int(4) << 8),
      () => ev(EV.PAD, int(16), 1, 0.9),
    ]);
    for (let i = 0; i < n; i++) {
      one();
    }
  };

  // ---- a restart: a new instance with the store the last one left; sometimes every stored file spoiled first
  const spoilFiles = () => {
    for (const [name, file] of [...files]) {
      const k = int(7);
      let b = Buffer.from(file);
      if (k === 0) {
        b = Buffer.from(Array.from({ length: file.length }, () => int(256)));
      } else if (k === 1) {
        b = b.subarray(0, int(file.length + 1));
      } else if (k === 2) {
        for (let i = 0; i < 1 + int(8) && b.length; i++) {
          b[int(b.length)] = int(256);
        }
      } else if (k === 3) {
        b = Buffer.alloc(0);
      } else if (k === 4) {
        b = Buffer.concat([b, Buffer.from(Array.from({ length: 1 + int(3000) }, () => int(256)))]);
      } else if (k === 5) {
        b = Buffer.from(Array.from({ length: file.length }, () => int(128)));
      } else {
        b = Buffer.alloc(file.length, pick([0, 127, 255]));
      }
      files.set(name, Buffer.from(b));
    }
    // (and files the firmware knows by name that were never written: bytes of no meaning, of the right size or not)
    for (const [name, n] of [
      ['kits.bin', 928],
      ['song6.bin', 213 + int(38200)],
      ['song.bin', OLD_SONG_BYTES],
      ['meter.bin', 1],
    ]) {
      if (chance(0.25)) {
        files.set(
          name,
          Buffer.from(
            Array.from({ length: chance(0.7) ? n : int(n + 100) }, () => (chance(0.5) ? int(128) : int(256))),
          ),
        );
      }
    }
    note(`// every stored file spoiled: ${[...files].map(([k, v]) => `${k} (${v.length})`).join(', ')}`, false);
  };
  const restart = () => {
    if (chance(0.5)) {
      ev(EV.SAVE, 0, 0, 0);
    }
    if (level > 0 && chance(0.35)) {
      spoilFiles();
    }
    st.restarts++;
    boot();
  };

  // ---- the firmware's MIDI out queue, emptied as a host does it (tests/host.mjs; the SDK's vibe_sdk.c, "MIDI out"):
  // w @0, r @4, dropped @8, sysex_len @12, 256 entries of 6 bytes @16, the SysEx's bytes @1552
  const heard = (bytes) => {
    echoes.push(bytes);
    if (echoes.length > 8) {
      echoes.shift();
    }
    if (bytes.length > 11 && HEAD.every((v, i) => bytes[i] === v) && bytes[4] === 0x05) {
      const k = bytes[5] | (bytes[6] << 7);
      const K = bytes[8] | (bytes[9] << 7);
      if (k === 0) {
        songParts = new Map();
      }
      songParts.set(k, bytes.slice(10, -1));
      if (songParts.size === K && [...Array(K).keys()].every((i) => songParts.has(i))) {
        baseSong = [...Array(K).keys()].flatMap((i) => songParts.get(i));
        songParts = new Map();
      }
    }
  };
  const drain = () => {
    const view = new DataView(memory());
    const base = at.midi;
    const w = view.getUint32(base, true);
    let r = view.getUint32(base + 4, true);
    if ((w - r) >>> 0 > 256) {
      fail('wrong-state', `the MIDI out queue says ${(w - r) >>> 0} messages wait; it holds 256`);
    }
    for (; r !== w; r = (r + 1) >>> 0) {
      const entry = base + 16 + (r % 256) * 6;
      const status = view.getUint8(entry + 2);
      const len = view.getUint8(entry + 5);
      if (!len && status === 0xf0) {
        const n = view.getUint32(base + 12, true);
        if (n < 2 || n > 256) {
          fail('wrong-state', `a SysEx of ${n} bytes in the MIDI out queue (2..256 fit)`);
        }
        const bytes = [...new Uint8Array(memory(), base + 1552, n)];
        if (bytes[0] !== 0xf0 || bytes[n - 1] !== 0xf7 || bytes.slice(1, -1).some((b) => b > 127)) {
          fail(
            'wrong-state',
            `a SysEx went out that is none: ${bytes.slice(0, 24).map(hex).join(' ')} ... (${n} bytes)`,
          );
        }
        view.setUint32(base + 12, 0, true);
        st.sysexOut++;
        heard(bytes);
      } else {
        const d1 = view.getUint8(entry + 3);
        const d2 = view.getUint8(entry + 4);
        if (len < 1 || len > 3 || status < 0x80 || d1 > 127 || d2 > 127) {
          fail(
            'wrong-state',
            `a MIDI message went out that is none: status ${hex(status)}, ${hex(d1)} ${hex(d2)}, length ${len}`,
          );
        }
        st.midiOut++;
      }
    }
    view.setUint32(base + 4, w, true);
    st.dropped = view.getUint32(base + 8, true);
  };

  // ---- one audio block, and all that is checked after it
  const block = () => {
    const k = int(20);
    const frames = k < 12 ? 1 + int(MAX_BLOCK) : k < 15 ? 128 : k < 17 ? MAX_BLOCK : k < 18 ? 1 + int(4) : lastFrames;
    lastFrames = frames;
    writeTransport();
    // (9: a number no output may hold - a frame the firmware does not write is found by it)
    const l = new Float32Array(memory(), at.io + 4 * MAX_BLOCK, frames);
    const r = new Float32Array(memory(), at.io + 8 * MAX_BLOCK, frames);
    l.fill(9);
    r.fill(9);
    if (trace) {
      note(
        `e.vibe_wasm_audio(${frames}, ${hasInput}); // block ${st.blocks + 1}, transport: has ${tp.has} playing ${tp.playing} bpm ${num(tp.bpm)} beat ${num(tp.beat)}`,
        false,
      );
    }
    Atomics.store(sharedInts, 1, st.blocks + 1);
    Atomics.add(sharedInts, 0, 1);
    const t = performance.now();
    e.vibe_wasm_audio(frames, hasInput);
    const ms = performance.now() - t;
    st.blocks++;

    if (memory().byteLength !== memBytes) {
      fail('memory', `the memory grew from ${memBytes} to ${memory().byteLength} bytes`);
    }
    const lo = new Float32Array(memory(), at.io + 4 * MAX_BLOCK, frames);
    const ro = new Float32Array(memory(), at.io + 8 * MAX_BLOCK, frames);
    for (let i = 0; i < frames; i++) {
      const a = lo[i];
      const b = ro[i];
      // (a NaN fails both comparisons)
      if (!(a >= -1 && a <= 1) || !(b >= -1 && b <= 1)) {
        const which = !(a >= -1 && a <= 1) ? ['out_l', a] : ['out_r', b];
        const what = `${which[0]}[${i}] of a block of ${frames} frames is ${which[1] === 9 ? 'not written' : num(which[1])}`;
        // not a number, or not written: the run ends here. Over full scale: it is counted, the first one is kept with
        // what led to it, and the run goes on - so that a sample a hair over 1 does not hide what comes after it
        if (!Number.isFinite(which[1]) || which[1] === 9) {
          fail('bad-audio', what);
        }
        if (!over.count) {
          over.what = what;
          over.where = `block ${st.blocks}, ${(st.frames / rate).toFixed(3)} s`;
          over.lines = tail();
        }
        over.count++;
        over.worst = Math.max(over.worst, Math.abs(which[1]));
        continue;
      }
      const p = Math.max(Math.abs(a), Math.abs(b));
      if (p > st.peak) {
        st.peak = p;
      }
    }
    drain();
    // (a host asks which parameters the firmware changed: all eight words now and then)
    if (st.blocks % 8 === 0) {
      for (let word = 0; word < 8; word++) {
        e.vibe_wasm_param_changed(word);
      }
    }

    // the time it took, against the time it plays
    const budget = (frames / rate) * 1000;
    st.audioMs += ms;
    if (ms / budget > st.worstRatio) {
      st.worstRatio = ms / budget;
      st.worstRatioBlock = st.blocks;
      st.worstRatioFrames = frames;
    }
    if (ms > st.worstMs) {
      st.worstMs = ms;
      st.worstMsBlock = st.blocks;
      st.worstMsFrames = frames;
    }
    st.frames += frames;
    if (tp.playing && tp.bpm > 0 && Number.isFinite(tp.bpm) && Number.isFinite(tp.beat)) {
      tp.beat += ((frames / rate) * tp.bpm) / 60;
    }
  };

  // ---- between two blocks
  const draw = () => {
    const t = performance.now();
    note('e.vibe_wasm_draw();', false);
    const fb = e.vibe_wasm_draw();
    if (fb <= 0 || fb + 480 * 320 * 2 > memory().byteLength) {
      fail('wrong-state', `vibe_wasm_draw gave a picture at ${fb}, outside the memory`);
    }
    if (e.vibe_wasm_stage_draw && (stageShown || (level > 0 && chance(0.1))) && chance(0.2)) {
      note('e.vibe_wasm_stage_draw();', false);
      e.vibe_wasm_stage_draw();
    }
    st.draws++;
    st.drawMaxMs = Math.max(st.drawMaxMs, performance.now() - t);
    Atomics.add(sharedInts, 0, 1);
  };
  const gap = () => {
    const seconds = lastFrames / rate;
    // what was put off until this block
    for (let i = 0; i < later.length; ) {
      if (later[i].block <= st.blocks) {
        const fn = later[i].fn;
        later.splice(i, 1);
        fn();
      } else {
        i++;
      }
    }
    for (let i = 0; i < sysexPerGap && sysexQueue.length; i++) {
      sysex(sysexQueue.shift());
    }
    if (chance(seconds * actionsPerSecond)) {
      action();
    }
    if (chance(seconds / 20)) {
      burst();
    }
    if (chance(seconds / restartEvery)) {
      restart();
    }
    if (fps && chance(seconds * fps)) {
      draw();
    }
  };

  // the last 40 events - or, with --trace, every call of the last second
  const tail = () => {
    const from = st.frames - rate;
    const lines = trace ? log.filter((x) => x.frames >= from) : log.filter((x) => x.isEvent).slice(-40);
    return lines.map(
      (x) => `[block ${x.block}] ${x.text.length > 4000 && !trace ? x.text.slice(0, 4000) + ' ...' : x.text}`,
    );
  };
  // samples over full scale (finite ones): how many, the highest, and the first with what led to it
  const over = { count: 0, worst: 0, what: '', where: '', lines: [] };

  // ---- the run
  let failure = null;
  try {
    // what the store holds before the first start: nothing, or a song of the firmware before
    if (oldSong && chance(0.3)) {
      files.set('song.bin', Buffer.from(oldSong));
    }
    if (level > 0 && chance(0.2)) {
      spoilFiles();
    }
    boot();
    while (st.frames < endFrames) {
      gap();
      block();
    }
    // at the end: saved, and a last start with an honest store - after all that it still starts and plays a second
    ev(EV.SAVE, 0, 0, 0);
    boot();
    storeMode = 'honest';
    for (let i = 0; i < rate / 128; i++) {
      block();
    }
    draw();
  } catch (err) {
    const where = `block ${st.blocks + 1}, ${(st.frames / rate).toFixed(3)} s`;
    if (err instanceof Failure) {
      failure = { kind: err.kind, what: err.message, where };
    } else {
      // a trap (WebAssembly.RuntimeError), or the stack ran out inside the firmware (a RangeError)
      const stack = String(err && err.stack ? err.stack : '')
        .split('\n')
        .filter((s) => s.includes('wasm://') || s.includes('wasm-function'))
        .slice(0, 8)
        .map((s) => s.trim());
      // (anything else is a mistake of this script: it is said as that, not as the firmware's)
      if (!(err instanceof WebAssembly.RuntimeError) && !stack.length) {
        throw err;
      }
      failure = {
        kind: 'trap',
        what: `${err && err.constructor ? err.constructor.name : 'Error'}: ${err && err.message}`,
        where,
        stack,
      };
    }
    failure.lines = tail();
    if (over.count) {
      failure.what += `   (before it, ${over.count} samples over full scale, the highest ${num(over.worst)})`;
    }
  }
  if (!failure && over.count) {
    failure = {
      kind: 'bad-audio',
      what: `${over.what} - the first of ${over.count} samples over full scale, the highest ${num(over.worst)}`,
      where: over.where,
      lines: over.lines,
      ranOn: true,
    };
  }
  return {
    seed,
    rate,
    level,
    failure,
    seconds: st.frames / rate,
    setup: `${actionsPerSecond} actions a second, ${fps} draws a second, a restart every ${restartEvery} s`,
    blocks: st.blocks,
    events: st.events,
    restarts: st.restarts,
    draws: st.draws,
    storeCalls: st.storeCalls,
    midiOut: st.midiOut,
    sysexOut: st.sysexOut,
    dropped: st.dropped,
    peak: st.peak,
    audioMs: st.audioMs,
    worstRatio: st.worstRatio,
    worstRatioBlock: st.worstRatioBlock,
    worstRatioFrames: st.worstRatioFrames,
    worstMs: st.worstMs,
    worstMsBlock: st.worstMsBlock,
    worstMsFrames: st.worstMsFrames,
    callMaxMs: st.callMaxMs,
    callMaxText: st.callMaxText,
    drawMaxMs: st.drawMaxMs,
    types: [...st.types].filter((t) => EV_NAME[t]).length,
    params: st.params.size,
    paramCount: params.length,
    gotSong: !!baseSong,
    hadOldSong: !!oldSong,
  };
}

// (the store's own pick: from its own PRNG)
function pick2(S, list) {
  return list[Math.floor(S() * list.length)];
}

// ================================================================================================================
// the main thread: the seeds, each in a thread; the watchdog; the report
// ================================================================================================================
if (!isMainThread) {
  parentPort.postMessage(fuzz(workerData));
} else {
  const args = process.argv.slice(2);
  const opt = (name, fallback) => {
    const i = args.indexOf(name);
    if (i < 0) {
      return fallback;
    }
    const v = Number(args[i + 1]);
    args.splice(i, 2);
    return v;
  };
  const flag = (name) => {
    const i = args.indexOf(name);
    if (i >= 0) {
      args.splice(i, 1);
    }
    return i >= 0;
  };
  const oneSeed = opt('--seed', null);
  const seedCount = opt('--seeds', 20);
  const first = opt('--first', 1);
  const minutes = opt('--minutes', 2);
  const level = opt('--level', 2);
  const jobs = Math.max(1, opt('--jobs', 1));
  const hangSeconds = opt('--hang', 30);
  const trace = flag('--trace');
  const sdkLoop = flag('--sdk-loop');
  const wasmFile = args[0] ?? 'build/sfx-forge.wasm';
  const bad = [oneSeed ?? 0, seedCount, first, minutes, level, jobs, hangSeconds].some((v) => !Number.isFinite(v));
  if (bad || args.length > 1 || ![0, 1, 2].includes(level) || !fs.existsSync(wasmFile)) {
    console.log(
      `usage: node tests/${NAME}.mjs build/sfx-forge.wasm [--seeds 20] [--seed N] [--minutes 2] [--level 0|1|2] [--trace]`,
    );
    console.log('       [--jobs 1] [--first 1] [--hang 30]');
    process.exit(1);
  }
  const seeds = oneSeed !== null ? [oneSeed] : Array.from({ length: seedCount }, (_, i) => first + i);
  const replay = (seed) => `node tests/${NAME}.mjs ${wasmFile} --seed ${seed} --level ${level} --trace`;

  console.log(
    `${NAME}: ${wasmFile}, ${seeds.length} seed${seeds.length > 1 ? 's' : ''} of ${minutes} device-minutes, level ${level}`,
  );

  let failed = 0;
  const done = [];
  const kinds = new Map();
  const say = (ok, line) => {
    console.log((ok ? '  ok    ' : '  FAIL  ') + line);
    if (!ok) {
      failed++;
    }
  };
  const count = (kind, what, seed) => {
    const key = `${kind}: ${what}`;
    kinds.set(key, [...(kinds.get(key) ?? []), seed]);
  };
  const report = (r) => {
    done.push(r);
    const real = (r.audioMs / 10 / Math.max(r.seconds, 1e-9)).toFixed(1);
    const facts =
      `${r.rate} Hz, ${r.seconds.toFixed(1)} s in ${r.blocks} blocks, ${r.events} events, ${r.restarts} restarts, ` +
      `peak ${r.peak.toFixed(4)}, audio ${real} % of real time (slowest block: ${r.worstRatio.toFixed(2)} of its budget, ` +
      `block ${r.worstRatioBlock} of ${r.worstRatioFrames} frames), ${r.midiOut + r.sysexOut} MIDI out`;
    if (!r.failure) {
      say(true, `seed ${r.seed}: ${facts}`);
      if (r.audioMs / 1000 > r.seconds) {
        say(
          false,
          `seed ${r.seed}: its blocks took ${(r.audioMs / 1000).toFixed(1)} s for ${r.seconds.toFixed(1)} s of sound: slower than real time`,
        );
        count('slow', 'the blocks of a seed take longer than they play', r.seed);
      }
      return;
    }
    const f = r.failure;
    say(false, `seed ${r.seed} (${r.rate} Hz; ${r.setup}): ${f.kind} at ${f.where}: ${f.what}`);
    count(
      f.kind,
      f.what
        .replace(/ {3}\(before it.*/, '')
        .replace(/ - the first of .*/, '')
        .replace(/_[lr]\[\d+\] of a block of \d+ frames/, ''),
      r.seed,
    );
    for (const s of f.stack ?? []) {
      console.log(`          ${s}`);
    }
    console.log(`        ${f.ranOn ? 'the seed ran on to its end' : 'before it'}: ${facts}`);
    console.log(trace ? '        every call of the last second:' : '        the last 40 events:');
    for (const s of f.lines) {
      console.log(`          ${s}`);
    }
    console.log(`        again: ${replay(r.seed)}`);
  };

  // ---- the threads, `jobs` at a time, and the watchdog over them
  const queue = seeds.slice();
  const running = new Set();
  const decoder = (shared) => {
    const ints = new Int32Array(shared, 0, 4);
    const bytes = new Uint8Array(shared, 16);
    const w = Atomics.load(ints, 2);
    const lines = [];
    for (let i = Math.max(0, w - RING); i < w; i++) {
      const slot = (i % RING) * RING_SLOT;
      const n = bytes[slot] | (bytes[slot + 1] << 8);
      lines.push(String.fromCharCode(...bytes.slice(slot + 2, slot + 2 + n)));
    }
    return { block: Atomics.load(ints, 1), lines };
  };
  const finish = () => {
    console.log('');
    const ok = done.filter((r) => !r.failure);
    if (ok.length) {
      const sum = (f) => ok.reduce((s, r) => s + f(r), 0);
      const slow = ok.reduce((a, r) => (r.worstRatio > a.worstRatio ? r : a));
      const long = ok.reduce((a, r) => (r.worstMs > a.worstMs ? r : a));
      const loud = ok.reduce((a, r) => (r.peak > a.peak ? r : a));
      const call = ok.reduce((a, r) => (r.callMaxMs > a.callMaxMs ? r : a));
      console.log(
        `  the ${ok.length} seeds that ran through: ${(sum((r) => r.seconds) / 60).toFixed(1)} device-minutes, ${sum((r) => r.blocks)} blocks, ` +
          `${sum((r) => r.events)} events, ${sum((r) => r.restarts)} restarts, ${sum((r) => r.draws)} draws, ${sum((r) => r.storeCalls)} store calls, ` +
          `${sum((r) => r.midiOut)} MIDI messages and ${sum((r) => r.sysexOut)} SysEx out (${sum((r) => r.dropped)} more dropped by the full queue)`,
      );
      console.log(
        `  audio: ${(sum((r) => r.audioMs) / 10 / sum((r) => r.seconds)).toFixed(1)} % of real time over all; the slowest block took ` +
          `${slow.worstRatio.toFixed(2)} of its budget (seed ${slow.seed}, block ${slow.worstRatioBlock}, ${slow.worstRatioFrames} frames at ${slow.rate} Hz); ` +
          `the longest block ${long.worstMs.toFixed(2)} ms (seed ${long.seed}, block ${long.worstMsBlock}, ${long.worstMsFrames} frames at ${long.rate} Hz)`,
      );
      console.log(
        `  the highest peak: ${loud.peak.toFixed(4)} (seed ${loud.seed}); the longest draw ${Math.max(...ok.map((r) => r.drawMaxMs)).toFixed(2)} ms`,
      );
      console.log(
        `  the longest call between blocks: ${call.callMaxMs.toFixed(2)} ms (seed ${call.seed}): ${call.callMaxText}`,
      );
      console.log(
        `  reached: ${Math.max(...ok.map((r) => r.types))} of ${Object.keys(EV).length} event types and ${Math.max(...ok.map((r) => r.params))} of ` +
          `${ok[0].paramCount} parameters in a seed; ${ok.filter((r) => r.gotSong).length} seeds got the firmware's song back and sent it in again`,
      );
      if (!ok[0].hadOldSong) {
        console.log('  (tests/data/DemoSong-format5.bytes is not there: no format-5 song was sent)');
      }
    }
    if (kinds.size) {
      console.log('  what failed, and in which seeds:');
      for (const [key, list] of kinds) {
        console.log(
          `    ${list.length} x ${key}   (seeds ${list.slice(0, 12).join(', ')}${list.length > 12 ? ', ...' : ''})`,
        );
      }
    }
    console.log(failed ? `${NAME} FAILED` : `${NAME} PASSED`);
    process.exit(failed ? 1 : 0);
  };
  const next = () => {
    while (running.size < jobs && queue.length) {
      const seed = queue.shift();
      const shared = new SharedArrayBuffer(SHARED_BYTES);
      const worker = new Worker(new URL(import.meta.url), {
        workerData: { wasmFile, seed, minutes, level, trace, shared, sdkLoop },
      });
      const run = { seed, worker, shared, tick: -1, since: Date.now(), over: false };
      const end = () => {
        if (!run.over) {
          run.over = true;
          running.delete(run);
          next();
        }
      };
      running.add(run);
      worker.on('message', (r) => {
        report(r);
        end();
      });
      worker.on('error', (err) => {
        say(false, `seed ${seed}: the fuzzer itself broke: ${err && err.stack ? err.stack : err}`);
        count('fuzzer', 'the fuzzer itself broke', seed);
        end();
      });
    }
    if (!running.size && !queue.length) {
      clearInterval(watch);
      finish();
    }
  };
  // the watchdog: a seed whose counter has not moved for --hang seconds stands in a call that does not come back
  const watch = setInterval(() => {
    for (const run of [...running]) {
      const tick = Atomics.load(new Int32Array(run.shared, 0, 4), 0);
      if (tick !== run.tick) {
        run.tick = tick;
        run.since = Date.now();
      } else if (Date.now() - run.since > hangSeconds * 1000 && !run.over) {
        const { block, lines } = decoder(run.shared);
        say(
          false,
          `seed ${run.seed}: hang at block ${block}: the last call has not come back for ${hangSeconds} s: ${lines[lines.length - 1] ?? ''}`,
        );
        count(
          'hang',
          (lines[lines.length - 1] ?? '').replace(/-?\d+(\.\d+)?(e[+-]?\d+)?/g, 'N').slice(0, 80),
          run.seed,
        );
        console.log('        the last calls:');
        for (const s of lines) {
          console.log(`          ${s}`);
        }
        console.log(`        again: ${replay(run.seed)}`);
        run.over = true;
        running.delete(run);
        run.worker.terminate().then(next);
      }
    }
  }, 500);
  next();
}
