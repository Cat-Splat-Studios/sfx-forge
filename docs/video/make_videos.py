"""Makes SFX FORGE's two videos from the firmware itself – no screen recording.

    python docs/video/make_videos.py [tour] [song]        (after sh build.sh; needs Node, ffmpeg and Pillow)

A video is a script of key presses with captions (below). The script is played to the firmware by the SDK's wasmtest,
which renders the display 30 times a second and the sound; this file lays each frame out – the device with the
firmware's display on its screen and the control in use lit, the display again larger, the caption – and ffmpeg joins
pictures and sound. Results: out/video/*.mp4.

device.png is the device as the VIBE FM SDK's emulator draws it (github.com/Vibe-FM-SDK/vibe-fm-sdk, Apache-2.0),
captured from the emulator at its own size, 1600 x 860. Without that file a plain drawing of the controls is used.
"""
import json
import os
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SDK = os.environ.get("VIBE_SDK", os.path.join(ROOT, ".tools", "vibe-fm-sdk"))
WASM = os.path.join(ROOT, "build", "sfx-forge.wasm")
FPS = 30
W, H = 1920, 1080
BG, PANEL, LINE, TEXT, DIM, EMBER = "#0E0B09", "#1B1613", "#3A2E26", "#F4EBDD", "#A08E7E", "#FF8A3C"
BUTTON = {"F1": 0, "F2": 1, "F3": 2, "F4": 3, "F5": 4, "F6": 5, "SHIFT": 6, "K1": 10, "K2": 11, "K3": 12, "K4": 13, "K5": 14, "K6": 15,
          "RETURN": 16, "HOME": 17, "LEFT": 18, "MENU": 19, "RIGHT": 20, "REC": 21, "STOP": 22, "PLAY": 23}
ENCODER = {"1": 0, "2": 1, "3": 2, "4": 3, "A": 4, "B": 5, "DATA": 6}
NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def font(size, bold=True):
    for name in (["segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"] if bold else ["segoeui.ttf", "arial.ttf", "DejaVuSans.ttf"]):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


class Take:
    """A script: what is pressed when, and what the video says about it."""

    def __init__(self, name, title):
        self.name, self.title, self.t = name, title, 0.0
        self.events, self.hits, self.captions, self.start = [], [], [], 0.0

    def wait(self, seconds):
        self.t += seconds

    def begin(self):
        """What came before is setting up and is not shown."""
        self.start = self.t

    def say(self, chapter, text):
        if self.captions:
            self.captions[-1][1] = self.t
        self.captions.append([self.t, 1e9, chapter, text])

    def hit(self, controls, label, seconds=0.7):
        self.hits.append((self.t, self.t + seconds, set(controls), label))

    def btn(self, name, label=None, gap=0.35, show=True):
        self.events.append((self.t, [3, BUTTON[name], 1]))
        self.events.append((self.t + 0.08, [3, BUTTON[name], 0]))
        if show:
            self.hit([name], label or name)
        self.t += gap

    def pad(self, n, label=None, hold=0.22, gap=0.45, vel=0.9, show=True):
        """n: 1..16, as on the device."""
        self.events.append((self.t, [1, n - 1, 1, vel]))
        self.events.append((self.t + hold, [1, n - 1, 0, 0]))
        if show:
            self.hit(["P%d" % n], label or "PAD %d" % n)
        self.t += gap

    def shift_pad(self, n, label, gap=0.7):
        self.events.append((self.t, [3, BUTTON["SHIFT"], 1]))
        self.events.append((self.t + 0.06, [1, n - 1, 1, 0.9]))
        self.events.append((self.t + 0.2, [1, n - 1, 0, 0]))
        self.events.append((self.t + 0.26, [3, BUTTON["SHIFT"], 0]))
        self.hit(["SHIFT", "P%d" % n], label, 0.9)
        self.t += gap

    def shift_btn(self, name, label, gap=0.7):
        self.events.append((self.t, [3, BUTTON["SHIFT"], 1]))
        self.events.append((self.t + 0.06, [3, BUTTON[name], 1]))
        self.events.append((self.t + 0.14, [3, BUTTON[name], 0]))
        self.events.append((self.t + 0.2, [3, BUTTON["SHIFT"], 0]))
        self.hit(["SHIFT", name], label, 0.9)
        self.t += gap

    def turn(self, enc, detents, label, seconds=1.0, after=0.3):
        n = max(1, int(seconds * FPS))
        for k in range(n):                           # whole detents only, spread over the time: they add up exactly
            part = round(detents * (k + 1) / n) - round(detents * k / n)
            if part:
                self.events.append((self.t + k * seconds / n, [4, ENCODER[enc], 0, part]))
        self.hit(["E" + enc], label, seconds + 0.3)
        self.t += seconds + after

    def key(self, note, hold=0.3, gap=0.4, vel=0.9, show=True):
        self.events.append((self.t, [9, note, 1, vel]))
        self.events.append((self.t + hold, [9, note, 0, 0]))
        if show:
            self.hit(["N%d" % note], "KEY  " + NOTE[note % 12] + str(note // 12 - 1), hold + 0.2)
        self.t += gap

    def pad_key(self, n, note, gap=1.0):
        """Hold a step's pad and play a key: the step gets that note."""
        self.events.append((self.t, [1, n - 1, 1, 0.9]))
        self.events.append((self.t + 0.1, [9, note, 1, 0.9]))
        self.events.append((self.t + 0.3, [9, note, 0, 0]))
        self.events.append((self.t + 0.4, [1, n - 1, 0, 0]))
        self.hit(["P%d" % n, "N%d" % note], "PAD %d  +  KEY %s%d" % (n, NOTE[note % 12], note // 12 - 1), 0.8)
        self.t += gap

    def midi(self, status, data1, data2):
        self.events.append((self.t, [5, 0, 0, 0, status | data1 << 8 | data2 << 16]))


# ---- the drawing of the device: which control is in use ---------------------------------------------------------------
def device(d, ox, oy, lit):
    small, tiny = font(17), font(14, False)

    def key(x, y, w, h, name, text=None):
        on = name in lit
        d.rounded_rectangle([ox + x, oy + y, ox + x + w, oy + y + h], 6, fill=EMBER if on else "#2A221D", outline=EMBER if on else LINE)
        t = text or name
        d.text((ox + x + (w - d.textlength(t, font=tiny)) / 2, oy + y + (h - 17) / 2), t, font=tiny, fill=BG if on else TEXT)

    def knob(x, y, r, name, text):
        on = name in lit
        d.ellipse([ox + x - r, oy + y - r, ox + x + r, oy + y + r], fill=EMBER if on else "#15110E", outline=EMBER if on else "#5A4A3E", width=3)
        d.line([ox + x, oy + y - r + 5, ox + x, oy + y - r / 3], fill=BG if on else TEXT, width=3)
        d.text((ox + x - d.textlength(text, font=tiny) / 2, oy + y + r + 4), text, font=tiny, fill=EMBER if on else DIM)

    for i in range(4):
        knob(36 + i * 72, 40, 26, "E%d" % (i + 1), str(i + 1))
    knob(340, 48, 36, "EDATA", "DATA")
    for i in range(16):
        on = "P%d" % (i + 1) in lit
        x, y = ox + 404 + (i % 4) * 44, oy + (i // 4) * 44
        d.rounded_rectangle([x, y, x + 40, y + 40], 6, fill=EMBER if on else "#2B2520", outline=EMBER if on else LINE)
        t = str(i + 1)
        d.text((x + (40 - d.textlength(t, font=tiny)) / 2, y + 11), t, font=tiny, fill=BG if on else DIM)
    for i in range(6):
        key(i * 64, 200, 58, 30, "F%d" % (i + 1))
    key(0, 250, 76, 30, "SHIFT")
    for i in range(6):
        key(92 + i * 82, 250, 76, 30, "K%d" % (i + 1))
    knob(30, 336, 22, "EA", "A")
    knob(96, 336, 22, "EB", "B")
    for i, n in enumerate(["LEFT", "MENU", "RIGHT"]):
        key(160 + i * 70, 310, 64, 28, n)
    for i, n in enumerate(["REC", "STOP", "PLAY"]):
        key(160 + i * 70, 346, 64, 28, n)
    key(400, 310, 64, 28, "HOME")
    played = {int(c[1:]) % 24 for c in lit if c[0] == "N"}
    whites = [0, 2, 4, 5, 7, 9, 11]
    for i in range(15):
        note = (i // 7) * 12 + whites[i % 7]
        d.rounded_rectangle([ox + i * 38, oy + 400, ox + i * 38 + 35, oy + 490], 4, fill=EMBER if note % 24 in played and note < 24 else "#E9E2D6")
    for i in range(14):
        if i % 7 not in (2, 6):
            note = (i // 7) * 12 + whites[i % 7] + 1
            d.rounded_rectangle([ox + i * 38 + 26, oy + 400, ox + i * 38 + 47, oy + 455], 3, fill=EMBER if note in played else "#15110E")
    d.text((ox, oy - 34), "THE DEVICE", font=small, fill=DIM)


# ---- the device as the emulator draws it: where its controls are, at its own size (1600 x 860) ----------------------------
HW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "device.png")
HW_SCALE = 0.75
SCREEN = (112, 54, 352, 235)
WHITE = [0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24]
BLACK = {1: 548, 3: 619, 6: 759, 8: 829, 10: 900, 13: 1040, 15: 1110, 18: 1251, 20: 1321, 22: 1391}


def control_box(name):
    """The place of a control on the device: (x, y, w, h, kind)."""
    fixed = {"EDATA": (1002, 52, 136, 136, "knob"), "EA": (120, 442, 96, 96, "knob"), "EB": (290, 442, 96, 96, "knob"),
             "SHIFT": (40, 360, 84, 44, "key"), "RETURN": (40, 178, 50, 40, "key"), "HOME": (40, 230, 50, 40, "key"),
             "LEFT": (59, 598, 120, 52, "key"), "MENU": (193, 598, 120, 52, "key"), "RIGHT": (327, 598, 120, 52, "key"),
             "REC": (59, 668, 120, 70, "key"), "STOP": (193, 668, 120, 70, "key"), "PLAY": (327, 668, 120, 70, "key")}
    if name in fixed:
        return fixed[name]
    if name[0] == "P":
        i = int(name[1:]) - 1
        return (1200 + (i % 4) * 92, 40 + (i // 4) * 92, 82, 82, "pad")
    if name[0] == "F":
        return (120 + (int(name[1:]) - 1) * 57, 314, 50, 28, "key")
    if name[0] == "K":
        return (480 + (int(name[1:]) - 1) * 120, 360, 84, 44, "key")
    if name in ("E1", "E2", "E3", "E4"):
        return (500 + (int(name[1:]) - 1) * 116, 52, 96, 96, "knob")
    if name[0] == "N":                               # a key: the note, folded onto the two octaves the device has
        k = (int(name[1:]) - 24) % 24
        if k in BLACK:
            return (BLACK[k], 452, 40, 255, "black")
        return (500 + WHITE.index(k) * 70.2, 712, 67, 142, "white")
    return None


def hardware_base():
    """The device with its screen dark and its pads unlit, at the size it has in the video."""
    im = Image.open(HW).convert("RGB")
    im.paste(im.crop((1282, 18, 1384, 132)), (1190, 18))    # (pad 1 was lit when the picture was taken: its glow goes)
    im.paste(im.crop((1160, 132, 1200, 224)), (1160, 40))
    im.paste(im.crop((1230, 46, 1252, 66)), (1206, 46))      # (… and so it is pad 2's face now: its number becomes a 1 again)
    ImageDraw.Draw(im).text((1210, 47), "1", font=font(14), fill="#8E8A86")
    for i in range(6):                                # (and the soft keys carried one page's captions: the display says what they are)
        x = 120 + i * 57
        ImageDraw.Draw(im).rectangle([x + 3, 319, x + 47, 337], fill=im.getpixel((x + 25, 317)))
    for i in range(16):                              # (the picture was taken with one kit's colours on the pads)
        x, y = 1200 + (i % 4) * 92, 40 + (i // 4) * 92
        pad = im.crop((x, y, x + 82, y + 82)).convert("L").point(lambda v: int(v * 0.8)).convert("RGB")
        im.paste(pad, (x, y))
    ImageDraw.Draw(im).rectangle([SCREEN[0], SCREEN[1], SCREEN[0] + SCREEN[2], SCREEN[1] + SCREEN[3]], fill="#0E0B09")
    return im.resize((int(1600 * HW_SCALE), int(860 * HW_SCALE)), Image.LANCZOS)


def hardware(im, ox, oy, lit, shot):
    """Draws the lit controls and the display onto the device at (ox, oy)."""
    k = HW_SCALE
    d = ImageDraw.Draw(im, "RGBA")
    im.paste(shot.resize((int(SCREEN[2] * k), int(SCREEN[3] * k)), Image.LANCZOS), (ox + int(SCREEN[0] * k), oy + int(SCREEN[1] * k)))
    for name in lit:
        box = control_box(name)
        if not box:
            continue
        x, y, w, h, kind = box
        x0, y0, x1, y1 = ox + x * k, oy + y * k, ox + (x + w) * k, oy + (y + h) * k
        if kind == "pad":
            d.rounded_rectangle([x0 - 5, y0 - 5, x1 + 5, y1 + 5], 12, fill=(255, 138, 60, 70))
            d.rounded_rectangle([x0, y0, x1, y1], 8, fill=(255, 150, 70, 255))
            d.rounded_rectangle([x0 + 8, y0 + 8, x1 - 8, y1 - 8], 6, fill=(255, 196, 140, 255))
        elif kind == "knob":
            d.ellipse([x0 - 4, y0 - 4, x1 + 4, y1 + 4], outline=(255, 138, 60, 255), width=5)
            d.ellipse([x0 - 10, y0 - 10, x1 + 10, y1 + 10], outline=(255, 138, 60, 90), width=5)
        elif kind == "key":
            d.rounded_rectangle([x0 - 4, y0 - 4, x1 + 4, y1 + 4], 8, fill=(255, 138, 60, 60))
            d.rounded_rectangle([x0, y0, x1, y1], 6, fill=(255, 138, 60, 150), outline=(255, 170, 110, 255), width=3)
        else:
            d.rounded_rectangle([x0, y0, x1, y1], 4, fill=(255, 138, 60, 235 if kind == "black" else 215))


def wrap(d, text, f, width):
    lines, line = [], ""
    for word in text.split():
        if d.textlength((line + " " + word).strip(), font=f) > width and line:
            lines.append(line)
            line = word
        else:
            line = (line + " " + word).strip()
    return lines + [line]


def render(take, tail=1.5):
    out = os.path.join(ROOT, "out", "video", take.name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"):
            os.remove(os.path.join(out, f))
    end = take.t + tail
    take.captions[-1][1] = end
    first = int(take.start * FPS)
    frames = int(end * FPS) - first
    steps = [{"t": round(t, 4), "ev": ev} for t, ev in take.events]
    steps += [{"t": round((first + k) / FPS, 4), "shot": "f%05d" % k} for k in range(frames)]
    steps.sort(key=lambda s: s["t"])
    script = os.path.join(out, "script.json")
    json.dump({"seconds": round(end + 0.2, 2), "steps": steps}, open(script, "w"))
    print(take.name, "- playing %d s to the firmware, %d pictures" % (end, frames))
    subprocess.run(["node", os.path.join(SDK, "tools", "wasmtest.mjs"), WASM, script, out], check=True, stdout=subprocess.DEVNULL)

    real = os.path.exists(HW)
    base = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(base)
    d.text((W - 60 - d.textlength("SFX FORGE  ·  " + take.title, font=font(24, False)), 62), "SFX FORGE  ·  " + take.title, font=font(24, False), fill=DIM)
    if real:                                          # the device, and its display again, larger
        base.paste(hardware_base(), (40, 140))
        d.rounded_rectangle([1262, 140, 1880, 566], 12, fill=PANEL, outline=LINE, width=2)
        d.text((1274, 574), "THE DISPLAY", font=font(18), fill=DIM)
    else:
        d.rounded_rectangle([52, 142, 1268, 958], 14, fill=PANEL, outline=LINE, width=2)
    big, cap, now, hint = font(54), font(40, False), font(40), font(22, False)
    mp4 = os.path.join(ROOT, "out", "video", take.name + ".mp4")
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-",
                           "-ss", "%.3f" % (first / FPS), "-i", os.path.join(out, "audio.wav"),
                           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", "-preset", "veryfast", "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", mp4],
                          stdin=subprocess.PIPE)
    last = ""
    for k in range(frames):
        t = (first + k) / FPS
        im = base.copy()
        d = ImageDraw.Draw(im)
        shot = Image.open(os.path.join(out, "f%05d.png" % k)).convert("RGB")
        chapter, text = "", ""
        for t0, t1, c, x in take.captions:
            if t0 <= t < t1:
                chapter, text = c, x
        lit, label = set(), ""
        for t0, t1, controls, what in take.hits:
            if t0 <= t < t1:
                lit |= controls
                label = what
        if label:
            last = label
        d.text((60, 44), chapter, font=big, fill=EMBER)
        if real:
            hardware(im, 40, 140, lit, shot)
            im.paste(shot.resize((600, 400), Image.LANCZOS), (1271, 153))
            d = ImageDraw.Draw(im)
            d.text((1274, 640), "NOW", font=hint, fill=DIM)
            for n, line in enumerate(wrap(d, label or last, now, 600)[:2]):
                d.text((1274, 672 + n * 50), line, font=now, fill=TEXT if label else "#5A4A3E")
            for n, line in enumerate(wrap(d, text, cap, 1800)[:2]):
                d.text((60, 842 + n * 54), line, font=cap, fill=TEXT)
        else:
            im.paste(shot.resize((1200, 800), Image.NEAREST), (60, 150))
            device(d, 1310, 190, lit)
            d.text((1310, 720), "NOW", font=hint, fill=DIM)
            d.text((1310, 752), label or last, font=now, fill=TEXT if label else "#5A4A3E")
            for n, line in enumerate(wrap(d, text, cap, 1800)[:2]):
                d.text((60, 964 + n * 48), line, font=cap, fill=TEXT)
        d.rectangle([0, H - 6, int(W * k / max(1, frames - 1)), H], fill=EMBER)
        ff.stdin.write(im.tobytes())
    ff.stdin.close()
    ff.wait()
    for f in os.listdir(out):
        if f.endswith(".png"):
            os.remove(os.path.join(out, f))
    print("wrote", mp4)


# ---- video 1: what it can do ------------------------------------------------------------------------------------------
def tour():
    v = Take("sfx-forge-tour", "the feature tour")
    v.say("SFX FORGE", "Sound effects, instruments and adaptive music for games - all synthesized, on VIBE FM.")
    v.wait(3.5)

    v.say("SOUND EFFECTS", "The SFX kit: sixteen synthesized sound effects, one on each pad.")
    for n in (1, 5, 9, 10, 13, 15, 8, 4):
        v.pad(n, gap=0.75)
    v.wait(0.6)

    v.say("MUTATE", "F2 MUTATE: one key, and every value moves a little. A new variation each time. F3 takes one back.")
    v.pad(9, "PAD 9  LASER", gap=0.9)
    for _ in range(4):
        v.btn("F2", "F2  MUTATE", gap=1.0)
    v.btn("F3", "F3  UNDO", gap=1.2)

    v.say("SHAPE IT", "A sound page is one picture with a knob's value in each corner. Turn a knob and it is written large.")
    v.btn("K1", "K1  SOUND: the next page", gap=0.8)
    v.turn("2", 45, "KNOB 2  FM DEPTH", 1.4)
    v.pad(9, gap=0.9)
    v.btn("K1", "K1  again: FILTER", gap=0.8)
    v.turn("1", -45, "KNOB 1  CUTOFF", 1.6)
    v.pad(9, gap=0.9)
    v.btn("K2", "K2  ENV", gap=0.8)
    v.turn("2", 30, "KNOB 2  DECAY", 1.4)
    v.pad(9, gap=1.2)

    v.say("FIVE ENGINES", "Five engines make the sounds: IMPACT, UI BLIP, SCI-FI FM, FOLEY and WAVETABLE.")
    v.btn("K1", "K1  SOUND", gap=0.7)
    for _ in range(4):
        v.turn("1", 1, "KNOB 1  ENGINE", 0.25, after=0.15)
        v.pad(9, gap=0.9)
    v.turn("1", 1, "KNOB 1  ENGINE", 0.25, after=0.4)

    v.say("INSTRUMENTS", "SHIFT + K1 changes kit. The MUSIC kit is a band: drums, FM bass, FM keys, brass, strings.")
    v.shift_btn("K1", "SHIFT + K1  the MUSIC kit", gap=1.2)
    for n in (3, 2, 13, 7):
        v.pad(n, gap=0.7)
    v.say("INSTRUMENTS", "The keys play the selected sound in tune - any sound is an instrument.")
    v.pad(8, "PAD 8  FM KEYS", gap=0.8)
    for note in (64, 67, 69, 71, 74, 71, 69, 67):
        v.key(note, hold=0.25, gap=0.3)
    v.pad(16, "PAD 16  STRINGS", gap=0.9)
    for note in (52, 55, 59):
        v.events.append((v.t, [9, note, 1, 0.8]))
        v.events.append((v.t + 1.8, [9, note, 0, 0]))
    v.hit(["N52", "N55", "N59"], "KEYS  a chord", 1.8)
    v.wait(2.8)

    v.say("THE SEQUENCER", "K5: the pads are the sixteen steps of the selected sound. A row is a beat.")
    v.btn("K5", "K5  SEQ", gap=0.9)
    for _ in range(6):
        v.btn("F2", "F2  next pattern", gap=0.22)
    v.wait(0.5)
    v.shift_pad(3, "SHIFT + PAD 3  select KICK")
    v.btn("F3", "F3  FILL", gap=0.8)
    v.shift_pad(2, "SHIFT + PAD 2  select SNARE")
    v.pad(5, "PAD 5  step 5", gap=0.5)
    v.pad(13, "PAD 13  step 13", gap=0.5)
    v.shift_pad(13, "SHIFT + PAD 13  select HI-HAT")
    v.btn("F3", "F3  FILL", gap=0.5)
    v.btn("F3", "F3  FILL again", gap=0.6)
    v.btn("PLAY", "PLAY", gap=2.4)
    v.say("THE SEQUENCER", "Hold a step and play a key: the step plays that note. Height is velocity.")
    v.shift_pad(7, "SHIFT + PAD 7  select FM BASS")
    for n, note in ((1, 40), (4, 40), (7, 43), (11, 45), (14, 47)):
        v.pad_key(n, note, gap=0.9)
    v.wait(3.0)
    v.btn("STOP", "STOP", gap=0.6)

    v.say("THE SONG", "K5 again: the song. Pads 1-8 are its patterns, the arrangement puts them in order.")
    v.btn("K5", "K5  again: SONG", gap=0.9)
    v.pad(1, "PAD 1  pattern A", gap=0.9)
    v.turn("3", -50, "KNOB 3  INTENSITY", 1.2)
    v.say("LAYERS", "INTENSITY is what the game sends. At zero, only the kick plays.")
    v.btn("PLAY", "PLAY", gap=4.2)
    v.say("LAYERS", "As it rises the band comes in: hat, bass, snare, strings, keys, brass, bells. One song, many moods.")
    v.turn("3", 50, "KNOB 3  INTENSITY", 11.0, after=3.5)

    v.say("AUTOMATION", "K5 a third time: automation. Any knob can have a lane - here the bass's filter.")
    v.btn("K5", "K5  again: AUTO", gap=0.9)
    v.shift_pad(7, "SHIFT + PAD 7  select FM BASS")
    v.pad(1, "PAD 1  a point", gap=0.5)
    v.turn("2", -45, "KNOB 2  VALUE", 1.0)
    v.pad(16, "PAD 16  a point", gap=0.5)
    v.turn("2", 35, "KNOB 2  VALUE", 1.0)
    v.btn("F3", "F3  RAMP", gap=4.0)

    v.say("THE MIXER", "K4 twice: a fader and a pan for every sound. The pads mute; SHIFT + pad solos.")
    v.btn("K4", "K4  FX", gap=0.5)
    v.btn("K4", "K4  again: MIX", gap=1.2)
    v.pad(8, "PAD 8  mute FM KEYS", gap=2.0)
    v.pad(8, "PAD 8  unmute", gap=1.0)
    v.shift_pad(7, "SHIFT + PAD 7  solo FM BASS", gap=2.4)
    v.btn("F4", "F4  ALL ON", gap=1.4)
    v.say("MASTER EFFECTS", "K4 again: the delay drawn as its echoes, the reverb as its fading.")
    v.btn("K4", "K4  again: MASTER", gap=1.0)
    v.turn("2", 35, "KNOB 2  FEEDBACK", 1.6)
    v.turn("3", 28, "KNOB 3  ROOM SIZE", 1.6, after=2.0)

    v.say("IN THE GAME", "The game runs the same engine. It sends intensity and cues; SFX FORGE sends sounds and the song.")
    v.btn("K6", "K6  LINK", gap=1.0)
    for value in list(range(127, 20, -6)) + list(range(20, 128, 5)):
        v.midi(0xB1, 14, value)
        v.wait(0.16)
    v.wait(2.0)
    v.btn("STOP", "STOP", gap=1.0)
    v.say("SFX FORGE", "Open source, MIT. The manual and a one-page cheat sheet are in the repository.")
    v.wait(4.0)
    return v


# ---- video 2: the demo song, played in like on a groovebox ---------------------------------------------------------------
STEP = 0.125                                         # a step at 120 BPM
LATE = (0.030, 0.042, 0.026, 0.038, 0.034, 0.045, 0.028, 0.040)   # a hand is never on the grid: 26-45 ms behind, still its step


def song():
    v = Take("sfx-forge-build-a-song", "building the demo song")
    # (not shown: empty the six patterns and the arrangement, so the song is built from nothing)
    v.btn("K5", show=False, gap=0.15)
    v.btn("K5", show=False, gap=0.15)
    for n in range(1, 7):
        v.pad(n, show=False, gap=0.12, hold=0.05)
        v.btn("F3", show=False, gap=0.12)
    v.events.append((v.t, [3, BUTTON["SHIFT"], 1])); v.events.append((v.t + 0.05, [1, 15, 1, 0.9])); v.events.append((v.t + 0.1, [1, 15, 0, 0])); v.events.append((v.t + 0.15, [3, BUTTON["SHIFT"], 0]))
    v.wait(0.3)
    v.pad(1, show=False, gap=0.15, hold=0.05)
    v.events.append((v.t, [4, ENCODER["2"], 0, -8]))       # CHAIN to 1: pattern A, round and round
    v.wait(0.1)
    v.btn("K5", show=False, gap=0.15)
    v.btn("K5", show=False, gap=0.15)
    v.wait(1.8)
    v.begin()

    v.say("BUILD A SONG", "The demo song, played in from nothing - like on a groovebox. Everything is on the K5 pages.")
    v.wait(4.0)

    v.say("REC + PLAY", "REC, then PLAY: the pattern loops to a metronome, and the pads are the drum kit. What you play is recorded.")
    v.btn("REC", "REC", gap=1.2)
    t0 = v.t                                          # the bar line: PLAY is pressed here, a bar is 2 seconds
    v.btn("PLAY", "PLAY", gap=0.05)
    hits = [0]

    def live(bar, step):                              # the moment of a step of a bar, a little late as a hand is
        hits[0] += 1
        v.t = max(v.t, t0 + bar * 2.0 + step * STEP + LATE[hits[0] % len(LATE)])

    def drum(bar, step, pad, name, vel=0.9):
        live(bar, step)
        v.events.append((v.t, [1, pad - 1, 1, vel]))
        v.events.append((v.t + 0.07, [1, pad - 1, 0, 0]))
        v.hit(["P%d" % pad], "PAD %d  %s" % (pad, name), 0.22)

    def play(bar, step, note, vel=0.9, hold=0.09):
        live(bar, step)
        v.events.append((v.t, [9, note, 1, vel]))
        v.events.append((v.t + hold, [9, note, 0, 0]))
        v.hit(["N%d" % note], "KEY  " + NOTE[note % 12] + str(note // 12 - 1), max(0.22, hold))

    def until(bar):
        v.t = max(v.t, t0 + bar * 2.0 - 0.02)

    v.say("1  THE KICK", "The kick, on every beat - pad 3. Each hit lands on the step it is nearest.")
    for step in (0, 4, 8, 12):
        drum(1, step, 3, "KICK")
    until(2)
    v.say("1  THE KICK", "It loops. One more hit, softer, just before beat three: how hard you play is recorded too.")
    drum(2, 7, 3, "KICK", vel=0.45)
    until(3)

    v.say("2  THE SNARE", "The snare on two and four - pad 2. The tiles show the sound you played last.")
    for step in (4, 12):
        drum(3, step, 2, "SNARE")
    v.say("2  THE SNARE", "SHIFT + REC turns the metronome off and on - also while it records.")
    until(4.0)
    v.shift_btn("REC", "SHIFT + REC  metronome off", gap=0.3)
    until(4.5)

    v.say("3  THE HI-HAT", "The hi-hat - pad 13: every second step, leaning on the off-beat.")
    until(5)
    for k, step in enumerate(range(0, 16, 2)):
        drum(5, step, 13, "HI-HAT", vel=0.85 if k % 2 else 0.5)
    until(7)

    v.shift_btn("REC", "SHIFT + REC  metronome on", gap=0.3)
    v.say("4  THE BASS", "SHIFT + a pad selects a sound without playing it in. Now the keys record: the bass line.")
    v.shift_pad(7, "SHIFT + PAD 7  select FM BASS", gap=0.5)
    until(8)
    for step, note in ((0, 28), (3, 28), (5, 40), (7, 28), (8, 31), (10, 33), (13, 35), (14, 38)):
        play(8, step, note, vel=1.0 if note >= 40 else 0.8)
    until(10)

    v.say("5  THE TUNE", "The melody, on FM KEYS - played over the loop, one pass.")
    v.shift_pad(8, "SHIFT + PAD 8  select FM KEYS", gap=0.5)
    until(11)
    for step, note in ((2, 64), (3, 67), (6, 69), (9, 67), (11, 64), (14, 62)):
        play(11, step, note)
    until(13)

    v.say("6  THE STRINGS", "Two long notes on the strings. A held sound holds until its next note, or the bar's end.")
    v.shift_pad(16, "SHIFT + PAD 16  select STRINGS", gap=0.5)
    until(14)
    play(14, 0, 52, vel=0.7, hold=0.8)
    play(14, 8, 50, vel=0.7, hold=0.8)
    until(16)

    v.say("7  BY HAND", "REC off: the pads are the sixteen steps again. Add one - the hat at the end of the bar - or fix a note.")
    v.btn("REC", "REC  off", gap=1.0)
    v.shift_pad(13, "SHIFT + PAD 13  select HI-HAT", gap=1.0)
    v.pad(16, "PAD 16  step 16", gap=1.6)
    v.say("7  BY HAND", "Knob 4 is a step's LENGTH: the strings let go after seven steps, before the next chord.")
    v.shift_pad(16, "SHIFT + PAD 16  select STRINGS", gap=1.0)
    v.turn("1", -15, "KNOB 1  STEP: to step 1", 0.8)    # (the frame was on step 16, from the hat)
    v.turn("4", 7, "KNOB 4  LENGTH", 1.0)
    v.turn("1", 8, "KNOB 1  STEP: to step 9", 0.8)
    v.turn("4", 7, "KNOB 4  LENGTH", 1.0, after=2.5)

    v.say("8  A SECOND PATTERN", "F5 COPY makes pattern B out of A. Hold a step and play a key to change its note: the answer.")
    v.btn("F5", "F5  COPY to the next pattern", gap=1.4)
    v.shift_pad(7, "SHIFT + PAD 7  select FM BASS", gap=1.0)
    v.pad_key(9, 33, gap=1.1)
    v.pad_key(12, 45, gap=1.1)
    v.pad(15, "PAD 15  step off", gap=1.0)
    v.pad_key(14, 31, gap=2.5)

    v.say("9  THE OTHER PATTERNS", "Chorus, breakdown and turnaround are made the same way. DEMO brings the finished six back.")
    v.btn("K5", "K5  again: SONG", gap=1.2)
    v.btn("F4", "F4  DEMO", gap=3.0)

    v.say("10  THE ARRANGEMENT", "SHIFT + a pattern's pad adds a bar. The row of letters is the song's order.")
    v.shift_pad(16, "SHIFT + PAD 16  clear the arrangement", gap=1.4)
    for n in (1, 2, 1, 2, 3, 4, 3, 4):
        v.shift_pad(n, "SHIFT + PAD %d  add pattern %s" % (n, "ABCD"[n - 1]), gap=0.75)
    v.wait(2.0)

    v.say("11  LAYERS", "Each instrument has a LAYER mark. Knob 4 sets it for the selected sound - here the brass.")
    v.turn("DATA", -1, "DATA  select BRASS", 0.4, after=0.8)
    v.turn("4", -20, "KNOB 4  LAYER", 1.2)
    v.turn("4", 20, "KNOB 4  LAYER", 1.2, after=0.8)
    v.say("11  LAYERS", "INTENSITY at zero: only the kick. A game raises it and the band comes in.")
    v.turn("3", -50, "KNOB 3  INTENSITY", 1.2, after=3.5)
    v.turn("3", 50, "KNOB 3  INTENSITY", 9.0, after=2.0)

    v.say("12  AUTOMATION", "K5 again: AUTO. Two points on the bass's filter and F3 RAMP - a sweep through the bar.")
    v.btn("K5", "K5  again: AUTO", gap=1.0)
    v.shift_pad(7, "SHIFT + PAD 7  select FM BASS", gap=1.0)
    v.pad(1, "PAD 1  a point", gap=0.6)
    v.turn("2", -45, "KNOB 2  VALUE", 1.0)
    v.pad(16, "PAD 16  a point", gap=0.6)
    v.turn("2", 35, "KNOB 2  VALUE", 1.0)
    v.btn("F3", "F3  RAMP", gap=4.0)

    v.say("13  THE MIX", "K4 twice: the mixer. DATA selects, knob 1 is the fader, knob 2 the pan.")
    v.btn("K4", "K4  FX", gap=0.5)
    v.btn("K4", "K4  again: MIX", gap=1.2)
    v.turn("DATA", 1, "DATA  select FM KEYS", 0.4, after=0.8)
    v.turn("1", -10, "KNOB 1  VOLUME", 1.2)
    v.turn("1", 10, "KNOB 1  VOLUME", 1.2)
    v.turn("2", 20, "KNOB 2  PAN", 1.2, after=2.0)

    v.say("14  KEEP IT, SEND IT", "F6 SAVE keeps the song. On the SONG page, F5 SEND gives it and its instruments to the game.")
    v.btn("K5", "K5  SEQ", gap=0.5)
    v.btn("K5", "K5  again: SONG", gap=1.0)
    v.btn("F6", "F6  SAVE", gap=1.6)
    v.btn("F5", "F5  SEND", gap=3.0)

    v.say("THE SONG", "That is the whole of it: played in, arranged, layered, automated, mixed.")
    v.wait(14.0)
    v.btn("STOP", "STOP", gap=1.0)
    v.say("SFX FORGE", "Open source, MIT. The manual and a one-page cheat sheet are in the repository.")
    v.wait(4.0)
    return v


if __name__ == "__main__":
    which = sys.argv[1:] or ["tour", "song"]
    if "tour" in which:
        render(tour())
    if "song" in which:
        render(song())
