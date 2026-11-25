// The MIDI torture, through the SDK's emulator and without a MIDI cable: the page of .tools/vibe-fm-sdk/emulator runs
// SFX FORGE in a Chrome of its own, the page's Web MIDI is a stand-in this script puts there, and the stream of
// tests/midi-torture is fed into the stand-in's input port, message by message, while the script watches that the
// firmware and the page stay alive.
//
// WHAT IT PROVES. The host JavaScript the emulator, the Playground and the VIBE FM plugin's window share
// (js/controllers.js, js/host.js, js/worklet.js) takes the whole stream - notes, controllers, valid and hostile SysEx,
// all at once - and hands it to the firmware in the browser's audio thread, and all through it:
//   - the stream leaves the page for the firmware: what the page posts to the worklet is counted on its way - every
//     SysEx given to the input goes on as 'sysex', the notes and controllers as VIBE_EV_MIDI, channel 10's 36-51 as
//     pads - and no controller profile or channel filter of the page stands between;
//   - no exception in the page, in the audio worklet or in a worker, no console error, no crashed target, and the
//     firmware does not trap (the worklet's "crash" message);
//   - the firmware answers the identity request (F0 7D 56 42 07 F7 -> F0 7D 56 42 08 ...) every few seconds;
//   - the display is still drawn (frames from the worklet are counted, and how often the picture changed);
//   - the audio still runs (the AudioContext is "running", its clock advances, no sample is NaN or infinite);
//   - at the end, that the firmware took what was sent: pad 1 of the generator's ending is heard, a pad request is
//     answered with the sound the ending put there, and a small song of this script's, sent and then asked back
//     (F0 7D 56 42 06 F7), returns byte for byte; after STOP and PLAY a song sounds (any song would: the bytes asked
//     back say it is this one).
// --selftest proves that the watchers can see a dead firmware: it stops the audio for a while, then makes the worklet
// stop the firmware as a trap does, and passes only if the probe, the audio clock and (for the trap) the frames FAIL
// in those seconds (and hold before). It reads the watchers, not a full run's verdict lines: that a full run ends
// FAILED was seen by hand (the AudioContext closed, or the input's listener taken away, ten seconds into a stream).
//
// WHAT IT CANNOT SEE. No real MIDI port, driver or cable is in it: the stand-in replaces navigator.requestMIDIAccess
// before the page's scripts run, so Chrome's own MIDI code (its SysEx collector, its permission prompt) is not
// tested. By default every line of the generator's dump is handed over as ONE message, as it stands (--framing raw):
// that is harsher than a browser, which hands over whole messages only (--framing webmidi does it that way, by this
// script's own reading of the rules). The messages come in bursts of about 10 ms, not byte by byte. The sound is
// measured, not judged: a peak says something sounds, not that it sounds right (an AnalyserNode beside the
// firmware's node, looked at ten times a second). What the worklet does with a message it was posted is not seen
// from here (it drops a SysEx over 4096 bytes without a word); only the firmware's answers say it arrived. The frames
// are the page's and come without audio too: a stopped AudioContext shows in the probe and the audio clock, not in
// the display's line. It is the emulator's host, not the plugin's native half and not
// the device. It does not start Unity.
//
// HOW TO RUN IT (Windows; Node 22, Chrome, Python 3, the .NET SDK for the generator; sh build.sh first, so that
// .tools/vibe-fm-sdk/build/sfx-forge.wasm is the firmware of today). From the repository's folder:
//   node tests/emulator-torture.mjs [--seed 1] [--level 1..3] [--seconds 120] [--framing raw|webmidi]
//                                   [--rate bytes-a-second] [--rate5 bytes-a-second] [--dump file] [--selftest]
//                                   [--port 8767] [--debug-port 9334] [--chrome path] [--python python]
//     --seconds   the length of the stream, cut in five equal phases as the generator does (120); the generator's
//                 ending (about 9 s) comes after it; in a short stream phases 3 and 4 run longer than their fifth
//     --rate      phases 1 to 4, in bytes a second: the level's own by default (3000, 30000, 300000)
//     --rate5     phase 5 ("all at once"): ten times --rate by default, 120000 at most
//     --dump      a dump made before (midi-torture --dry --dump file) instead of a new one
// It starts the emulator's own server (tools/emulator.py, which copies the firmwares, their list and the controller
// profiles into the SDK's emulator folder - emulator/firmware, emulator/controllers.json - as it always does; its
// opening of your browser is switched off) and a Chrome with a throwaway profile under out/torture, and stops both
// at the end, also when it fails. Its own files are all under out/ (the profile and the dumps in out/torture, the
// generator's build in out/midi-torture). Not part of sh build.sh: it needs a browser.
// The last line is "emulator-torture PASSED" or "emulator-torture FAILED" (exit code 0 or 1).
//
// How it reads, top to bottom: the options; the stand-in (JavaScript that runs in the page); the stream (the
// generator, its dump, the framing); the server, Chrome and how the script ends; the connection to Chrome; the page
// switched on; the watchers (probes, polls); the feed; the ending; the self-test; the verdict.
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

// ---- the options
const arg = (name, fallback) => {
  const i = process.argv.indexOf('--' + name);
  return i > 0 ? process.argv[i + 1] : fallback;
};
const ROOT = path.resolve(import.meta.dirname, '..');
const SELFTEST = process.argv.includes('--selftest');
const SEED = Number(arg('seed', '1'));
const LEVEL = Number(arg('level', '1'));
const SECONDS = Number(arg('seconds', SELFTEST ? '32' : '120'));
const FRAMING = arg('framing', 'raw');
const RATE = Number(arg('rate', String([3000, 30000, 300000][LEVEL - 1] || 3000)));
const RATE5 = Number(arg('rate5', String(Math.min(120000, RATE * 10))));
const DUMP = arg('dump', '');
const PORT = Number(arg('port', '8767'));
const DEBUG_PORT = Number(arg('debug-port', '9334'));
// (Chrome where its installer puts it, under the folder Windows names in ProgramFiles; elsewhere: --chrome or CHROME)
const CHROME = arg(
  'chrome',
  process.env.CHROME || path.join(process.env.ProgramFiles || '', 'Google', 'Chrome', 'Application', 'chrome.exe'),
);
const PYTHON = arg('python', 'python');
const FIRMWARE = 'sfx-forge';
const OUT = path.join(ROOT, 'out', 'torture');
const PROFILE = path.join(OUT, 'chrome-profile');
const PAGE_URL = `http://127.0.0.1:${PORT}/`;
const PROBE_EVERY = SELFTEST ? 700 : 3000;
const PROBE_WAIT = 2000;

// the buttons of the emulator's panel in the order the page creates them (as tests/emulator-link.mjs has them)
// prettier-ignore
const PANEL = [
  'RETURN', 'HOME', 'SHIFT', 'OCT-', 'OCT+', 'ENTER',
  'K1', 'K2', 'K3', 'K4', 'K5', 'K6',
  'F1', 'F2', 'F3', 'F4', 'F5', 'F6',
  'LEFT', 'MENU', 'RIGHT', 'REC', 'STOP', 'PLAY',
];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const say = (...a) => console.log('TORTURE:', ...a);
const lines = [];
let failed = 0;
function ok(good, what) {
  lines.push((good ? '  ok    ' : '  FAIL  ') + what);
  if (!good) {
    failed++;
  }
}
const note = (what) => lines.push('        ' + what);

// the firmware's version, as the emulator's list has it: the identity must carry these three bytes
const card = JSON.parse(fs.readFileSync(path.join(ROOT, '.tools/vibe-fm-sdk/build', FIRMWARE + '.json'), 'utf8'));
const VERSION = card.version.split('.').map(Number);

// ---- the stand-in: this function runs in the emulator's page, before any of the page's own scripts.
// It puts one MIDI input and one MIDI output where the page looks for them (their names are no Launchpad's, no MPK's
// and no controller profile's, so js/controllers.js takes the input as "any other controller" and lets SysEx
// through), counts what the firmware sends, and listens beside the page to what the audio worklet says.
function standIn() {
  const T = (window.__torture = {
    asked: 0,
    fed: 0,
    errors: [],
    out: { shorts: 0, sysex: 0, idents: 0, dumps: 0, pieces: 0, others: 0, bytes: 0 },
    version: null,
    dump: {},
    song: { count: 0, pieces: {} },
    waiters: [],
    nodes: 0,
    unheard: 0,
    given: { sysex: 0, kept: 0, short: 0 },
    posted: { sysex: 0, sysexBytes: 0, pad: 0, midi: 0, notes: 0, transport: 0, ev: 0, other: 0 },
    readys: 0,
    crashes: [],
    logs: [],
    frames: 0,
    changes: 0,
    hash: null,
    frameAt: 0,
    gap: 0,
    ctx: null,
    analyser: null,
    buf: null,
    peak: 0,
    peakPoll: 0,
    peakMark: 0,
    bad: 0,
  });
  const port = (type, name) => {
    const p = new EventTarget();
    Object.assign(p, {
      id: 'torture-' + type,
      name,
      manufacturer: 'tests/emulator-torture.mjs',
      type,
      version: '1',
      state: 'connected',
      connection: 'open',
      onmidimessage: null,
      onstatechange: null,
      open: () => Promise.resolve(p),
      close: () => Promise.resolve(p),
    });
    return p;
  };
  const input = (T.input = port('input', 'Torture In'));
  const output = (T.output = port('output', 'Torture Out'));
  const wake = (kind, value) => {
    const w = T.waiters;
    T.waiters = [];
    for (const x of w) {
      if (x.kind === kind && (x.slot === undefined || x.slot === value)) {
        x.done(true);
      } else {
        T.waiters.push(x);
      }
    }
  };
  // what the firmware sends: counted by kind; the last identity's version and the last dump of each pad are kept
  output.send = (data) => {
    const d = Array.from(data);
    const o = T.out;
    o.bytes += d.length;
    if (d[0] !== 0xf0) {
      o.shorts++;
      return;
    }
    o.sysex++;
    if (d[1] !== 0x7d || d[2] !== 0x56 || d[3] !== 0x42) {
      o.others++;
    } else if (d[4] === 8) {
      o.idents++;
      T.version = d.slice(6, 9);
      wake('ident');
    } else if (d[4] === 1) {
      o.dumps++;
      T.dump[d[5]] = d.slice(6, d.length - 1);
      wake('dump', d[5]);
    } else if (d[4] === 5) {
      o.pieces++;
      // (a piece of the song: its number, the count of pieces, its bytes - sfx-forge/fw_link.h, export_pump)
      const k = d[5] | (d[6] << 7);
      T.song.count = d[8] | (d[9] << 7);
      T.song.pieces[k] = d.slice(10, d.length - 1);
      if (Object.keys(T.song.pieces).length >= T.song.count) {
        wake('song');
      }
    } else {
      o.others++;
    }
  };
  output.clear = () => {};
  const access = {
    inputs: new Map([[input.id, input]]),
    outputs: new Map([[output.id, output]]),
    sysexEnabled: true,
    onstatechange: null,
  };
  Object.defineProperty(navigator, 'requestMIDIAccess', {
    configurable: true,
    value: () => {
      T.asked++;
      return Promise.resolve(access);
    },
  });
  if (window.Permissions) {
    const query = Permissions.prototype.query;
    Permissions.prototype.query = function (d) {
      if (d && d.name === 'midi') {
        return Promise.resolve({
          name: 'midi',
          state: 'granted',
          onchange: null,
          addEventListener() {},
          removeEventListener() {},
        });
      }
      return query.call(this, d);
    };
  }

  // one message into the input, as the browser would call the page; what the page's handler throws is kept
  T.send = (bytes) => {
    const h = input.onmidimessage;
    if (!h) {
      T.unheard++;
      return;
    }
    // (counted as the page's js/controllers.js sorts it: F0 first = a SysEx for the firmware, but for VIBE FM Link's
    // "drawn" answer, which the page keeps)
    if (bytes[0] !== 0xf0) {
      T.given.short++;
    } else if (bytes[1] === 0x7d && bytes[2] === 0x56 && bytes[3] === 0x4c && bytes[4] === 0x03) {
      T.given.kept++;
    } else {
      T.given.sysex++;
    }
    try {
      h.call(input, { data: bytes, target: input, timeStamp: performance.now() });
    } catch (e) {
      if (T.errors.length < 50) {
        const hex = Array.from(bytes.subarray(0, 48), (b) => b.toString(16).padStart(2, '0')).join(' ');
        T.errors.push({
          text: String(e && e.message ? e.message : e),
          stack: String((e && e.stack) || '')
            .split('\n')
            .slice(1, 4)
            .join(' | '),
          bytes: hex + (bytes.length > 48 ? ' … (' + bytes.length + ' bytes)' : ''),
          fed: T.fed,
        });
      }
    }
  };
  // many messages: "903c40,f07d564207f7,..."
  T.feed = (text) => {
    for (const m of text.split(',')) {
      const b = new Uint8Array(m.length >> 1);
      for (let i = 0; i < b.length; i++) {
        b[i] = parseInt(m.substr(i * 2, 2), 16);
      }
      T.fed++;
      T.send(b);
    }
    return T.fed;
  };
  const wait = (kind, slot, ms) =>
    new Promise((done) => {
      const w = { kind, slot, done };
      T.waiters.push(w);
      setTimeout(() => {
        T.waiters = T.waiters.filter((x) => x !== w);
        done(false);
      }, ms);
    });
  // the identity request, and whether an identity came within ms
  T.probe = (ms) => {
    const t0 = performance.now();
    const w = wait('ident', undefined, ms);
    T.send(new Uint8Array([0xf0, 0x7d, 0x56, 0x42, 0x07, 0xf7]));
    return w.then((got) => ({ got, ms: Math.round(performance.now() - t0), version: got ? T.version : null }));
  };
  // a pad's request, and its dump
  T.request = (slot, ms) => {
    delete T.dump[slot];
    const w = wait('dump', slot, ms);
    T.send(new Uint8Array([0xf0, 0x7d, 0x56, 0x42, 0x04, slot, 0xf7]));
    return w.then((got) => ({ got, patch: got ? T.dump[slot] : null }));
  };

  // the song's request (the firmware sends its 16 sounds, then the song in pieces), and the song's bytes
  T.requestSong = (ms) => {
    T.song = { count: 0, pieces: {} };
    const w = wait('song', undefined, ms);
    T.send(new Uint8Array([0xf0, 0x7d, 0x56, 0x42, 0x06, 0xf7]));
    return w.then((got) => {
      let bytes = [];
      for (let k = 0; k < T.song.count; k++) {
        bytes = bytes.concat(T.song.pieces[k] || []);
      }
      return { got, count: T.song.count, bytes };
    });
  };

  // every AudioWorkletNode the page makes is the firmware's: its messages are heard here too (the page's own handler
  // gets them as before), and an analyser listens beside its output (one more connection; the sound goes on as it did)
  const Base = window.AudioWorkletNode;
  if (Base) {
    window.AudioWorkletNode = class extends Base {
      constructor(ctx, name, options) {
        super(ctx, name, options);
        T.nodes++;
        T.ctx = ctx;
        // what the page hands to the worklet is counted on its way (and handed on as it is): the proof that the stream
        // is not only given to the page but leaves it for the firmware - SysEx as 'sysex', the rest as events
        const post = this.port.postMessage.bind(this.port);
        this.port.postMessage = (d, ...more) => {
          const c = T.posted;
          if (d && d.t === 'sysex') {
            c.sysex++;
            c.sysexBytes += d.bytes ? d.bytes.length : 0;
          } else if (d && d.t === 'ev') {
            c.ev++;
            if (d.type === 1) {
              c.pad++;
            } else if (d.type === 5) {
              c.midi++;
              const st = d.midi & 0xf0;
              if (st === 0x90 || st === 0x80) {
                c.notes++;
              }
            } else if (d.type === 8) {
              c.transport++;
            }
          } else {
            c.other++;
          }
          return post(d, ...more);
        };
        this.port.addEventListener('message', (e) => {
          const d = e.data;
          if (!d) {
            return;
          }
          if (d.t === 'frame') {
            const now = performance.now();
            if (T.frameAt) {
              T.gap = Math.max(T.gap, now - T.frameAt);
            }
            T.frameAt = now;
            T.frames++;
            const fb = new Uint16Array(d.fb);
            let h = 0;
            for (let i = 0; i < fb.length; i += 53) {
              h = (Math.imul(h, 31) + fb[i]) | 0;
            }
            if (h !== T.hash) {
              T.hash = h;
              T.changes++;
            }
          } else if (d.t === 'crash') {
            T.crashes.push({ error: String(d.error), fed: T.fed });
          } else if (d.t === 'ready') {
            T.readys++;
          } else if (d.t === 'log' && T.logs.length < 20) {
            T.logs.push(String(d.text));
          }
        });
        try {
          const a = ctx.createAnalyser();
          a.fftSize = 8192;
          this.connect(a);
          T.analyser = a;
          T.buf = new Float32Array(a.fftSize);
        } catch (e) {
          T.analyser = null;
        }
      }
    };
  }
  setInterval(() => {
    if (!T.analyser) {
      return;
    }
    T.analyser.getFloatTimeDomainData(T.buf);
    let p = 0;
    for (const v of T.buf) {
      if (!Number.isFinite(v)) {
        T.bad++;
      } else if (Math.abs(v) > p) {
        p = Math.abs(v);
      }
    }
    T.peak = Math.max(T.peak, p);
    T.peakPoll = Math.max(T.peakPoll, p);
    T.peakMark = Math.max(T.peakMark, p);
  }, 100);
  // the peak since the last call
  T.mark = () => {
    const p = T.peakMark;
    T.peakMark = 0;
    return p;
  };
  // how things stand: read once a second by the script
  T.health = () => {
    const now = performance.now();
    const gap = Math.max(T.gap, T.frameAt ? now - T.frameAt : 0);
    T.gap = 0;
    const peak = T.peakPoll;
    T.peakPoll = 0;
    const v = window.__vibe;
    return {
      now,
      fed: T.fed,
      frames: T.frames,
      changes: T.changes,
      gap,
      state: T.ctx ? T.ctx.state : 'none',
      time: T.ctx ? T.ctx.currentTime : 0,
      peak,
      top: T.peak,
      bad: T.bad,
      nodes: T.nodes,
      posted: T.posted,
      given: T.given,
      unheard: T.unheard,
      profile: !!(v && v.ctl && v.ctl.profileOf && v.ctl.profileOf(input.name)),
      channel: v && v.ctl ? v.ctl.channel : -1,
      readys: T.readys,
      crashes: T.crashes,
      errors: T.errors.length,
      out: T.out,
      menu: !!(v && v.panel && v.panel.menu && v.panel.menu.classList.contains('on')),
      firmware: (document.querySelector('#fw') || {}).value,
    };
  };
}

// ---- the stream: the generator's dump as a list of messages { hex, n, phase, where, pause }
// phase: 1..5, 6 = the generator's ending. pause: seconds to wait before the message (the ending's rests).
const hexOf = (bytes) => bytes.map((b) => b.toString(16).padStart(2, '0')).join('');

function readDump(file, wantEnd) {
  const body = [];
  const end = [];
  let phase = 0;
  let where = '';
  let pads = false;
  let rested = false;
  for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
    if (line[0] === '#') {
      const text = line.slice(2).replace(/^\[.*?\]\s*/, '');
      const p = /^PHASE (\d) of 5/.exec(text);
      if (p) {
        phase = Number(p[1]);
        where = 'phase ' + phase;
      } else if (text.startsWith('THE END')) {
        phase = 6;
        where = 'the ending';
      } else if (phase === 6 && text.trim().startsWith('pad 1')) {
        pads = true;
      } else if (/^\s*\d(\.\d+)?\s/.test(text)) {
        where = text.trim().slice(0, 70);
      }
    } else if (line[0] === 'M') {
      const hex = line.slice(2).replaceAll(' ', '').trim();
      if (!hex) {
        continue;
      }
      const m = { hex, n: hex.length / 2, phase, where, pause: 0, pads };
      if (phase === 6) {
        // the generator rests 2.2 s before the ending's song (a transfer that broke off is forgotten by then), and a
        // pad's hit lasts 150 ms with 850 ms to the next: here half a second around every message of the five hits
        if (!rested && hex.startsWith('f07d564205')) {
          rested = true;
          m.pause = 2.2;
        } else if (pads) {
          m.pause = 0.5;
        }
        end.push(m);
      } else {
        body.push(m);
      }
    }
  }
  return wantEnd ? body.concat(end) : body;
}

// "webmidi": the stream as a browser's Web MIDI input hands it over - whole messages only. A SysEx is collected over
// the lines until its F7; another status byte inside it throws it away; real-time bytes pass at once; data bytes
// without a status, an F7 alone and the status bytes MIDI has not defined are dropped. (This script's own reading of
// the rules, not Chrome's code.)
function asWebMidi(list) {
  const out = [];
  let sysex = null;
  let short = null;
  let need = 0;
  const lengthOf = (s) =>
    s >= 0xf0 ? (s === 0xf2 ? 3 : s === 0xf1 || s === 0xf3 ? 2 : s === 0xf6 ? 1 : 0) : (s & 0xe0) === 0xc0 ? 2 : 3;
  for (const m of list) {
    const emit = (bytes) => out.push({ ...m, hex: hexOf(bytes), n: bytes.length });
    for (let i = 0; i < m.hex.length; i += 2) {
      const b = parseInt(m.hex.substr(i, 2), 16);
      if (b >= 0xf8) {
        emit([b]);
      } else if (b === 0xf0) {
        sysex = [b];
        short = null;
      } else if (b === 0xf7) {
        if (sysex) {
          sysex.push(b);
          emit(sysex);
        }
        sysex = null;
        short = null;
      } else if (b >= 0x80) {
        sysex = null;
        need = lengthOf(b);
        short = need ? [b] : null;
        if (short && need === 1) {
          emit(short);
          short = null;
        }
      } else if (sysex) {
        sysex.push(b);
      } else if (short) {
        short.push(b);
        if (short.length === need) {
          emit(short);
          short = null;
        }
      }
    }
  }
  // (a pause belongs to the first message a line gave, not to each)
  let last = null;
  for (const m of out) {
    if (last && m.where === last.where && m.pause && m.pause === last.pause && m.hex !== last.hex && !m.pads) {
      m.pause = 0;
    }
    last = m;
  }
  return out;
}

// the generator, once a phase: phases 1 to 4 for a fifth of the length each at the level's rate, phase 5 for as many
// bytes as its fifth carries at --rate5 (dry, the generator's clock counts a million bytes a second there)
function generate() {
  fs.mkdirSync(OUT, { recursive: true });
  const each = SECONDS / 5;
  let list = [];
  const only = SELFTEST ? [1] : [1, 2, 3, 4, 5];
  for (const phase of only) {
    const file = path.join('out', 'torture', `seed${SEED}-level${LEVEL}-phase${phase}.txt`);
    const seconds = SELFTEST ? SECONDS : phase === 5 ? (each * RATE5) / 1e6 : each;
    const args = ['run', '-c', 'Release', '--project', 'tests/midi-torture'];
    if (phase !== only[0]) {
      args.push('--no-build');
    }
    args.push('--', '--dry', '--dump', file, '--seed', String(SEED), '--level', String(LEVEL));
    args.push('--phase', String(phase), '--seconds', seconds.toFixed(6));
    const r = spawnSync('dotnet', args, { cwd: ROOT, encoding: 'utf8' });
    if (r.status !== 0 || !/midi-torture PASSED/.test(r.stdout || '')) {
      throw new Error(
        'the generator failed for phase ' + phase + ': ' + (r.stdout || '').slice(-400) + (r.stderr || ''),
      );
    }
    list = list.concat(readDump(path.join(ROOT, file), phase === only[only.length - 1]));
  }
  return list;
}

let stream = DUMP ? readDump(path.resolve(DUMP), true) : generate();
if (FRAMING === 'webmidi') {
  stream = asWebMidi(stream);
} else if (FRAMING !== 'raw') {
  throw new Error('--framing is raw or webmidi');
}
// when each message is due, in seconds from the stream's start
{
  let t = 0;
  for (const m of stream) {
    t += m.pause;
    m.at = t;
    t += m.n / (m.phase === 5 ? RATE5 : m.phase === 6 ? 6000 : RATE);
  }
}
// the self-test's sabotage, in seconds of the stream: the audio stopped for a while, then the firmware stopped as a
// trap stops it. Its stream ends there: with the firmware stopped the page shows its library, and a pad of the
// stream would start a firmware from it.
const sabotage = { suspend: 8, resume: 17, kill: 25 };
if (SELFTEST) {
  stream = stream.filter((m) => m.at < sabotage.kill);
}
const streamSeconds = stream.length ? stream[stream.length - 1].at : 0;
const streamBytes = stream.reduce((s, m) => s + m.n, 0);
say(
  SELFTEST ? 'SELF-TEST.' : '',
  DUMP ? 'dump ' + path.basename(DUMP) : `seed ${SEED}, level ${LEVEL}`,
  `· ${stream.length} messages, ${streamBytes} bytes, ${streamSeconds.toFixed(0)} s · framing ${FRAMING}`,
  `· ${RATE} bytes a second, phase 5 ${RATE5}`,
);

// ---- the emulator's server, Chrome, and how the script ends
// tools/emulator.py as it is, but its webbrowser.open (which would open the page in the user's own browser) answers
// "no" without opening anything
const server = spawn(
  PYTHON,
  [
    '-c',
    'import runpy, sys, webbrowser\n' +
      'webbrowser.open = lambda *a, **k: False\n' +
      'sys.argv = sys.argv[1:]\n' +
      "runpy.run_path(sys.argv[0], run_name='__main__')\n",
    path.join('.tools', 'vibe-fm-sdk', 'tools', 'emulator.py'),
    String(PORT),
  ],
  { cwd: ROOT, stdio: ['ignore', 'ignore', 'pipe'] },
);
let serverSaid = '';
let serverGone = false;
server.stderr.on('data', (d) => (serverSaid = (serverSaid + d).slice(-600)));
server.on('exit', () => (serverGone = true));

fs.rmSync(PROFILE, { recursive: true, force: true });
fs.mkdirSync(PROFILE, { recursive: true });
let chrome = null;
let browser = null;
let browserPid = 0;
let ending = false;

const alive = (pid) => {
  try {
    process.kill(pid, 0);
    return true;
  } catch (e) {
    return false;
  }
};
const answers = async (u) => {
  try {
    await fetch(u, { signal: AbortSignal.timeout(700) });
    return true;
  } catch (e) {
    return false;
  }
};

// the browser this script started and the server are stopped; true when both are gone
async function stopAll() {
  try {
    if (browser && !browser.closed()) {
      await Promise.race([browser.send('Browser.close'), sleep(2000)]);
    }
  } catch (e) {
    /* gone */
  }
  for (let i = 0; i < 25 && browserPid && alive(browserPid); i++) {
    await sleep(200);
  }
  for (const pid of [browserPid, chrome && chrome.pid]) {
    try {
      if (pid && alive(pid)) {
        process.kill(pid);
      }
    } catch (e) {
      /* gone */
    }
  }
  try {
    server.kill();
  } catch (e) {
    /* gone */
  }
  for (let i = 0; i < 25 && !serverGone; i++) {
    await sleep(100);
  }
  await sleep(300);
  const left = [];
  if (await answers(PAGE_URL)) {
    left.push('the server still answers');
  }
  if (await answers(`http://127.0.0.1:${DEBUG_PORT}/json/version`)) {
    left.push('the browser still answers');
  }
  return left;
}

// every way out goes through here: the verdict's lines, both processes stopped, the last line, the exit code
async function finish(why) {
  if (ending) {
    return;
  }
  ending = true;
  if (why) {
    ok(false, 'the test itself could not go on: ' + why);
  }
  const left = await stopAll();
  ok(left.length === 0, left.length ? left.join(', ') : 'the server and the browser this script started are stopped');
  console.log(lines.join('\n'));
  console.log(failed ? 'emulator-torture FAILED' : 'emulator-torture PASSED');
  process.exit(failed ? 1 : 0);
}
process.on('SIGINT', () => finish('stopped with Ctrl+C'));
process.on('SIGTERM', () => finish('stopped'));
process.on('uncaughtException', (e) => finish(e && e.message ? e.message : String(e)));
process.on('unhandledRejection', (e) => finish(e && e.message ? e.message : String(e)));
// (the last resort, if the script is left another way: nothing of ours stays behind)
process.on('exit', () => {
  for (const pid of [browserPid, chrome && chrome.pid, server.pid]) {
    try {
      if (pid) {
        process.kill(pid);
      }
    } catch (e) {
      /* gone */
    }
  }
});
// a run that hangs is ended: the stream's length and four minutes
setTimeout(() => finish('it took far longer than the stream'), (streamSeconds + 240) * 1000).unref();

// the server answers (ten seconds for that), and it is the one started here
for (let i = 0; ; i++) {
  if (serverGone) {
    await finish(`the emulator's server did not start on port ${PORT} (in use? --port): ` + serverSaid.trim());
  }
  if (await answers(PAGE_URL + 'firmware/index.json')) {
    break;
  }
  if (i > 50) {
    await finish('no answer from the emulator at ' + PAGE_URL);
  }
  await sleep(200);
}

chrome = spawn(
  CHROME,
  [
    '--remote-debugging-port=' + DEBUG_PORT,
    '--user-data-dir=' + PROFILE,
    '--no-first-run',
    '--no-default-browser-check',
    '--autoplay-policy=no-user-gesture-required',
    '--disable-backgrounding-occluded-windows',
    '--disable-renderer-backgrounding',
    '--disable-background-timer-throttling',
    '--window-size=1500,1000',
    '--window-position=40,40',
    'about:blank',
  ],
  { stdio: 'ignore' },
);

// ---- the connection to Chrome (the DevTools protocol)
async function json(u) {
  for (let i = 0; i < 75; i++) {
    try {
      return await (await fetch(u)).json();
    } catch (e) {
      await sleep(200);
    }
  }
  throw new Error('no answer from ' + u);
}

// a web socket to the browser or to a page: send(method, params, session) gives a promise of the answer, on(listener)
// hears the events (those of a worker or worklet attached to the page carry its sessionId)
function connect(ws) {
  const sock = new WebSocket(ws);
  let id = 0;
  let closed = false;
  const waiting = new Map();
  const listeners = [];
  sock.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && waiting.has(m.id)) {
      const [yes, no] = waiting.get(m.id);
      waiting.delete(m.id);
      if (m.error) {
        no(new Error(m.error.message));
      } else {
        yes(m.result);
      }
    } else {
      for (const l of listeners) {
        l(m);
      }
    }
  };
  sock.onclose = () => {
    closed = true;
    for (const [, no] of waiting.values()) {
      no(new Error('the connection to Chrome closed'));
    }
    waiting.clear();
  };
  return new Promise((ready, never) => {
    sock.onerror = () => never(new Error('no web socket to Chrome'));
    sock.onopen = () =>
      ready({
        send: (method, params = {}, sessionId) =>
          new Promise((yes, no) => {
            if (closed) {
              no(new Error('the connection to Chrome closed'));
              return;
            }
            waiting.set(++id, [yes, no]);
            sock.send(JSON.stringify(sessionId ? { id, method, params, sessionId } : { id, method, params }));
          }),
        on: (l) => listeners.push(l),
        closed: () => closed,
      });
  });
}

// what went wrong in the page, in the worklet or in a worker, with the second of the stream it happened in
// (before the stream: a negative time)
const trouble = [];
const attached = new Map();
let t0 = 0;
const clock = () => (t0 ? (Date.now() - t0) / 1000 : -1);
function listen(conn) {
  conn.on((m) => {
    const where = m.sessionId ? attached.get(m.sessionId) || 'a worker' : 'the page';
    const p = m.params || {};
    if (m.method === 'Runtime.exceptionThrown') {
      const d = p.exceptionDetails || {};
      const text = (d.exception && d.exception.description) || d.text || 'an exception';
      trouble.push({
        t: clock(),
        where,
        kind: 'exception',
        text: text.split('\n').slice(0, 3).join(' | '),
        url: d.url,
      });
    } else if (m.method === 'Runtime.consoleAPICalled' && (p.type === 'error' || p.type === 'assert')) {
      const text = (p.args || []).map((a) => (a.value !== undefined ? String(a.value) : a.description || a.type));
      trouble.push({ t: clock(), where, kind: 'console.error', text: text.join(' ').slice(0, 300) });
    } else if (m.method === 'Log.entryAdded' && p.entry && p.entry.level === 'error') {
      trouble.push({
        t: clock(),
        where,
        kind: 'log (' + p.entry.source + ')',
        text: String(p.entry.text).slice(0, 300),
        url: p.entry.url,
      });
    } else if (m.method === 'Inspector.targetCrashed' || m.method === 'Target.targetCrashed') {
      trouble.push({ t: clock(), where, kind: 'crashed', text: 'the target crashed (' + m.method + ')' });
    } else if (m.method === 'Target.attachedToTarget') {
      const info = p.targetInfo || {};
      attached.set(p.sessionId, info.type + (info.url ? ' ' + info.url.split('/').pop() : ''));
      for (const [method, params] of [
        ['Runtime.enable', {}],
        ['Log.enable', {}],
        ['Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }],
      ]) {
        conn.send(method, params, p.sessionId).catch(() => {});
      }
    }
  });
}

const version = await json(`http://127.0.0.1:${DEBUG_PORT}/json/version`);
browser = await connect(version.webSocketDebuggerUrl);
listen(browser);
try {
  const info = await browser.send('SystemInfo.getProcessInfo');
  browserPid = (info.processInfo.find((p) => p.type === 'browser') || {}).id || 0;
} catch (e) {
  browserPid = 0;
}
await browser.send('Target.setDiscoverTargets', { discover: true });
const target = (await json(`http://127.0.0.1:${DEBUG_PORT}/json/list`)).find((t) => t.type === 'page');
const page = await connect(target.webSocketDebuggerUrl);
listen(page);
await page.send('Page.enable');
await page.send('Runtime.enable');
await page.send('Log.enable');
await page.send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true });
await page.send('Page.addScriptToEvaluateOnNewDocument', { source: `(${standIn.toString()})()` });

// runs JavaScript in the emulator's page and gives its value
const run = async (expression) => {
  const r = await page.send('Runtime.evaluate', {
    expression,
    awaitPromise: true,
    returnByValue: true,
    userGesture: true,
  });
  if (r.exceptionDetails) {
    throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  }
  return r.result.value;
};
// waits until an expression is true in the page; false after `seconds`
async function until(expression, seconds) {
  for (let i = 0; i < seconds * 5; i++) {
    if (await run(expression)) {
      return true;
    }
    await sleep(200);
  }
  return false;
}

// ---- the page switched on: the firmware, the power switch, MIDI, the port - what a hand does in the top bar
await page.send('Page.navigate', { url: PAGE_URL });
await until(`document.readyState === 'complete' && document.querySelectorAll('#fw option').length > 2`, 15);
const firmwares = await run(`[...document.querySelectorAll('#fw option')].map(o => o.value)`);
const fw = firmwares.find((v) => v.includes(FIRMWARE));
if (!fw) {
  await finish('the emulator lists no firmware like ' + FIRMWARE + ': ' + JSON.stringify(firmwares));
}
await run(
  `(() => { const s = document.querySelector('#fw'); s.value = ${JSON.stringify(fw)}; ` +
    `s.dispatchEvent(new Event('change', { bubbles: true })); })()`,
);
await sleep(1500);
await run(`(() => { const b = document.querySelector('#power-on'); if (b && b.offsetParent) b.click(); })()`);
const started = await until(`window.__torture.frames > 3 && window.__torture.readys > 0`, 15);
ok(
  started,
  started ? `the emulator runs ${card.name} ${card.version} and draws` : 'the firmware did not start in the emulator',
);
if (!started) {
  await finish();
}
if (!(await run(`document.querySelector('#tb-midi').classList.contains('on')`))) {
  await run(`document.querySelector('#tb-midi').click()`);
}
await sleep(1500);
const ports = await run(`[...document.querySelectorAll('#midiout option')].map(o => o.value)`);
await run(
  `(() => { const s = document.querySelector('#midiout'); s.value = 'Torture Out'; ` +
    `s.dispatchEvent(new Event('change', { bubbles: true })); })()`,
);
await sleep(300);
const wired = await run(
  `({ asked: __torture.asked, handler: typeof __torture.input.onmidimessage === 'function', ` +
    `out: !!(__vibe.ctl.outPort && __vibe.ctl.outPort === __torture.output), status: document.querySelector('#status')?.textContent || '' })`,
);
ok(
  wired.asked > 0 && wired.handler && wired.out && ports.includes('Torture Out'),
  `the page asked for MIDI ${wired.asked} time(s) and got the stand-in: it listens on "Torture In" and sends to "Torture Out"`,
);
if (!(wired.handler && wired.out)) {
  await finish();
}

// ---- the watchers: the identity probe every few seconds, the page's health every second
const probes = [];
const polls = [];
let watching = true;
let lastPoll = null;
async function probeOnce() {
  const t = clock();
  const r = await run(`__torture.probe(${PROBE_WAIT})`);
  const right = r.got && r.version && VERSION.every((v, i) => r.version[i] === v);
  probes.push({ t, got: r.got, right, ms: r.ms, version: r.version });
  return r;
}
async function pollOnce() {
  const h = await run(`__torture.health()`);
  if (lastPoll) {
    const dt = (h.now - lastPoll.now) / 1000;
    polls.push({
      t: clock(),
      fps: (h.frames - lastPoll.frames) / dt,
      changes: h.changes - lastPoll.changes,
      gap: h.gap,
      state: h.state,
      audio: (h.time - lastPoll.time) / dt,
      peak: h.peak,
    });
  }
  lastPoll = h;
  return h;
}
const probing = (async () => {
  while (watching) {
    await probeOnce();
    await sleep(PROBE_EVERY);
  }
})();
const polling = (async () => {
  while (watching) {
    await pollOnce();
    await sleep(1000);
  }
})();

// what the watchers saw between two seconds of the stream
// (probesTo: the probes begun before that second - one begun later may be answered by what comes after the window)
function seen(from, to, probesTo = to) {
  const pr = probes.filter((p) => p.t >= from && p.t < probesTo);
  const po = polls.filter((p) => p.t >= from && p.t < to);
  let run_ = 0;
  let worst = 0;
  for (const p of pr) {
    run_ = p.got ? 0 : run_ + 1;
    worst = Math.max(worst, run_);
  }
  const min = (a) => (a.length ? Math.min(...a) : NaN);
  const max = (a) => (a.length ? Math.max(...a) : NaN);
  return {
    probes: pr.length,
    missed: pr.filter((p) => !p.got).length,
    wrong: pr.filter((p) => p.got && !p.right).length,
    missedInARow: worst,
    slowest: max(pr.filter((p) => p.got).map((p) => p.ms)),
    polls: po.length,
    fpsMin: min(po.map((p) => p.fps)),
    fpsMax: max(po.map((p) => p.fps)),
    gap: max(po.map((p) => p.gap)),
    changes: po.reduce((s, p) => s + p.changes, 0),
    audioMin: min(po.map((p) => p.audio)),
    audioMax: max(po.map((p) => p.audio)),
    notRunning: po.filter((p) => p.state !== 'running').length,
    peak: max(po.map((p) => p.peak)),
  };
}

// before the stream: three seconds in which everything must be well, or the test says nothing
await sleep(3200);
{
  const s = seen(-2, 0);
  const good =
    s.probes > 0 && s.missed === 0 && s.wrong === 0 && s.fpsMin > 5 && s.audioMin > 0.8 && s.notRunning === 0;
  ok(
    good,
    `before the stream: the identity is answered (${VERSION.join('.')}, ${s.slowest} ms at most), ` +
      `${s.fpsMin.toFixed(0)} frames a second, the audio clock runs (${s.audioMin.toFixed(2)} s a second)`,
  );
  if (!good) {
    note(JSON.stringify(s));
    watching = false;
    await finish();
  }
}

// ---- the feed: every 10 ms the messages that are due, in one call to the page
const phases = [];
let lagMost = 0;
async function feed(list) {
  let i = 0;
  while (i < list.length) {
    const now = clock();
    let j = i;
    let size = 0;
    while (j < list.length && list[j].at <= now && j - i < 5000 && size < 400000) {
      size += list[j].hex.length;
      j++;
    }
    if (j === i) {
      await sleep(Math.min(250, Math.max(2, (list[i].at - now) * 1000)));
      continue;
    }
    if (!phases.length || phases[phases.length - 1].phase !== list[i].phase) {
      phases.push({ phase: list[i].phase, from: now, messages: 0, bytes: 0 });
      say(`+${now.toFixed(0)} s  ${list[i].phase === 6 ? 'the ending' : 'phase ' + list[i].phase}`);
    }
    lagMost = Math.max(lagMost, now - list[i].at);
    const p = phases[phases.length - 1];
    // (a batch never runs over a phase's end: the phases are counted apart)
    let k = i;
    while (k < j && list[k].phase === p.phase) {
      k++;
    }
    j = k;
    p.messages += j - i;
    for (let x = i; x < j; x++) {
      p.bytes += list[x].n;
    }
    await run(
      `__torture.feed(${JSON.stringify(
        list
          .slice(i, j)
          .map((m) => m.hex)
          .join(','),
      )})`,
    );
    p.to = clock();
    i = j;
    await sleep(8);
  }
}

// a button of the device's panel, clicked with the mouse where the page shows it (tests/emulator-link.mjs)
async function press(name) {
  const found = await run(
    `(() => { const el = document.querySelectorAll('button.btn')[${PANEL.indexOf(name)}]; if (!el) return false; ` +
      `el.scrollIntoView({ block: 'center' }); const r = el.getBoundingClientRect(); ` +
      `return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`,
  );
  if (!found) {
    return false;
  }
  for (const type of ['mousePressed', 'mouseReleased']) {
    await page.send('Input.dispatchMouseEvent', { type, x: found.x, y: found.y, button: 'left', clickCount: 1 });
    await sleep(90);
  }
  await sleep(300);
  return true;
}

const send = (bytes) => run(`__torture.feed(${JSON.stringify(hexOf(bytes))})`);

// ---- the self-test's sabotage
if (SELFTEST) {
  setTimeout(() => {
    say('SELF-TEST: the AudioContext is suspended (the power off)');
    run(`__vibe.ctx.suspend().then(() => 1)`).catch(() => {});
  }, sabotage.suspend * 1000);
  setTimeout(() => {
    say('SELF-TEST: the AudioContext is resumed');
    run(`__vibe.ctx.resume().then(() => 1)`).catch(() => {});
  }, sabotage.resume * 1000);
  // (a message the worklet cannot read: it catches the error as it catches a trap of the firmware, stops the firmware
  // and tells the page "crash")
  setTimeout(() => {
    say('SELF-TEST: the worklet is made to stop the firmware, as after a trap');
    run(`__vibe.node.port.postMessage({ t: 'sysex', bytes: null })`).catch(() => {});
  }, sabotage.kill * 1000);
}

// ---- the stream
// (how often the page had started the firmware by now: choosing a firmware while the device is off starts it twice,
// by the page's own doing - what counts is that none is started after this)
const healthBefore = JSON.parse(JSON.stringify(await run(`__torture.health()`)));
const startsBefore = healthBefore.nodes;
ok(
  !healthBefore.profile && healthBefore.channel === 0,
  healthBefore.profile || healthBefore.channel !== 0
    ? `the page would not hand everything on: a controller profile claims "Torture In" (${healthBefore.profile}) or ` +
        `its MIDI channel filter is on (${healthBefore.channel})`
    : `no controller profile claims "Torture In" and the page's MIDI channel is "all": nothing is mapped or filtered away`,
);
t0 = Date.now();
const pads = stream.findIndex((m) => m.pads);
await feed(pads < 0 ? stream : stream.slice(0, pads));
let padPeak = null;
if (pads >= 0) {
  await sleep(600);
  await run(`__torture.mark()`);
  await feed(stream.slice(pads));
  await sleep(700);
  padPeak = await run(`__torture.mark()`);
}
const fedSeconds = clock();

// ---- the ending (not in the self-test: its firmware is dead by now)
const after = {};
if (!SELFTEST) {
  await sleep(1500);
  // everything let go once more, whatever the dump's own ending did
  const off = [];
  for (let c = 0; c < 16; c++) {
    off.push(hexOf([0xb0 | c, 123, 0]), hexOf([0xb0 | c, 120, 0]));
  }
  await run(`__torture.feed(${JSON.stringify(off.join(','))})`);
  await sleep(2500);
  await run(`__torture.mark()`);
  await sleep(1200);
  after.silence = await run(`__torture.mark()`);

  // a small song of this script's: pattern A, sound 1 on the four quarters (core/sfx_song.h, "A song as bytes")
  const fill = (n, v) => new Array(n).fill(v);
  const quarters = [0, 4, 8, 12];
  // prettier-ignore
  const song = [
    6, 16, 0, 120, 0, 0, 1, ...fill(16, 0), ...fill(16, 100), ...fill(16, 64), 0, 0, 0,
    2, 64, 91, 40, 0, 0, 0, 0, 0, ...fill(32, 0), ...fill(48, 0),
    0, 1, 0, 0, 0, 0, 0, 0, 0x11, 0x22, 0x00,
    ...fill(16, 0).map((_, i) => (quarters.includes(i) ? 60 : 0)),
    ...fill(16, 5).map((v, i) => (quarters.includes(i) ? 7 : v)),
    ...fill(16, 0),
    ...fill(7 * 8, 0),
  ];
  const count = Math.ceil(song.length / 240);
  for (let k = 0; k < count; k++) {
    await send([0xf0, 0x7d, 0x56, 0x42, 0x05, k, 0, 6, count, 0, ...song.slice(k * 240, k * 240 + 240), 0xf7]);
    await sleep(50);
  }
  await sleep(500);
  after.menu = (await run(`__torture.health()`)).menu;
  after.stop = await press('STOP');
  await run(`__torture.mark()`);
  after.play = await press('PLAY');
  await sleep(4000);
  after.song = await run(`__torture.mark()`);
  await press('STOP');
  await sleep(500);
  after.request = await run(`__torture.request(0, ${PROBE_WAIT})`);
  after.sent = song;
  after.back = await run(`__torture.requestSong(6000)`);
}
await sleep(SELFTEST ? 7000 : 1000);
const total = clock();
watching = false;
await Promise.all([probing, polling]);
const end = await run(`__torture.health()`);
const pageErrors = await run(`__torture.errors`);
const firmwareLogs = await run(`__torture.logs`);

// ---- the verdict
const hz = (x) => (Number.isFinite(x) ? x.toFixed(0) : '?');
const f2 = (x) => (Number.isFinite(x) ? x.toFixed(2) : '?');
const during = trouble.filter((x) => x.t >= 0);
const before = trouble.filter((x) => x.t < 0);
const show = (list, most = 6) => {
  for (const x of list.slice(0, most)) {
    const url = x.url ? ' · ' + x.url.replace(/^https?:\/\/[^/]+\//, '') : '';
    note(`[${x.t < 0 ? 'before the stream' : '+' + x.t.toFixed(1) + ' s'}] ${x.where}, ${x.kind}: ${x.text}${url}`);
  }
  if (list.length > most) {
    note(`… and ${list.length - most} more`);
  }
};
note(
  `Chrome exposed to the script: ${attached.size ? [...new Set(attached.values())].join(', ') : 'no worker or worklet target'} ` +
    `(the page itself always)`,
);
if (before.length) {
  note(`${before.length} error(s) of the page's own start, before any MIDI (not counted):`);
  show(before, 4);
}

if (SELFTEST) {
  const a = seen(0, sabotage.suspend - 0.2);
  const b = seen(sabotage.suspend + 1.5, sabotage.resume - 0.2, sabotage.resume - PROBE_WAIT / 1000 - 0.1);
  const c = seen(sabotage.resume + 2.5, sabotage.kill - 0.2);
  const d = seen(sabotage.kill + 1, total);
  const crashes = end.crashes.length;
  ok(
    a.probes > 0 && a.missed === 0 && a.fpsMin > 5 && a.audioMin > 0.8,
    `alive, seconds 0 to ${sabotage.suspend}: ${a.probes} identity probes all answered, ${hz(a.fpsMin)} frames a second ` +
      `at least, the audio clock at ${f2(a.audioMin)}`,
  );
  ok(
    b.probes > 0 && b.missed === b.probes && b.audioMax < 0.2 && b.notRunning === b.polls,
    `audio stopped, seconds ${sabotage.suspend} to ${sabotage.resume}: the checks FAIL as they must - ${b.missed} of ` +
      `${b.probes} identity probes unanswered, the audio clock at ${f2(b.audioMax)}, the context not "running" in ` +
      `${b.notRunning} of ${b.polls} looks (frames still come, ${hz(b.fpsMin)} a second: the page draws without audio)`,
  );
  ok(
    c.probes > 0 && c.missed === 0 && c.fpsMin > 5 && c.audioMin > 0.8,
    `audio back, seconds ${sabotage.resume} to ${sabotage.kill}: ${c.probes} identity probes all answered again, ` +
      `the audio clock at ${f2(c.audioMin)}`,
  );
  ok(
    d.probes > 0 && d.missed === d.probes && d.fpsMax === 0 && crashes === 1 && end.nodes === startsBefore,
    `firmware stopped, from second ${sabotage.kill}: the checks FAIL as they must - ${d.missed} of ${d.probes} identity ` +
      `probes unanswered, ${hz(d.fpsMax)} frames a second, ${crashes} "crash" from the worklet` +
      (crashes ? ` ("${end.crashes[0].error.slice(0, 60)}")` : ''),
  );
  note(`the sound's peak after the stop: ${f2(d.peak)} (before it: ${f2(a.peak)})`);
  await finish();
}

const s = seen(0, total);
ok(
  end.fed >= stream.length,
  `the stream went in: ${stream.length} messages, ${streamBytes} bytes in ${fedSeconds.toFixed(0)} s ` +
    `(the feed was ${lagMost.toFixed(2)} s behind at most)`,
);
{
  // what left the page for the worklet while the stream ran (probes, this script's song and request included)
  const g = { sysex: end.given.sysex - healthBefore.given.sysex, short: end.given.short - healthBefore.given.short };
  const w = {};
  for (const k of Object.keys(end.posted)) {
    w[k] = end.posted[k] - healthBefore.posted[k];
  }
  const channel = stream.some((m) => {
    const b = parseInt(m.hex.slice(0, 2), 16);
    return b >= 0x80 && b < 0xf0;
  });
  ok(
    end.unheard === 0 && w.sysex === g.sysex && g.sysex > 0 && (!channel || w.midi > 0),
    `the page handed it on to the worklet: ${w.sysex} of ${g.sysex} SysEx as 'sysex' (${w.sysexBytes} bytes), and of ` +
      `${g.short} other messages ${w.midi} as VIBE_EV_MIDI (${w.notes} of them notes), ${w.pad} as pads, ${w.transport} ` +
      `as transport (the rest is the page's own: clock, pedal, what MIDI does not define)` +
      (end.unheard ? ` - ${end.unheard} messages found NO listener on the input` : ''),
  );
}
for (const p of phases) {
  const x = seen(p.from, p.to ?? total);
  const secs = Math.max(0.001, (p.to ?? total) - p.from);
  note(
    `${p.phase === 6 ? 'the ending' : 'phase ' + p.phase}: ${p.messages} messages, ${p.bytes} bytes in ${secs.toFixed(1)} s ` +
      `(${hz(p.messages / secs)} messages a second) · identity ${x.probes - x.missed}/${x.probes} · frames ${hz(x.fpsMin)}-` +
      `${hz(x.fpsMax)} a second · audio clock ${f2(x.audioMin)}`,
  );
}
ok(
  during.length === 0,
  during.length
    ? `${during.length} exception(s) or error(s) in the page, the worklet or a worker during the stream:`
    : 'no exception, no console error and no crashed target in the page, the worklet or a worker',
);
show(during);
ok(
  pageErrors.length === 0,
  pageErrors.length
    ? `the PAGE's own MIDI handler (js/controllers.js, js/host.js) threw on ${pageErrors.length} message(s):`
    : "the page's MIDI handler threw on no message",
);
for (const e of pageErrors.slice(0, 6)) {
  note(`message ${e.fed}: ${e.text} · ${e.stack} · bytes ${e.bytes}`);
}
ok(
  end.crashes.length === 0 && end.nodes === startsBefore && end.firmware === fw,
  end.crashes.length
    ? `the FIRMWARE stopped (the worklet's "crash"): ${end.crashes[0].error} - at message ${end.crashes[0].fed}` +
        (stream[end.crashes[0].fed - 1] ? `, in "${stream[end.crashes[0].fed - 1].where}"` : '')
    : end.nodes === startsBefore && end.firmware === fw
      ? 'the firmware never stopped and was never started again' +
        (startsBefore > 1 ? ` (the page had started it ${startsBefore} times before the stream, by itself)` : '')
      : `a firmware was started ${end.nodes - startsBefore} time(s) DURING the stream; the one chosen at the end is ` +
        `"${end.firmware}"`,
);
ok(
  s.probes > 0 && s.missedInARow < 2 && s.wrong === 0,
  `the identity: ${s.probes - s.missed} of ${s.probes} probes answered within ${PROBE_WAIT / 1000} s ` +
    `(${s.slowest} ms the slowest; ${s.missed} missed, ${s.missedInARow} in a row at most - two in a row fail), ` +
    `version ${VERSION.join('.')} in ${s.wrong ? 'NOT all' : 'all'} of them; ${end.out.idents} identities came in all`,
);
ok(
  s.fpsMin > 1 && s.gap < 2000,
  `the display: ${end.frames} frames, ${hz(s.fpsMin)} to ${hz(s.fpsMax)} a second, ${hz(s.gap)} ms the longest ` +
    `wait for one, the picture changed ${end.changes} times`,
);
ok(
  s.notRunning === 0 && s.audioMin > 0.5 && end.bad === 0,
  `the audio: the context "running" in ${s.polls - s.notRunning} of ${s.polls} looks, its clock at ${f2(s.audioMin)} to ` +
    `${f2(s.audioMax)} s a second, ${end.bad} samples not finite, the peak ${f2(end.top)}`,
);
if (end.top > 1.0) {
  note(`(a peak above 1.0 at the firmware's output, before the page's volume: ${f2(end.top)})`);
}
if (padPeak !== null) {
  ok(padPeak > 0.005, `the generator's ending: pad 1, played five times, is heard (peak ${padPeak.toFixed(3)})`);
} else {
  note('the dump has no ending with pad 1: not listened for');
}
ok(!after.menu, after.menu ? 'the firmware library is open over the display' : 'the firmware library did not open');
ok(
  after.stop && after.play && after.song > 0.005,
  `STOP, then PLAY: the song plays (peak ${f2(after.song)}; after all notes off it was ${after.silence.toFixed(3)})`,
);
{
  // the song asked back (F0 7D 56 42 06 F7): the firmware packs the song it holds - this script's, if it arrived
  const b = after.back;
  const differs = b.got ? after.sent.findIndex((v, i) => b.bytes[i] !== v) : -1;
  ok(
    b.got && b.bytes.length === after.sent.length && differs < 0,
    b.got
      ? differs < 0 && b.bytes.length === after.sent.length
        ? `the song asked back is this script's song, byte for byte (${b.bytes.length} bytes in ${b.count} pieces)`
        : `the song asked back is NOT this script's song (${b.bytes.length} bytes for ${after.sent.length}, the first ` +
          `difference at byte ${differs})`
      : `the song asked back did NOT come whole within 6 s (${Object.keys(b.bytes).length} bytes)`,
  );
}
{
  // the plain sound the generator's ending puts on every pad (tests/midi-torture/Program.cs, Wire.Patch kind 3)
  // prettier-ignore
  const plain = [2, 1, 80, 64, 40, 40, 40, 0, 110, 10, 70, 0, 0, 0, 20, 64, 40, 0, 0, 0, 0, 0, 0, 0, 20, 100, 0];
  plain.push(plain.reduce((a, b) => a + b, 0) & 127);
  const r = after.request;
  const same = r.got && r.patch.length === 28 && plain.every((v, i) => r.patch[i] === v);
  ok(
    r.got && (padPeak === null || same),
    r.got
      ? `a pad request is answered` +
          (padPeak === null
            ? ''
            : same
              ? ", with the ending's plain sound"
              : ' - but NOT with the sound the ending loaded')
      : `a pad request is NOT answered within ${PROBE_WAIT / 1000} s`,
  );
}
note(
  `the firmware sent: ${end.out.shorts} short messages, ${end.out.sysex} SysEx (${end.out.idents} identities, ` +
    `${end.out.dumps} sounds, ${end.out.pieces} pieces of a song, ${end.out.others} others)`,
);
if (firmwareLogs.length) {
  note(`the firmware logged ${firmwareLogs.length} line(s): ` + firmwareLogs.slice(0, 3).join(' · ').slice(0, 300));
}
await finish();
