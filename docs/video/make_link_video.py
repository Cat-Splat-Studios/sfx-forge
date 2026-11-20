"""Makes the link video: SFX FORGE in the emulator and the showcase in Unity, joined by two MIDI ports, both recorded at once.

    python docs/video/make_link_video.py            (needs ffmpeg, Pillow and numpy)   ->  out/video/sfx-forge-unity-link.mp4

It is put together from one recording session (see "Recording the link" in unity/Demo/Assets/Showcase/README.md):
  out/link/emulator/        what the emulator's page showed, as JPEGs with their wall-clock times (tests/emulator-link.mjs --frames)
  out/unity-video/link-session/   the game's pictures, its sound and link.json: the wall clock of every frame and everything
                                  that went over the ports (the Unity take SfxShowcase.Tests.LinkSession)
For each of the game's frames the emulator picture of the same moment on the wall clock is shown beside it. Nothing is
staged afterwards: the captions and the list on the right are written from link.json. The sound is the game's; the
emulator's own sound was not recorded.
"""
import json
import os
import subprocess
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TAKE = os.path.join(ROOT, "out", "unity-video", "link-session")
EMU = os.path.join(ROOT, "out", "link", "emulator")
OUT = os.path.join(ROOT, "out", "video", "sfx-forge-unity-link.mp4")
FPS, W, H = 30, 1920, 1080
BG, PANEL, LINE, TEXT, DIM, EMBER, BLUE = "#0E0B09", "#1B1613", "#3A2E26", "#F4EBDD", "#A08E7E", "#FF8A3C", "#6EC6FF"
DEVICE = (28, 243, 1092, 515)         # in the emulator's page (1484 x 905): the device's upper half, its display,
SCREEN = (100, 268, 340, 447)         # and the line under the device that says what MIDI came in
STATUS = (396, 822, 724, 842)
PEAK = 0.89                           # the video's largest sample: the take is brought up to this, never over


def font(size, bold=True):
    for name in (["segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"] if bold else ["segoeui.ttf", "arial.ttf", "DejaVuSans.ttf"]):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def wrap(d, text, f, width):
    lines, line = [], ""
    for word in text.split():
        if d.textlength((line + " " + word).strip(), font=f) > width and line:
            lines.append(line)
            line = word
        else:
            line = (line + " " + word).strip()
    return lines + ([line] if line else [])


def captions(events, frames):
    """What the video says, and from which frame: written from what went over the ports."""
    def first(direction, has):
        return next((e["f"] for e in events if e["direction"] == direction and has in e["what"]), None)
    send, song = first("cue", "F5"), first("device to game", "the song")
    export = next((e["f"] for e in events if e["direction"] == "cue" and e["what"] == "K1"), None)
    patch = first("device to game", "a patch")
    run = first("game to device", "SOFT START")
    song_words = next(e["what"] for e in events if e["direction"] == "device to game" and "the song" in e["what"])
    messages = song_words.split(" in ")[1].split(" messages")[0]
    patch_words = next(e["what"] for e in events if e["direction"] == "device to game" and "a patch" in e["what"])
    pad = patch_words.split("pad ")[1].split(",")[0]
    out = [(0, "THE LINK", "Left: SFX FORGE in the emulator. Right: the showcase in Unity. Two MIDI ports join them; both were recorded at once."),
           (run, "THE GAME DRIVES THE DEVICE", "The game sends its music's intensity every frame, and the name of each section. The device's INTENSITY follows Brio's speed."),
           (send, "SEND", "SEND is pressed on the device's SONG page: 16 sounds and the song leave it, piece by piece."),
           (song, "THE SONG ARRIVES", "All of it is in Unity: 7,699 bytes in %s messages. The game's music player has the device's song from here." % messages),
           (export, "A SOUND, CHANGED", "On the device a sound is mutated, then EXPORT is pressed."),
           (patch, "THE PATCH ARRIVES", "Pad %s's patch is in Unity, and the game plays it: three notes on top of the music." % pad),
           (frames - 150, "SFX FORGE", "What is made on the device plays in the game, while the game plays. The manual: docs/UNITY.md, section 6.")]
    return [c for c in out if c[0] is not None]


def main():
    link = json.load(open(os.path.join(TAKE, "link.json"), encoding="utf-8"))
    wall, events = link["frameWallClock"], link["events"]
    emu = json.load(open(os.path.join(EMU, "frames.json"), encoding="utf-8"))["frames"]
    telemetry = json.load(open(os.path.join(TAKE, "telemetry.json"), encoding="utf-8"))
    frames = min(len(wall), telemetry["frameCount"])
    if not emu:
        sys.exit("no emulator pictures in " + EMU)
    # the emulator's clock is the browser's; both are wall clocks of this machine, so no offset is applied
    print("game frames: %d (%.1f s)   emulator pictures: %d (%.1f s)" % (frames, frames / FPS, len(emu), emu[-1]["t"] - emu[0]["t"]))

    # ---- the sound: the take, brought up to PEAK ----
    w = wave.open(os.path.join(TAKE, "audio.wav"))
    rate, channels = w.getframerate(), w.getnchannels()
    audio = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
    gain = PEAK / max(1e-6, float(np.abs(audio).max()))
    audio = audio * gain
    fade = int(0.4 * rate) * channels
    audio[-fade:] *= np.repeat(np.linspace(1.0, 0.0, fade // channels), channels)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    wav = os.path.join(ROOT, "out", "link", "video-audio.wav")
    o = wave.open(wav, "wb")
    o.setnchannels(channels); o.setsampwidth(2); o.setframerate(rate)
    o.writeframes((np.clip(audio, -1, 1) * 32767).astype(np.int16).tobytes())
    o.close()
    print("sound: gain %.2f, peak %.2f" % (gain, float(np.abs(audio).max())))

    caps = captions(events, frames)
    big, cap, small, mono, label = font(46), font(34, False), font(22, False), font(22), font(20)
    base = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(base)
    title = "SFX FORGE  ·  the device and the game, linked"
    d.text((W - 60 - d.textlength(title, font=font(24, False)), 58), title, font=font(24, False), fill=DIM)
    d.text((60, 132), "THE DEVICE  ·  SFX FORGE in the emulator", font=label, fill=EMBER)
    d.text((1000, 132), "THE GAME  ·  the showcase in Unity", font=label, fill=BLUE)
    d.rounded_rectangle([52, 162, 948, 668], 12, fill=PANEL, outline=LINE, width=2)
    d.rounded_rectangle([992, 162, 1868, 668], 12, fill=PANEL, outline=LINE, width=2)
    d.text((60, 690), "OVER THE PORTS", font=label, fill=DIM)

    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-",
                           "-i", wav, "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", "-preset", "veryfast", "-c:a", "aac", "-b:a", "192k",
                           "-shortest", "-movflags", "+faststart", OUT], stdin=subprocess.PIPE)
    k, shown, device, screen, status = 0, None, None, None, None
    for f in range(frames):
        im = base.copy()
        d = ImageDraw.Draw(im)
        while k + 1 < len(emu) and emu[k + 1]["t"] <= wall[f]:
            k += 1
        if shown != k:
            page = Image.open(os.path.join(EMU, emu[k]["file"])).convert("RGB")
            device = page.crop(DEVICE).resize((880, 225), Image.LANCZOS)
            screen = page.crop(SCREEN).resize((336, 251), Image.LANCZOS)
            status = page.crop(STATUS).resize((492, 30), Image.LANCZOS)
            shown = k
        im.paste(device, (60, 170))
        im.paste(screen, (60, 405))
        d.text((416, 410), "ITS DISPLAY, LARGER", font=label, fill=DIM)
        d.text((416, 560), "THE LAST MIDI IT RECEIVED", font=label, fill=DIM)
        im.paste(status, (416, 592))
        game = Image.open(os.path.join(TAKE, "f%05d.png" % f)).convert("RGB").resize((860, 484), Image.LANCZOS)
        im.paste(game, (1000, 172))
        frame = telemetry["frames"][f]
        intensity = frame["music"]["intensity"]
        d.text((1000, 684), "the music's intensity", font=small, fill=DIM)
        d.rounded_rectangle([1220, 692, 1620, 708], 6, fill="#2A211B")
        d.rounded_rectangle([1220, 692, 1220 + int(400 * intensity), 708], 6, fill=EMBER)
        d.text((1636, 684), "%.2f  -> the device" % intensity, font=small, fill=TEXT)

        # the list: the last things that went over the ports, the newest at the bottom and bright for a moment
        recent = [e for e in events if e["f"] <= f and e["direction"] != "cue" and "intensity" not in e["what"]][-5:]
        y = 722
        for e in recent:
            to_device = e["direction"] == "game to device"
            fresh = f - e["f"] < 45
            arrow = "game  →  device" if to_device else "device  →  game"
            d.text((60, y), arrow, font=mono, fill=(BLUE if to_device else EMBER) if fresh else DIM)
            d.text((300, y), e["what"][:112], font=small, fill=TEXT if fresh else DIM)
            y += 30
        chapter, text = "", ""
        for at, c, t in caps:
            if at <= f:
                chapter, text = c, t
        d.text((60, 44), chapter, font=big, fill=EMBER)
        for n, line in enumerate(wrap(d, text, cap, 1800)[:2]):
            d.text((60, 900 + n * 50), line, font=cap, fill=TEXT)
        d.rectangle([0, H - 6, int(W * f / max(1, frames - 1)), H], fill=EMBER)
        ff.stdin.write(im.tobytes())
    ff.stdin.close()
    ff.wait()
    print("wrote", OUT)


if __name__ == "__main__":
    main()
