"""Builds the manual's pictures (docs/img/) from screenshots of the firmware.

    python docs/figures.py <folder with the screenshots>

The screenshots are 480 x 320 (the device's display) and 960 x 540 (the stage), as the SDK's wasmtest renders them.
Needs Pillow. Each annotated picture gets numbered marks; the manual's tables explain the numbers.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

SRC = sys.argv[1] if len(sys.argv) > 1 else "out/doc"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
os.makedirs(OUT, exist_ok=True)

ORANGE, INK, BG, PANEL, LINE, TEXT, DIM = "#FF8A3C", "#0B0F18", "#0B0F18", "#151C2B", "#26304A", "#E8ECF4", "#8E99AE"


def font(size, bold=True):
    for name in (["arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"] if bold else ["arial.ttf", "segoeui.ttf", "DejaVuSans.ttf"]):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def mark(d, x, y, n, r=15):
    """A numbered mark, centred on (x, y)."""
    d.ellipse([x - r - 2, y - r - 2, x + r + 2, y + r + 2], fill=INK)
    d.ellipse([x - r, y - r, x + r, y + r], fill=ORANGE)
    t = str(n)
    f = font(int(r * 1.25))
    w = d.textlength(t, font=f)
    d.text((x - w / 2, y - r * 0.74), t, font=f, fill=INK)


def shot(name, scale=2):
    im = Image.open(os.path.join(SRC, name + ".png")).convert("RGB")
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def plain(src, dst=None, scale=2):
    shot(src, scale).save(os.path.join(OUT, (dst or src) + ".png"), optimize=True)


def annotated(src, dst, marks, scale=2, r=15):
    im = shot(src, scale)
    d = ImageDraw.Draw(im)
    for n, (x, y) in enumerate(marks, 1):
        mark(d, x * scale, y * scale, n, r)
    im.save(os.path.join(OUT, dst + ".png"), optimize=True)


# ---- the device's pages, with marks -----------------------------------------------------------------------------------
# (the marks sit in the gaps and margins beside what they name, so nothing is hidden)
annotated("sfx-sound", "page-sound", [(42, 8), (310, 17), (384, 17), (240, 44), (150, 75), (330, 67), (350, 124), (94, 208), (240, 251), (240, 301)])
annotated("seq-playing", "page-seq", [(160, 65), (194, 65), (7, 92), (473, 165), (240, 251), (240, 301)])
annotated("song", "page-song", [(238, 77), (250, 111), (473, 127), (473, 176), (240, 251), (240, 301)])
annotated("t-auto", "page-auto", [(228, 65), (473, 135), (473, 202), (124, 251), (356, 251), (160, 301)])
annotated("mix", "page-mix", [(473, 79), (473, 150), (7, 112), (124, 251), (356, 251), (80, 301)])
annotated("master", "page-master", [(226, 98), (458, 98), (124, 251), (356, 251)])
annotated("link", "page-link", [(228, 205), (458, 140), (458, 174), (240, 251)])
annotated("seq-playing-stage", "stage", [(150, 34), (190, 14), (342, 14), (462, 14), (454, 45), (14, 180), (328, 316), (200, 345), (14, 414), (14, 458), (14, 510),
                                         (458, 240), (946, 240), (946, 434), (946, 458), (458, 502), (946, 491)], scale=1, r=11)
annotated("auto-stage", "stage-auto", [(459, 414), (14, 458), (946, 240), (946, 491)], scale=1, r=11)

# ---- the tutorials: a picture a step ---------------------------------------------------------------------------------
for n in ("t-sfx-1", "t-sfx-2", "t-sfx-3", "t-sfx-4", "t-beat-1", "t-beat-2", "t-beat-3", "t-beat-4", "env", "lfo", "fx", "music-wave"):
    plain(n)


# ---- the device: what is where ---------------------------------------------------------------------------------------
def device_map():
    W, H = 1200, 660
    im = Image.new("RGB", (W, H), "#070A10")
    d = ImageDraw.Draw(im)
    f, small = font(17), font(13, False)
    d.rounded_rectangle([20, 20, W - 20, H - 20], 26, fill="#1B1F27", outline="#2C3340", width=2)

    def key(x, y, w, h, label="", fill="#2A303B"):
        d.rounded_rectangle([x, y, x + w, y + h], 6, fill=fill, outline="#3A4252")
        if label:
            tw = d.textlength(label, font=small)
            d.text((x + (w - tw) / 2, y + (h - 15) / 2), label, font=small, fill=TEXT)

    def knob(x, y, r, label=""):
        d.ellipse([x - r, y - r, x + r, y + r], fill="#11151C", outline="#4A5468", width=3)
        d.line([x, y - r + 5, x, y - r / 3], fill=TEXT, width=3)
        if label:
            tw = d.textlength(label, font=small)
            d.text((x - tw / 2, y + r + 6), label, font=small, fill=DIM)

    # the display, with the pages it shows
    d.rounded_rectangle([110, 56, 470, 296], 8, fill=BG, outline="#3A4252", width=2)
    d.rounded_rectangle([122, 66, 152, 86], 4, fill="#36C5FF")
    d.text((160, 66), "LASER", font=f, fill=TEXT)
    for i, n in enumerate(["SOUND", "ENV", "LFO", "FX", "SEQ", "LINK"]):
        d.rounded_rectangle([122 + i * 57, 96, 174 + i * 57, 112], 4, fill="#36C5FF" if i == 0 else PANEL)
    d.rounded_rectangle([122, 120, 372, 216], 5, fill=PANEL)
    d.polygon([(130, 168), (132, 128), (250, 160), (364, 167), (364, 169), (250, 176), (132, 208)], fill="#36C5FF")
    for i in range(16):
        d.rounded_rectangle([382 + (i % 4) * 20, 120 + (i // 4) * 24, 398 + (i % 4) * 20, 140 + (i // 4) * 24], 3, fill="#24405A")
    for i in range(4):
        d.rounded_rectangle([122 + i * 86, 224, 200 + i * 86, 262], 5, fill=PANEL)
    for i in range(6):
        d.rounded_rectangle([122 + i * 57, 268, 174 + i * 57, 286], 4, fill="#1C2436")
    key(60, 150, 34, 28, "RET")
    key(60, 190, 34, 28, "HOME")
    for i in range(6):
        key(110 + i * 61, 310, 54, 24, "F" + str(i + 1))
    for i in range(4):
        knob(545 + i * 85, 130, 30, str(i + 1))
    knob(905, 140, 46, "DATA")
    for i in range(16):
        d.rounded_rectangle([985 + (i % 4) * 44, 56 + (i // 4) * 44, 1023 + (i % 4) * 44, 94 + (i // 4) * 44], 6, fill="#2B3C52", outline="#3A4252")
    for i, n in enumerate(["SHIFT", "OCT -", "OCT +", "ENTER"]):
        key(60 + i * 76, 366, 68, 30, n)
    for i, n in enumerate(["K1 SOUND", "K2 ENV", "K3 LFO", "K4 FX", "K5 SEQ", "K6 LINK"]):
        key(390 + i * 96, 366, 88, 30, n, "#243247")
    knob(105, 462, 26, "A")
    knob(195, 462, 26, "B")
    for i, n in enumerate(["LEFT", "MENU", "RIGHT"]):
        key(60 + i * 62, 528, 56, 26, n)
    for i, (n, c) in enumerate([("REC", "#5A2630"), ("STOP", "#2A303B"), ("PLAY", "#23503C")]):
        key(60 + i * 62, 562, 56, 26, n, c)
    for i in range(15):
        d.rounded_rectangle([290 + i * 58, 426, 344 + i * 58, 606], 5, fill="#E9EDF3")
    for i in range(14):
        if i % 7 not in (2, 6):
            d.rounded_rectangle([329 + i * 58, 426, 363 + i * 58, 536], 4, fill="#12151B")
    for n, (x, y) in enumerate([(452, 74), (492, 322), (672, 84), (846, 96), (968, 208), (40, 381), (976, 381), (150, 428), (262, 541), (262, 575), (1134, 410), (40, 128)], 1):
        mark(d, x, y, n)
    im.save(os.path.join(OUT, "device.png"), optimize=True)


device_map()


# ---- layers: how the game's intensity brings the band in --------------------------------------------------------------
def layers():
    rows = [("KICK", 0, "#FF5A36"), ("HI-HAT", 20, "#7BE07B"), ("FM BASS", 35, "#36C5FF"), ("SNARE", 50, "#FF5A36"), ("STRINGS", 60, "#C48BFF"),
            ("CHOIR", 60, "#C48BFF"), ("FM KEYS", 72, "#36C5FF"), ("BRASS", 90, "#C48BFF"), ("BELLS", 100, "#C48BFF")]
    W, H, x0, x1, y0 = 1000, 80 + len(rows) * 40 + 70, 150, 960, 70
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    f, small = font(18), font(14, False)
    d.text((x0, 18), "INTENSITY  -  what the game sends, from calm to all-out", font=f, fill="#FF8A3C")
    for k in range(5):
        x = x0 + (x1 - x0) * k / 4
        d.line([x, y0 - 8, x, H - 60], fill=LINE, width=1)
        t = str(k * 25) + " %"
        d.text((x - d.textlength(t, font=small) / 2, H - 52), t, font=small, fill=DIM)
    for i, (name, level, colour) in enumerate(rows):
        y = y0 + i * 40
        x = x0 + (x1 - x0) * level / 127
        d.text((x0 - 14 - d.textlength(name, font=f), y + 5), name, font=f, fill=TEXT)
        d.rounded_rectangle([x0, y + 4, x1, y + 30], 6, fill="#121824")
        d.rounded_rectangle([x, y + 4, x1, y + 30], 6, fill=colour)
        d.text((x + 10, y + 8), "plays from here", font=small, fill=INK)
    d.text((x0, H - 28), "A sound plays while INTENSITY is at or above its LAYER mark. One song: nine instruments coming in one by one.", font=small, fill=DIM)
    im.save(os.path.join(OUT, "layers.png"), optimize=True)


layers()
print("figures written to", OUT)
