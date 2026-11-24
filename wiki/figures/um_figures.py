# SPDX-License-Identifier: MIT
"""The pictures of the wiki's Unity music pages (prefix um-). Run from the repository root:

    python wiki/figures/um_figures.py

It reads the demo song's LAYER marks from unity/Demo/Assets/SfxDemo/DemoSong.bytes and the song Inspector's picture
from wiki/img/unity-inspector-song.png, and writes only into wiki/img:

    um-band-by-intensity.png   which instruments of the demo song play at which intensity
    um-song-inspector-marks.png  the song's Inspector (page STEPS) with numbered marks in a margin beside it
"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
WIKI = os.path.dirname(HERE)
ROOT = os.path.dirname(WIKI)
OUT = os.path.join(WIKI, "img")
SONG = os.path.join(ROOT, "unity", "Demo", "Assets", "SfxDemo", "DemoSong.bytes")

# the house colours (as docs/unity_figures.py has them)
BG = "#0E0B09"
PANEL = "#1B1613"
LINE = "#3A2E26"
TEXT = "#F4EBDD"
DIM = "#A08E7E"
EMBER = "#FF8A3C"
INK = "#0E0B09"
S = 2  # drawn at twice the size

# the song's sounds by index (unity/Demo/Assets/Showcase/Scripts/Audio/MusicKit.cs)
# fmt: off
KIT = [
    "Timpani", "Snare", "Kick", "Cowbell", "Pizzicato", "Brass", "FM bass", "FM keys", "Harp", "Celesta", "Organ",
    "Sweep", "Hi-hat", "Bells", "Choir", "Strings",
]
# fmt: on
# the nine sounds the demo song has notes for (docs/UNITY.md, "Intensity and layers"), with a colour a kind
PLAYS = {
    "Kick": "#FF5A36",
    "Hi-hat": "#7BE07B",
    "FM bass": "#36C5FF",
    "Snare": "#FF5A36",
    "Strings": "#C48BFF",
    "Choir": "#C48BFF",
    "FM keys": "#36C5FF",
    "Brass": "#C48BFF",
    "Bells": "#C48BFF",
}

_fonts = {}


def font(size, bold=False):
    key = (size, bold)
    if key not in _fonts:
        names = ["arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"] if bold else ["arial.ttf", "segoeui.ttf", "DejaVuSans.ttf"]
        _fonts[key] = ImageFont.load_default()
        for name in names:
            try:
                _fonts[key] = ImageFont.truetype(name, size)
                break
            except OSError:
                pass
    return _fonts[key]


def layer_marks():
    """The 16 LAYER marks, 0..127: bytes 7..22 of the packed song, which follows 16 patches of 28 bytes."""
    raw = open(SONG, "rb").read()
    song = raw[16 * 28 :]
    return [song[7 + i] for i in range(16)]


def band_by_intensity():
    marks = layer_marks()
    rows = sorted(((marks[KIT.index(name)], name) for name in PLAYS), key=lambda r: (r[0], KIT.index(r[1])))
    columns = [0.0, 0.2, 0.3, 0.4, 0.5, 0.6, 0.75, 1.0]
    w, h = 980, 500
    im = Image.new("RGB", (w * S, h * S), BG)
    d = ImageDraw.Draw(im)

    def text(x, y, s, size=15, fill=TEXT, bold=False, anchor="la"):
        d.text((x * S, y * S), s, font=font(size * S, bold), fill=fill, anchor=anchor)

    def box(x0, y0, x1, y1, fill, outline=None, r=7):
        d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], r * S, fill=fill, outline=outline, width=S if outline else 0)

    text(24, 20, "ONE SONG, MANY BANDS", 19, EMBER, True)
    text(280, 23, "the demo song: who plays at which intensity", 15, DIM)
    x0, y0, cw, rh = 250, 96, 86, 34
    text(x0 - 16, y0 - 30, "music.intensity =", 14, DIM, False, "ra")
    for c, v in enumerate(columns):
        text(x0 + c * cw + cw / 2 - 4, y0 - 30, ("%g" % v), 16, TEXT, True, "ma")
    for r, (mark, name) in enumerate(rows):
        y = y0 + r * rh
        text(24, y + 6, name.upper(), 15, TEXT, True)
        text(x0 - 16, y + 7, "always" if mark == 0 else "from %.2f" % (mark / 127.0), 13, DIM, False, "ra")
        for c, v in enumerate(columns):
            on = int(v * 127.0 + 0.5) >= mark  # as the engine compares (core/sfx_song.h, sfx_player_render)
            x = x0 + c * cw
            box(x, y, x + cw - 8, y + rh - 8, PLAYS[name] if on else PANEL, None if on else LINE)
    y = y0 + len(rows) * rh + 4
    text(x0 - 16, y + 2, "let through", 14, DIM, False, "ra")
    for c, v in enumerate(columns):
        n = sum(1 for mark, _ in rows if int(v * 127.0 + 0.5) >= mark)
        text(x0 + c * cw + cw / 2 - 4, y, "%d of 9" % n, 14, EMBER, True, "ma")
    text(24, h - 52, "A filled cell: the sound's notes start at that intensity. A sound plays while the intensity is at or above its", 13, DIM)
    text(24, h - 32, "LAYER mark, and only in a pattern that has notes for it. The marks are read from DemoSong.bytes.", 13, DIM)
    im.resize((w, h), Image.LANCZOS).save(os.path.join(OUT, "um-band-by-intensity.png"), optimize=True)


def song_inspector_marks():
    """The Inspector's picture with a margin on its left, and a numbered mark in the margin beside each part."""
    shot = Image.open(os.path.join(OUT, "unity-inspector-song.png")).convert("RGB")
    margin = 44
    im = Image.new("RGB", (shot.width + margin, shot.height), BG)
    im.paste(shot, (margin, 0))
    d = ImageDraw.Draw(im)
    # the y of each part in the picture (its own pixels): the top line, the title, the four numbers, the patterns,
    # the arrangement, the page keys, the step rows, INTENSITY and LAYER, the sound's three numbers, the steps in
    # words, the soft keys, the link line
    parts = [32, 55, 90, 141, 188, 221, 396, 587, 636, 682, 714, 746]
    r = 13
    for n, y in enumerate(parts, 1):
        x = margin / 2 + 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=EMBER)
        d.text((x, y), str(n), font=font(15, True), fill=INK, anchor="mm")
    im.save(os.path.join(OUT, "um-song-inspector-marks.png"), optimize=True)


band_by_intensity()
song_inspector_marks()
print("2 figures written to wiki/img")
