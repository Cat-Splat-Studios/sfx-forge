#!/usr/bin/env python3
"""Checks what the Unity capture rig recorded (out/unity-video/<take>/).

For every take it prints the number of pictures, their size, the length and level of audio.wav, and whether
telemetry.json agrees with both. Then two measurements of "do sound and picture agree":

  * events: for a sound that starts in silence, the frame telemetry.json gives for its event against the place its
    sound starts in the wav. The take "sync-check" is made for this (three jumps, three Chimes, no music).
  * music: the frames in which telemetry.json says a new bar began against the bar lines the song's tempo puts in
    the wav. They must not drift over a take.

Run it from the repository's root after recording:

    python docs/video/check_unity_capture.py            # every take
    python docs/video/check_unity_capture.py full-run   # one take

It reads the takes from the folder in the environment variable SFXFORGE_CAPTURE_DIR if that is set (the takes use
the same variable), otherwise from out/unity-video.

It only reads. Exit code 1 if a take is silent, clipped, of the wrong size, or out of step.
"""
import json
import math
import os
import struct
import sys
import wave

# the same folder the takes were recorded into: SFXFORGE_CAPTURE_DIR, or out/unity-video in the repository
ROOT = os.environ.get("SFXFORGE_CAPTURE_DIR") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "out", "unity-video"
)
WIDTH = 1920
HEIGHT = 1080
# an event's sound may start this many frames from its picture
MOST_FRAMES_APART = 2.0
ONSET_EVENTS = {"Jumped": "Jump (baked clip)", "ChimeCollected": "Chime (live voice)"}


def png_size(path):
    with open(path, "rb") as handle:
        head = handle.read(24)
    return struct.unpack(">II", head[16:24])


def read_wav(path):
    with wave.open(path, "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        count = handle.getnframes()
        raw = handle.readframes(count)
    assert width == 2, "the rig writes 16-bit samples"
    values = struct.unpack("<%dh" % (count * channels), raw)
    # one loudness per sample frame: the larger of the channels, 0..1
    mono = [max(abs(values[i * channels + c]) for c in range(channels)) / 32767.0 for i in range(count)]
    return rate, channels, mono


def decibels(level):
    return -120.0 if level <= 0 else 20.0 * math.log10(level)


def onset(mono, rate, start, end):
    """The first sample in [start, end) that is clearly louder than the 100 ms before `start`."""
    before = mono[max(0, start - rate // 10) : start] or [0.0]
    floor = max(before)
    threshold = max(0.004, floor * 4.0)
    for index in range(start, min(end, len(mono))):
        if mono[index] > threshold:
            return index, floor
    return None, floor


def check_events(telemetry, mono, rate):
    """Event frame against sound onset, for events whose sound starts in near silence."""
    per_frame = telemetry["audio"]["samplesPerFrame"]
    frames = telemetry["frames"]
    worst = 0.0
    rows = []
    for event in telemetry["events"]:
        if event["name"] not in ONSET_EVENTS:
            continue
        frame = event["f"]
        # where the wav stood when the event's frame began: nothing of it can sound earlier
        start = frames[frame - 1]["audioEnd"] if frame > 0 else 0
        found, floor = onset(mono, rate, start, start + per_frame * 6)
        if floor > 0.004:
            continue  # something else was sounding: not a clean measurement
        if found is None:
            rows.append((event, None))
            worst = float("inf")
            continue
        apart = (found - frame * per_frame) / per_frame
        rows.append((event, apart))
        worst = max(worst, abs(apart))
    return rows, worst


def check_bars(telemetry):
    """The frames in which a new bar began against the song's bar lines, both in samples of the wav."""
    song = telemetry.get("song")
    if not song or not song["bpm"]:
        return None
    rate = telemetry["audio"]["sampleRate"]
    bar_samples = song["stepsPerBar"] * 60.0 / song["bpm"] / 4.0 * rate
    lines = []
    previous = None
    for frame in telemetry["frames"]:
        music = frame.get("music")
        if not music or not music["playing"]:
            previous = None
            continue
        if previous is not None and music["bar"] == previous["bar"] + 1:
            # the bar line fell inside the samples rendered for this frame
            lines.append((music["bar"], previous["audioEnd"], frame["audioEnd"]))
        previous = dict(bar=music["bar"], audioEnd=frame["audioEnd"])
    if len(lines) < 2:
        return None
    # one offset fits all: bar b begins at offset + b * bar_samples, which must lie in each frame's window
    first_bar = lines[0][0]
    low = -float("inf")
    high = float("inf")
    for bar, begin, end in lines:
        shift = (bar - first_bar) * bar_samples
        low, high = max(low, begin - shift), min(high, end - shift)
    slack = (high - low) / rate * 1000.0
    return len(lines), bar_samples / rate, slack


def check_take(name):
    """One take's folder: prints what it holds and returns what is wrong with it, as lines. In this order:
    the pictures, the sound, telemetry.json, each event's sound against its picture, the music's bar lines."""
    folder = os.path.join(ROOT, name)

    # ---- the pictures: there, numbered without a gap, the right size
    pictures = sorted(f for f in os.listdir(folder) if f.startswith("f") and f.endswith(".png"))
    problems = []
    if not pictures:
        return ["no pictures"]
    expected = ["f%05d.png" % i for i in range(len(pictures))]
    if pictures != expected:
        problems.append("the pictures are not numbered f00000 .. without gaps")
    for sample in (pictures[0], pictures[len(pictures) // 2], pictures[-1]):
        if png_size(os.path.join(folder, sample)) != (WIDTH, HEIGHT):
            problems.append("%s is not %dx%d" % (sample, WIDTH, HEIGHT))

    # ---- the sound: stereo, not silent, not clipped, as long as the pictures
    rate, channels, mono = read_wav(os.path.join(folder, "audio.wav"))
    peak = max(mono) if mono else 0.0
    rms = math.sqrt(sum(v * v for v in mono) / len(mono)) if mono else 0.0
    seconds = len(pictures) / 30.0
    wav_seconds = len(mono) / rate
    print(
        "%-18s %5d pictures  %7.3f s   wav %7.3f s  %d Hz %d ch  peak %.4f (%.1f dBFS)  rms %.4f (%.1f dBFS)"
        % (name, len(pictures), seconds, wav_seconds, rate, channels, peak, decibels(peak), rms, decibels(rms))
    )
    if channels != 2:
        problems.append("the wav is not stereo")
    if peak < 0.01:
        problems.append("the wav is silent")
    full_scale = sum(1 for v in mono if v >= 1.0)
    if full_scale:
        problems.append("%d sample frames are at full scale: the mix was clipped" % full_scale)
    if abs(seconds - wav_seconds) > 0.03:
        problems.append("the wav and the pictures differ by %.3f s" % (wav_seconds - seconds))

    # ---- telemetry.json: a frame a picture, and its last frame ends where the sound ends
    with open(os.path.join(folder, "telemetry.json"), encoding="utf-8") as handle:
        telemetry = json.load(handle)
    if telemetry["frameCount"] != len(pictures) or len(telemetry["frames"]) != len(pictures):
        problems.append("telemetry.json has %d frames" % len(telemetry["frames"]))
    if telemetry["frames"] and telemetry["frames"][-1]["audioEnd"] != len(mono):
        problems.append("telemetry.json's last audioEnd is not the wav's length")
    print(
        "%-18s telemetry: %d frames, %d events, %d sounds"
        % ("", len(telemetry["frames"]), len(telemetry["events"]), len(telemetry["sounds"]))
    )

    # ---- the events that start a sound in silence: how far the sound is from the picture
    rows, worst = check_events(telemetry, mono, rate)
    for event, apart in rows:
        label = ONSET_EVENTS[event["name"]]
        if apart is None:
            print("%-18s   frame %4d  %-20s NO SOUND FOUND" % ("", event["f"], label))
        else:
            print(
                "%-18s   frame %4d  %-20s sound starts %+.2f frames (%+.1f ms) from the frame's start"
                % ("", event["f"], label, apart, apart * 1000.0 / 30.0)
            )
    if rows:
        print("%-18s events measured in silence: %d, furthest %.2f frames" % ("", len(rows), worst))
        if worst > MOST_FRAMES_APART:
            problems.append("an event's sound is %.2f frames from its picture" % worst)

    # ---- the music: do its bar lines fit one grid?
    bars = check_bars(telemetry)
    if bars:
        count, bar_seconds, slack = bars
        verdict = (
            "all fit one grid, with %.1f ms to spare" % slack
            if slack >= 0
            else "DO NOT fit one grid (%.1f ms short)" % -slack
        )
        print("%-18s music: %d bar lines, a bar is %.3f s: %s" % ("", count, bar_seconds, verdict))
        if slack < 0:
            problems.append("the music's bar lines drift against the frames")
    return problems


def main():
    if not os.path.isdir(ROOT):
        print("nothing recorded yet: %s does not exist" % os.path.normpath(ROOT))
        return 1
    names = sys.argv[1:] or sorted(
        d for d in os.listdir(ROOT) if os.path.isfile(os.path.join(ROOT, d, "telemetry.json"))
    )
    failed = False
    for name in names:
        for problem in check_take(name):
            failed = True
            print("%-18s PROBLEM: %s" % ("", problem))
    stills = os.path.join(ROOT, "stills")
    if not sys.argv[1:] and os.path.isdir(stills):
        found = sorted(f for f in os.listdir(stills) if f.endswith(".png"))
        wrong = [f for f in found if png_size(os.path.join(stills, f)) != (WIDTH, HEIGHT)]
        print(
            "%-18s %5d pictures%s"
            % ("stills", len(found), "" if not wrong else "  PROBLEM: wrong size: " + ", ".join(wrong))
        )
        failed = failed or bool(wrong)
    print("CAPTURE CHECK: %s" % ("FAILED" if failed else "ok"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
