// Writes the three scenes of the finer grid - tests/grid.json, tests/regrid.json, tests/take64.json - which the SDK's
// wasmtest plays and check-grid.mjs, check-regrid.mjs and check-take64.mjs judge (each says at its top what its scene
// does). The .json files are kept in the repository; this is how they were made, and how to change them.
//
//   node tests/make-grid-scenes.mjs
//
// An event is what wasmtest hands the firmware: [type, id, down, value, midi] at a time in seconds - 1 a pad, 3 a
// button, 4 an encoder, 5 MIDI in (here a controller on channel 2: 14 the intensity, 15 the pattern).
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const HERE = path.dirname(fileURLToPath(import.meta.url));

// the buttons these scenes press, by the number the firmware knows them by
// prettier-ignore
const BUTTON = {
  F1: 0, F2: 1, F3: 2, F4: 3, F5: 4, F6: 5,
  SHIFT: 6, K5: 14, LEFT: 18, RIGHT: 20, REC: 21, STOP: 22, PLAY: 23,
};

// A scene: a list of events, each at its time. Every method adds events and gives the scene back, so that calls can
// be chained; a method without a time uses the one `at` set last.
class Scene {
  constructor() {
    this.steps = [];
    this.t = 0;
  }

  // one event, as wasmtest takes it
  ev(t, ...e) {
    this.steps.push({ t: +t.toFixed(4), ev: e });
    return this;
  }

  at(t) {
    this.t = t;
    return this;
  }

  // controller n with value v on channel 2
  cc(n, v) {
    return this.ev(this.t, 5, 0, 0, 0, 0xb1 | (n << 8) | (v << 16));
  }

  // a button pressed and let go
  btn(n, t = this.t) {
    this.ev(t, 3, BUTTON[n], 1);
    this.ev(t + 0.04, 3, BUTTON[n], 0);
    return this;
  }

  // SHIFT + a button
  shift(n, t = this.t) {
    this.ev(t, 3, BUTTON.SHIFT, 1);
    this.ev(t + 0.02, 3, BUTTON[n], 1);
    this.ev(t + 0.05, 3, BUTTON[n], 0);
    this.ev(t + 0.07, 3, BUTTON.SHIFT, 0);
    return this;
  }

  // pad n (1 to 16) hit
  pad(n, t = this.t, vel = 0.9) {
    this.ev(t, 1, n - 1, 1, vel);
    this.ev(t + 0.04, 1, n - 1, 0, 0);
    return this;
  }

  // SHIFT + pad n: on SEQ that selects the pad's sound
  shiftPad(n, t = this.t) {
    this.ev(t, 3, BUTTON.SHIFT, 1);
    this.ev(t + 0.02, 1, n - 1, 1, 0.9);
    this.ev(t + 0.05, 1, n - 1, 0, 0);
    this.ev(t + 0.07, 3, BUTTON.SHIFT, 0);
    return this;
  }

  // SHIFT held and encoder A turned down ten clicks: swing 0
  swing0(t = this.t) {
    this.ev(t, 3, BUTTON.SHIFT, 1);
    for (let k = 0; k < 10; k++) {
      this.ev(t + 0.02 + k * 0.01, 4, 4, 0, -1);
    }
    this.ev(t + 0.16, 3, BUTTON.SHIFT, 0);
    return this;
  }

  // SEQ -> SONG, SEND, AUTO, SEQ again: 1.3 s
  send(t = this.t) {
    this.btn('K5', t);
    this.btn('F5', t + 0.12);
    this.btn('K5', t + 1.0);
    this.btn('K5', t + 1.12);
    return this;
  }

  // a screenshot of the display
  shot(name, t = this.t) {
    this.steps.push({ t: +t.toFixed(4), shot: name });
    return this;
  }

  // the scene as a script for wasmtest: the events in the order of their times, one a line
  write(file, seconds) {
    const steps = this.steps.slice().sort((a, b) => a.t - b.t);
    const json = JSON.stringify({ seconds, float: true, steps });
    const oneEventALine = json.replace(/\{"t"/g, '\n{"t"').replace(']}', '\n]}');
    fs.writeFileSync(file, oneEventALine + '\n');
  }
}

// ---- grid.json
{
  const s = new Scene();
  s.at(0.05).cc(15, 0x70); // pattern H
  s.btn('K5', 0.2); // SEQ
  s.shot('seq-16', 0.3);
  s.shiftPad(13, 0.35); // the hi-hat
  s.swing0(0.6);
  s.shift('F6', 1.0); // 1/32
  s.pad(1, 1.2).pad(4, 1.4); // steps 1 and 4 (index 0, 3)
  s.shift('RIGHT', 1.6); // steps 17-32
  s.pad(2, 1.8).pad(7, 2.0); // steps 18 and 23 (index 17, 22)
  s.shot('seq-32-window2', 2.2);
  s.ev(2.3, 3, BUTTON.SHIFT, 1).shot('seq-shift', 2.4).ev(2.45, 3, BUTTON.SHIFT, 0);
  s.send(2.6);
  s.btn('PLAY', 4.0).btn('STOP', 8.1);
  s.shift('F6', 8.4); // 1/64
  for (let k = 0; k < 5; k++) {
    s.btn('F3', 8.7 + k * 1.6);
    s.send(8.9 + k * 1.6);
  }
  s.at(16.8).cc(15, 0x60); // pattern G, 16 steps
  s.pad(5, 17.0);
  s.shift('LEFT', 17.2);
  s.shot('seq-16-all-shown', 17.35);
  s.pad(6, 17.5);
  s.send(17.7);
  s.write(path.join(HERE, 'grid.json'), 19.5);
}

// ---- regrid.json
{
  const s = new Scene();
  s.at(0.05).cc(14, 0); // intensity 0: the kick alone
  s.btn('K5', 0.2);
  s.btn('PLAY', 1.0).btn('STOP', 5.02); // pattern A, two bars at 16
  s.shift('F6', 5.5).shift('F6', 5.7); // 1/64
  s.btn('PLAY', 6.0).btn('STOP', 10.02); // two bars at 64
  s.send(10.3); // song 1: A at 64
  s.shift('F5', 11.8).shift('F5', 12.0); // nothing between the sixteenths: no question, 1/16 again
  s.send(12.3); // song 2: the factory song
  s.at(13.8).cc(14, 127);
  s.at(13.9).cc(15, 0x70); // H: 1/32, the hat on step 2 (index 1)
  s.shiftPad(13, 14.0);
  s.shift('F6', 14.2);
  s.pad(2, 14.4);
  s.at(14.6).cc(15, 0x60); // G: 1/32, the hat on steps 1 and 2
  s.shift('F6', 14.8);
  s.pad(1, 15.0).pad(2, 15.2);
  s.shift('F5', 15.4); // G: the question
  s.shot('regrid-ask', 15.55);
  s.btn('RIGHT', 15.7); // H
  s.shift('F5', 15.9); // H: its own question - the one for G does not count here
  s.btn('LEFT', 16.1); // G
  s.shift('F5', 16.3).shift('F5', 16.5); // asked, and again: lowered
  s.send(16.8); // song 3: G at 16 with step 1, H at 32 with step 2
  s.btn('F2', 18.3); // back
  s.send(18.5); // song 4: G at 32 with steps 1 and 2
  s.btn('F3', 20.0).btn('F3', 20.2); // G: the hat on every eighth
  s.btn('PLAY', 21.0);
  s.shift('F6', 23.6); // raised while it plays, between two eighths of bar 2
  s.btn('STOP', 27.02);
  s.send(27.3); // song 5: G at 64
  s.write(path.join(HERE, 'regrid.json'), 29.2);
}

// ---- take64.json
{
  const s = new Scene();
  s.at(0.05).cc(15, 0x70); // pattern H
  s.btn('K5', 0.2);
  s.shiftPad(3, 0.35); // the kick
  s.swing0(0.5);
  s.shift('F6', 0.72).shift('F6', 0.82); // 1/64
  s.btn('REC', 0.92);
  s.btn('PLAY', 1.0); // bar lines at 1, 3, 5 s
  s.pad(3, 3.1).pad(3, 3.29).pad(3, 4.01); // 100, 290 and 1010 ms after the bar line
  s.shot('seq-64-take', 4.3);
  s.btn('STOP', 6.2); // 1.2 s into bar 3: the window is steps 33-48
  s.btn('REC', 6.3);
  s.shot('seq-64-after', 6.4);
  s.send(6.5); // song 1: what was played in
  s.pad(1, 8.0); // the tile of step 33: off
  s.send(8.2); // song 2
  s.btn('REC', 9.7);
  s.btn('PLAY', 10.0); // the bar line at 10 s
  // ERASE held from 200 to 400 ms: steps 7 to 13 pass under it
  s.ev(10.2, 3, BUTTON.F4, 1).ev(10.4, 3, BUTTON.F4, 0);
  s.btn('STOP', 11.0);
  s.btn('REC', 11.1);
  s.send(11.3); // song 3
  s.write(path.join(HERE, 'take64.json'), 13.0);
}
