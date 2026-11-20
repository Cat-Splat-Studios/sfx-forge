// The device end of the link, without hands: opens the SDK's emulator in a Chrome of its own (a throwaway profile),
// allows that page Web MIDI, switches SFX FORGE on and puts its MIDI out on a port. Unity is then the other end
// (unity/Demo/Assets/Showcase/Tests/LinkTakes.cs). Nothing of the emulator is changed: this only clicks what a hand would.
//
//   python .tools/vibe-fm-sdk/tools/emulator.py 8766        (the emulator's own server, in another terminal)
//   node tests/emulator-link.mjs [--out "SFX to Unity"] [--seconds 600] [--frames out/link/emulator] [--keys "K5,K5,F5"]
//
// Needs: Node 22, Chrome, a virtual MIDI cable with two ports (docs/UNITY.md, "From the device to the game").
// --spy logs what MIDI reaches the page and what the page hands to the firmware. (Measured with it: in this setup the page
//   hands every incoming message to the firmware twice, whether this script clicks MIDI or the page connects by itself. The
//   firmware has to live with that, and since 1.3.1 it does.) --twice clicks MIDI once more on top.
// --frames records what the page shows as JPEGs with their times (frames.json), for a video.
// --keys presses the device's buttons after start (names as in docs/video/make_videos.py: F1-F6, K1-K6, SHIFT, PLAY, STOP, REC …),
//   "wait:2" waits two seconds. While it runs, a line on stdin does the same: "K5", "F5", "wait:1", "quit".
// --cues names a file another program appends such lines to (the Unity take LinkSession does); it is emptied at start,
//   and "quit-after:4" ends this script four seconds later.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';

const arg = (name, fallback) => { const i = process.argv.indexOf('--' + name); return i > 0 ? process.argv[i + 1] : fallback; };
const URL_ = arg('url', 'http://127.0.0.1:8766/');
const OUT = arg('out', 'SFX to Unity');
const SECONDS = Number(arg('seconds', '600'));
const FRAMES = arg('frames', '');
const KEYS = arg('keys', '');
const CUES = arg('cues', '');
const PORT = Number(arg('port', '9333'));
const FIRMWARE = arg('firmware', 'sfx-forge');
const CHROME = arg('chrome', process.env.CHROME || 'C:/Program Files/Google/Chrome/Application/chrome.exe');
const PROFILE = path.resolve(arg('profile', 'out/link/chrome-profile'));
// the buttons of the emulator's panel in the order the page creates them (their labels are the firmware's and change with its page)
const PANEL = ['RETURN', 'HOME', 'SHIFT', 'OCT-', 'OCT+', 'ENTER', 'K1', 'K2', 'K3', 'K4', 'K5', 'K6', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'LEFT', 'MENU', 'RIGHT', 'REC', 'STOP', 'PLAY'];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const say = (...a) => console.log('EMULATOR:', ...a);

fs.mkdirSync(PROFILE, { recursive: true });
const chrome = spawn(CHROME, ['--remote-debugging-port=' + PORT, '--user-data-dir=' + PROFILE, '--no-first-run', '--no-default-browser-check',
  '--autoplay-policy=no-user-gesture-required', '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding', '--disable-background-timer-throttling',
  '--window-size=1500,1000', '--window-position=40,40', 'about:blank'], { stdio: 'ignore' });
let browser = null, quitting = false;
// every way out goes through here: the browser this script started is closed (its first process has handed over to a child,
// so killing that would not do), then the script ends
async function quit(code = 0) {
  if (quitting) return; quitting = true;
  try { if (browser) await Promise.race([browser.send('Browser.close'), sleep(1500)]); } catch (e) { /* gone */ }
  try { chrome.kill(); } catch (e) { /* gone */ }
  process.exit(code);
}
process.on('SIGINT', () => quit(0)); process.on('SIGTERM', () => quit(0));
process.on('uncaughtException', (e) => { console.error('EMULATOR: failed:', e.message); quit(1); });
process.on('unhandledRejection', (e) => { console.error('EMULATOR: failed:', e && e.message ? e.message : e); quit(1); });
chrome.on('exit', () => { if (browser && !quitting) { say('the browser closed'); process.exit(0); } });   // (before that: Chrome hands over to its own child and the first process ends)

async function json(u) { for (let i = 0; i < 50; i++) { try { return await (await fetch(u)).json(); } catch (e) { await sleep(200); } } throw new Error('no answer from ' + u); }
function connect(ws) {
  const sock = new WebSocket(ws); let id = 0; const waiting = new Map(), listeners = [];
  sock.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && waiting.has(m.id)) { const [ok, no] = waiting.get(m.id); waiting.delete(m.id); m.error ? no(new Error(m.error.message)) : ok(m.result); } else for (const l of listeners) l(m); };
  return new Promise((ready) => { sock.onopen = () => ready({
    send: (method, params = {}) => new Promise((ok, no) => { waiting.set(++id, [ok, no]); sock.send(JSON.stringify({ id, method, params })); }),
    on: (l) => listeners.push(l) }); });
}

const version = await json(`http://127.0.0.1:${PORT}/json/version`);
browser = await connect(version.webSocketDebuggerUrl);
const origin = new URL(URL_).origin;
await browser.send('Browser.grantPermissions', { origin, permissions: ['midi', 'midiSysex'] });
const target = (await json(`http://127.0.0.1:${PORT}/json/list`)).find((t) => t.type === 'page');
const page = await connect(target.webSocketDebuggerUrl);
await page.send('Page.enable'); await page.send('Runtime.enable');
const run = async (expression) => { const r = await page.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true, userGesture: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text); return r.result.value; };
await page.send('Page.navigate', { url: URL_ });
await sleep(2500);

// the firmware, the power switch, MIDI, the port: the same four things a hand does in the top bar
const firmwares = await run(`[...document.querySelectorAll('#fw option')].map(o => o.value)`);
const fw = firmwares.find((v) => v.includes(FIRMWARE));
if (!fw) { say('no firmware like', FIRMWARE, 'in', JSON.stringify(firmwares)); await quit(1); }
await run(`(() => { const s = document.querySelector('#fw'); s.value = ${JSON.stringify(fw)}; s.dispatchEvent(new Event('change', { bubbles: true })); })()`);
await sleep(1500);
await run(`(() => { const b = document.querySelector('#power-on'); if (b && b.offsetParent) b.click(); })()`);
await sleep(2500);
// MIDI: the button in the top bar, unless the page shows it as on already (it connects by itself when MIDI was allowed
// before, and grantPermissions above allowed it). --twice clicks regardless.
if (process.argv.includes('--twice') || !(await run(`document.querySelector('#tb-midi').classList.contains('on')`))) await run(`document.querySelector('#tb-midi').click()`);
await sleep(2000);
const ports = await run(`[...document.querySelectorAll('#midiout option')].map(o => o.value)`);
if (!ports.includes(OUT)) { say('the emulator sees no MIDI output named', JSON.stringify(OUT), '- it sees', JSON.stringify(ports)); await quit(1); }
await run(`(() => { const s = document.querySelector('#midiout'); s.value = ${JSON.stringify(OUT)}; s.dispatchEvent(new Event('change', { bubbles: true })); })()`);
const inputs = await run(`navigator.requestMIDIAccess({ sysex: true }).then(a => [...a.inputs.values()].map(p => p.name))`);
// (The emulator listens on every MIDI input, and a virtual cable's port is an input as well as an output: the page hears
// what the firmware sends and hands it back to it. The firmware knows its own echo; nothing is done about it here.)
say('ready: firmware', fw, '· MIDI out on', JSON.stringify(OUT), '· listening on', JSON.stringify(inputs));

// a button of the device: the page's own keyboard and mouse would do the same; window.vibe_ev is what its panel calls
async function press(name) {
  if (name.startsWith('wait:')) { await sleep(1000 * Number(name.slice(5))); return; }
  if (PANEL.indexOf(name.toUpperCase()) < 0) { say('no button', name); return; }
  const found = await run(`(() => { const el = document.querySelectorAll('button.btn')[${PANEL.indexOf(name.toUpperCase())}]; if (!el) return false;
    const r = el.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
  if (!found) { say('the page has no button', name); return; }
  await page.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: found.x, y: found.y, button: 'left', clickCount: 1 });
  await sleep(90);
  await page.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: found.x, y: found.y, button: 'left', clickCount: 1 });
  say('pressed', name);
  await sleep(350);
}

if (FRAMES) {
  fs.mkdirSync(FRAMES, { recursive: true });
  const times = []; let n = 0;
  page.on((m) => { if (m.method !== 'Page.screencastFrame') return;
    const file = 'f' + String(n++).padStart(5, '0') + '.jpg';
    fs.writeFileSync(path.join(FRAMES, file), Buffer.from(m.params.data, 'base64'));
    times.push({ file, t: m.params.metadata.timestamp });
    page.send('Page.screencastFrameAck', { sessionId: m.params.sessionId }).catch(() => {}); });
  await page.send('Page.startScreencast', { format: 'jpeg', quality: 85, everyNthFrame: 2 });
  const startedAt = Date.now() / 1000;
  process.on('exit', () => fs.writeFileSync(path.join(FRAMES, 'frames.json'), JSON.stringify({ wallClockAtStart: startedAt, frames: times })));
  say('recording the page into', FRAMES);
}

if (CUES) {                                          // lines another program appends: pressed in order, as they come
  fs.mkdirSync(path.dirname(CUES), { recursive: true }); fs.writeFileSync(CUES, '');
  let read = 0, busy = false;
  setInterval(async () => { if (busy) return; busy = true;
    try { const lines = fs.readFileSync(CUES, 'utf8').split(String.fromCharCode(10)).map((l) => l.trim()).filter(Boolean);
      while (read < lines.length) { const k = lines[read++].trim();
        if (k.startsWith('quit-after:')) { setTimeout(() => quit(), 1000 * Number(k.slice(11))); } else await press(k); } }
    catch (e) { say('cue file:', e.message); }
    busy = false; }, 50);
  say('reading cues from', CUES);
}
if (process.argv.includes('--spy')) {                 // what MIDI reaches the page, port by port: a line every few seconds
  await run(`navigator.requestMIDIAccess({ sysex: true }).then((a) => { window.__spy = {}; a.inputs.forEach((p) => p.addEventListener('midimessage', (m) => {
    const d = m.data, key = p.name + ' ' + (d[0] === 0xF0 ? 'sysex type ' + d[4] + ' len ' + d.length : 'status ' + d[0].toString(16)); window.__spy[key] = (window.__spy[key] || 0) + 1; })); })`);
  await run(`(() => { const post = MessagePort.prototype.postMessage; MessagePort.prototype.postMessage = function (m, ...rest) {
    if (m && m.t === 'sysex') { const key = 'to the firmware: sysex type ' + m.bytes[4] + ' len ' + m.bytes.length; window.__spy[key] = (window.__spy[key] || 0) + 1; } return post.call(this, m, ...rest); }; })()`);
  setInterval(async () => { try { const v = await run(`(() => { const s = JSON.stringify(window.__spy); window.__spy = {}; return s; })()`); if (v !== '{}') say('heard', v); } catch (e) { /* busy */ } }, 3000);
}
for (const k of KEYS.split(',').map((s) => s.trim()).filter(Boolean)) await press(k);

readline.createInterface({ input: process.stdin }).on('line', async (line) => { const k = line.trim(); if (k === 'quit') quit(); else if (k) await press(k); });
setTimeout(quit, SECONDS * 1000);
