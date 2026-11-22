"""Builds the Unity manual's pictures (docs/img/unity-*.png): diagrams, charts and one annotated game still.

    python docs/unity_figures.py [folder with the capture takes]

The diagrams are drawn from nothing. The charts are drawn from real data:
  - the layer marks, the tempo and the arrangement are read from unity/Demo/Assets/SfxDemo/DemoSong.bytes;
  - the curves are read from the telemetry.json of the capture takes (default folder: out/unity-video), which
    the showcase's "Capture" tests record (unity/Demo/Assets/Showcase/README.md, "Recording it");
  - the annotated still is two frames of the full-run take;
  - the pictures of the editor UI (unity-inspector-*.png) are copies of a few of the pictures the inspectors' tests
    write to out/inspector-shots (tests/unity-inspectors/README.md has the command). They are the Inspector's element
    tree and stylesheet rendered in a panel to a texture, not photographs of the Inspector window.
Without the takes, or without those pictures, only what needs neither is written, and the script says which it left
out. SFXFORGE_FIGURES=inspectors copies the editor pictures alone and leaves the charts as they are.

Needs Pillow. Same look as the cheat sheet of docs/figures.py (that script draws when it is imported, so its
font helper is repeated here instead of imported). Everything is drawn at twice the size and scaled down.
"""

import json
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# the takes: the folder given as argument, else SFXFORGE_CAPTURE_DIR (which the takes and the video script use), else
# out/unity-video
TAKES = (
    sys.argv[1]
    if len(sys.argv) > 1
    else os.environ.get("SFXFORGE_CAPTURE_DIR") or os.path.join(ROOT, "out", "unity-video")
)
OUT = os.path.join(HERE, "img")
SONG = os.path.join(ROOT, "unity", "Demo", "Assets", "SfxDemo", "DemoSong.bytes")
os.makedirs(OUT, exist_ok=True)

# the device manual's warm palette (docs/figures.py, the cheat sheet)
BG = "#0E0B09"
PANEL = "#1B1613"
PANEL2 = "#261E19"
LINE = "#3A2E26"
TEXT = "#F4EBDD"
DIM = "#A08E7E"
EMBER = "#FF8A3C"
INK = "#0E0B09"
# one colour a route, in every picture
BAKED = "#FFD23F"
LIVE = "#36C5FF"
HELD = "#7BE07B"
MUSIC = "#C48BFF"
RED = "#FF5A36"
S = 2  # drawn at twice the size
MAX_BYTES = 400 * 1024

# the level (Scripts/Core/Sections.cs and the table of SectionCues in Scripts/Audio/MusicDirector.cs); checked against
# the takes when they are there. The fifth value is the pattern the section holds: a letter, SONG_ORDER (none: the
# song plays in its own order, SectionCue.SongOrder) or KEEP (whatever is held stays held, SectionCue.KeepPattern).
# The sixth is the floor: the intensity when the player stands still.
SONG_ORDER = "song order"
KEEP = "keeps"
SECTIONS = [
    ("SoftStart", "SOFT START", 0, 90, SONG_ORDER, 0.42),
    ("RollingSines", "ROLLING SINES", 90, 220, SONG_ORDER, 0.45),
    ("FullCycle", "FULL CYCLE", 220, 340, SONG_ORDER, 0.50),
    ("WooferSteps", "WOOFER STEPS", 340, 460, SONG_ORDER, 0.50),
    ("StaticField", "STATIC FIELD", 460, 600, "E", 0.50),
    ("SplitSignal", "SPLIT SIGNAL", 600, 800, SONG_ORDER, 0.55),
    ("Overdrive", "OVERDRIVE", 800, 960, SONG_ORDER, 0.60),
    ("TheResonator", "THE RESONATOR", 960, 1000, KEEP, 0.42),
]
# Scripts/Core/DesignTargets.cs
TOP_SPEED = 22.0
CRUISE_SPEED = 10.0
RUN01 = CRUISE_SPEED / TOP_SPEED  # the player's Speed01 at a run
# the song's sounds by index (Scripts/Audio/MusicKit.cs)
# fmt: off
KIT = [
    "Timpani", "Snare", "Kick", "Cowbell", "Pizzicato", "Brass", "FM bass", "FM keys", "Harp", "Celesta", "Organ",
    "Sweep", "Hi-hat", "Bells", "Choir", "Strings",
]
# fmt: on
PATTERN_COLOUR = {
    "A": "#7BE07B",
    "B": "#36C5FF",
    "C": "#FFD23F",
    "D": "#FF8A3C",
    "E": "#C48BFF",
    "F": "#FF5A36",
    "H": "#6EE7A8",
}

_fonts = {}


def font(size, bold=True, mono=False):
    key = (size, bold, mono)
    if key not in _fonts:
        if mono:
            names = ["consolab.ttf", "DejaVuSansMono-Bold.ttf"] if bold else ["consola.ttf", "DejaVuSansMono.ttf"]
        else:
            names = (
                ["arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"]
                if bold
                else ["arial.ttf", "segoeui.ttf", "DejaVuSans.ttf"]
            )
        _fonts[key] = ImageFont.load_default()
        for name in names:
            try:
                _fonts[key] = ImageFont.truetype(name, size)
                break
            except OSError:
                pass
    return _fonts[key]


class Canvas:
    """A picture in the house style. All positions are in final pixels."""

    def __init__(self, w, h, title=None, sub=None):
        self.w, self.h = w, h
        self.im = Image.new("RGB", (w * S, h * S), BG)
        self.d = ImageDraw.Draw(self.im)
        if title:
            self.text(24, 20, title, 19, EMBER, True)
            if sub:
                self.text(24 + self.tw(title, 19, True) + 16, 23, sub, 15, DIM)

    def tw(self, s, size=15, bold=False, mono=False):
        return self.d.textlength(s, font=font(size * S, bold, mono)) / S

    def text(self, x, y, s, size=15, fill=TEXT, bold=False, anchor="la", mono=False):
        self.d.text((x * S, y * S), s, font=font(size * S, bold, mono), fill=fill, anchor=anchor)

    def line(self, pts, fill=TEXT, width=2):
        self.d.line([(x * S, y * S) for x, y in pts], fill=fill, width=int(width * S), joint="curve")

    def dash(self, x0, y0, x1, y1, fill=DIM, width=1.5, on=7, off=5):
        length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
        if length <= 0:
            return
        ux = (x1 - x0) / length
        uy = (y1 - y0) / length
        at = 0.0
        while at < length:
            end = min(at + on, length)
            self.line([(x0 + ux * at, y0 + uy * at), (x0 + ux * end, y0 + uy * end)], fill, width)
            at += on + off

    def box(self, x0, y0, x1, y1, fill=PANEL, outline=LINE, r=10, width=1):
        self.d.rounded_rectangle(
            [x0 * S, y0 * S, x1 * S, y1 * S], r * S, fill=fill, outline=outline, width=int(width * S) if outline else 0
        )

    def rect(self, x0, y0, x1, y1, fill):
        self.d.rectangle([x0 * S, y0 * S, x1 * S, y1 * S], fill=fill)

    def ellipse(self, x0, y0, x1, y1, fill=None, outline=None, width=1):
        self.d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=fill, outline=outline, width=int(width * S))

    def poly(self, pts, fill):
        self.d.polygon([(x * S, y * S) for x, y in pts], fill=fill)

    def arrow(self, x0, y0, x1, y1, fill=DIM, width=2, head=9, dashed=False):
        length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
        ux, uy = (x1 - x0) / length, (y1 - y0) / length
        bx, by = x1 - ux * head, y1 - uy * head
        if dashed:
            self.dash(x0, y0, bx, by, fill, width)
        else:
            self.line([(x0, y0), (bx, by)], fill, width)
        self.poly(
            [(x1, y1), (bx - uy * head * 0.55, by + ux * head * 0.55), (bx + uy * head * 0.55, by - ux * head * 0.55)],
            fill,
        )

    def chip(self, x, y, label, colour, size=13, pad=8, h=22, mono=False):
        """A filled label; returns the x after it."""
        w = self.tw(label, size, True, mono) + pad * 2
        self.box(x, y, x + w, y + h, fill=colour, outline=None, r=5)
        self.text(x + w / 2, y + h / 2, label, size, INK, True, "mm", mono)
        return x + w

    def tag(self, x, y, label, colour, size=13, pad=8, h=22, mono=False):
        """An outlined label; returns the x after it."""
        w = self.tw(label, size, False, mono) + pad * 2
        self.box(x, y, x + w, y + h, fill=PANEL2, outline=colour, r=5)
        self.text(x + w / 2, y + h / 2, label, size, TEXT, False, "mm", mono)
        return x + w

    def mark(self, x, y, n, colour=EMBER, r=14):
        """A numbered mark, centred on (x, y) - as in the device manual."""
        self.ellipse(x - r - 2, y - r - 2, x + r + 2, y + r + 2, fill=INK)
        self.ellipse(x - r, y - r, x + r, y + r, fill=colour)
        self.text(x, y, str(n), int(r * 1.25), INK, True, "mm")

    def paste(self, picture, x, y, w, h):
        self.im.paste(picture.convert("RGB").resize((w * S, h * S), Image.LANCZOS), (x * S, y * S))

    def legend(self, x, y, items, size=14):
        for colour, label in items:
            self.box(x, y + 2, x + 16, y + 18, fill=colour, outline=None, r=4)
            self.text(x + 24, y + 10, label, size, TEXT, False, "lm")
            x += 24 + self.tw(label, size) + 26
        return x

    def save(self, name):
        path = os.path.join(OUT, name + ".png")
        im = self.im.resize((self.w, self.h), Image.LANCZOS)
        im.save(path, optimize=True)
        if os.path.getsize(path) > MAX_BYTES:  # a game still: 256 colours are enough
            im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(path, optimize=True)
        written.append((name + ".png", im.width, im.height, os.path.getsize(path)))


written = []
left_out = []
ROUTES = [(BAKED, "baked clip"), (LIVE, "live voice"), (HELD, "held and bent"), (MUSIC, "music")]


# ---- the data ------------------------------------------------------------------------------------------------------
def read_song():
    """DemoSong.bytes: 16 patches of 28 bytes, then the packed song (unity/com.catsplat.sfxforge/Runtime/SfxSong.cs)."""
    raw = open(SONG, "rb").read()
    song = raw[16 * 28 :]
    # the head is the same in every format since the mixer: byte 1 the bar in sixteenths, 2 and 3 the tempo, 6 the
    # chain, 7.. the 16 layer marks. Format 6 (firmware 1.6.0) has no fixed length: the factory song is 2487 bytes
    # (7699 in format 5)
    layers = [song[7 + i] / 127.0 for i in range(16)]
    return {"bpm": song[2] << 7 | song[3], "steps": song[1], "chain": song[6], "layers": layers, "bytes": len(song)}


def read_director():
    """The MusicDirector's numbers the pictures print, read from its source (the serialized defaults; the scene is
    built from them)."""
    path = os.path.join(ROOT, "unity", "Demo", "Assets", "Showcase", "Scripts", "Audio", "MusicDirector.cs")
    src = open(path, encoding="utf-8").read()
    return {
        name: float(re.search(r"\bfloat %s = ([0-9.]+)f;" % name, src).group(1))
        for name in (
            "riseSeconds",
            "fallSeconds",
            "titleIntensity",
            "musicVolume",
            "hurtVolume",
            "hurtDipSeconds",
            "checkpointIntensity",
        )
    }


def read_take(name):
    path = os.path.join(TAKES, name, "telemetry.json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


song = read_song()
director = read_director()
full_run = read_take("full-run")
held_take = read_take("held-sounds")
adaptive_take = read_take("adaptive-music")
hurt_take = read_take("hurt-and-goal")
ARRANGEMENT = "ABABCDCDEEABCDCF"  # the demo song's 16 bars
if full_run:  # the tables above must be what the game recorded
    assert "".join("ABCDEFGH"[p] for p in full_run["song"]["arrangement"]) == ARRANGEMENT, "the arrangement changed"
    for (ident, _, x0, x1, hold, floor), s in zip(SECTIONS, full_run["sections"]):
        recorded_hold = {-2: SONG_ORDER, -1: KEEP}.get(s["pattern"], s["letter"])
        assert (ident, x0, x1, hold) == (s["id"], s["startX"], s["endX"], recorded_hold) and abs(
            floor - s["intensityFloor"]
        ) < 1e-6, ("a section changed: " + ident)
    for i, mark in enumerate(song["layers"]):
        assert abs(mark - full_run["song"]["instruments"][i]["layer"]) < 0.001, "a layer mark changed"


# ---- 1. the three routes -------------------------------------------------------------------------------------------
def routes():
    c = Canvas(1400, 668, "THREE ROUTES", "how a patch becomes sound in Unity")

    # a column a route: its colour, its name, what it is, its four steps (code, note), a caption, the showcase's
    # sounds that take it
    cols = [
        (
            BAKED,
            "1  BAKED CLIP",
            "rendered once, played as an ordinary clip",
            [
                ("SfxPatch  /  SfxPreset", "the sound: 28 bytes"),
                ("preset.Bake(sampleRate, midiNote: 76)", "once, at load"),
                ("AudioClip", "stereo, with its chorus, delay and reverb"),
                ("AudioSource.PlayOneShot(clip, volume)", "every time"),
            ],
            "The cheapest. Always the same. Works in 3D.",
            "Jump  ·  Land  ·  Footsteps  ·  Woofer  ·  11 more",
        ),
        (
            LIVE,
            "2  LIVE VOICE",
            "synthesized each time it plays",
            [
                ("SfxPreset", "the sound: 28 bytes"),
                ("SfxPlayer.Play(velocity, semitones, midiNote)", "every time"),
                ("the engine, while the note sounds", "up to 16 notes of one patch"),
                ("AudioSource", "the one beside the SfxPlayer; no clip"),
            ],
            "Pitch and loudness chosen at each trigger.",
            "Chime  ·  ChimeScatter",
        ),
        (
            HELD,
            "3  HELD AND BENT",
            "one note, moved while it sounds",
            [
                ("a held patch", "SUSTAIN above 0"),
                ("SfxPlayer.Play(1, 0, midiNote)", "when it starts"),
                ("pitchSemitones  cutoffOctaves  fm  level", "written every frame"),
                ("SfxPlayer.Release()", "when it ends: it fades over RELEASE"),
            ],
            "Follows the game for as long as it lasts.",
            "RollHum  ·  Wind  ·  WindUpRev",
        ),
    ]

    # each column: its panel and heading, the steps as boxes joined by arrows, the caption and the examples
    for i, (colour, name, what, steps, caption, examples) in enumerate(cols):
        x0 = 24 + i * 458
        x1 = x0 + 436
        c.box(x0, 62, x1, 648, fill=PANEL, outline=LINE, r=12)
        c.rect(x0 + 1, 74, x0 + 6, 112, colour)
        c.text(x0 + 20, 76, name, 19, colour, True)
        c.text(x0 + 20, 101, what, 14, DIM)
        y = 136
        for n, (code, note) in enumerate(steps):
            c.box(
                x0 + 20,
                y,
                x1 - 20,
                y + 66,
                fill=PANEL2,
                outline=colour if n == len(steps) - 1 or (i == 2 and n == 2) else LINE,
                r=8,
            )
            c.text(x0 + 34, y + 12, code, 14 if len(code) > 40 else 15, TEXT, True, mono=True)
            c.text(x0 + 34, y + 39, note, 14, DIM)
            if n < len(steps) - 1:
                c.arrow((x0 + x1) / 2, y + 68, (x0 + x1) / 2, y + 92, colour, 2.5, 10)
            y += 94
        c.text(x0 + 20, 526, caption, 15, TEXT, True)
        c.line([(x0 + 20, 560), (x1 - 20, 560)], LINE, 1)
        c.text(x0 + 20, 574, "IN THE SHOWCASE", 12, DIM, True)
        c.text(x0 + 20, 598, examples, 15, colour, True)
    c.save("unity-routes")


# ---- 2. a held sound, bent -----------------------------------------------------------------------------------------
def held():
    if not held_take:
        left_out.append("unity-held.png (take held-sounds)")
        return
    frames = held_take["frames"]
    starts = [s["f"] for s in held_take["sounds"] if s["sound"] == "RollHum" and s["cause"] == "is held"]
    ends = [s["f"] for s in held_take["sounds"] if s["sound"] == "RollHum" and s["cause"] == "is let go"]
    # the long roll through the loop
    play = starts[-1]
    release = ends[-1]
    assert release > play + 60, "the take held-sounds no longer ends with a long roll"
    # after Release the SfxPlayer still sounds for the patch's RELEASE; the take may end before it has faded
    faded = next((f["f"] for f in frames[release:] if not f["held"]["RollHum"]["sounding"]), len(frames) - 1)
    f0, f1 = max(0, play - 45), min(len(frames) - 1, faded + 12)
    t0 = frames[f0]["t"]
    c = Canvas(
        1400,
        720,
        "A HELD SOUND, BENT",
        "the showcase's roll hum over one roll, as recorded (take held-sounds, 30 frames a second)",
    )
    x0, x1 = 250, 1250

    def X(f):
        return x0 + (x1 - x0) * (f - f0) / (f1 - f0)

    rows = [
        ("speed01", "the player's speed, 0 … 1", lambda f: f["speed01"], 0, 1, TEXT, "what the game reads"),
        ("pitchSemitones", "0 … +12", lambda f: f["held"]["RollHum"]["pitchSemitones"], 0, 12, HELD, "speed01 × 12"),
        (
            "cutoffOctaves",
            "−1 … +1.5",
            lambda f: f["held"]["RollHum"]["cutoffOctaves"],
            -1,
            1.5,
            HELD,
            "Lerp(−1, 1.5, speed01)",
        ),
        ("level", "0.35 … 1", lambda f: f["held"]["RollHum"]["level"], 0.35, 1, HELD, "Lerp(0.35, 1, speed01)"),
    ]
    top = 118
    rh = 104
    gap = 26
    bottom = top + len(rows) * (rh + gap) - gap
    c.rect(X(play), top - 12, X(release), bottom + 8, "#16201A")  # held
    c.rect(X(release), top - 12, X(faded), bottom + 8, "#1A1A14")  # ringing out
    for n, (name, rng, get, lo, hi, colour, how) in enumerate(rows):
        y0 = top + n * (rh + gap)
        y1 = y0 + rh
        c.text(x0 - 18, y0 + 26, name, 16, colour, True, "ra", mono=True)
        c.text(x0 - 18, y0 + 50, rng, 14, DIM, False, "ra")
        c.text(x0 - 18, y0 + 72, how, 13, DIM, False, "ra")
        c.line([(x0, y0), (x1, y0)], LINE, 1)
        c.line([(x0, y1), (x1, y1)], LINE, 1)
        c.text(x1 + 10, y0, ("%g" % hi) if hi != 12 else "+12", 13, DIM, False, "lm")
        c.text(x1 + 10, y1, "%g" % lo, 13, DIM, False, "lm")

        def Y(v, y0=y0, y1=y1, lo=lo, hi=hi):
            return y1 - (y1 - y0) * (max(lo, min(hi, v)) - lo) / (hi - lo)

        if n == 0:
            c.line([(X(f["f"]), Y(get(f))) for f in frames[f0 : f1 + 1]], colour, 2.5)
        else:  # only written while the player rolls
            c.line([(X(f["f"]), Y(get(f))) for f in frames[f0 : play + 1]], LINE, 2)
            c.line([(X(f["f"]), Y(get(f))) for f in frames[play : release + 1]], colour, 3)
            c.line([(X(f["f"]), Y(get(f))) for f in frames[release : f1 + 1]], "#4E7A4E", 2)
        peak = max(frames[play : release + 1], key=get)
        if n > 0:
            c.text(
                X(peak["f"]),
                Y(get(peak)) + 18,
                ("+" if n < 3 else "") + ("%.2f" % get(peak)).rstrip("0").rstrip(".") + " at top speed",
                13,
                colour,
                True,
                "ma",
            )
    for f, label, sub, colour, anchor in (
        (play, "Play(1, 0, 40)", "rolling starts: hold the note E2", HELD, "la"),
        (release, "Release()", "rolling ends", EMBER, "ra"),
    ):
        c.line([(X(f), top - 34), (X(f), bottom + 8)], colour, 2)
        dx = 8 if anchor == "la" else -8
        c.text(X(f) + dx, top - 54, label, 16, colour, True, anchor, mono=True)
        c.text(X(f) + dx, top - 32, sub, 13, DIM, False, anchor)
    c.text(X(release) + 8, top - 52, "then the patch's", 13, DIM)
    c.text(X(release) + 8, top - 34, "RELEASE: it rings out", 13, DIM)
    c.line([(X(faded), top - 12), (X(faded), bottom + 8)], LINE, 1)
    for k in range(int(frames[f0]["t"]) + 1, int(frames[f1]["t"]) + 1):  # a tick a second
        f = f0 + round((k - t0) * 30)
        c.line([(X(f), bottom + 8), (X(f), bottom + 14)], DIM, 1)
        c.text(X(f), bottom + 18, "%d s" % (k - int(frames[play]["t"])), 13, DIM, False, "ma")
    c.text(
        x0,
        676,
        "The three live values are written every frame while the player rolls. The player glides to each new value "
        "over 30 ms, so there are no steps.",
        14,
        DIM,
    )
    c.text(
        x0,
        696,
        "After Release the game stops writing: the values stay where they were while the patch's RELEASE fades the "
        "note.",
        14,
        DIM,
    )
    c.save("unity-held")


# ---- 3. layers against speed ---------------------------------------------------------------------------------------
def layers():
    marks = song["layers"]
    # the kick, then every sound with a mark
    used = [2] + sorted((i for i in range(16) if marks[i] > 0), key=lambda i: (marks[i], KIT[i] != "Strings"))
    family = {"Kick": RED, "Snare": RED, "Hi-hat": "#7BE07B", "FM bass": LIVE, "FM keys": LIVE}
    floors = {}
    for _, name, _, _, _, floor in SECTIONS:
        floors.setdefault(floor, []).append(name.title())
    x0 = 190
    x1 = 1300
    y0 = 84
    row = 40
    rrow = 36
    bottom = y0 + len(used) * row
    ry0 = bottom + 78
    H = ry0 + len(floors) * rrow + 66
    c = Canvas(
        1400,
        H,
        "LAYERS AGAINST SPEED",
        "the demo song's LAYER marks, and where the showcase's sections and speed put the intensity",
    )

    def X(v):
        return x0 + (x1 - x0) * v

    def up_to(v):
        """The band at intensity v, named by the last sounds to have joined."""
        top = max(m for m in marks if m <= v + 1e-9)
        return " and ".join(KIT[i] if KIT[i].startswith("FM") else KIT[i].lower() for i in used if marks[i] == top)

    for i, idx in enumerate(used):
        y = y0 + i * row
        c.text(x0 - 16, y + 17, KIT[idx].upper(), 16, TEXT, True, "rm")
        c.box(x0, y + 4, x1, y + 30, fill="#17120F", outline=None, r=6)
    for i, idx in enumerate(used):  # each mark, down to the sections' rows
        if marks[idx] > 0:
            c.dash(X(marks[idx]), y0 + i * row + 17, X(marks[idx]), ry0 + len(floors) * rrow - 6, "#54443A", 1, 3, 4)
    for i, idx in enumerate(used):
        y = y0 + i * row
        name = KIT[idx]
        mark = marks[idx]
        c.box(X(mark), y + 4, x1, y + 30, fill=family.get(name, MUSIC), outline=None, r=6)
        c.text(X(mark) + 10, y + 17, "from %.2f" % mark if mark > 0 else "always", 14, INK, True, "lm")
    c.line([(x0, bottom + 12), (x1, bottom + 12)], LINE, 1)
    for k in range(0, 11):
        v = k / 10
        c.line([(X(v), bottom + 12), (X(v), bottom + 18)], DIM, 1)
        if k % 2 == 0:
            c.box(X(v) - 16, bottom + 21, X(v) + 16, bottom + 38, fill=BG, outline=None, r=0)
            c.text(X(v), bottom + 22, "%.1f" % v, 13, DIM, False, "ma")
    c.text(x0 - 16, bottom + 30, "intensity", 13, DIM, False, "rm")
    # a row for each floor: where the intensity is with the player standing still, at a run and at top speed
    c.text(24, ry0 - 22, "IN EACH SECTION", 12, DIM, True)
    for n, (floor, names) in enumerate(sorted(floors.items())):
        y = ry0 + n * rrow
        run = floor + (1 - floor) * RUN01  # Mathf.Lerp(floor, 1, Speed01)
        c.text(24, y + 22, ", ".join(names), 13, TEXT, False, "lm")
        c.line([(X(floor), y + 22), (x1, y + 22)], EMBER, 3)
        c.ellipse(X(floor) - 6, y + 16, X(floor) + 6, y + 28, fill=EMBER)
        c.line([(X(run), y + 12), (X(run), y + 32)], TEXT, 3)
        c.line([(x1, y + 14), (x1, y + 30)], EMBER, 3)
        label = "standing still: %.2f, up to the %s" % (floor, up_to(floor))
        c.box(X(floor) - 16 - c.tw(label, 13), y + 13, X(floor) - 10, y + 31, fill=BG, outline=None, r=0)
        c.text(X(floor) - 14, y + 22, label, 13, EMBER, False, "rm")
        label = "at a run: %.2f, up to the %s" % (run, up_to(run))
        c.box(X(run) + 6, y + 2, X(run) + 12 + c.tw(label, 13, True), y + 18, fill=BG, outline=None, r=0)
        c.text(X(run) + 8, y + 10, label, 13, TEXT, True, "lm")
        if n == 0:
            c.text(x1 + 10, y + 22, "top speed: 1", 13, EMBER, False, "lm")
    c.text(
        24,
        H - 46,
        "A sound plays while the intensity is at or above its mark. The showcase sets intensity = Lerp(floor, 1, "
        "Speed01): the floor of the section the player is in when",
        14,
        DIM,
    )
    c.text(
        24,
        H - 26,
        "standing still, 1 at top speed (%.0f m/s); a run is %.0f m/s, Speed01 %.2f. The marks are read from "
        "DemoSong.bytes. The kit's other seven sounds have mark 0 and no notes in the song."
        % (TOP_SPEED, CRUISE_SPEED, RUN01),
        14,
        DIM,
    )
    c.text(x1 - 300, 14, "here a colour is a kind of instrument, not a route:", 12, DIM)
    c.legend(x1 - 300, 34, [(RED, "drums"), ("#7BE07B", "hi-hat"), (LIVE, "FM"), (MUSIC, "wavetable")], 13)
    c.save("unity-layers")


# ---- 4. intensity follows speed, and a hit -------------------------------------------------------------------------
def intensity():
    if not adaptive_take or not hurt_take:
        left_out.append("unity-intensity.png (takes adaptive-music and hurt-and-goal)")
        return
    c = Canvas(1400, 800, "INTENSITY FOLLOWS SPEED", "the MusicDirector's values, as recorded")
    marks = sorted(set(round(m, 3) for m in song["layers"] if m > 0))
    names = {m: " + ".join(KIT[i] for i in range(16) if round(song["layers"][i], 3) == m) for m in marks}

    def panel(take, f0, f1, x0, x1, y0, y1, title, floor, with_layers):
        frames = take["frames"][f0 : f1 + 1]
        c.text(x0, y0 - 32, title, 15, TEXT, True)

        def X(f):
            return x0 + (x1 - x0) * (f - f0) / (f1 - f0)

        def Y(v):
            return y1 - (y1 - y0) * v

        c.box(x0, y0, x1, y1, fill="#120E0C", outline=None, r=0)
        for v in (0, 0.25, 0.5, 0.75, 1):
            c.line([(x0, Y(v)), (x1, Y(v))], LINE, 1)
            c.text(x0 - 8, Y(v), "%g" % v, 13, DIM, False, "rm")
        if with_layers:
            for m in marks:
                c.dash(x0, Y(m), x1, Y(m), "#5A4A6E", 1, 3, 5)
                c.text(x1 + 8, Y(m), names[m], 13, MUSIC, False, "lm")
        c.dash(x0, Y(floor), x1, Y(floor), EMBER, 1.5)
        c.line([(X(f["f"]), Y(f["speed01"])) for f in frames], TEXT, 1.5)
        c.line([(X(f["f"]), Y(f["music"]["intensity"])) for f in frames], EMBER, 3.5)
        for k in range(0, int((f1 - f0) / 30) + 1, 2 if f1 - f0 > 330 else 1):
            c.line([(X(f0 + k * 30), y1), (X(f0 + k * 30), y1 + 6)], DIM, 1)
            c.text(X(f0 + k * 30), y1 + 9, "%d s" % k, 13, DIM, False, "ma")
        return X, Y

    def floor_of(frame, take):
        return next(x["intensityFloor"] for x in take["sections"] if x["id"] == frame["section"])

    # walking, running, a Wind-Up Burst, standing: up fast, down slowly
    frames = adaptive_take["frames"]
    floor = floor_of(frames[0], adaptive_take)
    assert all(
        f["section"] == frames[0]["section"] for f in frames
    ), "the take adaptive-music no longer stays in one section"
    X, Y = panel(
        adaptive_take,
        0,
        len(frames) - 1,
        70,
        1180,
        100,
        400,
        "Standing, walking, running, a Wind-Up Burst, standing again  (take adaptive-music, in %s)"
        % frames[0]["sectionName"].title(),
        floor,
        True,
    )
    c.text(X(8), Y(floor) + 8, "the section's floor, %.2f: the intensity standing still" % floor, 13, EMBER)
    burst = next(e["f"] for e in adaptive_take["events"] if e["name"] == "BurstReleased")
    # the run before the burst: the last frame of its steady top speed, where the intensity has arrived
    fastest = max(f["speed01"] for f in frames[:burst])
    steady = [f for f in frames[:burst] if f["speed01"] >= fastest - 0.005]
    run = steady[-1]
    assert (
        abs(run["music"]["intensity"] - (floor + (1 - floor) * run["speed01"])) < 0.01
    ), "the intensity at a run is no longer Lerp(floor, 1, Speed01)"
    c.text(
        X(steady[0]["f"]) - 4,
        Y(run["music"]["intensity"]) - 44,
        "running: Speed01 %.2f" % run["speed01"],
        13,
        TEXT,
        True,
    )
    c.text(
        X(steady[0]["f"]) - 4,
        Y(run["music"]["intensity"]) - 26,
        "intensity Lerp(%.2f, 1, %.2f) = %.2f" % (floor, run["speed01"], run["music"]["intensity"]),
        13,
        EMBER,
        True,
    )
    top = max(frames[burst:], key=lambda f: f["music"]["intensity"])
    c.text(X(burst) - 10, Y(0.62), "up: %g s" % director["riseSeconds"], 14, EMBER, True, "ra")
    c.text(X(burst) - 10, Y(0.62) + 19, "for the whole range", 13, DIM, False, "ra")
    stop = next(f["f"] for f in frames[top["f"] :] if f["speed01"] <= 0.001)
    c.text(X(stop) + 34, Y(0.70), "down: %g s" % director["fallSeconds"], 14, EMBER, True)
    c.text(X(stop) + 34, Y(0.70) + 19, "for the whole range", 13, DIM)
    assert top["music"]["intensity"] >= max(
        song["layers"]
    ), "the burst of the take adaptive-music no longer brings in the whole band"
    c.text(
        X(top["f"]) + 6,
        Y(top["music"]["intensity"]) - 20,
        "%.2f: the whole band" % top["music"]["intensity"],
        13,
        TEXT,
        True,
    )

    # a hit
    hit = next(e["f"] for e in hurt_take["events"] if e["name"] == "Hurt")
    f0, f1 = hit - 105, hit + 120
    at_hit = hurt_take["frames"][hit]
    floor = floor_of(at_hit, hurt_take)
    X, Y = panel(
        hurt_take,
        f0,
        f1,
        70,
        1180,
        478,
        650,
        "A hit  (take hurt-and-goal, in %s)" % at_hit["sectionName"].title(),
        floor,
        False,
    )
    c.line([(X(hit), 470), (X(hit), 760)], RED, 2)
    c.text(X(hit) + 10, 486, "Hurt", 15, RED, True)
    assert at_hit["music"]["intensity"] < min(
        m for m in song["layers"] if m > 0
    ), "a hit no longer takes the intensity under every mark"
    c.text(X(hit) + 10, 506, "intensity to 0 at once: only the kick is left", 13, TEXT)
    c.text(X(f1) - 8, Y(floor) + 6, "the section's floor, %.2f" % floor, 13, EMBER, False, "ra")
    vy0, vy1 = 690, 760
    c.box(70, vy0, 1180, vy1, fill="#120E0C", outline=None, r=0)
    hframes = hurt_take["frames"][f0 : f1 + 1]
    full = director["musicVolume"]
    assert abs(hframes[0]["music"]["volume"] - full) < 1e-4, "the take was not recorded at MusicDirector's musicVolume"

    def V(v):
        return vy1 - (vy1 - vy0) * v / (full * 7 / 6)

    for v in (0, full / 2, full):
        c.line([(70, V(v)), (1180, V(v))], LINE, 1)
        c.text(62, V(v), "%g" % v, 13, DIM, False, "rm")
    c.line([(X(f["f"]), V(f["music"]["volume"])) for f in hframes], MUSIC, 3)
    low = min(hframes, key=lambda f: f["music"]["volume"])
    back = next(f["f"] for f in hurt_take["frames"][low["f"] :] if f["music"]["volume"] >= full - 1e-4)
    c.text(
        X(hit) + 10,
        V(full * 0.14),
        "volume %.2f → %.2f in %.0f ms, back in %.1f s"
        % (full, low["music"]["volume"], director["hurtDipSeconds"] * 1000, (back - low["f"]) / 30),
        13,
        TEXT,
    )
    c.text(1188, (vy0 + vy1) / 2, "music volume", 13, MUSIC, False, "lm")
    c.legend(820, 22, [(EMBER, "music intensity"), (TEXT, "the player's Speed01")], 13)
    c.text(1188, 76, "joins at its mark:", 13, DIM)
    c.save("unity-intensity")


# ---- 5. cues -------------------------------------------------------------------------------------------------------
def cues():
    recorded = []
    if full_run:
        bars = {}
        for f in full_run["frames"]:
            m = f["music"]
            if m["playing"] and m["bar"] >= 0:
                bars.setdefault(m["bar"], (m["letter"], f["sectionName"], f["phase"]))
        recorded = [bars[b] for b in sorted(bars)]
    H = 716 if recorded else 560
    c = Canvas(1400, H, "CUEING A PATTERN", "what CuePattern does in a song with an arrangement")
    x0 = 300
    w = 62
    h = 46

    def cell(x, y, letter, cw=w, ch=h, under=None, dim=False, size=22):
        colour = PATTERN_COLOUR[letter]
        c.box(x + 2, y, x + cw - 2, y + ch, fill=PANEL2 if dim else colour, outline=colour, r=6)
        c.text(x + cw / 2, y + ch / 2, letter, size, colour if dim else INK, True, "mm")
        if under is not None:
            c.text(x + cw / 2, y + ch + 5, str(under), 12, DIM, False, "ma")

    def label(y, title, sub, code=False):
        c.text(24, y + 6, title, 15, TEXT, True, mono=code)
        for n, s in enumerate(sub):
            c.text(24, y + 28 + n * 18, s, 13, DIM)

    def cue(x, y, text=None, above=False):
        c.arrow(x, y - 26, x, y - 3, EMBER, 2.5, 9)
        if text:
            c.text(x + (-4 if above else 8), y - (40 if above else 26), text, 13, EMBER, True, "lm")

    y = 84
    label(y, "The arrangement", ["the demo song: 16 bars,", "then round again"])
    for i, letter in enumerate(ARRANGEMENT):
        cell(x0 + i * w, y, letter, under=i + 1, dim=True)
    c.text(x0 + 16 * w + 12, y + 23, "bar", 12, DIM, False, "lm")

    y = 214
    assert (
        ARRANGEMENT.count("F") == 1 and ARRANGEMENT[15] == "F"
    ), "the example cues F, which the arrangement has once, as its last bar"
    label(y, "CuePattern(5)", ["once, during bar 2:", "here the cue lasts one bar"], True)
    row = [("A", 1), ("B", 2), ("F", 16)] + [(ARRANGEMENT[i], i + 1) for i in range(0, 11)]
    for i, (letter, bar) in enumerate(row):
        cell(x0 + i * w, y, letter, under=bar, dim=i < 2)
    cue(x0 + 2 * w, y, "the next bar line: on to the arrangement's next F, bar 16 …")
    c.text(x0 + 14 * w + 12, y + 14, "… and the arrangement", 13, DIM, False, "lm")
    c.text(x0 + 14 * w + 12, y + 32, "walks on from there", 13, DIM, False, "lm")

    y = 346
    label(y, "CuePattern(4)", ["again whenever Bar changes:", "the pattern E is held"], True)
    for i in range(16):
        letter = "AB"[i] if i < 2 else "E"
        cell(x0 + i * w, y, letter, under=(1, 2)[i] if i < 2 else 9 + i % 2, dim=i < 2)
        if i >= 2:
            cue(x0 + i * w, y, "a cue is waiting at every bar line" if i == 2 else None, True)

    y = 466
    label(y, "CuePattern(7)", ["H is not in the arrangement:", "it loops until the next cue"], True)
    for i in range(16):
        cell(x0 + i * w, y, "AB"[i] if i < 2 else "H", dim=i < 2)
    cue(x0 + 2 * w, y, "one cue")

    if recorded:
        y = 606
        letters = "".join(letter for letter, _, _ in recorded)

        def bar_of(name, section=None):  # the bar an event fell in
            e = next(
                e
                for e in full_run["events"]
                if e["name"] == name and (section is None or full_run["frames"][e["f"]]["section"] == section)
            )
            return full_run["frames"][e["f"]]["music"]["bar"]

        # a cue takes the next bar line, and the hold's last cue was made at the start of the bar in which the hold
        # ended
        holds = [
            (
                bar_of("SectionEntered", "StaticField") + 1,
                bar_of("SectionEntered", "SplitSignal") + 1,
                "E",
                "E held: Static Field",
            ),
            (bar_of("FinalLaunch") + 1, len(recorded) - 1, "F", "F held: FinalLaunch"),
        ]
        at = 0  # the place in the arrangement
        for i, letter in enumerate(letters):  # the picture says: the song's own order, but for two holds
            hold = next((h for h in holds if h[0] <= i <= h[1]), None)
            if hold:
                assert letter == hold[2], "bar %d of the full run is not the held %s" % (i, hold[2])
                if i == hold[1]:  # the arrangement walks on from the last held bar
                    at = max(k for k in range(16) if ARRANGEMENT[k] == hold[2]) + 1
            else:
                assert letter == ARRANGEMENT[at % 16], "bar %d of the full run is not in the song's order" % i
                at += 1
        label(
            y - 8,
            "The showcase's run",
            ["as recorded (take full-run):", "%d bars in the song's own order," % len(recorded), "but for two holds"],
        )
        cw = (16 * w) / len(recorded)
        last = None
        for i, (letter, section, phase) in enumerate(recorded):
            held = any(a <= i <= b for a, b, _, _ in holds)
            cell(x0 + i * cw, y, letter, cw, 34, size=13, dim=not held)
            name = "title" if phase == "Title" else section.title()
            if name != last:
                c.line([(x0 + i * cw, y - 8 if name != "title" else y - 24), (x0 + i * cw, y + 40)], DIM, 1)
                short = {"The Resonator": ""}.get(name, name)
                c.text(x0 + i * cw + 4, y + 44 if name != "title" else y - 24, short, 12, DIM)
                last = name
        for a, b, letter, text in holds:
            colour = PATTERN_COLOUR[letter]
            xa, xb = x0 + a * cw + 2, x0 + (b + 1) * cw - 2
            c.line([(xa, y - 5), (xa, y - 10), (xb, y - 10), (xb, y - 5)], colour, 2)
            right = xb > x0 + 16 * w - 60
            c.text(xb if right else xa, y - 28, text, 12, colour, True, "ra" if right else "la")
    c.text(
        24,
        H - 28,
        "A bar is 2 seconds (120 BPM, 16 sixteenths). The numbers under the bars are the arrangement's bars. Rows 2 "
        "to 4 follow the engine's rules (core/sfx_song.h).",
        13,
        DIM,
    )
    c.save("unity-cues")


# ---- 6. the link ---------------------------------------------------------------------------------------------------
def link():
    c = Canvas(1400, 640, "THE LINK", "two MIDI ports between the device and Unity (Windows only)")
    c.box(24, 70, 284, 610, fill=PANEL, outline=EMBER, r=14, width=2)
    c.text(154, 100, "SFX FORGE", 22, EMBER, True, "ma")
    for n, s in enumerate(["the hardware,", "the browser Playground,", "or the plugin"]):
        c.text(154, 134 + n * 20, s, 14, DIM, False, "ma")
    c.box(1116, 70, 1376, 610, fill=PANEL, outline=LIVE, r=14, width=2)
    c.text(1246, 100, "UNITY", 22, LIVE, True, "ma")
    for n, s in enumerate(
        ["the SFX Forge package", "SfxLink.Shared", "Window > SFX Forge"]
        # no package id: the figure is also in the standalone package
    ):
        c.text(1246, 134 + n * 20, s, 14, DIM, False, "ma")

    def lane(y, title, colour, rows, to_unity):
        c.text(700, y, title, 14, colour, True, "ma")
        for n, (left, wire, right) in enumerate(rows):
            yy = y + 44 + n * 44
            if to_unity:
                c.arrow(292, yy, 1108, yy, colour, 2, 10)
            else:
                c.arrow(1108, yy, 292, yy, colour, 2, 10)
            c.text(300, yy - 10, left, 14, TEXT, to_unity, "ls")
            c.text(1100, yy - 10, right, 14, TEXT, not to_unity, "rs", mono=False)
            tw = c.tw(wire, 13) + 16
            c.box(700 - tw / 2, yy - 11, 700 + tw / 2, yy + 11, fill=BG, outline=None, r=4)
            c.text(700, yy, wire, 13, DIM, False, "mm")
        return y + 44 + len(rows) * 44

    y = lane(
        84,
        "DEVICE  →  UNITY",
        EMBER,
        [
            ("F5 EXPORT on a sound page", "a patch, SysEx", "PatchReceived  →  an SfxPreset"),
            ("F5 SEND on the SONG page", "16 patches + the song in pieces", "SongReceived  →  an SfxSong"),
            ("the pads", "notes 36 – 51", "PadDown  ·  SfxForgeLink.onPad"),
            ("LINK page: encoders 1 – 4, A, B", "CC 20 – 25", "EncoderChanged  ·  SfxEncoderBinding"),
        ],
        True,
    )
    tw = c.tw("receiving in Unity: not tested on hardware", 13, True) + 20
    c.box(700 - tw / 2, y - 26, 700 + tw / 2, y, fill=PANEL2, outline=RED, r=6)
    c.text(700, y - 13, "receiving in Unity: not tested on hardware", 13, RED, True, "mm")
    lane(
        y + 30,
        "UNITY  →  DEVICE",
        LIVE,
        [
            ("loads a pad  ·  replaces kit and song", "SysEx", "SendPatch  ·  SendSong"),
            ("plays a pad", "notes 36 – 51", "PlayPad  ·  ReleasePad"),
            ("shows it on its top line", "SysEx, 24 characters", "SendText"),
            (
                "bends the held sound: pitch, cutoff, timbre, level",
                "CC 10 – 13, channel 2",
                "SendTelemetry  ·  SfxTelemetrySender",
            ),
            ("the music: intensity, cue a pattern", "CC 14, CC 15, channel 2", "SendIntensity  ·  CuePattern"),
        ],
        False,
    )
    c.save("unity-link")


# ---- 7. the showcase, with the route of everything you hear --------------------------------------------------------
# The two frames of the full run the marks were placed on by hand: the first frame at which the player has reached
# this x, rolling on the ground. A new recording may differ by a frame; if it differs by more, the marks point at
# nothing.
ROUTE_FRAMES = [(240.1, 17.8, "FULL CYCLE"), (342.2, 13.1, "WOOFER STEPS")]  # x in metres, speed in m/s, the section


def showcase_routes():
    if not full_run:
        left_out.append("unity-showcase-routes.png (take full-run)")
        return
    chosen = []
    for x, speed, section in ROUTE_FRAMES:
        f = next(f for f in full_run["frames"] if f["phase"] == "Playing" and f["x"] >= x)
        assert (
            f["x"] - x < 1.0
            and abs(f["speed"] - speed) < 1.0
            and f["rolling"]
            and f["grounded"]
            and f["sectionName"] == section
        ), (
            "the full run changed near x = %.0f m (frame %d): look at unity-showcase-routes.png and place its marks "
            "again" % (x, f["f"])
        )
        chosen.append(f)
    paths = [os.path.join(TAKES, "full-run", "f%05d.png" % f["f"]) for f in chosen]
    if not all(os.path.isfile(p) for p in paths):
        left_out.append(
            "unity-showcase-routes.png (take full-run, frames %d and %d)" % (chosen[0]["f"], chosen[1]["f"])
        )
        return
    W = 1400
    h1 = 788
    crop = (150, 560)
    gap = 46
    foot = 44
    h2 = crop[1] - crop[0]
    c = Canvas(W, h1 + gap + h2 + foot + gap)
    a, b = Image.open(paths[0]), Image.open(paths[1])
    k = b.width / W
    c.paste(a, 0, 0, W, h1)
    y2 = h1 + gap
    c.paste(b.crop((0, int(crop[0] * k), b.width, int(crop[1] * k))), 0, y2, W, h2)

    def strip(y, f):
        m = f["music"]
        r = f["held"]["RollHum"]
        wd = f["held"]["Wind"]
        s = (
            "frame %d of the full run   ·   %.1f m/s = %.0f %% of top speed   ·   intensity %.2f   ·   pattern %s   "
            "·   RollHum +%.1f semitones, level %.2f   ·   Wind level %.2f"
            % (
                f["f"],
                f["speed"],
                f["speed01"] * 100,
                m["intensity"],
                m["letter"],
                r["pitchSemitones"],
                r["level"],
                wd["level"],
            )
        )
        c.text(W / 2, y + gap / 2, s, 14, DIM, False, "mm")

    strip(h1, chosen[0])
    strip(y2 + h2, chosen[1])
    for n, (x, y, colour) in enumerate(
        [(596, 574, HELD), (438, 462, HELD), (560, 452, LIVE), (800, 668, LIVE), (272, 732, MUSIC), (1098, 50, MUSIC)],
        1,
    ):
        c.mark(x, y, n, colour, 16)
    for n, (x, y, colour) in enumerate([(276, 240, BAKED), (184, 300, MUSIC), (1246, 286, BAKED), (590, 300, HELD)], 7):
        c.mark(x, y2 + y, n, colour, 16)
    c.legend(W / 2 - 250, y2 + h2 + gap + 10, ROUTES, 15)
    c.save("unity-showcase-routes")


# ---- 8. the event seam ---------------------------------------------------------------------------------------------
def seam():
    c = Canvas(1400, 660, "THE EVENT SEAM", "audio listens and reads; it never changes the game")
    c.box(24, 70, 380, 600, fill=PANEL, outline=LINE, r=14)
    c.text(44, 88, "GAMEPLAY AND LEVEL", 17, TEXT, True)
    c.text(44, 114, "never reference SFX FORGE", 14, DIM)
    for n, (name, what) in enumerate(
        [
            ("PlayerController", "jumps, lands, steps, rolls"),
            ("GameSession", "starts, respawns, counts"),
            ("Chime", "is collected"),
            ("Woofer", "throws the player"),
            ("TuningFork", "is a checkpoint"),
            ("Clipper · StaticBurr", "hurt"),
            ("SectionTrigger", "a new section"),
            ("Resonator", "the goal"),
        ]
    ):
        y = 152 + n * 46
        c.box(44, y, 360, y + 38, fill=PANEL2, outline=LINE, r=7)
        c.text(56, y + 19, name, 14, TEXT, True, "lm", mono=True)
        c.text(348, y + 19, what, 13, DIM, False, "rm")
    c.text(44, 544, "Scripts/Gameplay · Scripts/Level", 13, DIM)
    c.text(44, 566, "they call GameEvents.Raise…()", 13, DIM)

    c.box(470, 70, 800, 462, fill=PANEL, outline=EMBER, r=14, width=2)
    c.text(635, 88, "GameEvents", 19, EMBER, True, "ma", mono=True)
    c.text(635, 116, "19 C# events, in Scripts/Core", 14, DIM, False, "ma")
    # fmt: off
    events = [
        "Jumped", "Landed(impactSpeed)", "Footstep(speed01)", "ChimeCollected(combo)", "Hurt",
        "CheckpointReached(index)", "SectionEntered(section)", "GoalReached",
    ]
    # fmt: on
    for n, e in enumerate(events):
        c.text(490, 150 + n * 31, e, 15, TEXT, False, mono=True)
        c.line([(490, 174 + n * 31), (780, 174 + n * 31)], LINE, 1)
    c.text(490, 150 + len(events) * 31 + 6, "… and 11 more", 14, DIM)
    c.box(470, 486, 800, 600, fill=PANEL, outline=HELD, r=14, width=1)
    c.text(635, 500, "IPlayerState", 17, HELD, True, "ma", mono=True)
    c.text(635, 526, "no event: read every frame", 14, DIM, False, "ma")
    c.text(635, 552, "Speed01 · IsRolling · IsGrounded", 14, TEXT, False, "ma", mono=True)
    c.text(635, 574, "IsCharging · Charge01", 14, TEXT, False, "ma", mono=True)

    c.box(890, 70, 1376, 600, fill=PANEL, outline=LINE, r=14)
    c.text(910, 88, "SCRIPTS/AUDIO", 17, TEXT, True)
    c.text(910, 114, "the only runtime assembly that references CatSplat.SfxForge", 14, DIM)
    listeners = [
        ("SoundEffects", "one-shots", BAKED, [("AudioSource.PlayOneShot", BAKED), ("SfxPlayer", LIVE)], 152),
        ("MusicDirector", "the music", MUSIC, [("SfxMusicPlayer", MUSIC)], 300),
        ("ContinuousSounds", "held sounds", HELD, [("SfxPlayer", HELD)], 448),
    ]
    for name, what, colour, targets, y in listeners:
        c.box(910, y, 1356, y + 124, fill=PANEL2, outline=colour, r=10)
        c.text(928, y + 14, name, 17, colour, True, mono=True)
        c.text(928, y + 42, what, 14, DIM)
        c.text(928, y + 74, "plays through", 13, DIM)
        x = 928
        for label, col in targets:
            x = c.chip(x, y + 92, label, col, 13, mono=True) + 8
    for y in (214, 362):
        c.arrow(388, y - 50 if y == 214 else y - 40, 462, y - 50 if y == 214 else y - 40, EMBER, 3, 12)
    c.arrow(808, 214, 902, 214, EMBER, 3, 12)
    c.arrow(808, 362, 902, 362, EMBER, 3, 12)
    c.text(425, 142, "raise", 13, EMBER, True, "ma")
    c.text(855, 192, "listen", 13, EMBER, True, "ma")
    c.arrow(388, 543, 462, 543, HELD, 2.5, 11, dashed=True)
    c.arrow(808, 530, 902, 510, HELD, 2.5, 11, dashed=True)
    c.arrow(808, 505, 902, 400, HELD, 2.5, 11, dashed=True)
    c.text(425, 520, "state", 13, HELD, True, "ma")
    c.arrow(808, 440, 902, 476, EMBER, 3, 12)
    c.text(866, 546, "read", 13, HELD, True, "ma")
    c.text(
        24,
        622,
        "Every arrow points to the right. Delete Scripts/Audio (and the editor and test files that name it) and the "
        "game is the same game, silent.",
        14,
        DIM,
    )
    c.save("unity-seam")


# ---- 9. the level, its sections and their patterns -----------------------------------------------------------------
def level_strip():
    H = 746 if full_run else 350
    c = Canvas(
        1400,
        H,
        "THE LEVEL AND ITS MUSIC",
        "1000 m, eight sections: each sets a floor under the intensity; the song plays in its own order but for two "
        "holds",
    )
    x0, x1 = 60, 1340

    def X(m):
        return x0 + (x1 - x0) * m / 1000.0

    y0, y1 = 100, 250
    for n, (_, name, a, b, hold, floor) in enumerate(SECTIONS):
        colour = PATTERN_COLOUR.get(hold, DIM)
        c.box(X(a) + 2, y0, X(b) - 2, y1, fill=PANEL, outline=colour, r=8)
        c.rect(X(a) + 3, y0 + 1, X(b) - 3, y0 + 6, colour)
        mid = (X(a) + X(b)) / 2
        narrow = X(b) - X(a) < 90
        c.text(
            mid if not narrow else X(b), y0 - 12, name, 12 if narrow else 13, TEXT, True, "ms" if not narrow else "rs"
        )
        if hold == SONG_ORDER:
            c.text(mid, y0 + 64, "song order", 16, DIM, True, "mm")
        elif hold == KEEP:
            for k, word in enumerate(("keeps", "what is", "held")):
                c.text(mid, y0 + 46 + k * 18, word, 12, DIM, False, "mm")
        else:
            c.text(mid, y0 + 52, hold, 54, colour, True, "mm")
            c.text(mid, y0 + 92, "held", 14, colour, True, "mm")
        c.text(mid, y0 + 122, ("%.2f" if narrow else "floor %.2f") % floor, 14, TEXT, False, "mm")
        c.text(X(a), y1 + 8, str(a), 13, DIM, False, "ma")
    c.text(X(1000), y1 + 8, "1000 m", 13, DIM, False, "ma")
    c.text(
        x0,
        y1 + 34,
        "The floor is the music's intensity when the player stands still in the section; speed fills the rest of "
        "the way to 1. The song walks through its 16 bars by itself,",
        13,
        DIM,
    )
    c.text(
        x0,
        y1 + 52,
        "round and round. Static Field holds pattern E (cued again on every bar). FinalLaunch, in Overdrive, holds "
        "F until the goal: The Resonator keeps what is held.",
        13,
        DIM,
    )
    if not full_run:
        left_out.append("the recorded curves of unity-level-strip.png (take full-run)")
        c.save("unity-level-strip")
        return

    frames = [f for f in full_run["frames"] if f["phase"] == "Playing"]
    py0, py1 = 364, 386
    gy0, gy1 = 396, 646

    def Y(v):
        return gy1 - (gy1 - gy0) * v

    c.text(
        x0,
        py0 - 32,
        "One run, as recorded (take full-run: the test pilot, %.0f s)" % frames[-1]["runSeconds"],
        15,
        TEXT,
        True,
    )
    # the bars of the song, against the distance (the furthest point so far, as for the curves below)
    runs = []
    far = -1.0
    for f in frames:
        if f["x"] > far and f["music"]["playing"]:
            far = f["x"]
            x = min(1000, max(0, far))
            key = (f["music"]["bar"], f["music"]["letter"])
            if runs and runs[-1][0] == key:
                runs[-1][2] = x
            else:
                runs.append([key, runs[-1][2] if runs else x, x])
    for (bar, letter), a, b in runs:
        colour = PATTERN_COLOUR[letter]
        c.box(X(a) + 1, py0, max(X(a) + 2, X(b) - 1), py1, fill=PANEL2, outline=colour, r=3)
        if X(b) - X(a) >= 13:
            c.text((X(a) + X(b)) / 2, (py0 + py1) / 2, letter, 12, colour, True, "mm")
    c.text(x0 - 8, (py0 + py1) / 2, "bar", 13, DIM, False, "rm")
    c.box(x0, gy0, x1, gy1, fill="#120E0C", outline=None, r=0)
    for v in (0, 0.25, 0.5, 0.75, 1):
        c.line([(x0, Y(v)), (x1, Y(v))], LINE, 1)
        c.text(x0 - 8, Y(v), "%g" % v, 13, DIM, False, "rm")
    for _, _, a, b, hold, floor in SECTIONS:
        c.line([(X(a), gy0), (X(a), gy1)], LINE, 1)
        c.dash(X(a), Y(floor), X(b), Y(floor), EMBER, 1.5)

    # against the distance: keep the furthest point so far, so a loop (where x runs back) does not scribble
    def curve(get):
        pts = []
        far = -1.0
        for f in frames:
            if f["x"] > far:
                far = f["x"]
                pts.append((X(min(1000, max(0, f["x"]))), Y(get(f))))
        return pts

    c.line(curve(lambda f: f["speed01"]), TEXT, 1.2)
    c.line(curve(lambda f: f["music"]["intensity"]), EMBER, 3)
    notes = []
    for e in full_run["events"]:
        if e["name"] in ("CheckpointReached", "FinalLaunch", "GoalReached"):
            notes.append((e["name"], full_run["frames"][e["f"]]["x"]))
    for name, x in notes:
        if name == "CheckpointReached":
            c.line([(X(x), gy0), (X(x), gy1 + 8)], MUSIC, 1.5)
            c.text(X(x) + 5, gy1 + 12, "Tuning Fork: bells, lift to %g" % director["checkpointIntensity"], 12, MUSIC)
        elif name == "FinalLaunch":
            c.line([(X(x), py0 - 6), (X(x), gy1 + 30)], PATTERN_COLOUR["F"], 2)
            c.text(
                X(x) - 6,
                gy1 + 34,
                "FinalLaunch at %.0f m: pattern F from the next bar" % x,
                13,
                PATTERN_COLOUR["F"],
                True,
                "ra",
            )
        else:
            c.line([(X(x), py0 - 6), (X(x), gy1 + 54)], TEXT, 2)
            c.text(
                X(x) - 6,
                gy1 + 58,
                "the goal at %.0f m: the song stops, the fanfare is played by hand" % x,
                13,
                TEXT,
                True,
                "ra",
            )
    c.legend(x0, gy1 + 34, [(EMBER, "music intensity"), (TEXT, "the player's Speed01")], 13)
    c.dash(x0, gy1 + 68, x0 + 16, gy1 + 68, EMBER, 1.5, 5, 3)
    c.text(x0 + 24, gy1 + 68, "the section's floor: the intensity standing still", 13, TEXT, False, "lm")
    c.save("unity-level-strip")


# ---- 10. what to copy ----------------------------------------------------------------------------------------------
def copy_map():
    # fmt: off
    audio = [
        "SoundLibrary", "GameSound", "SoundRoute", "SoundDefinition", "SoundBinding", "Pitch", "SoundEffects",
        "LiveVoicePool", "ContinuousSounds", "HeldSound", "MusicDirector", "SectionCue", "MusicNote", "MusicKit",
        "AudioInstaller", "PresetFactory", "BuildProbe",
    ]
    core = [
        "GameEvents", "PlayerBase", "IPlayerState", "IPlayerActor", "IPlayerInput", "TerrainPlane", "GameSessionBase",
        "IGameSession", "GamePhase", "SectionId",
    ]
    # fmt: on
    for folder, names in (("Audio", audio), ("Core", core)):  # the picture must not lie about the folder
        d = os.path.join(ROOT, "unity", "Demo", "Assets", "Showcase", "Scripts", folder)
        on_disk = {n[:-3] for n in os.listdir(d) if n.endswith(".cs")}
        assert set(names) <= on_disk, "missing in Scripts/%s: %s" % (folder, sorted(set(names) - on_disk))
        if folder == "Audio":
            assert set(names) == on_disk, "Scripts/Audio changed: %s" % sorted(on_disk ^ set(names))
        else:
            core_total = len(on_disk)
    song_asset = os.path.join(ROOT, "unity", "Demo", "Assets", "Showcase", "Song", "DemoSong.asset")
    assert os.path.isfile(song_asset) and os.path.isfile(song_asset + ".meta"), "the demo song moved"
    c = Canvas(1400, 720, "COPY THIS INTO YOUR GAME", "the audio folder, the ten files of Core it needs, and a song")
    c.box(24, 66, 800, 696, fill=PANEL, outline=LINE, r=14)
    c.box(900, 66, 1376, 696, fill=PANEL, outline=LINE, r=14)
    c.text(44, 82, "THE SHOWCASE", 15, DIM, True)
    c.text(920, 82, "YOUR GAME", 15, DIM, True)

    def tree(x, y, depth, name, colour=DIM, bold=False, note=None):
        c.text(x + depth * 22, y, name, 15, colour, bold, mono=True)
        if note:
            c.text(x + depth * 22 + c.tw(name, 15, bold, True) + 14, y + 1, note, 13, DIM)

    def files(x, y, names, colour, cols=2, cw=180):
        rows = (len(names) + cols - 1) // cols
        for i, n in enumerate(names):
            c.text(x + (i // rows) * cw, y + (i % rows) * 21, n + ".cs", 14, colour, False, mono=True)
        return y + rows * 21

    y = 112
    tree(44, y, 0, "Showcase/", TEXT, True, "the showcase's folder")
    tree(44, y + 26, 1, "Scripts/", TEXT, True)
    c.box(84, y + 52, 780, y + 264, fill="#241A12", outline=EMBER, r=10)
    tree(54, y + 62, 2, "Audio/", EMBER, True, "all %d scripts, and SfxShowcase.Audio.asmdef" % len(audio))
    files(120, y + 90, audio, TEXT, 3, 215)
    c.text(120, y + 232, "The whole sound of the game. The only runtime code that names CatSplat.SfxForge.", 13, DIM)
    c.box(84, y + 278, 780, y + 432, fill="#12202A", outline=LIVE, r=10)
    tree(54, y + 288, 2, "Core/", LIVE, True, "ten of its %d files: the seam the audio listens to" % core_total)
    files(120, y + 316, core, TEXT, 3, 215)
    c.text(120, y + 402, "Or none of them: put your own events and values in their place.", 13, DIM)
    tree(44, y + 448, 2, "Art/   Gameplay/   Level/", DIM, False, "the game itself: not needed")
    tree(
        44,
        y + 476,
        1,
        "Editor/   Generated/   Tests/   Docs/",
        DIM,
        False,
        "the builder, generated assets, tests, notes",
    )
    tree(44, y + 504, 1, "Showcase.unity", DIM)
    tree(44, y + 532, 1, "Song/DemoSong.asset", EMBER, True, "the song, with its .meta")
    c.legend(44, y + 564, [(EMBER, "copy"), (LIVE, "copy, or replace with your own"), (LINE, "leave")], 13)

    y = 112
    tree(920, y, 0, "Packages/", TEXT, True)
    tree(920, y + 26, 1, "manifest.json", TEXT)
    c.text(964, y + 52, "the SFX Forge package is in its list", 13, DIM)
    tree(920, y + 92, 0, "Assets/", TEXT, True)
    c.box(940, y + 118, 1356, y + 262, fill="#241A12", outline=EMBER, r=10)
    tree(932, y + 128, 1, "Audio/", EMBER, True, "the %d scripts" % len(audio))
    c.text(954, y + 160, "its .asmdef references", 13, DIM)
    c.text(954, y + 182, "CatSplat.SfxForge", 14, TEXT, True, mono=True)
    c.text(954, y + 204, "and the assembly the Core files are in", 13, DIM)
    c.text(954, y + 232, "(in the showcase: SfxShowcase.Core)", 13, DIM)
    c.box(940, y + 278, 1356, y + 392, fill="#12202A", outline=LIVE, r=10)
    tree(932, y + 288, 1, "Core/", LIVE, True, "the ten files")
    c.text(954, y + 320, "Your game raises the events:", 13, DIM)
    c.text(954, y + 342, "GameEvents.RaiseJumped();", 14, TEXT, False, mono=True)
    c.text(954, y + 364, "and its player derives from PlayerBase", 13, DIM)
    tree(920, y + 406, 1, "DemoSong.asset", EMBER, True, "MusicKit and the cues fit this song")
    tree(920, y + 430, 1, "… your game", DIM)
    c.text(920, y + 462, "In a scene:", 13, DIM)
    c.text(920, y + 482, "AudioInstaller.Install(player, session,", 14, TEXT, False, mono=True)
    c.text(920, y + 502, "                       song, presetFactory)", 14, TEXT, False, mono=True)
    c.text(920, y + 526, "builds the Audio object and wires it. It needs", 13, DIM)
    c.text(920, y + 546, "a PlayerBase, a GameSessionBase and the SfxSong;", 13, DIM)
    c.text(920, y + 566, "presetFactory may be null.", 13, DIM)
    c.arrow(788, 270, 932, 290, EMBER, 4, 15)
    c.arrow(788, 468, 932, 448, LIVE, 4, 15)
    c.save("unity-copy")


# ---- 11. the Audio object ------------------------------------------------------------------------------------------
def audio_tree():
    c = Canvas(1400, 560, "THE AUDIO OBJECT", "as AudioInstaller.Install builds it: every object and its components")
    nodes = [
        (0, "Audio", None, [], "the root; nothing on it"),
        (
            1,
            "Music",
            MUSIC,
            ["AudioSource", "SfxMusicPlayer", "MusicDirector"],
            "the SfxSong; playOnEnable on; startPattern 0",
        ),
        (
            1,
            "Effects",
            BAKED,
            ["AudioSource", "LiveVoicePool", "SoundEffects"],
            "the 19 sound bindings; bakes 14 clips in Awake",
        ),
        (2, "Footsteps", BAKED, ["AudioSource"], "a second source: its pitch follows the speed"),
        (2, "Live Voice 1, 2, 3", LIVE, ["AudioSource", "SfxPlayer"], "no preset: the pool gives one at each trigger"),
        (1, "Continuous", HELD, ["ContinuousSounds"], "the three players below, and each one's MIDI note"),
        (2, "Roll Hum", HELD, ["AudioSource", "SfxPlayer"], "its held preset; playOnEnable off"),
        (2, "Wind", HELD, ["AudioSource", "SfxPlayer"], "its held preset; playOnEnable off"),
        (2, "Wind-Up Rev", HELD, ["AudioSource", "SfxPlayer"], "its held preset; playOnEnable off"),
    ]
    x0 = 40
    y0 = 84
    row = 46
    ind = 44
    ys = [y0 + i * row for i in range(len(nodes))]
    for i, (depth, name, colour, comps, note) in enumerate(nodes):  # the tree's lines
        if depth == 0:
            continue
        parent = max(j for j in range(i) if nodes[j][0] == depth - 1)
        px = x0 + (depth - 1) * ind + 14
        c.line([(px, ys[parent] + 34), (px, ys[i] + 17), (x0 + depth * ind - 4, ys[i] + 17)], LINE, 2)
    for i, (depth, name, colour, comps, note) in enumerate(nodes):
        x, y = x0 + depth * ind, ys[i]
        w = 230
        c.box(x, y, x + w, y + 34, fill=PANEL2, outline=colour or DIM, r=8, width=1.5)
        if colour:
            c.rect(x + 1, y + 6, x + 5, y + 28, colour)
        c.text(x + 16, y + 17, name, 15, TEXT, True, "lm")
        cx = 390
        for comp in comps:
            own = comp not in ("AudioSource",)
            cx = (
                c.chip(cx, y + 6, comp, colour, 13, mono=True) if own else c.tag(cx, y + 6, comp, LINE, 13, mono=True)
            ) + 8
        c.text(850, y + 17, note, 14, DIM, False, "lm")
    c.text(390, 62, "COMPONENTS", 12, DIM, True)
    c.text(850, 62, "WHAT THE INSTALLER SETS", 12, DIM, True)
    c.legend(40, 512, ROUTES, 14)
    c.text(40 + 640, 522, "Every AudioSource: 2D, no clip, playOnAwake off.", 14, DIM, False, "lm")
    c.save("unity-audio-tree")


# ---- the editor UI: a few of the pictures the inspectors' tests took of it -----------------------------------------
# docs/img name -> the picture in out/inspector-shots (<what>-<width in pixels>.png). Written by the EditMode tests of
# tests/unity-inspectors; each is the element tree and stylesheet the Inspector hosts, rendered in a panel to a
# texture. Nobody photographed the Inspector window itself. The window's pictures show a model the test filled in
# (ports named VIBE FM, patches, messages): no device was connected.
SHOTS = os.path.join(ROOT, "out", "inspector-shots")
INSPECTOR_SHOTS = [
    # the sound inspector, page OSC (the showcase's Jump)
    ("unity-inspector-sound-osc", "sound-osc-640"),
    # page ENV of a held sound, with HOLD (the showcase's RollHum)
    ("unity-inspector-sound-env", "sound-env-640"),
    # page TONE at 340 pixels: the soft keys wrap
    ("unity-inspector-sound-narrow", "sound-tone-340"),
    # the song inspector, page STEPS (the demo song)
    ("unity-inspector-song", "song-640"),
    # the same page of a song with finer patterns: B at 32 steps, C and F at 64
    ("unity-inspector-song-fine", "song-32-640"),
    # page MIX
    ("unity-inspector-song-mix", "song-mix-640"),
    # page AUTO, pattern F
    ("unity-inspector-song-auto", "song-auto-640"),
    # Window > SFX Forge, shown a filled-in model
    ("unity-inspector-window", "window-700"),
    # the same window, not connected
    ("unity-inspector-window-off", "window-off-700"),
    # the sound inspector's tools: NEW, after a click on ZAP
    ("unity-inspector-tools-new", "sound-tools-new-640"),
    # VARY: MORE LIKE THIS with FREQ locked
    ("unity-inspector-tools-vary", "sound-tools-vary-640"),
    # LEVEL: three sounds after MATCH RMS
    ("unity-inspector-tools-level", "sound-tools-level-640"),
    # SHARE: the sound as text
    ("unity-inspector-tools-share", "sound-tools-share-640"),
    # the Inspector of a variation set, baked
    ("unity-inspector-variation-set", "variation-set-640"),
    # Window > SFX Forge Bake, shown a filled-in model
    ("unity-inspector-bake-window", "bake-window-700"),
]
SHOT_MAX_BYTES = 300 * 1024


def inspectors():
    missing = [shot for _, shot in INSPECTOR_SHOTS if not os.path.isfile(os.path.join(SHOTS, shot + ".png"))]
    if missing:  # all or none: the manual shows them side by side
        left_out.append(
            "the editor pictures (unity-inspector-*.png): run the tests of tests/unity-inspectors first; missing in "
            "out/inspector-shots: " + ", ".join(missing)
        )
        return
    for name, shot in INSPECTOR_SHOTS:
        im = Image.open(os.path.join(SHOTS, shot + ".png")).convert("RGB")  # as it is: not scaled, not cropped
        path = os.path.join(OUT, name + ".png")
        im.save(path, optimize=True)
        assert os.path.getsize(path) <= SHOT_MAX_BYTES, "%s.png is larger than %d KB" % (name, SHOT_MAX_BYTES // 1024)
        written.append((name + ".png", im.width, im.height, os.path.getsize(path)))


# fmt: off
FIGURES = [
    routes, held, layers, intensity, cues, link, showcase_routes, seam, level_strip, copy_map, audio_tree, inspectors,
]
# fmt: on
# SFXFORGE_FIGURES=link,copy_map draws only those (the others need the captured takes); without it, all of them
ONLY = [n.strip() for n in os.environ.get("SFXFORGE_FIGURES", "").split(",") if n.strip()]
unknown = sorted(set(ONLY) - {f.__name__ for f in FIGURES})
assert not unknown, "SFXFORGE_FIGURES names no figure: %s" % unknown
for figure in FIGURES:
    if not ONLY or figure.__name__ in ONLY:
        figure()

for name, w, h, size in written:
    print("%-28s %4d x %-4d %6.1f KB%s" % (name, w, h, size / 1024.0, "   TOO LARGE" if size > MAX_BYTES else ""))
for what in left_out:
    if what.startswith("the editor pictures"):
        print("left out, " + what)
    else:
        print("left out, no capture found in %s: %s" % (TAKES, what))
print("%d figures written to %s" % (len(written), OUT))
