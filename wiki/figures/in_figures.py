#!/usr/bin/env python3
"""Draws the three pictures of the wiki's internals pages into wiki/img.

    node wiki/figures/in_shots.mjs       (first, once: the device screenshot wiki/img/in-shot-seq.png)
    python wiki/figures/in_figures.py    (from the repository's root; needs Pillow)

    in-frame-parts.png    the SEQ page of the display, with the parts of on_draw (sfx-forge/fw_draw.h) marked
    in-patch-bytes.png    the 28 bytes of a patch: the factory sound TIMPANI, byte by byte
    in-song-layout.png    a packed song of format 6: the head to scale, the factory song's eight blocks, one block

The numbers are read from the repository where a file holds them: the patch and the blocks' sizes from
unity/Demo/Assets/SfxDemo/DemoSong.bytes (16 patches of 28 bytes, then the packed factory song). The head's layout is
the table in core/sfx_song.h, section 4, written out below as HEAD.
The colours are the house colours of docs/figures.py and docs/unity_figures.py (copied: importing those files would
draw their pictures again).
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "wiki", "img")
DEMO = os.path.join(ROOT, "unity", "Demo", "Assets", "SfxDemo", "DemoSong.bytes")

# everything is drawn S times larger and scaled down at the end, so that edges are smooth
S = 2

BG = "#0B0F18"
INK = "#0B0F18"
PANEL = "#151C2B"
LINE = "#26304A"
TEXT = "#E8ECF4"
DIM = "#8E99AE"
ORANGE = "#FF8A3C"
GREY = "#3A4560"
# a colour for each sound page of the device (the engines' colours of the firmware, and two more)
C_OSC = "#FF5A36"
C_TONE = "#FFD23F"
C_FILTER = "#36C5FF"
C_WAVE = "#C48BFF"
C_ENV = "#7BE07B"
C_LFO = "#6EE7A8"
C_FX = "#FF8A3C"

_fonts = {}


def font(size, bold=False, mono=False):
    """One of the usual fonts this machine has, in that size; Pillow's own small font if it has none."""
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
    """A picture. All positions are in final pixels."""

    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.im = Image.new("RGB", (w * S, h * S), BG)
        self.d = ImageDraw.Draw(self.im)

    def text(self, x, y, s, size=15, fill=TEXT, bold=False, anchor="la", mono=False):
        self.d.text((x * S, y * S), s, font=font(size * S, bold, mono), fill=fill, anchor=anchor)

    def width(self, s, size=15, bold=False, mono=False):
        return self.d.textlength(s, font=font(size * S, bold, mono)) / S

    def line(self, pts, fill=DIM, width=2):
        self.d.line([(x * S, y * S) for x, y in pts], fill=fill, width=int(width * S))

    def box(self, x0, y0, x1, y1, fill=PANEL, outline=None, r=6, width=1):
        self.d.rounded_rectangle(
            [x0 * S, y0 * S, x1 * S, y1 * S], r * S, fill=fill, outline=outline, width=int(width * S) if outline else 0
        )

    def mark(self, x, y, n, colour=ORANGE, r=14):
        """A numbered mark, centred on (x, y)."""
        self.d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=colour)
        self.text(x, y, str(n), int(r * 1.25), INK, True, "mm")

    def paste_hard(self, picture, x, y, scale):
        """A screenshot, enlarged with hard pixels."""
        big = picture.convert("RGB").resize((picture.width * scale * S, picture.height * scale * S), Image.NEAREST)
        self.im.paste(big, (x * S, y * S))

    def save(self, name):
        path = os.path.join(OUT, name + ".png")
        self.im.resize((self.w, self.h), Image.LANCZOS).save(path, optimize=True)
        print("wiki/img/%s.png  %d x %d  %.1f KB" % (name, self.w, self.h, os.path.getsize(path) / 1024.0))


# ---- 1. a frame of the display, part by part ------------------------------------------------------------------------
def frame_parts():
    shot = Image.open(os.path.join(OUT, "in-shot-seq.png"))
    scale = 2
    left = 64
    top = 20
    w = shot.width * scale
    h = shot.height * scale
    c = Canvas(left + w + 20, top + h + 250)
    c.paste_hard(shot, left, top, scale)

    # the three parts that are seen, as rows of the display (in the screenshot's own pixels): a bracket and a mark
    seen = [(3, 2, 56), (5, 60, 288), (6, 291, 318)]
    for n, y0, y1 in seen:
        a = top + y0 * scale
        b = top + y1 * scale
        c.line([(left - 12, a), (left - 22, a), (left - 22, b), (left - 12, b)], ORANGE, 2)
        c.mark(left - 40, (a + b) / 2, n)

    # all seven, in words
    rows = [
        (1, "every frame, seen or not", "out_pump (the link), tour_pump (the tour), seq_poll (what the audio did)"),
        (2, "HOME", "the page's controls instead of the page: a HELP_... table, and nothing else is drawn"),
        (3, "the top line", "header; under it the page's name, the pattern, six marks for K1 to K6"),
        (4, "the 16 pads' lights", "not on the display: sent to the host when a light changes (led_cache)"),
        (5, "the page itself", "one draw_... function a page, and the four values over encoders 1 to 4"),
        (6, "F1 to F6", "the captions (soft_caption), or the tour's caption"),
        (7, "FOCUS", "not drawn: where the SDK's frame may go (K1 to K6, the four values, F1 to F6)"),
    ]
    y = top + h + 24
    for n, name, what in rows:
        on_picture = n in (3, 5, 6)
        c.mark(left - 40 + 14, y + 10, n, ORANGE if on_picture else GREY, 11)
        c.text(left, y + 10, name, 15, TEXT, True, "lm")
        c.text(left + 210, y + 10, what, 14, DIM, False, "lm")
        y += 31
    c.save("in-frame-parts")


# ---- 2. the 28 bytes of a patch --------------------------------------------------------------------------------------
def patch_bytes():
    with open(DEMO, "rb") as f:
        patch = f.read(28)
    # the names of the 25 values in wire order (core/sfx_core.h, SFX_P_*), and the device page each is on
    # (sfx-forge/fw_state.h, PAGE_PARAMS)
    # fmt: off
    values = [
        ("FREQ", C_OSC), ("P.AMT", C_OSC), ("P.DEC", C_OSC), ("A", C_TONE), ("B", C_TONE), ("NOISE", C_TONE),
        ("CUT", C_FILTER), ("RES", C_FILTER), ("DECAY", C_ENV), ("CRUSH", C_TONE), ("ATK", C_ENV), ("SUS", C_ENV),
        ("REL", C_ENV), ("F.ENV", C_FILTER), ("F.DEC", C_FILTER), ("RATE", C_LFO), ("PITCH", C_LFO),
        ("FILT", C_LFO), ("TIMB", C_LFO), ("SPRD", C_WAVE), ("CHO", C_FX), ("DLY", C_FX), ("REV", C_FX),
        ("LEVEL", C_FX), ("TABLE", C_WAVE),
    ]
    # fmt: on
    cells = [("VER", GREY), ("ENG", C_OSC)] + values + [("SUM", GREY)]
    assert len(cells) == 28

    cw = 38
    left = 24
    top = 78
    c = Canvas(left * 2 + cw * 28, 268)
    c.text(left, 18, "A patch on the wire: 28 bytes, each 0..127", 18, ORANGE, True)
    c.text(left, 46, "the factory sound TIMPANI (pad 1 of the MUSIC kit), as the device sends it", 14, DIM)
    for i, (name, colour) in enumerate(cells):
        x = left + i * cw
        c.text(x + cw / 2, top - 6, str(i), 11, DIM, False, "mb")
        c.box(x + 1, top, x + cw - 1, top + 44, fill=PANEL, outline=colour, r=5, width=2)
        c.text(x + cw / 2, top + 22, "%02X" % patch[i], 15, TEXT, True, "mm", True)
        c.text(x + cw / 2, top + 58, name, 10, colour if colour != GREY else DIM, True, "mm")

    # what the three kinds of byte are
    y = top + 84
    c.text(left, y, "byte 0", 13, TEXT, True)
    c.text(left + 62, y, "the patch format: 2", 13, DIM)
    c.text(left + 250, y, "byte 1", 13, TEXT, True)
    c.text(left + 312, y, "the engine, 0..4", 13, DIM)
    c.text(left + 470, y, "bytes 2..26", 13, TEXT, True)
    c.text(left + 566, y, "the 25 values", 13, DIM)
    c.text(left + 700, y, "byte 27", 13, TEXT, True)
    c.text(left + 770, y, "the sum of bytes 0..26, its low 7 bits", 13, DIM)

    # the colours: the page of the device a value is on
    y += 36
    c.text(left, y + 9, "on the device's page", 13, DIM, False, "lm")
    x = left + 160
    pages = [
        ("OSC", C_OSC),
        ("TONE", C_TONE),
        ("FILTER", C_FILTER),
        ("WAVE", C_WAVE),
        ("ENV", C_ENV),
        ("LFO", C_LFO),
        ("FX", C_FX),
    ]
    for name, colour in pages:
        c.box(x, y + 2, x + 14, y + 16, fill=colour, r=3)
        c.text(x + 22, y + 9, name, 13, TEXT, False, "lm")
        x += 22 + c.width(name, 13) + 28
    c.save("in-patch-bytes")


# ---- 3. a packed song of format 6 -----------------------------------------------------------------------------------
# the head: (offset, bytes, name, colour) - core/sfx_song.h, section 4
HEAD = [
    (0, 1, "format", ORANGE),
    (1, 1, "bar", C_TONE),
    (2, 2, "tempo", C_TONE),
    (4, 1, "swing", C_TONE),
    (5, 1, "scale", C_TONE),
    (6, 1, "chain", C_TONE),
    (7, 16, "layers", C_ENV),
    (23, 16, "faders", C_FILTER),
    (39, 16, "pans", C_FILTER),
    (55, 3, "mute", C_FILTER),
    (58, 8, "master", C_FX),
    (66, 1, "arrangement's length", C_WAVE),
    (67, 32, "arrangement", C_WAVE),
    (99, 48, "24 lanes x 2", C_LFO),
]


def song_layout():
    with open(DEMO, "rb") as f:
        song = f.read()[16 * 28 :]
    assert song[0] == 6
    # the eight blocks of the factory song: where each begins and how long it is (as sfx_song_unpack counts it)
    blocks = []
    at = 147
    for _ in range(8):
        steps = 16 * (1, 2, 4)[song[at]]
        bit_bytes = (steps + 6) // 7
        rows = bin(song[at + 1] | song[at + 2] << 7 | song[at + 3] << 14).count("1")
        lanes = bin(song[at + 4] | song[at + 5] << 7 | song[at + 6] << 14 | song[at + 7] << 21).count("1")
        size = 8 + rows * (bit_bytes + 3 * steps) + lanes * steps
        blocks.append((at, size, rows, lanes))
        at += size
    assert at == len(song)

    left = 24
    px = 6.0  # pixels a byte, in the head
    width = int(147 * px)
    c = Canvas(left * 2 + width, 570)
    c.text(left, 18, "A packed song, format 6", 18, ORANGE, True)
    c.text(left + 232, 21, "every byte 0..127", 14, DIM)

    # the head, to scale. Bytes 0 to 6 are one byte each but the tempo: they share one line of names above
    top = 104
    c.text(left, top - 56, "the head: 147 bytes, always", 14, TEXT, True)
    c.text(left, top - 30, "bytes 0..6:", 12, DIM)
    c.text(left + 70, top - 30, "format", 12, ORANGE, True)
    c.text(left + 116, top - 30, "bar   tempo (2 bytes)   swing   scale   chain", 12, C_TONE, True)
    c.line([(left + 1, top - 10), (left + 7 * px - 1, top - 10)], DIM, 1)
    c.line([(left + 1, top - 10), (left + 1, top - 4)], DIM, 1)
    c.line([(left + 7 * px - 1, top - 10), (left + 7 * px - 1, top - 4)], DIM, 1)
    for off, n, name, colour in HEAD:
        x0 = left + off * px
        x1 = left + (off + n) * px
        c.box(x0 + 1, top, x1 - 1, top + 40, fill=PANEL, outline=colour, r=3, width=2)
        if n >= 8:
            c.text((x0 + x1) / 2, top + 20, name, 12, TEXT, True, "mm")
            c.text(x0 + 2, top + 46, str(off), 11, DIM)
        elif off >= 7:
            # a narrow one: its name on a leader below
            mid = (x0 + x1) / 2
            c.line([(mid, top + 40), (mid, top + 64)], colour, 1)
            # (two of them stand close: the first is written to the left of its leader, the second to the right)
            right = off == 66
            label = "%s (%d)" % (name, off)
            c.text(mid + (4 if right else -4), top + 62, label, 11, colour, True, "la" if right else "ra")
    c.text(left + width, top + 46, "147", 11, DIM, False, "ra")

    # the eight blocks of the factory song, to scale
    top2 = 250
    total = len(song) - 147
    c.text(left, top2 - 26, "then 8 pattern blocks, A to H, each as long as it needs", 14, TEXT, True)
    note = "here: the factory song, %d bytes in all (G and H are empty: 8 bytes each)" % len(song)
    c.text(left + width, top2 - 24, note, 13, DIM, False, "ra")
    for k, (off, size, rows, lanes) in enumerate(blocks):
        x0 = left + (off - 147) / total * width
        x1 = left + (off - 147 + size) / total * width
        c.box(x0 + 1, top2, max(x1 - 1, x0 + 5), top2 + 40, fill=PANEL, outline=ORANGE, r=3, width=2)
        if size > 50:
            c.text((x0 + x1) / 2, top2 + 20, "ABCDEFGH"[k], 16, TEXT, True, "mm")
            c.text((x0 + x1) / 2, top2 + 52, "%d bytes" % size, 12, DIM, False, "mm")
            c.text((x0 + x1) / 2, top2 + 68, "%d rows, %d lanes" % (rows, lanes), 11, DIM, False, "mm")

    # one block, not to scale
    top3 = 406
    c.text(left, top3 - 26, "one block (G = 16, 32 or 64 steps; B = 3, 5 or 10 bytes of step bits)", 14, TEXT, True)
    parts = [
        ("grid", "1", 62, ORANGE),
        ("sounds with a row", "3", 136, C_ENV),
        ("lanes with a row", "4", 136, C_LFO),
        ("step bits", "B", 82, C_ENV),
        ("notes", "G", 70, C_ENV),
        ("velocities", "G", 88, C_ENV),
        ("lengths", "G", 76, C_ENV),
        ("points", "G", 76, C_LFO),
    ]
    x = left
    starts = []
    for name, n, w, colour in parts:
        starts.append(x)
        c.box(x + 1, top3, x + w - 1, top3 + 44, fill=PANEL, outline=colour, r=3, width=2)
        c.text(x + w / 2, top3 + 15, name, 12, TEXT, True, "mm")
        c.text(x + w / 2, top3 + 32, n, 12, DIM, False, "mm", True)
        x += w
        if name in ("lanes with a row", "lengths"):
            x += 14
    end = x
    # what repeats
    y = top3 + 56
    c.line([(starts[0], y), (starts[0], y + 8), (starts[3] - 16, y + 8), (starts[3] - 16, y)], DIM, 1)
    c.text((starts[0] + starts[3] - 16) / 2, y + 14, "8 bytes, always", 12, DIM, False, "ma")
    c.line([(starts[3], y), (starts[3], y + 8), (starts[7] - 16, y + 8), (starts[7] - 16, y)], C_ENV, 1)
    c.text((starts[3] + starts[7] - 16) / 2, y + 14, "once for each sound with a row", 12, C_ENV, False, "ma")
    c.line([(starts[7], y), (starts[7], y + 8), (end - 2, y + 8), (end - 2, y)], C_LFO, 1)
    c.text((starts[7] + end) / 2, y + 14, "once for each", 12, C_LFO, False, "ma")
    c.text((starts[7] + end) / 2, y + 30, "lane with a row", 12, C_LFO, False, "ma")
    c.text(left, top3 + 124, "an empty pattern is 8 bytes; a full one at 64 steps is 4776", 13, DIM)
    c.save("in-song-layout")


frame_parts()
patch_bytes()
song_layout()
