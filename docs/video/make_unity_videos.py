"""Makes the two videos of SFX FORGE in Unity from takes the showcase game recorded of itself - no screen recording.

    python docs/video/make_unity_videos.py [showcase] [how]        (needs ffmpeg, Pillow and numpy)

The takes are recorded first, by the Play mode tests of unity/Demo/Assets/Showcase/Tests/CaptureTakes.cs (see
"Recording it" in the showcase's README). Each is a folder in out/unity-video/ with the game's pictures (1920 x 1080,
30 a second), its sound (audio.wav, as SFX FORGE made it in Unity) and telemetry.json: what the music and every sound
did in each frame.

A video is a list of parts (below): a take, or a slide. This file lays each frame out - the game's picture, beside it
what SFX FORGE is doing in that frame (read from the telemetry), the caption - and ffmpeg joins pictures and sound. A
take's sound is its own wav, cut where its pictures are cut; a slide is silent. Nothing else is ever heard. Each take
is brought to one loudness with a single gain (measured with ffmpeg's ebur128, never above -2 dBFS before the AAC
encoder, which adds up to a decibel and a half). Results: videos/*.mp4, the folder of the finished videos, which is
committed. A video's sound track on the way is a wav in out/video/, removed when the video is written.

The takes are recorded at half the listener's volume (CaptureRecorder.ListenerVolume): Unity limits its output to full
scale before a take can read it, so only a take with room above it shows what the game's mix reaches (every take
asserts that this stays under full scale at full volume). The gain here brings the level back up.

The song plays in its own order, the arrangement in the telemetry (song.arrangement). Where the song is in it is not
in the telemetry: it is worked out from the pattern of each bar with the rule of core/sfx_song.h (a bar line goes on
to the next bar of the arrangement with the pattern that plays). Whether a pattern is HELD is worked out from the
events with the rules of MusicDirector.cs: a section with a pattern holds it, a section in the song's order ends the
hold, the final launch holds the pattern named there.

Which instrument has notes in which pattern is not in the telemetry either: it is read from the song itself,
unity/Demo/Assets/SfxDemo/DemoSong.bytes (the packed format of core/sfx_song.h).

The pages of code in the second video are excerpts carried in this file. excerpt() checks every line of them against
the source file named above it, so a page cannot show code that is no longer there.
"""

import bisect
import glob
import json
import os
import re
import subprocess
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.dont_write_bytecode = True  # (no __pycache__ beside the scripts)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# the house style is the device videos'
# fmt: off
from make_videos import (
    ROOT, VIDEOS, FPS, W, H, BG, PANEL, LINE, TEXT, DIM, EMBER, font, wrap,
)
# fmt: on

TAKES = os.environ.get("SFXFORGE_CAPTURE_DIR", os.path.join(ROOT, "out", "unity-video"))
# a picture is 1600 sample frames of sound
RATE = 48000
PER_FRAME = 48000 // FPS
# no sample above -2 dBFS: AAC adds up to a decibel and a half
PEAK = 10 ** (-2 / 20)
# a caption is on screen for at least a second per 14 characters ...
READ = 14.0
SHORTEST = 2.5  # ... and never less than this
GAME = (40, 128, 1376, 774)  # where the game's picture is: x, y, width, height (16:9)
SIDE = (1440, 128, 440, 774)  # the panel beside it
UNLIT = "#2A221D"
GLOW = "#3A2414"
SONG = os.path.join(ROOT, "unity", "Demo", "Assets", "SfxDemo", "DemoSong.bytes")
AUDIO = os.path.join(ROOT, "unity", "Demo", "Assets", "Showcase", "Scripts", "Audio")
LETTERS = "ABCDEFGH"
# SectionCue.SongOrder: no pattern is held, the song plays in its own order
SONG_ORDER = -2


def final_launch_pattern():
    """The pattern MusicDirector cues and holds at the final launch: it is not in the telemetry."""
    found = re.search(
        r"int finalLaunchPattern = Pattern([A-H]);",
        open(os.path.join(AUDIO, "MusicDirector.cs"), encoding="utf-8").read(),
    )
    if not found:
        sys.exit("MusicDirector.cs no longer names its finalLaunchPattern: final_launch_pattern() must follow it")
    return LETTERS.index(found.group(1))


FINAL_LAUNCH = final_launch_pattern()


def mono(size, bold=False):
    for name in (
        ["consolab.ttf", "courbd.ttf", "DejaVuSansMono-Bold.ttf"]
        if bold
        else ["consola.ttf", "cour.ttf", "DejaVuSansMono.ttf"]
    ):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return font(size, bold)


# ---- the song: which instrument has notes in which pattern ---------------------------------------------------------
def song_notes():
    """For each of the eight patterns, the set of instruments (0..15) with at least one step on in it.

    Reads the packed song of format 6 (core/sfx_song.h, "FORMAT 6"): a head of 147 bytes, then eight pattern
    blocks. A block names its grid, the sounds with a row and the lanes with a row; only those rows follow.
    """
    patches = 16 * 28
    tracks = 16
    patterns = 8
    lanes = 24
    head = 147
    # a grid's steps a bar, and the bytes its step bits take (7 bits a byte)
    grid_steps = [16, 32, 64]
    grid_bit_bytes = [3, 5, 10]
    song = open(SONG, "rb").read()[patches:]
    wrong = "DemoSong.bytes is not a song of format 6: song_notes() must follow core/sfx_song.h"
    if len(song) < head + patterns * 8 or song[0] != 6:
        sys.exit(wrong)
    notes = []
    at = head
    for _ in range(patterns):
        if at + 8 > len(song) or song[at] > 2:
            sys.exit(wrong)
        steps = grid_steps[song[at]]
        bit_bytes = grid_bit_bytes[song[at]]
        row_mask = song[at + 1] | song[at + 2] << 7 | song[at + 3] << 14
        lane_mask = song[at + 4] | song[at + 5] << 7 | song[at + 6] << 14 | song[at + 7] << 21
        at += 8
        playing = set()
        for t in range(tracks):
            if row_mask >> t & 1:
                # a row may hold notes or lengths with no step on: only a step that is on is a note
                if any(song[at : at + bit_bytes]):
                    playing.add(t)
                at += bit_bytes + 3 * steps
        at += steps * sum(lane_mask >> lane & 1 for lane in range(lanes))
        notes.append(playing)
    if at != len(song):
        sys.exit(wrong)
    return notes


NOTES = song_notes()

# ---- a take: its pictures, its sound, what SFX FORGE did -----------------------------------------------------------
SOUND_NAME = {
    "UiConfirm": "UI confirm",
    "TuckStart": "Tuck",
    "RollHum": "Roll hum",
    "WindUpRev": "Wind-Up rev",
    "ClipperPop": "Clipper pop",
    "TuningFork": "Tuning Fork",
    "ChimeScatter": "Chime scatter",
    "Land": "Landing",
    "Bell answer": "Bell answer",
    "Music dip": "Music dips",
    "Fanfare": "Fanfare",
}
HELD = [("RollHum", "Roll hum"), ("Wind", "Wind"), ("WindUpRev", "Wind-Up rev")]
INSTRUMENT_NAME = {"FmBass": "FM bass", "FmKeys": "FM keys", "HiHat": "Hi-hat"}
ROUTE_NAME = {"live": "live voice"}  # the manual's name for route 2
loaded = {}


class Recording:
    def __init__(self, name):
        self.name = name
        self.dir = os.path.join(TAKES, name)
        if not os.path.exists(os.path.join(self.dir, "telemetry.json")):
            sys.exit(
                "no take '%s' in %s - record it first (see the showcase's README, \"Recording it\")" % (name, TAKES)
            )
        t = json.load(open(os.path.join(self.dir, "telemetry.json")))
        self.frames = t["frames"]
        self.count = t["frameCount"]
        if len(glob.glob(os.path.join(self.dir, "f*.png"))) != self.count or len(self.frames) != self.count:
            sys.exit("take '%s': telemetry.json and the pictures do not belong together - record it again" % name)
        self.events = t["events"]
        self.instruments = t["song"]["instruments"]
        # the lamps: every instrument with notes in some pattern, by its layer mark, then those only played by hand
        by_hand = {n["sound"] for phrase in t["song"]["byHand"].values() for n in phrase}
        in_song = set().union(*NOTES)
        self.lamps = sorted((i for i in self.instruments if i["index"] in in_song), key=lambda i: i["layer"]) + [
            i for i in self.instruments if i["index"] in by_hand - in_song
        ]
        self.only_by_hand = by_hand - in_song
        self.marks = sorted({i["layer"] for i in self.lamps if i["layer"] > 0})
        self.floor = {s["id"]: s["intensityFloor"] for s in t["sections"]}
        self.arrangement = t["song"]["arrangement"]
        self.follow_song(t)
        sixteenth = 60.0 / t["song"]["bpm"] / 4 * FPS  # in pictures
        # instrument: [(first frame, last frame)] of notes played by hand
        self.hand = {}
        # the frames of the footsteps; every other sound: (frame, name, note, route)
        self.steps = []
        self.played = []
        for s in t["sounds"]:
            for n in t["song"]["byHand"].get(s["sound"], []):  # a one-shot (length 0) is lit for six sixteenths
                first = s["f"] + n["start"] * sixteenth
                self.hand.setdefault(n["sound"], []).append((first, first + max(n["length"], 6) * sixteenth))
            if s["sound"].startswith("Footstep"):
                self.steps.append(s["f"])
            elif s["cause"] != "is let go":
                note = ""
                if s["sound"] == "Chime":
                    note = (s["detail"] or "").replace(" of the scale", "")
                elif s["sound"] == "ChimeScatter":
                    note = s["detail"] or ""
                self.played.append(
                    (s["f"], SOUND_NAME.get(s["sound"], s["sound"]), note, ROUTE_NAME.get(s["route"], s["route"]))
                )
        self.at = [p[0] for p in self.played]
        with wave.open(os.path.join(self.dir, "audio.wav"), "rb") as w:
            if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (RATE, 2, 2):
                sys.exit(name + ": audio.wav is not 48 kHz 16-bit stereo")
            self.sound = (
                np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float32) / 32768.0
            )
        # Unity mixes whole blocks: the wav is up to a block shorter or longer
        need = self.count * PER_FRAME
        if len(self.sound) < need:
            self.sound = np.vstack([self.sound, np.zeros((need - len(self.sound), 2), np.float32)])
        self.sound = self.sound[:need]
        # fmt: off
        out = subprocess.run(
            [
                "ffmpeg", "-hide_banner", "-nostats", "-i", os.path.join(self.dir, "audio.wav"), "-af", "ebur128",
                "-f", "null", "-",
            ],
            capture_output=True,
            text=True,
        ).stderr
        # fmt: on
        self.loudness = float(re.findall(r"I:\s+(-?[\d.]+) LUFS", out)[-1])
        self.peak = float(np.abs(self.sound).max())

    def follow_song(self, t):
        """For every frame: the pattern MusicDirector holds (None: the song's own order), and the bar of the
        arrangement."""
        cue = {s["id"]: s["pattern"] for s in t["sections"]}
        changes = {}  # frame: the pattern held from it on
        for e in t["events"]:
            if e["name"] in ("SectionEntered", "Respawned"):
                pattern = cue.get(e["value"], SONG_ORDER)
                if pattern >= 0 or pattern == SONG_ORDER:  # SectionCue.KeepPattern leaves what is held
                    changes[e["f"]] = pattern if pattern >= 0 else None
            elif e["name"] == "FinalLaunch":
                changes[e["f"]] = FINAL_LAUNCH
        held = cue.get(self.frames[0]["section"], SONG_ORDER)  # a take that begins in a section began with its cue
        held = held if held >= 0 else None
        arr = self.arrangement
        at = None
        bar = None
        self.holds = []
        self.arranged = []
        for k, f in enumerate(self.frames):
            held = changes.get(k, held)
            m = f["music"]
            if not m["playing"] or not arr:
                at = None
                bar = None
            # the first bar seen: bars since Play, if nothing was cued before
            elif bar is None:
                at = m["bar"] % len(arr)
                if arr[at] != m["pattern"]:
                    at = arr.index(m["pattern"]) if m["pattern"] in arr else None
                bar = m["bar"]
            # a bar line: on to the next bar of the arrangement with this pattern
            elif m["bar"] != bar:
                start = -1 if at is None else at
                at = next(
                    (j % len(arr) for j in range(start + 1, start + 1 + len(arr)) if arr[j % len(arr)] == m["pattern"]),
                    None,
                )
                bar = m["bar"]
            self.holds.append(held)
            self.arranged.append(at)

    def gain(self, target):
        """One number for the whole take: to the target loudness, but no sample above -2 dBFS."""
        return min(10 ** ((target - self.loudness) / 20), PEAK / max(self.peak, 1e-6))

    def picture(self, k):
        return (
            Image.open(os.path.join(self.dir, "f%05d.png" % k)).convert("RGB").resize((GAME[2], GAME[3]), Image.LANCZOS)
        )

    def when(self, event, value=None, nth=0):
        """The time in the take, seconds, of the nth event of a name (and a value): captions are tied to what
        happens."""
        found = [e["t"] for e in self.events if e["name"] == event and (value is None or e["value"] == value)]
        if len(found) <= nth:
            sys.exit("take '%s' has no event %s %s (number %d)" % (self.name, event, value or "", nth + 1))
        return found[nth]


def recording(name):
    if name not in loaded:
        loaded[name] = Recording(name)
    return loaded[name]


def still(name):
    path = os.path.join(TAKES, "stills", name + ".png")
    if not os.path.exists(path):
        sys.exit(
            "no still '%s' in %s - record the take Stills (see the showcase's README, \"Recording it\")"
            % (name, os.path.dirname(path))
        )
    return Image.open(path).convert("RGB").resize((GAME[2], GAME[3]), Image.LANCZOS)


# ---- the parts of a video ------------------------------------------------------------------------------------------
class Clip:
    """A stretch of a take: frames first..last of its pictures and exactly the sound that belongs to them."""

    def __init__(self, take, captions, first=0.0, last=None, focus=""):
        self.take = recording(take)
        self.focus = focus
        self.first = int(round(first * FPS))
        self.end = self.take.count if last is None else int(round(last * FPS))
        self.frames = self.end - self.first
        # t: seconds of the take
        self.captions = [(int(round(t * FPS)) - self.first, chapter, text) for t, chapter, text in captions]

    def sound(self, target):
        s = self.take.sound[self.first * PER_FRAME : self.end * PER_FRAME] * self.take.gain(target)
        # a cut is never a click
        fade_in = int(0.03 * RATE)
        fade_out = int(0.5 * RATE)
        s[:fade_in] *= np.linspace(0, 1, fade_in)[:, None]
        s[-fade_out:] *= np.linspace(1, 0, fade_out)[:, None]
        return s

    def draw(self, im, d, k):
        k += self.first
        im.paste(self.take.picture(k), GAME[:2])
        panel(d, self.take, k, self.focus)


class Slide:
    """A still page: silent. paint(im, d) draws it once. It stays long enough for its caption and its lit code to be
    read."""

    def __init__(self, seconds, chapter, text, paint):
        self.reading = len(text) + getattr(paint, "reading", 0)
        self.frames = int(round(max(seconds, self.reading / READ + 1.0) * FPS))
        self.paint = paint
        self.page = None
        self.captions = [(0, chapter, text)]
        for name in getattr(paint, "stills", []):  # fail before rendering, not in the middle
            still(name)

    def sound(self, target):
        return np.zeros((self.frames * PER_FRAME, 2), np.float32)

    def draw(self, im, d, k):
        if self.page is None:
            self.page = Image.new("RGB", (W, H), BG)
            self.paint(self.page, ImageDraw.Draw(self.page))
            self.page = self.page.crop((0, 120, W, 910))
        im.paste(self.page, (0, 120))


# ---- what SFX FORGE is doing: the panel beside the game ------------------------------------------------------------
def lamp(d, x, y, on, size=14):
    d.ellipse([x, y, x + size, y + size], fill=EMBER if on else UNLIT, outline=EMBER if on else "#5A4A3E")


def panel(d, take, k, focus):
    x, y, w, h = SIDE
    f = take.frames[k]
    m = f["music"]
    label = font(17)
    name = font(20)
    value = font(20, False)
    small = font(16, False)
    d.rounded_rectangle([x, y, x + w, y + h], 12, fill=PANEL, outline=LINE, width=2)
    d.text((x, y - 30), "WHAT SFX FORGE IS DOING", font=font(18), fill=DIM)
    px, pw = x + 20, w - 40

    def head(yy, text, key, right=""):
        d.text((px, yy), text, font=label, fill=EMBER if key == focus else DIM)
        if right:
            d.text((px + pw - d.textlength(right, font=small), yy + 1), right, font=small, fill=DIM)

    head(y + 12, "SECTION", "section")
    d.text((px, y + 31), f["sectionName"], font=font(28), fill=TEXT)

    # the song: its pattern, its bar, where it is in its arrangement
    hold = take.holds[k]
    if not m["playing"]:
        head(y + 76, "THE SONG", "pattern")
        d.text((px, y + 106), "the song has stopped", font=font(24, False), fill=DIM)
    else:
        if hold is None:
            head(y + 76, "THE SONG", "pattern", "in its own order")
        else:
            head(y + 76, "THE SONG", "pattern")
            right = (
                "%s IS HELD" % LETTERS[hold] if m["pattern"] == hold else "%s from the next bar line" % LETTERS[hold]
            )
            d.text((px + pw - d.textlength(right, font=label), y + 76), right, font=label, fill=EMBER)
        d.text((px, y + 90), m["letter"] or "-", font=font(46), fill=EMBER)
        d.text((px + 50, y + 110), "bar %d" % (m["bar"] + 1), font=font(24, False), fill=TEXT)
        for s in range(16):  # the sixteen steps of the bar
            sx = px + 168 + s * 14 + (s // 4) * 6
            d.rectangle(
                [sx, y + 114, sx + 10, y + 132], fill=EMBER if s == m["step"] else ("#5A4A3E" if s % 4 == 0 else UNLIT)
            )
    at = take.arranged[k]  # the arrangement: one cell a bar, the bar that plays lit
    cell = font(15)
    for n, pattern in enumerate(take.arrangement):
        cx = px + n * 24 + (n // 4) * 5
        here = m["playing"] and n == at
        waits = m["playing"] and hold is not None and pattern == hold  # a bar of the pattern that is held or cued
        d.rectangle(
            [cx, y + 150, cx + 21, y + 174],
            fill=EMBER if here else UNLIT,
            outline=EMBER if here or waits else "#5A4A3E",
            width=2 if waits else 1,
        )
        letter = LETTERS[pattern]
        d.text(
            (cx + 11 - d.textlength(letter, font=cell) / 2, y + 152),
            letter,
            font=cell,
            fill=BG if here else (TEXT if waits else DIM),
        )

    floor = take.floor.get(f["section"]) if m["playing"] and f["phase"] == "Playing" else None
    head(y + 190, "INTENSITY", "intensity", "from the section's floor to 1, by speed")
    d.rounded_rectangle([px, y + 218, px + pw, y + 236], 4, fill=UNLIT)
    if m["playing"]:
        d.rounded_rectangle([px, y + 218, px + max(8, int(pw * min(1.0, m["intensity"]))), y + 236], 4, fill=EMBER)
    for mark in take.marks:  # where each layer comes in
        tx = px + int(pw * mark)
        d.line([tx, y + 214, tx, y + 240], fill=TEXT, width=2)
    if floor is not None:  # where the intensity rests when the player stands still
        tx = px + int(pw * floor)
        d.polygon([(tx, y + 238), (tx - 6, y + 247), (tx + 6, y + 247)], fill=EMBER)
    d.text(
        (px + pw - d.textlength("%.2f" % m["intensity"], font=name), y + 246),
        "%.2f" % m["intensity"],
        font=name,
        fill=TEXT if m["playing"] else DIM,
    )
    d.text((px, y + 250), "a line: a layer joins", font=small, fill=DIM)
    d.polygon([(px + 178, y + 256), (px + 172, y + 266), (px + 184, y + 266)], fill=EMBER)
    d.text((px + 192, y + 250), "the floor", font=small, fill=DIM)

    head(y + 280, "VOLUME", "volume")  # SfxMusicPlayer.volume: it dips after a hit
    d.rounded_rectangle([px + 96, y + 284, px + pw - 60, y + 296], 3, fill=UNLIT)
    d.rounded_rectangle(
        [px + 96, y + 284, px + 96 + max(4, int((pw - 156) * min(1.0, m["volume"]))), y + 296],
        3,
        fill=EMBER if m["playing"] else "#5A4A3E",
    )
    d.text(
        (px + pw - d.textlength("%.2f" % m["volume"], font=name), y + 278), "%.2f" % m["volume"], font=name, fill=TEXT
    )

    head(y + 312, "LAYERS", "layers", "lit: it plays now")
    rows = (len(take.lamps) + 1) // 2
    for n, i in enumerate(take.lamps):
        cx = px + 204 * (n // rows)
        yy = y + 337 + (n % rows) * 25
        by_hand = any(a <= k < b for a, b in take.hand.get(i["index"], []))
        let_in = m["playing"] and i["index"] in m["layers"]
        on = by_hand or (let_in and i["index"] in NOTES[m["pattern"]])
        if on:
            lamp(d, cx, yy + 4, True)
        elif (
            let_in
            and i["index"] not in take.only_by_hand
            # the intensity lets it in, but this pattern has no notes for it
        ):
            d.ellipse([cx, yy + 4, cx + 14, yy + 18], fill=UNLIT, outline=EMBER, width=2)
        else:
            lamp(d, cx, yy + 4, False)
        d.text(
            (cx + 22, yy),
            INSTRUMENT_NAME.get(i["name"], i["name"]),
            font=name if on else value,
            fill=TEXT if on else DIM,
        )
        right = (
            "by hand"
            if by_hand or i["index"] in take.only_by_hand
            else ("%.2f" % i["layer"] if i["layer"] > 0 else "always")
        )
        d.text((cx + 190 - d.textlength(right, font=small), yy + 3), right, font=small, fill=EMBER if on else DIM)
    d.text((px, y + 337 + rows * 25 + 1), "ring: let in, but no notes in this pattern", font=small, fill=DIM)

    head(y + 496, "FOLLOWING THE GAME", "held", "held, bent every frame")
    for n, (key, text) in enumerate(HELD):
        yy = y + 521 + n * 27
        v = f["held"][key]
        on = v["held"]  # from Hold to LetGo; the tail after that is not shown
        lamp(d, px, yy + 4, on)
        d.text((px + 22, yy), text, font=name if on else value, fill=TEXT if on else DIM)
        d.rounded_rectangle([px + 168, yy + 6, px + 268, yy + 18], 3, fill=UNLIT)
        if on:
            d.rounded_rectangle(
                [px + 168, yy + 6, px + 168 + max(4, int(100 * min(1.0, v["level"]))), yy + 18], 3, fill=EMBER
            )
            bend = "+%.1f st" % v["pitchSemitones"] if key != "Wind" else "%+.1f oct" % v["cutoffOctaves"]
            d.text((px + pw - d.textlength(bend, font=small), yy + 3), bend, font=small, fill=TEXT)
    yy = y + 521 + 3 * 27
    n = bisect.bisect_right(take.steps, k)
    on = n > 0 and k - take.steps[n - 1] < 4
    lamp(d, px, yy + 4, on)
    d.text((px + 22, yy), "Footsteps", font=name if on else value, fill=TEXT if on else DIM)
    d.text((px + pw - d.textlength("baked", font=small), yy + 3), "baked", font=small, fill=DIM)

    head(y + 640, "JUST PLAYED", "played", "sound, route")
    n = bisect.bisect_right(take.at, k)
    rows = [p for p in take.played[max(0, n - 3) : n] if k - p[0] < 6 * FPS]
    for r, (at, what, note, route) in enumerate(reversed(rows)):
        yy = y + 665 + r * 27
        fresh = k - at < 8
        colour = EMBER if fresh else (TEXT if r == 0 else DIM)
        d.text((px, yy), what, font=name if r == 0 else value, fill=colour)
        if note:
            d.text((px + d.textlength(what + "  ", font=name), yy + 3), note, font=small, fill=DIM)
        d.text(
            (px + pw - d.textlength(route, font=name if route != "baked" else value), yy),
            route,
            font=name if route != "baked" else value,
            fill=EMBER if route != "baked" else colour,
        )


# ---- slides --------------------------------------------------------------------------------------------------------
def code(lines, key, title=""):
    """A page of code: the lines as they are in the source, the key line(s) lit; a title names the file if no line
    does."""

    def paint(im, d):
        size = 34
        while size > 24 and (
            max(d.textlength(line, font=mono(size)) for line in lines) > 1700 or len(lines) * int(size * 1.5) > 730
        ):
            size -= 1
        f = mono(size)
        pitch = int(size * 1.5)
        if len(lines) * pitch > 740:
            sys.exit("a page of code does not fit: %d lines" % len(lines))
        top = 150 + max(0, (740 - len(lines) * pitch) // 2)
        d.rounded_rectangle([40, 128, W - 40, 902], 12, fill=PANEL, outline=LINE, width=2)
        if title:
            d.text((90, top - 70), title, font=font(26), fill=DIM)
        for n, line in enumerate(lines):
            yy = top + n * pitch
            if n in key:
                d.rectangle([52, yy - 6, W - 52, yy + pitch - 10], fill=GLOW)
                d.rectangle([52, yy - 6, 60, yy + pitch - 10], fill=EMBER)
            cut = line.find("//")
            head, tail = (line, "") if cut < 0 else (line[:cut], line[cut:])
            d.text((90, yy), head, font=mono(size, True) if n in key else f, fill=TEXT)
            d.text((90 + d.textlength(head, font=f), yy), tail, font=f, fill=DIM)

    paint.reading = sum(len(lines[n].strip()) for n in key)  # the lit lines are read as well as the caption
    return paint


def picture(name, heading, rows):
    """One of the game's stills, and beside it a few plain lines."""

    def paint(im, d):
        x, y, w, h = SIDE
        im.paste(still(name), GAME[:2])
        d.rounded_rectangle([x, y, x + w, y + h], 12, fill=PANEL, outline=LINE, width=2)
        d.text((x + 24, y + 26), heading, font=font(20), fill=DIM)
        yy = y + 76
        for big, text in rows:
            d.text((x + 24, yy), big, font=font(44), fill=EMBER)
            yy += 56
            for line in wrap(d, text, font(24, False), w - 48):
                d.text((x + 24, yy), line, font=font(24, False), fill=TEXT)
                yy += 32
            yy += 26

    paint.stills = [name]
    return paint


def table(rows):
    """Rows of a number or a name, a line of text, and where to read."""

    def paint(im, d):
        d.rounded_rectangle([40, 128, W - 40, 902], 12, fill=PANEL, outline=LINE, width=2)
        pitch = min(104, 720 // len(rows))
        top = 150 + (740 - len(rows) * pitch) // 2
        for n, (big, text, where) in enumerate(rows):
            yy = top + n * pitch
            d.text((90, yy - 6), big, font=font(40), fill=EMBER)
            d.text((90 + 290 * (len(big) > 2) + 70 * (len(big) <= 2), yy), text, font=font(32, False), fill=TEXT)
            if where:
                d.text((W - 90 - d.textlength(where, font=mono(26)), yy + 6), where, font=mono(26), fill=DIM)
            if n:
                d.line([90, yy - 16, W - 90, yy - 16], fill=LINE, width=1)

    return paint


def seam(im, d):
    """Gameplay raises events, audio listens; only the audio folder knows SFX FORGE."""
    head = font(30)
    body = font(24, False)
    code_font = mono(24)
    small = font(20, False)

    # a box with a title and lines of text; a line is (text, is it code?)
    def box(x0, y0, x1, y1, title, lines, lit=False):
        d.rounded_rectangle([x0, y0, x1, y1], 14, fill=PANEL, outline=EMBER if lit else LINE, width=3 if lit else 2)
        d.text((x0 + 24, y0 + 18), title, font=head, fill=EMBER if lit else TEXT)
        for n, (text, is_code) in enumerate(lines):
            d.text(
                (x0 + 24, y0 + 70 + n * 36 + 6 * is_code),
                text,
                font=code_font if is_code else body,
                fill=TEXT if is_code else DIM,
            )

    # a level arrow from (x0, y0) to (x1, y1) with a word above or under it
    def arrow(x0, y0, x1, y1, text, above=True):
        d.line([x0, y0, x1, y1], fill=EMBER, width=4)
        s = 1 if x1 > x0 else -1
        d.polygon([(x1, y1), (x1 - 18 * s, y1 - 11), (x1 - 18 * s, y1 + 11)], fill=EMBER)
        d.text(
            ((x0 + x1) / 2 - d.textlength(text, font=small) / 2, y1 - 34 if above else y1 + 12),
            text,
            font=small,
            fill=TEXT,
        )

    # the top row: gameplay raises events, GameEvents carries them, audio listens
    box(
        60,
        170,
        600,
        520,
        "GAMEPLAY",
        [
            ("Scripts/Gameplay, Scripts/Level", False),
            ("says what happened:", False),
            ("GameEvents.RaiseJumped();", True),
            ("GameEvents.RaiseLanded(impactSpeed);", True),
            ("", False),
            ("knows nothing about sound", False),
        ],
    )
    box(
        720,
        170,
        1200,
        520,
        "GameEvents",
        [
            ("Scripts/Core: a static class", False),
            ("of 19 C# events", False),
            ("Jumped", True),
            ("Landed(impactSpeed)", True),
            ("ChimeCollected(combo)", True),
            ("SectionEntered(section)", True),
        ],
    )
    box(
        1320,
        170,
        1860,
        520,
        "AUDIO",
        [
            ("Scripts/Audio listens:", False),
            ("GameEvents.Jumped += OnJumped;", True),
            ("", False),
            ("SoundEffects", True),
            ("ContinuousSounds", True),
            ("MusicDirector", True),
        ],
        lit=True,
    )
    arrow(600, 345, 716, 345, "raises")
    arrow(1200, 345, 1316, 345, "listens")

    # the bottom row: the player's state, which the audio reads every frame, and the package only the audio names
    box(
        60,
        620,
        1200,
        850,
        "THE PLAYER'S STATE",
        [
            ("What sounds all the time has no event. The audio reads it every frame:", False),
            ("Speed01   IsRolling   IsGrounded   IsCharging   Charge01", True),
        ],
    )
    d.line([1500, 520, 1500, 735], fill=EMBER, width=4)
    arrow(1500, 735, 1204, 735, "reads, every frame")
    box(
        1560,
        620,
        1860,
        850,
        "SFX FORGE",
        [("the package:", False), ("CatSplat.SfxForge", True), ("", False), ("only AUDIO names it", False)],
        lit=True,
    )
    d.line([1710, 520, 1710, 616], fill=EMBER, width=4)
    d.polygon([(1710, 620), (1699, 602), (1721, 602)], fill=EMBER)


# ---- putting a video together --------------------------------------------------------------------------------------
def render(name, title, parts, target):
    total = sum(p.frames for p in parts)
    # (first frame, last frame, chapter, text) over the whole video
    captions = []
    at = 0
    for p in parts:
        for k, chapter, text in p.captions:
            if captions:
                captions[-1][1] = at + k
            captions.append([at + k, total, chapter, text])
        at += p.frames
    for k0, k1, chapter, text in captions:  # long enough to read?
        need = max(SHORTEST, len(text) / READ)
        if k0 < 0 or (k1 - k0) / FPS + 1e-6 < need:
            sys.exit(
                "%s: the caption at %.1f s is on screen %.2f s, it needs %.2f s: %s"
                % (name, k0 / FPS, (k1 - k0) / FPS, need, text)
            )
    for p in parts:  # a slide: its caption and its lit lines of code
        if isinstance(p, Slide) and p.frames / FPS + 1e-6 < p.reading / READ:
            sys.exit(
                "%s: a slide is on screen %.1f s, its caption and lit code need %.1f s: %s"
                % (name, p.frames / FPS, p.reading / READ, p.captions[0][2])
            )
    # the sound track on the way; the video itself goes to videos/
    out = os.path.join(ROOT, "out", "video")
    os.makedirs(out, exist_ok=True)
    os.makedirs(VIDEOS, exist_ok=True)
    sound = np.vstack([p.sound(target) for p in parts])
    track = os.path.join(out, name + ".wav")
    with wave.open(track, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((np.clip(sound, -1, 1) * 32767).astype("<i2").tobytes())
    for take in sorted({p.take.name: p.take for p in parts if isinstance(p, Clip)}.values(), key=lambda t: t.name):
        g = take.gain(target)
        print(
            "  %-18s %6.1f LUFS, peak %.3f  ->  gain %+5.1f dB  ->  %6.1f LUFS, peak %.3f"
            % (take.name, take.loudness, take.peak, 20 * np.log10(g), take.loudness + 20 * np.log10(g), take.peak * g)
        )
    print(name, "- %d parts, %d pictures, %.1f s" % (len(parts), total, total / FPS))
    at = 0
    for p in parts:  # where each part begins, for whoever checks the video
        print(
            "  %6.1f s  %-5s %5.1f s  %s"
            % (at / FPS, "take" if isinstance(p, Clip) else "slide", p.frames / FPS, p.captions[0][1])
        )
        at += p.frames

    base = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(base)
    d.text(
        (W - 60 - d.textlength("SFX FORGE  ·  " + title, font=font(24, False)), 62),
        "SFX FORGE  ·  " + title,
        font=font(24, False),
        fill=DIM,
    )
    big = font(54)
    cap = font(40, False)
    for k0, k1, chapter, text in captions:  # inside the picture's margins?
        if d.textlength(chapter, font=big) > 1100 or len(wrap(d, text, cap, 1800)) > 2:
            sys.exit("%s: does not fit: %s / %s" % (name, chapter, text))
    mp4 = os.path.join(VIDEOS, name + ".mp4")
    # fmt: off
    ff = subprocess.Popen(
        [
            "ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H),
            "-r", str(FPS), "-i", "-", "-i", track, "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20",
            "-preset", "veryfast", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", mp4,
        ],
        stdin=subprocess.PIPE,
    )
    # fmt: on
    k, c = 0, 0
    for p in parts:
        for n in range(p.frames):
            while c + 1 < len(captions) and captions[c + 1][0] <= k:
                c += 1
            im = base.copy()
            d = ImageDraw.Draw(im)
            p.draw(im, d, n)
            d.text((60, 44), captions[c][2], font=big, fill=EMBER)
            for row, line in enumerate(wrap(d, captions[c][3], cap, 1800)):
                d.text((60, 924 + row * 54), line, font=cap, fill=TEXT)
            d.rectangle([0, H - 6, int(W * k / max(1, total - 1)), H], fill=EMBER)
            ff.stdin.write(im.tobytes())
            k += 1
    ff.stdin.close()
    ff.wait()
    os.remove(track)
    print("wrote", mp4)


# ---- video 1: the showcase, start to goal --------------------------------------------------------------------------
def showcase():
    # the first video: a slide, the take of the full run with its captions, a slide
    take = recording("full-run")

    def enter(section, plus=0.0):  # seconds of the take at which a section begins
        return take.when("SectionEntered", section) + plus

    # the captions of the run: (seconds of the take, chapter, text). Most are tied to the moment a section begins.
    run = [  # the run itself begins at 3.0 s of the take
        (
            0.0,
            "BRIO: FIRST HARMONIC",
            "Every sound you hear is synthesized while the game runs. There is no audio file.",
        ),
        (
            6.0,
            "SOFT START",
            "Each Chime in a row is one step higher up a scale, up to ten steps. A live voice plays it.",
        ),
        (
            enter("RollingSines"),
            "ROLLING SINES",
            "The music is the device's demo song, in its own order: sixteen bars, round and round.",
        ),
        (
            enter("RollingSines", 6.5),
            "ROLLING SINES",
            "INTENSITY: the section's floor when you stand still, 1 at top speed. A lit lamp plays now.",
        ),
        (enter("FullCycle"), "FULL CYCLE", "A roll holds a hum. Its pitch and filter follow your speed."),
        (enter("FullCycle", 4.5), "FULL CYCLE", "At top speed the wind joins, and every layer is let in."),
        (
            take.when("CheckpointReached", 0),
            "FULL CYCLE",
            "A Tuning Fork is a checkpoint. The song's own bells answer it, in time.",
        ),
        (
            enter("WooferSteps", 5.6),
            "WOOFER STEPS",
            "Jump, landing, Woofer: baked once at the start, then played as ordinary clips.",
        ),
        (
            enter("StaticField"),
            "STATIC FIELD",
            "Static Field holds pattern E, the song's breakdown. It is cued again on every bar line.",
        ),
        (
            enter("StaticField", 6.7),
            "STATIC FIELD",
            "A Clipper pop is a baked clip, tuned to the song's key. JUST PLAYED names each sound's route.",
        ),
        (
            enter("SplitSignal"),
            "SPLIT SIGNAL",
            "The hold ends, and the song walks on through its arrangement from where it is.",
        ),
        (
            enter("SplitSignal", 6.6),
            "SPLIT SIGNAL",
            "Footsteps are two baked clips. Their pitch rises with your speed.",
        ),
        (
            enter("SplitSignal", 12.1),
            "SPLIT SIGNAL",
            "Nineteen sound effects, one song. All of it is patches and numbers.",
        ),
        (
            enter("Overdrive"),
            "OVERDRIVE",
            "Overdrive's floor is %.2f: the FM keys stay at a standstill. At speed, brass and bells join."
            % take.floor["Overdrive"],
        ),
        (
            enter("Overdrive", 7.1),
            "OVERDRIVE",
            "Leaving the last ramp cues pattern %s, the song's turnaround. It is held until the goal."
            % LETTERS[FINAL_LAUNCH],
        ),
        (
            enter("TheResonator"),
            "THE RESONATOR",
            "The goal stops the song. The fanfare is played by hand on the song's own instruments.",
        ),
    ]

    # the video's parts, in order
    parts = [
        Slide(
            5.0,
            "SFX FORGE IN UNITY",
            "Brio: First Harmonic. A small game in which every sound is SFX FORGE.",
            picture(
                "05-loop",
                "THE SHOWCASE",
                [
                    ("19", "sound effects, each a patch written in code"),
                    ("1", "song of 16 instruments, with layers"),
                    ("0", "audio files"),
                ],
            ),
        ),
        Clip("full-run", run),
        Slide(
            7.5,
            "WHERE TO FIND IT",
            "Open source, MIT. Open unity/Demo in Unity 6000.3, open the scene Showcase.unity and press Play.",
            table(
                [
                    ("The game", "one level, start to goal", "unity/Demo/Assets/Showcase"),
                    ("Its sound", "sixteen short files", "unity/Demo/Assets/Showcase/Scripts/Audio"),
                    ("The manual", "every value, recipes, the API", "docs/UNITY.md"),
                    ("The package", "Windows x64", "unity/com.catsplat.sfxforge"),
                ]
            ),
        ),
    ]
    render("sfx-forge-unity-showcase", "in Unity: the showcase", parts, -16.0)


# ---- video 2: how it works, and how to do it in your own game ------------------------------------------------------
# FIRST is the FirstSound script of docs/UNITY.md ("Five minutes"), with one of its two buttons left out. It is in no
# file of the repository. From BAKED on, every line is in the file named in the comment above it, as it is shown;
# "..." marks lines left out.
FIRST = [
    "using CatSplat.SfxForge;",
    "using UnityEngine;",
    "",
    "public class FirstSound : MonoBehaviour",
    "{",
    "    SfxPlayer player;",
    "",
    "    void Start()",
    "    {",
    "        player = gameObject.AddComponent<SfxPlayer>();",
    "    }",
    "",
    "    void OnGUI()",
    "    {",
    "        if (GUI.Button(new Rect(20, 20, 160, 40), \"Laser\")) player.Play(SfxPatch.Default);",
    "    }",
    "}",
]
BAKED = [
    "// SoundEffects.cs - once, at the start: every baked sound becomes an AudioClip",
    "foreach (SoundBinding binding in sounds)",
    "{",
    "    if (binding.Route != SoundRoute.Baked || binding.Preset == null)",
    "    {",
    "        continue;",
    "    }",
    "    AudioClip clip = binding.Preset.Bake(sampleRate, midiNote: binding.MidiNote);",
    "    if (clip != null)",
    "    {",
    "        bakedClips[binding.Sound] = clip;",
    "    }",
    "}",
    "",
    "// an event of the game plays it, like any clip",
    "void OnJumped() => PlayBaked(GameSound.Jump);",
    "",
    "oneShotSource.PlayOneShot(clip, bindings[sound].Volume * volumeScale);",
]
LIVE = [
    "// SoundEffects.cs - each Chime of a combo: one step further up the scale",
    "void OnChimeCollected(int combo)",
    "{",
    "    ...",
    "    int step = Mathf.Clamp(combo - 1, 0, chimeMaximumSteps);",
    "    PlayLive(GameSound.Chime, 1f, Pitch.PentatonicSemitones(step));",
    "}",
    "",
    "// LiveVoicePool.cs - an SfxPlayer synthesizes it now",
    "voice.preset = preset;",
    "voice.Play(velocity, semitones, midiNote);",
]
HOLD = [
    "// HeldSound.cs - hold: start the patch, as a MIDI note",
    "Player.Play(1f, 0f, midiNote);",
    "",
    "// ContinuousSounds.cs - every frame of a roll: bend it with the speed",
    "rollHumPlayer.pitchSemitones = speed01 * rollPitchRange;",
    "rollHumPlayer.cutoffOctaves = Mathf.Lerp(rollSlowCutoff, rollFastCutoff, speed01);",
    "rollHumPlayer.level = Mathf.Lerp(rollSlowLevel, 1f, speed01);",
    "",
    "// HeldSound.cs - let go: it fades over the patch's RELEASE",
    "Player.Release();",
]
SECTIONS = [
    "// MusicDirector.cs - each section: the pattern it holds or the song's order, and its floor",
    "new SectionCue(SectionId.SoftStart, SectionCue.SongOrder, 0.42f),",
    "new SectionCue(SectionId.RollingSines, SectionCue.SongOrder, 0.45f),",
    "new SectionCue(SectionId.FullCycle, SectionCue.SongOrder, 0.50f),",
    "new SectionCue(SectionId.WooferSteps, SectionCue.SongOrder, 0.50f),",
    "new SectionCue(SectionId.StaticField, PatternE, 0.50f),",
    "new SectionCue(SectionId.SplitSignal, SectionCue.SongOrder, 0.55f),",
    "new SectionCue(SectionId.Overdrive, SectionCue.SongOrder, 0.60f),",
    "new SectionCue(SectionId.TheResonator, SectionCue.KeepPattern, 0.42f),",
]
INTENSITY = [
    "// MusicDirector.cs - TargetIntensity: the section's floor at rest, 1 at top speed",
    "float target = Mathf.Lerp(currentCue.IntensityFloor, 1f, player != null ? player.Speed01 : 0f);",
    "",
    "// Update: fast in, slow out",
    "float target = TargetIntensity();",
    "float seconds = target > intensity ? riseSeconds : fallSeconds;",
    "intensity = Mathf.MoveTowards(intensity, target, Time.deltaTime / seconds);",
    "...",
    "music.intensity = intensity;",
]
CUES = [
    "// MusicDirector.cs - a pattern is cued for the next bar line ...",
    "void Cue(int pattern)",
    "{",
    "    heldPattern = pattern;",
    "    music.CuePattern(pattern);",
    "}",
    "// ... and again in every new bar, or the song's arrangement walks on",
    "void HoldPattern()",
    "{",
    "    int bar = music.Bar;",
    "    if (bar == lastBar)",
    "    {",
    "        return;",
    "    }",
    "    lastBar = bar;",
    "    if (heldPattern >= 0)",
    "    {",
    "        music.CuePattern(heldPattern);",
    "    }",
    "}",
]
MOMENTS = [
    "// MusicDirector.cs - hurt: only the kick is left, and the music dips",
    "duckTarget = 1f;",
    "intensity = 0f;",
    "...",
    "music.volume = musicVolume * Mathf.Lerp(1f, hurtVolume, duck);",
    "",
    "// the goal: the song stops, the fanfare is played on its instruments",
    "music.Stop();",
    "...",
    "music.SetMaster(SfxMaster.ReverbSize, fanfareReverbSize);",
    "...",
    "music.PlaySound(note.Sound, note.Velocity, note.MidiNote);",
    "...",
    "music.ReleaseSound(note.Sound, note.MidiNote);",
]


def excerpt(lines):
    """Checks a page of code against its source: every line must be a line of the file named in the comment above it."""
    source = None
    name = ""
    for line in lines:
        text = line.strip()
        # this file's own comments; one that begins with a file's name opens it
        if text.startswith("//"):
            found = re.match(r"// (\w+\.cs) - ", text)
            if found:
                name = found.group(1)
                source = {row.strip() for row in open(os.path.join(AUDIO, name), encoding="utf-8")}
        elif text and text != "..." and text not in source:
            sys.exit("a page of code shows a line that is not in %s: %s" % (name, text))
    return lines


def how():
    adaptive = recording("adaptive-music")
    cues = recording("sections-and-cues")
    hurt = recording("hurt-and-goal")
    shots = recording("one-shots")
    held = recording("held-sounds")
    first_chime = shots.when("ChimeCollected") - 0.7  # the cut between the baked part and the live part
    burst = adaptive.when("BurstReleased")
    hit = hurt.when("Hurt")
    goal = hurt.when("GoalReached")
    parts = [
        Slide(
            7.0,
            "SFX FORGE IN UNITY",
            "How the showcase makes its sound, and how to do it in your own game. The pages of code are silent.",
            picture(
                "03-rolling-sines",
                "IN THIS VIDEO",
                [
                    ("1", "install, and a first sound"),
                    ("2 3 4", "three ways to play a sound: baked, live, held"),
                    ("5", "music that follows the game"),
                    ("6 7", "the event seam, and what to copy"),
                ],
            ),
        ),
        Slide(
            9.0,
            "1  INSTALL",
            "Add the package from disk in the Package Manager, or add this line to the dependencies. The path is "
            "yours.",
            code(
                [
                    "{",
                    "  \"dependencies\": {",
                    "    \"com.catsplat.sfxforge\": \"file:<path to>/com.catsplat.sfxforge\"",
                    "  }",
                    "}",
                ],
                {2},
                "Packages/manifest.json",
            ),
        ),
        Slide(
            9.0,
            "1  A FIRST SOUND",
            "Put this on any GameObject and press Play. One line makes the sound: a laser, synthesized as you click.",
            code(FIRST, {14}),
        ),
        Slide(
            9.5,
            "2  BAKED",
            "Route 1, baked. Render the patch into a clip once, then play it like any clip. For short sounds that "
            "are always the same.",
            code(excerpt(BAKED), {7}),
        ),
        Clip(
            "one-shots",
            [(0.0, "2  BAKED", "Two jumps and two landings, all baked clips.")],
            last=first_chime,
            focus="played",
        ),
        Slide(
            9.0,
            "3  LIVE",
            "Route 2, a live voice. An SfxPlayer synthesizes the sound when it plays, so each one can have its own "
            "pitch.",
            code(excerpt(LIVE), {10}),
        ),
        Clip(
            "one-shots",
            [
                (first_chime, "3  LIVE", "Each Chime is one step higher, up to step 10. Watch JUST PLAYED."),
                (
                    shots.when("Skidded") + 0.3,
                    "3  LIVE",
                    "The skid and the Woofer are baked. Each sound has the route that fits it.",
                ),
            ],
            first=first_chime,
            focus="played",
        ),
        Slide(
            10.0,
            "4  HELD",
            "Route 3, held. Start a sustained patch on its own SfxPlayer, write its live values every frame, then "
            "let go.",
            code(excerpt(HOLD), {4}),
        ),
        Clip(
            "held-sounds",
            [
                (0.0, "4  HELD", "The Wind-Up rev starts again on each tap. Its pitch follows the charge."),
                (
                    held.when("BurstReleased") + 1.2,
                    "4  HELD",
                    "That was the Burst, a baked whoosh. Next a roll: a hum is held while Brio rolls.",
                ),
                (
                    held.when("TuckStarted", nth=1) + 1.3,
                    "4  HELD",
                    "Faster: the hum is higher, brighter and louder, and the wind joins.",
                ),
            ],
            focus="held",
        ),
        Slide(
            9.0,
            "5  MUSIC: THE SONG",
            "One SfxMusicPlayer plays one song, in the song's own order. A section sets a floor; one section holds "
            "a pattern.",
            code(excerpt(SECTIONS), {5}),
        ),
        Slide(
            8.5,
            "5  MUSIC: INTENSITY",
            "Write intensity every frame: it decides which instruments play. Here the player's speed fills it from "
            "the floor to 1.",
            code(excerpt(INTENSITY), {1, 8}),
        ),
        Clip(
            "adaptive-music",
            [
                (
                    0.0,
                    "5  MUSIC: INTENSITY",
                    "Standing: %.2f, the rhythm section. Walking, then running: strings, then FM keys come in."
                    % adaptive.floor["SoftStart"],
                ),
                (burst - 2.6, "5  MUSIC: INTENSITY", "Now a Wind-Up at rest."),
                (
                    burst,
                    "5  MUSIC: INTENSITY",
                    "A Burst, nearly top speed: brass and bells join. The band thins out more slowly.",
                ),
            ],
            focus="layers",
        ),
        Slide(
            9.0,
            "5  MUSIC: CUES",
            "CuePattern changes the pattern on the next bar line. Cue it again in every bar to hold it.",
            code(excerpt(CUES), {4, 15, 16, 17}),
        ),
        Clip(
            "sections-and-cues",
            [
                (0.0, "5  MUSIC: CUES", "Woofer Steps: the song in its own order. Static Field will cue pattern E."),
                (
                    cues.when("SectionEntered", "StaticField") + 0.1,
                    "5  MUSIC: CUES",
                    "The change comes on the bar line, and E is held. The song's bells answer the Tuning Fork.",
                ),
            ],
            focus="pattern",
        ),
        Slide(
            10.0,
            "5  MUSIC: MOMENTS",
            "The game's moments are a few more calls: a dip when you are hurt, notes played by hand at the goal.",
            code(excerpt(MOMENTS), {4, 11}),
        ),
        Clip(
            "hurt-and-goal",
            [
                (0.0, "5  MUSIC: MOMENTS", "Split Signal: rhythm section, strings, then FM keys."),
                (hit, "5  MUSIC: MOMENTS", "A hit: Chimes scatter, the band drops to the kick, the volume dips."),
                (
                    hurt.when("SectionEntered", "TheResonator"),
                    "5  MUSIC: MOMENTS",
                    "Both came back in two seconds. Now the goal.",
                ),
                (
                    goal + 0.3,
                    "5  MUSIC: MOMENTS",
                    "The song stops. Strings, brass, bells and timpani: PlaySound, by hand.",
                ),
            ],
            focus="volume",
        ),
        Slide(
            9.0,
            "6  THE EVENT SEAM",
            "Gameplay raises events and never calls SFX FORGE. The audio folder listens, and reads the player's state.",
            seam,
        ),
        Slide(
            10.0,
            "7  COPY IT",
            "The steps, and the file that shows each. Or copy Scripts/Audio whole, with ten files of Scripts/Core "
            "and a song.",
            table(
                [
                    ("1", "Add the package", "Packages/manifest.json"),
                    ("2", "Make a seam: a static class of events", "Scripts/Core/GameEvents.cs"),
                    ("3", "Write your sounds as patches, in one file", "Scripts/Audio/SoundLibrary.cs"),
                    ("4", "One-shots: bake them, or play them live", "SoundEffects.cs, LiveVoicePool.cs"),
                    ("5", "Held sounds: one SfxPlayer each", "ContinuousSounds.cs, HeldSound.cs"),
                    ("6", "Music: one SfxMusicPlayer, a song and a director", "MusicDirector.cs, DemoSong.asset"),
                    ("7", "Put it in the scene", "AudioInstaller.cs"),
                ]
            ),
        ),
        Slide(
            6.0,
            "WHERE TO FIND IT",
            "Open source, MIT. The Unity manual has every value, thirteen recipes and the API.",
            table(
                [
                    ("The game", "one level, start to goal", "unity/Demo/Assets/Showcase"),
                    ("Its sound", "sixteen short files", "unity/Demo/Assets/Showcase/Scripts/Audio"),
                    ("The manual", "every value, recipes, the API", "docs/UNITY.md"),
                    ("The package", "Windows x64", "unity/com.catsplat.sfxforge"),
                ]
            ),
        ),
    ]
    render("sfx-forge-unity-how-it-works", "in Unity: how it works", parts, -18.0)


if __name__ == "__main__":
    which = sys.argv[1:] or ["showcase", "how"]
    if "showcase" in which:
        showcase()
    if "how" in which:
        how()
