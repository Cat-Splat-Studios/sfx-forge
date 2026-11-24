#!/usr/bin/env python3
"""The drawn pictures of the wiki's first Unity pages (prefix us-).

    python wiki/figures/us_figures.py          (from the repository root; needs Pillow)

Writes into wiki/img only:
    us-preset-inspector-parts.png   the Inspector of a sound with its parts numbered (Unity-Quick-Start,
                                    Unity-Presets-And-Patches); the screenshot is wiki/img/unity-inspector-sound-osc.png
    us-one-shot-or-held.png         what SUSTAIN decides: a one-shot's level over time beside a held sound's
                                    (Unity-Presets-And-Patches, Unity-Held-Sounds)

The colours are those of docs/unity_figures.py. That file is not imported: importing it draws its own figures.
"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(os.path.dirname(HERE), "img")

BG = "#0E0B09"
PANEL = "#1B1613"
LINE = "#3A2E26"
TEXT = "#F4EBDD"
DIM = "#A08E7E"
EMBER = "#FF8A3C"
INK = "#0E0B09"
BAKED = "#FFD23F"
LIVE = "#36C5FF"
HELD = "#7BE07B"
S = 2  # drawn at twice the size, then made smaller

_fonts = {}


def font(size, bold=False, mono=False):
    key = (size, bold, mono)
    if key not in _fonts:
        if mono:
            names = ["consolab.ttf", "DejaVuSansMono-Bold.ttf"] if bold else ["consola.ttf", "DejaVuSansMono.ttf"]
        elif bold:
            names = ["arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"]
        else:
            names = ["arial.ttf", "segoeui.ttf", "DejaVuSans.ttf"]
        _fonts[key] = ImageFont.load_default()
        for name in names:
            try:
                _fonts[key] = ImageFont.truetype(name, size)
                break
            except OSError:
                pass
    return _fonts[key]


class Canvas:
    """All positions are in final pixels."""

    def __init__(self, w, h, title, sub):
        self.w, self.h = w, h
        self.im = Image.new("RGB", (w * S, h * S), BG)
        self.d = ImageDraw.Draw(self.im)
        self.text(24, 20, title, 19, EMBER, True)
        self.text(24 + self.d.textlength(title, font=font(19 * S, True)) / S + 16, 23, sub, 15, DIM)

    def text(self, x, y, s, size=15, fill=TEXT, bold=False, anchor="la", mono=False):
        self.d.text((x * S, y * S), s, font=font(size * S, bold, mono), fill=fill, anchor=anchor)

    def line(self, pts, fill=TEXT, width=2.0):
        self.d.line([(x * S, y * S) for x, y in pts], fill=fill, width=int(width * S), joint="curve")

    def box(self, x0, y0, x1, y1, fill=PANEL, outline=LINE, r=10):
        self.d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], r * S, fill=fill, outline=outline, width=S)

    def mark(self, x, y, n, r=13):
        self.d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=EMBER)
        self.text(x, y, str(n), 16, INK, True, "mm")

    def paste(self, picture, x, y):
        w, h = picture.size
        self.im.paste(picture.convert("RGB").resize((w * S, h * S), Image.LANCZOS), (x * S, y * S))

    def save(self, name):
        path = os.path.join(IMG, name + ".png")
        self.im.resize((self.w, self.h), Image.LANCZOS).save(path, optimize=True)
        print("%-34s %4d x %-4d %6.1f KB" % (name + ".png", self.w, self.h, os.path.getsize(path) / 1024.0))


def inspector_parts():
    shot = Image.open(os.path.join(IMG, "unity-inspector-sound-osc.png"))  # 640 x 608
    x0, y0 = 64, 64
    c = Canvas(1290, y0 + shot.height + 24, "THE INSPECTOR OF A SOUND", "an SfxPreset, page OSC: the showcase's Jump")
    c.paste(shot, x0, y0)
    # (y in the screenshot, name, what it is)
    parts = [
        (32, "The top line", "the pad SEND goes to, the asset's name, the engine, the link dot"),
        (76, "The page tabs", "OSC  TONE  FILTER  WAVE  ENV  LFO  FX: seven pages hold the 25 values"),
        (124, "Value tiles", "up to four a page, two above the picture and two below. Drag one up or down"),
        (214, "The picture", "the sound as the device draws it: one voice, without its effects"),
        (356, "The level line", "peak and RMS of the sound as PLAY and BAKE WAV render it"),
        (388, "The soft keys", "PLAY, MUTATE, UNDO, RANDOM, BAKE WAV, SEND, and three more below"),
        (446, "The link note", "NOT CONNECTED here: no device is needed to make and play sounds"),
        (518, "TOOLS", "four tabs: NEW, VARY, LEVEL, SHARE. NEW is open, with its ten kinds"),
    ]
    lx = x0 + shot.width + 48
    ly = y0 + 18
    for n, (y, name, what) in enumerate(parts, 1):
        c.mark(x0 - 26, y0 + y, n)
        c.line([(x0 - 12, y0 + y), (x0 - 2, y0 + y)], EMBER, 2)
        c.mark(lx, ly, n)
        c.text(lx + 26, ly - 19, name, 16, TEXT, True)
        c.text(lx + 26, ly + 3, what, 13, DIM)
        ly += 70
    c.save("us-preset-inspector-parts")


def one_shot_or_held():
    c = Canvas(1100, 430, "SUSTAIN DECIDES", "the level of one note over time: a one-shot, and a held sound")
    base = 330
    top = 168

    def panel(x0, x1, colour, title, sub):
        c.box(x0, 64, x1, 406)
        c.text(x0 + 20, 80, title, 17, colour, True)
        c.text(x0 + 20, 104, sub, 13, DIM)
        c.line([(x0 + 30, base), (x1 - 30, base)], LINE, 1.5)
        c.text(x1 - 30, base + 28, "time", 12, DIM, False, "ra")

    def label(x, y, s, colour=DIM, anchor="ma", mono=False):
        c.text(x, y, s, 13, colour, False, anchor, mono)

    def upright(x, colour, text, mono=True):
        c.line([(x, top - 14), (x, base)], colour, 1.5)
        c.text(x + 6, top - 16, text, 13, colour, True, "la", mono)

    # a one-shot: SUSTAIN 0
    x0, x1 = 24, 500
    panel(x0, x1, BAKED, "SUSTAIN 0: A ONE-SHOT", "it plays to its end by itself")
    a = x0 + 70
    pts = [(a, base), (a + 26, top)]
    for i in range(1, 41):  # the decay, falling to nothing
        t = i / 40.0
        pts.append((a + 26 + t * 250, base - (base - top) * (1 - t) ** 2.4))
    c.line(pts, BAKED, 3)
    upright(a, TEXT, "Play()")
    label(a + 13, base + 8, "ATTACK")
    label(a + 150, base + 8, "DECAY")
    label(a + 290, base - 30, "ends by itself", BAKED, "la")
    c.text(x0 + 20, 372, "You call nothing more.", 14, TEXT, True)

    # held: SUSTAIN above 0
    x0, x1 = 524, 1076
    panel(x0, x1, HELD, "SUSTAIN ABOVE 0: HELD", "it sounds until it is let go")
    a = x0 + 70
    sus = top + 60
    rel = a + 330
    pts = [(a, base), (a + 26, top)]
    for i in range(1, 21):  # the decay, falling to the sustain level
        t = i / 20.0
        pts.append((a + 26 + t * 80, sus - (sus - top) * (1 - t) ** 2.4))
    pts.append((rel, sus))
    for i in range(1, 21):  # the release
        t = i / 20.0
        pts.append((rel + t * 110, base - (base - sus) * (1 - t) ** 2.4))
    c.line(pts, HELD, 3)
    upright(a, TEXT, "Play()")
    upright(rel, EMBER, "Release()")
    label(a + 13, base + 8, "ATTACK")
    label(a + 66, base + 8, "DECAY")
    label(a + 218, sus - 22, "held at SUSTAIN, for as long as the game wants", HELD)
    label(rel + 55, base + 8, "RELEASE")
    c.text(x0 + 20, 372, "You must call Release(), or it never stops.", 14, TEXT, True)
    c.save("us-one-shot-or-held")


if __name__ == "__main__":
    inspector_parts()
    one_shot_or_held()
