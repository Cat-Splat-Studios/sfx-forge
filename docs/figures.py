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


# ---- the cheat sheet: every function and how to reach it, on one picture ------------------------------------------------
def cheat():
    W, H = 1640, 1120
    bg, panel, line, text, dim, ember = "#0E0B09", "#1B1613", "#3A2E26", "#F4EBDD", "#A08E7E", "#FF8A3C"
    key = {"K1": "#C48BFF", "K2": "#FFD23F", "K3": "#7BE07B", "K4": "#36C5FF", "K5": "#FF5A36", "K6": "#6EE7A8"}
    im = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(im)
    big, f, b, sm = font(34), font(17, False), font(17), font(14, False)
    d.text((30, 22), "SFX FORGE", font=big, fill=text)
    d.text((250, 36), "cheat sheet  -  every page, and what each control does on it", font=f, fill=dim)
    rows = [
        ("K1", "SOUND", [("OSC", "ENGINE · FREQ · PITCH ENV · ENV TIME", ""), ("TONE", "A · B · NOISE · CRUSH", "A B: the engine's own two"),
                         ("FILTER", "CUTOFF · RESO · FILTER ENV · F.ENV TIME", ""), ("WAVE", "TABLE · SPREAD", "wavetable sounds only")]),
        ("K2", "ENV", [("ENVELOPE", "ATTACK · DECAY · SUSTAIN · RELEASE", "SUSTAIN off: a one-shot")]),
        ("K3", "LFO", [("LFO", "RATE · > PITCH · > FILTER · > TIMBRE", "")]),
        ("K4", "FX", [("EFFECT SENDS", "CHORUS · DELAY · REVERB · LEVEL", "of the selected sound"),
                      ("MIXER", "VOLUME · PAN · DRIVE · OUTPUT", "pads: mute    SHIFT + pad: solo"),
                      ("MASTER EFFECTS", "TIME · FEEDBACK · ROOM SIZE · DAMPING", "of the delay and the reverb")]),
        ("K5", "SEQ", [("SEQUENCER", "STEP · NOTE · VELOCITY · LENGTH", "pads: the 16 steps    pad + key: its note"),
                       ("SONG", "PATTERN · CHAIN · INTENSITY · LAYER", "pads 1-8: pattern    9-16: intensity"),
                       ("AUTOMATION", "STEP · VALUE · TARGET", "pads: a point on / off")]),
        ("K6", "LINK", [("GAME LINK", "send CC 20 · 21 · 22 · 23", "A B: CC 24 25")]),
    ]
    y = 92
    d.text((30, y - 4), "THE PAGES", font=b, fill=ember)
    d.text((150, y - 2), "press a K key again for its next page  ·  encoders 1-4 are the four values named", font=sm, fill=dim)
    y += 30
    for k, name, pages in rows:
        d.rounded_rectangle([30, y, 150, y + 86], 10, fill=key[k])
        d.text((44, y + 12), k, font=big, fill=bg)
        d.text((44, y + 54), name, font=b, fill=bg)
        for i, (title, enc, note) in enumerate(pages):
            x = 166 + i * 366
            d.rounded_rectangle([x, y, x + 352, y + 86], 10, fill=panel, outline=line)
            d.text((x + 14, y + 9), title, font=b, fill=key[k])
            if len(pages) > 1:
                t = "x" + str(i + 1)
                d.text((x + 338 - d.textlength(t, font=sm), y + 11), t, font=sm, fill=dim)
            d.text((x + 14, y + 35), enc, font=sm, fill=text)
            d.text((x + 14, y + 59), note, font=sm, fill=dim)
        y += 98

    def block(x, y, w, title, items, kw=150):
        d.text((x, y), title, font=b, fill=ember)
        y += 30
        for a, t in items:
            d.rounded_rectangle([x, y, x + kw, y + 26], 6, fill=panel, outline=line)
            d.text((x + 8, y + 4), a, font=sm, fill=text)
            d.text((x + kw + 12, y + 4), t, font=sm, fill=dim)
            y += 32
        return y

    y0 = y + 16
    block(30, y0, 500, "EVERYWHERE", [
        ("PADS", "play the kit's sounds"), ("KEYS", "the selected sound, in tune"), ("DATA", "select a sound"),
        ("PLAY  STOP  REC", "the song; REC records pads and keys"), ("LEFT  RIGHT", "page before / after - on K5 pages: pattern"),
        ("A", "output volume - on SEQ and AUTO: tempo"), ("B", "SEQ: the scale - SONG: steps in a bar"),
        ("HOME", "this page's controls"), ("MENU", "the tour"), ("RETURN", "back to the first page")])
    block(560, y0, 500, "WITH SHIFT", [
        ("K1", "the other kit: SFX / MUSIC"), ("MENU  MENU", "factory reset"), ("F1", "the preset before"),
        ("an encoder", "turn fast"), ("A  (SEQ, AUTO)", "the swing"), ("a pad  (SEQ, AUTO)", "hear and select that sound"),
        ("a pad  (MIX)", "solo"), ("pad 1-8  (SONG)", "add that pattern as a bar"), ("pad 9-15  (SONG)", "take the last bar away"),
        ("pad 16  (SONG)", "no arrangement: the chain plays")], kw=170)
    block(1110, y0, 500, "F1 - F6", [
        ("sound pages", "PRESET · MUTATE · UNDO · RANDOM · EXPORT · SAVE"), ("SEQUENCER", "< PAT · PAT > · FILL · CLEAR · COPY · SAVE"),
        ("SONG", "PLAY · COPY · CLR PAT · DEMO · SEND · SAVE"), ("AUTOMATION", "< PAT · PAT > · RAMP · CLEAR · COPY · SAVE"),
        ("MIXER, MASTER", "MUTE · SOLO · RESET · ALL ON · PLAY · SAVE")], kw=130)
    block(1110, y0 + 206, 500, "A STEP", [
        ("pad", "on / off"), ("pad held + key", "its note"), ("encoder 3", "velocity 1 - 8"), ("encoder 4", "length: AUTO, 1 - 15 steps")], kw=130)
    im.save(os.path.join(OUT, "cheatsheet.png"), optimize=True)


cheat()
print("figures written to", OUT)
