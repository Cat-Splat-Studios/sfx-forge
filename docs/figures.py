"""Builds the manual's pictures (docs/img/) from screenshots of the firmware.

    node docs/shots.mjs                    takes the screenshots, into out/doc (after sh build.sh)
    python docs/figures.py [folder]        the folder with the screenshots; out/doc unless another is named

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
annotated("sfx-sound", "page-sound", [(200, 17), (308, 17), (384, 17), (130, 44), (354, 44), (240, 104), (178, 87), (238, 304)])
annotated("seq-playing", "page-seq", [(306, 17), (384, 17), (218, 44), (128, 114), (250, 150), (250, 256), (440, 276), (238, 304)])
annotated("song", "page-song", [(306, 17), (128, 114), (128, 230), (250, 150), (250, 256), (238, 304)])
annotated("lanes-auto", "page-auto", [(296, 44), (128, 172), (250, 125), (250, 276), (238, 304)])
annotated("mix", "page-mix", [(128, 172), (250, 125), (250, 258), (80, 304)])
annotated("master", "page-master", [(90, 125), (318, 125), (178, 87), (302, 87)])
annotated("link", "page-link", [(180, 75), (250, 150), (250, 258)])
annotated("seq-playing-stage", "stage", [(128, 11), (192, 11), (344, 11), (454, 14), (449, 46), (14, 180), (328, 316), (200, 345), (14, 414), (14, 458), (14, 510),
                                         (458, 240), (947, 240), (947, 434), (947, 458), (458, 502), (947, 491), (774, 49)], scale=1, r=11)
annotated("auto-stage", "stage-auto", [(459, 414), (14, 458), (947, 206), (947, 491)], scale=1, r=11)

# ---- loop a pattern and play into it: four moments ----------------------------------------------------------------------
annotated("loop-1", "loop-1", [(306, 17), (80, 304), (250, 276)])                      # LOOP in the top line, F1, the pattern strip
annotated("loop-2", "loop-2", [(380, 17), (248, 44), (250, 276)])                      # A>G, EDIT G - PLAYS A, G blinks
annotated("loop-3", "loop-3", [(384, 17), (128, 114), (250, 256), (238, 304)])         # the REC dot, the hits, the footer, F4 ERASE
annotated("loop-4", "loop-4", [(176, 17), (92, 304)])                                  # UNDO: TAKE, F2

# ---- a finer grid: 1/32, the second 16 steps, 1/64, the question, a take, the SONG page, the stage ------------------------
annotated("grid-1", "grid-1", [(218, 44), (128, 114), (250, 80), (250, 256)])           # 1/32 in the title, the 16 steps shown, STEP, the bar strip
annotated("grid-2", "grid-2", [(182, 17), (128, 230), (250, 256)])                     # STEPS 17-32, the roll, the frame in the bar strip
plain("grid-3")                                                                        # 1/64: the roll on every other step
plain("grid-4")                                                                        # AGAIN: G DROPS 4
annotated("grid-take", "grid-take", [(128, 114), (250, 80), (250, 256), (468, 262)])   # the tiles go with the bar, STEP stays, the strip, the red hint
plain("grid-song")                                                                     # 32 in the corner of G
annotated("grid-stage", "stage-grid", [(774, 49), (947, 338)], scale=1, r=11)          # the GRID chip, a row of 32 cells

# ---- the tutorials: a picture a step ---------------------------------------------------------------------------------
for n in ("t-sfx-1", "t-sfx-2", "t-sfx-3", "t-sfx-4", "t-beat-1", "t-beat-2", "t-beat-3", "t-beat-4", "t-beat-5", "env", "lfo", "fx", "music-wave", "touch", "lanes-sound"):
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

    # the display: a picture with a value in each corner
    d.rounded_rectangle([110, 56, 470, 296], 8, fill="#0E0B09", outline="#3A4252", width=2)
    d.rounded_rectangle([122, 66, 152, 86], 4, fill="#36C5FF")
    d.text((160, 66), "LASER", font=f, fill=TEXT)
    d.text((122, 92), "SOUND  OSC", font=small, fill="#36C5FF")
    for i in range(6):
        d.rounded_rectangle([372 + i * 15, 95, 382 + i * 15, 102], 2, fill="#36C5FF" if i == 0 else "#3A2E26")
    d.rounded_rectangle([122, 112, 458, 262], 6, fill="#1B1613")
    d.polygon([(130, 187), (132, 150), (250, 178), (450, 186), (450, 188), (250, 196), (132, 224)], fill="#36C5FF")
    for cx, cy in ((128, 118), (366, 118), (128, 226), (366, 226)):
        d.rectangle([cx, cy, cx + 86, cy + 30], fill="#0E0B09")
        d.ellipse([cx + 4, cy + 5, cx + 24, cy + 25], outline="#36C5FF", width=2)
    for i in range(6):
        d.rounded_rectangle([122 + i * 57, 270, 174 + i * 57, 288], 9, fill="#36241C")
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
    W, H = 1640, 1734
    bg, panel, line, text, dim, ember = "#0E0B09", "#1B1613", "#3A2E26", "#F4EBDD", "#A08E7E", "#FF8A3C"
    key = {"K1": "#C48BFF", "K2": "#FFD23F", "K3": "#7BE07B", "K4": "#36C5FF", "K5": "#FF5A36", "K6": "#6EE7A8"}
    im = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(im)
    big, f, b, sm = font(34), font(17, False), font(17), font(14, False)
    d.text((30, 22), "SFX FORGE", font=big, fill=text)
    d.text((250, 36), "cheat sheet  -  every page, every shortcut", font=f, fill=dim)
    d.text((W - 30 - d.textlength("two kits of 16 sounds: SFX and MUSIC  ·  SHIFT + ENTER changes kit", font=sm), 38),
           "two kits of 16 sounds: SFX and MUSIC  ·  SHIFT + ENTER changes kit", font=sm, fill=dim)
    rows = [
        ("K1", "SOUND", [("OSC", "ENGINE · FREQ · PITCH ENV · ENV TIME", "picture: the whole sound"), ("TONE", "A · B · NOISE · CRUSH", "A B: the engine's own two (below)"),
                         ("FILTER", "CUTOFF · RESO · FILTER ENV · F.ENV TIME", "picture: the filter's curve"), ("WAVE", "TABLE · SPREAD", "wavetable sounds only")]),
        ("K2", "ENV", [("ENVELOPE", "ATTACK · DECAY · SUSTAIN · RELEASE", "SUSTAIN off: a one-shot. Above: held")]),
        ("K3", "LFO", [("LFO", "RATE · > PITCH · > FILTER · > TIMBRE", "")]),
        ("K4", "FX", [("EFFECT SENDS", "CHORUS · DELAY · REVERB · LEVEL", "of the selected sound"),
                      ("MIXER", "VOLUME · PAN · DRIVE · OUTPUT", "pads: mute    SHIFT + pad: solo"),
                      ("MASTER EFFECTS", "TIME · FEEDBACK · ROOM SIZE · DAMPING", "of the delay and the reverb")]),
        ("K5", "SEQ", [("SEQUENCER", "STEP · NOTE · VELOCITY · LENGTH", "pads: 16 steps of the bar    REC lit: the kit"),
                       ("SONG", "PATTERN · CHAIN · INTENSITY · LAYER", "pads 1-8: pattern    9-16: intensity"),
                       ("AUTOMATION", "STEP · VALUE · TARGET", "pads: points    opens on the knob turned last")]),
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

    # (beside the short rows: how the display is laid out)
    def screen(x, y, title, note):
        d.rounded_rectangle([x, y, x + 168, y + 110], 8, fill=panel, outline=line)
        d.text((x + 184, y + 6), title, font=b, fill=text)
        for n, t in enumerate(note):
            d.text((x + 184, y + 34 + n * 22), t, font=sm, fill=dim)

    def ring(x, y, n):
        d.ellipse([x - 11, y - 11, x + 11, y + 11], outline=ember, width=2)
        d.text((x - d.textlength(str(n), font=sm) / 2, y - 8), str(n), font=sm, fill=text)

    sx, sy = 540, 266
    d.text((sx, sy - 34), "THE DISPLAY", font=b, fill=ember)
    d.text((sx + 130, sy - 32), "a page is one of two kinds", font=sm, fill=dim)
    screen(sx, sy, "A PICTURE", ["the sound pages, MASTER", "the four values in its corners:", "1 2 on top, 3 4 below.", "A knob you turn shows large"])
    d.polygon([(sx + 34, sy + 55), (sx + 36, sy + 34), (sx + 134, sy + 53), (sx + 134, sy + 57), (sx + 36, sy + 76)], fill="#36C5FF")
    for n, (cx, cy) in enumerate([(sx + 18, sy + 18), (sx + 150, sy + 18), (sx + 18, sy + 92), (sx + 150, sy + 92)], 1):
        ring(cx, cy, n)
    sx += 500
    screen(sx, sy, "THE PADS", ["SEQ, SONG, AUTO, MIX", "16 tiles, laid out as the pads.", "The four values in a column,", "1 at the top"])
    for n in range(16):
        d.rounded_rectangle([sx + 8 + (n % 4) * 24, sy + 8 + (n // 4) * 24, sx + 28 + (n % 4) * 24, sy + 28 + (n // 4) * 24], 3,
                            fill="#36C5FF" if n in (0, 3, 5, 7, 8, 10, 13, 14) else "#35251D")
    for n in range(4):
        ring(sx + 134, sy + 19 + n * 24, n + 1)

    def block(x, y, title, items, kw=150):
        d.text((x, y), title, font=b, fill=ember)
        y += 30
        for a, t in items:
            d.rounded_rectangle([x, y, x + kw, y + 26], 6, fill=panel, outline=line)
            d.text((x + 8, y + 4), a, font=sm, fill=text)
            d.text((x + kw + 12, y + 4), t, font=sm, fill=dim)
            y += 32
        return y

    y0 = y + 16
    block(30, y0, "EVERYWHERE", [
        ("PADS", "play the kit's sounds"), ("KEYS", "the selected sound, in tune"), ("DATA", "select a sound"),
        ("PLAY  STOP", "what the K5 pages' top line says: LOOP or SONG"), ("LEFT  RIGHT", "page before / after - on K5 pages: pattern"),
        ("A", "output volume - K5 pages: tempo - LINK: CC 24"), ("B", "SEQ: the scale - SONG: the bar in 16ths"),
        ("HOME", "this page's controls"), ("MENU", "the tour"), ("RETURN", "back to the first page")])
    block(30, y0 + 366, "LOOP, SONG, REC  -  the K5 pages", [
        ("F1", "LOOP: the pattern repeats.  SONG: the arrangement"), ("LEFT  RIGHT", "another pattern: it starts on the bar line"),
        ("REC + PLAY", "play it in: pads the kit, keys the notes. It loops"), ("F4 held  (REC)", "ERASE the selected sound as the bar passes"),
        ("F2", "UNDO one step: edits, a take, a key. Again: redo")])
    block(560, y0, "WITH SHIFT", [
        ("ENTER", "the other kit: SFX / MUSIC"), ("REC", "the metronome off / on"), ("MENU  MENU", "factory reset"), ("F1  (sound pages)", "the preset before"),
        ("an encoder", "turn fast"), ("A  (K5 pages)", "the swing"),
        ("F5  F6  (SEQ, AUTO)", "the pattern's grid: 1/16, 1/32, 1/64"), ("LEFT  RIGHT", "SEQ, AUTO: the 16 steps before / after"), ("a pad  (SEQ, AUTO)", "hear and select that sound"),
        ("a pad  (MIX)", "solo"), ("pad 1-8  (SONG)", "add that pattern as a bar"), ("pad 9-15  (SONG)", "take the last bar away"),
        ("pad 16  (SONG)  x2", "no arrangement: the chain plays"), ("F4  (SONG)  x2", "the demo song")], kw=170)
    y1 = block(1110, y0, "F1 - F6", [
        ("sound pages", "PRESET · MUTATE · UNDO · RANDOM · EXPORT · SAVE"), ("SEQUENCER", "LOOP/SONG · UNDO · FILL · CLEAR · COPY · SAVE"),
        ("SONG", "LOOP/SONG · UNDO · CLR PAT · NEW x2 · SEND · SAVE"), ("AUTOMATION", "LOOP/SONG · UNDO · RAMP · CLEAR · COPY · SAVE"),
        ("MIXER, MASTER", "MUTE · SOLO · RESET · ALL ON · PLAY · SAVE")], kw=130)
    block(1110, y1 + 16, "A STEP", [
        ("pad", "on / off  (REC not lit)"), ("pad held + key", "its note"), ("encoder 2", "its note, along the scale"), ("encoder 3", "velocity 1 - 8"), ("encoder 4", "length: AUTO, or in steps")], kw=130)

    y2 = y0 + 580
    y3 = block(30, y2, "THE FIVE ENGINES  -  what A and B are", [
        ("IMPACT", "FM RATIO · FM DEPTH      hits, kicks, explosions"), ("UI BLIP", "ARP STEP · ARP SPEED     coins, menus, chiptune"),
        ("SCI-FI FM", "FM RATIO · FM DEPTH      lasers, FM bass and keys"), ("FOLEY", "GRAIN RATE · GRAIN AMT   steps, wind, hi-hats"),
        ("WAVETABLE", "POSITION · WARP          pads, strings, brass")], kw=110)
    block(30, y3 + 16, "AUTOMATE A KNOB", [
        ("1", "turn the knob, on its own page (MUSIC kit, MIX or MASTER)"), ("2", "K5 until AUTO: TARGET names that knob"),
        ("3", "pads: a point on a step.  Encoder 2: its value"), ("4", "F3 RAMP: straight lines from point to point"),
        ("5", "TARGET to another knob: a second lane. 24 a song")], kw=30)
    block(560, y2, "THE STAGE  -  with the mouse", [
        ("LOOP / SONG", "what PLAY plays: click to change"), ("click a pad", "play and select"), ("drag a value", "or the wheel over it (SHIFT: bigger steps)"), ("click a step", "on / off"),
        ("wheel on a step", "its note.  SHIFT: velocity.  CTRL: length"), ("click a row's name", "hear and select that sound"),
        ("right-click it", "mute"), ("right-click a pattern", "add a bar of it to the arrangement"), ("right-click a bar", "the arrangement ends before it"),
        ("AUTO tab", "the rows become lanes: drag in one to draw"), ("GRID", "click: finer.  right-click: coarser"),
        ("SFX / MUSIC", "the kit the pads show")], kw=170)
    y4 = block(1110, y2, "FROM A GAME  -  MIDI, channel 2", [
        ("CC 10", "pitch, up to +24 semitones"), ("CC 11", "cutoff, 64 = none, +-4 octaves"), ("CC 12", "timbre: FM depth, wavetable position"),
        ("CC 13", "level"), ("CC 14", "INTENSITY: the layers come in"), ("CC 15", "cue a pattern (value / 16), on the bar line"), ("notes 36-51", "play the pads")], kw=110)
    block(1110, y4 + 16, "KEEP AND SEND", [
        ("F6 SAVE", "both kits and the song, on the device"), ("F5 EXPORT", "the selected sound, to the game"), ("SONG: F5 SEND", "the MUSIC kit and the song, to the game")], kw=130)
    im.save(os.path.join(OUT, "cheatsheet.png"), optimize=True)


cheat()
print("figures written to", OUT)
