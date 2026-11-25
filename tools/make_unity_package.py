#!/usr/bin/env python3
"""Makes the standalone Unity package repository from this repository.

    python tools/make_unity_package.py [target folder]        (default: ../unity-sfx-forge)

Everything in the target except .git is written by this script, and written again from nothing on
every run: the package (Runtime, Editor), the showcase as a sample (Samples~/Showcase), the manuals
(Documentation~) with their figures, the videos and the previews cut from them, the built Vibe (Firmware~),
the front page, and a .meta file for everything Unity will see. Nothing is edited by hand over there: change
the source here, or this script, and run it again.

It builds the package in a new folder beside the target and checks it there: no e-mail address, no
path of the machine it ran on (also inside the firmware's zip), every relative link resolves. Only
when all of that passed does it replace what it owns in the target. When something it needs is
missing, or a check fails, it stops with an error and the target is as it was. It never starts Unity.

Needs Python 3 and ffmpeg on the PATH. The videos must exist (videos/, made by docs/video/*.py),
the figures must be in docs/img (docs/unity_figures.py; its pictures of the editor UI are copies of what the
tests of tests/unity-inspectors wrote to out/inspector-shots),
the Vibe must be built (build/sfx-forge.zip, made by build.sh, which also strips its debug sections),
and the SDK and Zig must be in .tools: the licence texts of what is compiled into the firmware and
the native library are read from there (Documentation~/licenses).
"""
# How this file reads, from top to bottom:
#   the constants        VIDEOS is the one you edit when a video was made again
#   small helpers        fail, src, need, read_text, write_text, replace_once, the .meta files, copying, run
#   the target           prepare_target, install
#   one make_* function for each part of the package, in the order build() calls them:
#                        make_package_json, make_code, make_sample, make_manuals, make_videos, make_firmware,
#                        make_licences, make_readme, make_companions
#   the three checks     check_metas, check_privacy, check_links; report_sizes
#   build, main          build() is the table of contents: read it first
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# tools/strip_wasm_debug.py: what a .wasm must not hold
import strip_wasm_debug  # noqa: E402

PACKAGE_ID = "com.catsplatstudios.sfxforge"
OLD_PACKAGE_ID = "com.catsplat.sfxforge"
VERSION = "1.0.0"
RELEASED = "2026-11-20"
DISPLAY_NAME = "SFX Forge"
UNITY = "6000.3"
UNITY_TESTED = "6000.3.8f1"
FIRMWARE_VERSION = "1.7.2"
AUTHOR = {"name": "Cat Splat Studios", "url": "https://github.com/Cat-Splat-Studios"}
# Unity names the imported folder after this (Assets/Samples/<package name>/<version>/<this>) and turns characters
# a file name cannot hold into "_": a colon here gave the folder "Showcase_ Brio - First Harmonic". Keep it plain.
SAMPLE_NAME = "Showcase"
SDK_URL = "https://github.com/Vibe-FM-SDK/vibe-fm-sdk"
# The licence of everything that is Cat Splat Studios' own, as an SPDX id (LICENSE, ADDITIONAL-PERMISSIONS.md).
LICENCE_ID = "GPL-3.0-or-later"

SRC_PACKAGE = "unity/com.catsplat.sfxforge"
SRC_SHOWCASE = "unity/Demo/Assets/Showcase"
SRC_OLD_SONG = "unity/Demo/Assets/SfxDemo/DemoSong.bytes"

# What this script owns at the target's root. A run removes exactly these and writes them again.
# Anything else there (except .git) stops the run: it is not ours to delete or to leave in a package.
# fmt: off
OWNED = [
    "package.json", "package.json.meta", "README.md", "README.md.meta", "CHANGELOG.md", "CHANGELOG.md.meta",
    "LICENSE.md", "LICENSE.md.meta", "ADDITIONAL-PERMISSIONS.md", "ADDITIONAL-PERMISSIONS.md.meta",
    "Third Party Notices.md", "Third Party Notices.md.meta", "Runtime", "Runtime.meta", "Editor", "Editor.meta",
    "Samples~", "Documentation~", "Firmware~", ".gitignore", ".gitattributes",
]
# fmt: on

# The five videos: file, title, what it shows (one line), where the preview is cut (start, seconds),
# where the poster frame is taken, and how long the whole video is expected to be (checked, +-2 s).
VIDEOS = [
    dict(
        file="sfx-forge-unity-link.mp4",
        title="The link: device and game, joined over MIDI",
        seconds=31,
        shows=(
            "SFX FORGE in the emulator beside the game in Unity. SEND is pressed on the device and the song arrives "
            "in the running game; a sound is mutated, EXPORT is pressed, and the game plays the patch."
        ),
        clip=(12.0, 13.5),
        poster=13.5,
    ),
    dict(
        file="sfx-forge-unity-showcase.mp4",
        title="The showcase game, start to goal",
        seconds=115,
        shows="*Brio: First Harmonic* played through, with a panel naming what SFX FORGE is doing at each moment: "
        "the song's bar, the intensity, the layers, every sound and its route.",
        clip=(28.0, 10.0),
        poster=33.0,
    ),
    dict(
        file="sfx-forge-unity-how-it-works.mp4",
        title="How it works in Unity",
        seconds=246,
        shows="The three ways to play a sound and the adaptive music, one at a time: a page of the showcase's code, "
        "then the game doing it.",
        clip=(143.0, 10.0),
        poster=150.0,
    ),
    dict(
        file="sfx-forge-tour.mp4",
        title="The device: a tour",
        seconds=143,
        shows=(
            "The instrument itself: sound effects, MUTATE, the five engines, the sequencer and its finer grid, "
            "layers that follow INTENSITY, automation, the mixer, the link page."
        ),
        clip=(88.0, 10.0),
        poster=94.0,
    ),
    dict(
        file="sfx-forge-build-a-song.mp4",
        title="The device: building the song",
        seconds=224,
        shows=(
            "A song played in from nothing with pads and keys over a looping pattern, with ERASE and UNDO, a second "
            "pattern with a hi-hat roll on a grid of 1/32, then arranged, layered, automated, played on the PERFORM "
            "page, mixed and sent to the game."
        ),
        clip=(24.8, 10.0),
        poster=33.3,
    ),
]
# The pictures of the editor UI the front page shows (docs/img, copied there by docs/unity_figures.py from what the
# inspectors' tests rendered). The manual uses them too; they are named here so a missing one stops the run by name.
README_FIGURES = ["unity-inspector-sound-osc.png", "unity-inspector-song.png", "unity-inspector-window.png"]
PREVIEW_WIDTH = 640
PREVIEW_FPS = 10
POSTER_WIDTH = 960

# fmt: off
TEXT_EXTENSIONS = {
    ".md", ".cs", ".json", ".asmdef", ".meta", ".unity", ".asset", ".txt", ".gitignore", ".gitattributes", "",
}
# fmt: on
ALLOWED_DRIVE_PATHS = ["C:/Program Files/Unity/Hub/Editor/", "C:\\\\somewhere\\\\", "C:\\somewhere\\"]


def fail(message):
    sys.exit("make_unity_package: ERROR: " + message)


def src(*parts):
    return os.path.join(ROOT, *parts)


def need(path, what):
    if not os.path.exists(path):
        fail("%s is missing: %s" % (what, os.path.relpath(path, ROOT)))
    return path


def read_text(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text if text.endswith("\n") else text + "\n")


def replace_once(text, old, new, where):
    """A replacement that must find its text exactly once: if the source changed, say so instead of shipping a stale
    line."""
    count = text.count(old)
    if count != 1:
        fail(
            "%s: expected exactly one occurrence of %r, found %d. The source changed: update "
            "tools/make_unity_package.py." % (where, old[:90], count)
        )
    return text.replace(old, new)


def guid_for(relative_path):
    return hashlib.md5((PACKAGE_ID + "/" + relative_path.replace("\\", "/")).encode("utf-8")).hexdigest()


def write_meta(target, relative_path, importer):
    body = "fileFormatVersion: 2\nguid: %s\n" % guid_for(relative_path)
    if importer == "folder":
        body += "folderAsset: yes\nDefaultImporter:\n"
    else:
        body += importer + ":\n"
    body += "  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n"
    write_text(os.path.join(target, relative_path + ".meta"), body)


def copy_file(source, destination):
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copyfile(source, destination)


def copy_tree_with_metas(source, destination, what):
    """Copies a Unity folder byte for byte and insists that every file and folder in it has its .meta (the GUIDs are
    the source's)."""
    need(source, what)
    count = 0
    for folder, folders, files in os.walk(source):
        folders.sort()
        for name in sorted(folders + files):
            path = os.path.join(folder, name)
            if not name.endswith(".meta") and not os.path.exists(path + ".meta"):
                fail("%s: %s has no .meta file" % (what, os.path.relpath(path, ROOT)))
            if name.endswith(".meta") and not os.path.exists(path[:-5]):
                fail("%s: %s has no file or folder beside it" % (what, os.path.relpath(path, ROOT)))
        for name in sorted(files):
            path = os.path.join(folder, name)
            copy_file(path, os.path.join(destination, os.path.relpath(path, source)))
            count += 1
    return count


def inside(path, folder):
    """Is the path the folder or something in it? (Another drive is simply outside.)"""
    path, folder = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(folder))
    return path == folder or path.startswith(folder.rstrip(os.sep) + os.sep)


def run(arguments, what):
    result = subprocess.run(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        fail("%s failed:\n%s" % (what, result.stderr.decode("utf-8", "replace")[-1500:]))
    return result.stdout.decode("utf-8", "replace")


# ---------------------------------------------------------------- the target


def prepare_target(target):
    target = os.path.abspath(target)
    root = os.path.abspath(ROOT)
    if inside(target, root):
        fail("the target must not be this repository or a folder inside it: " + target)
    if os.path.exists(target):
        if not os.path.isdir(target):
            fail("the target exists and is not a folder")
        strangers = sorted(name for name in os.listdir(target) if name != ".git" and name not in OWNED)
        if strangers:
            fail(
                "the target holds things this script does not write, so it will not touch the folder: "
                + ", ".join(strangers)
            )
    return target  # nothing is deleted or written here: see install()


def install(staging, target):
    """The package was built and checked in the staging folder: only now is the target touched."""
    strangers = sorted(name for name in os.listdir(staging) if name not in OWNED)
    if strangers:
        fail("this script wrote something it does not own (add it to OWNED): " + ", ".join(strangers))
    os.makedirs(target, exist_ok=True)
    for name in OWNED:  # exactly these, by name
        path = os.path.join(target, name)
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path)
        elif os.path.lexists(path):
            os.remove(path)
    for name in sorted(os.listdir(staging)):
        shutil.move(os.path.join(staging, name), os.path.join(target, name))


# ---------------------------------------------------------------- package, sample


def make_package_json(target):
    old = json.loads(read_text(need(src(SRC_PACKAGE, "package.json"), "the source package's manifest")))
    if old.get("name") != OLD_PACKAGE_ID:
        fail("the source package is no longer named " + OLD_PACKAGE_ID)
    # fmt: off
    manifest = {
        "name": PACKAGE_ID,
        "version": VERSION,
        "displayName": DISPLAY_NAME,
        "description": (
            "Plays the sounds and songs of SFX FORGE, a game-audio synthesizer for VIBE FM, in a Unity game with "
            "the device's own engine: patches written in code or exported from the device become assets, sound "
            "effects are synthesized live or baked to clips, and a song's layers follow the game. Includes a MIDI "
            "link to the device for tuning while the game runs, the manuals, and a showcase game as a sample. "
            "Windows x64 only."
        ),
        "unity": UNITY,
        "keywords": [
            "audio", "synthesizer", "sound effects", "procedural audio", "adaptive music", "midi", "sysex", "vibe fm",
        ],
        "author": AUTHOR,
        "license": LICENCE_ID,
        # The built-in modules the code uses: AudioSource and AudioMixer, the telemetry sender's Rigidbody,
        # Rigidbody2D and Animator sources, UI Toolkit (UIElements) for the Inspectors and the editor window, and
        # IMGUI, which UI Toolkit itself needs. Two are for the sample only: Screen Capture (the build probe's
        # screenshot) and Image Conversion (the capture rig's PNGs).
        "dependencies": {
            "com.unity.modules.animation": "1.0.0",
            "com.unity.modules.audio": "1.0.0",
            "com.unity.modules.imageconversion": "1.0.0",
            "com.unity.modules.imgui": "1.0.0",
            "com.unity.modules.physics": "1.0.0",
            "com.unity.modules.physics2d": "1.0.0",
            "com.unity.modules.screencapture": "1.0.0",
            "com.unity.modules.uielements": "1.0.0",
        },
        "samples": [
            {
                "displayName": SAMPLE_NAME,
                "description": (
                    "Brio: First Harmonic, a one-level 2D platformer in which every sound is SFX FORGE at runtime: "
                    "19 sound effects written as patches in code and a song whose layers follow the player's speed. "
                    "Needs the built-in render pipeline and the legacy Input Manager (or Both); its tests need the "
                    "Test Framework package."
                ),
                "path": "Samples~/Showcase",
            }
        ],
    }
    # fmt: on
    write_text(os.path.join(target, "package.json"), json.dumps(manifest, indent=2, ensure_ascii=False))
    copy_file(
        need(src(SRC_PACKAGE, "package.json.meta"), "the manifest's .meta"), os.path.join(target, "package.json.meta")
    )


def make_code(target):
    counts = {}
    for name in ("Runtime", "Editor"):
        counts[name] = copy_tree_with_metas(
            src(SRC_PACKAGE, name), os.path.join(target, name), "the package's " + name + " folder"
        )
        copy_file(need(src(SRC_PACKAGE, name + ".meta"), name + ".meta"), os.path.join(target, name + ".meta"))
    plugin = need(os.path.join(target, "Runtime", "Plugins", "x86_64", "sfxforge.dll"), "the native plugin")
    # The plugin's platforms must be written down, not left to Unity's guess from the folder's name: a package
    # installed from a git URL is read-only, so Unity could not write them there.
    meta = read_text(plugin + ".meta")
    wanted = (
        "PluginImporter:",
        "    Editor:\n      enabled: 1",
        "    Win64:\n      enabled: 1",
        "    Any:\n      enabled: 0",
        "    Linux64:\n      enabled: 0",
        "    OSXUniversal:\n      enabled: 0",
        "    Win:\n      enabled: 0",
    )
    missing = [w.split("\n")[0].strip() for w in wanted if w not in meta]
    if missing:
        fail(
            "sfxforge.dll.meta does not pin the plugin to the editor and Windows x64 (looked at: %s). Set it in "
            "Unity's "
            "Inspector (Editor + Windows x86_64 only), Apply, and keep the .meta." % ", ".join(missing)
        )
    for asmdef, name in (
        ("Runtime/CatSplat.SfxForge.asmdef", "CatSplat.SfxForge"),
        ("Editor/CatSplat.SfxForge.Editor.asmdef", "CatSplat.SfxForge.Editor"),
    ):
        if json.loads(read_text(os.path.join(target, asmdef))).get("name") != name:
            fail(asmdef + " is not the assembly " + name)
    return counts


def make_sample(target):
    destination = os.path.join(target, "Samples~", "Showcase")
    count = copy_tree_with_metas(src(SRC_SHOWCASE), destination, "the showcase")
    for name in (
        "Showcase.unity",
        "Song/DemoSong.asset",
        "Song/DemoSong.bytes",
        "Editor/SfxShowcase.Editor.asmdef",
        "Tests/SfxShowcase.Tests.asmdef",
    ):
        need(os.path.join(destination, name), "the showcase's " + name)

    # The song is kept twice in the source (the older demo has its own copy). They must be the same song.
    with (
        open(src(SRC_SHOWCASE, "Song", "DemoSong.bytes"), "rb") as a,
        open(need(src(SRC_OLD_SONG), "the older demo's song"), "rb") as b,
    ):
        if a.read() != b.read():
            fail(
                "Showcase/Song/DemoSong.bytes and SfxDemo/DemoSong.bytes differ: copy the regenerated song to the "
                "showcase and rebuild it"
            )

    # The tests must not break a project that has no Test Framework: the assembly is compiled only with it.
    tests = json.loads(read_text(os.path.join(destination, "Tests", "SfxShowcase.Tests.asmdef")))
    gate = [d.get("define") for d in tests.get("versionDefines", []) if d.get("name") == "com.unity.test-framework"]
    if not gate or gate[0] not in tests.get("defineConstraints", []):
        fail(
            "Tests/SfxShowcase.Tests.asmdef is not constrained on com.unity.test-framework (versionDefines + "
            "defineConstraints)"
        )

    # The scene must point at the sample's own song and at nothing outside the sample or the package.
    guids = set()
    for folder, _, files in os.walk(destination):
        for name in files:
            if name.endswith(".meta"):
                found = re.search(r"^guid: ([0-9a-f]{32})", read_text(os.path.join(folder, name)), re.M)
                if found:
                    guids.add(found.group(1))
    for folder, _, files in os.walk(os.path.join(target, "Runtime")):
        for name in files:
            if name.endswith(".meta"):
                guids.add(re.search(r"^guid: ([0-9a-f]{32})", read_text(os.path.join(folder, name)), re.M).group(1))
    outside = set()
    for folder, _, files in os.walk(destination):
        for name in files:
            if os.path.splitext(name)[1] in (".unity", ".asset"):
                for guid in re.findall(r"guid: ([0-9a-f]{32})", read_text(os.path.join(folder, name))):
                    if guid not in guids and not guid.startswith("0000000000000000"):
                        outside.add((name, guid))
    if outside:
        fail(
            "the showcase refers to assets that are not in the sample or the package: "
            + ", ".join("%s -> %s" % o for o in sorted(outside))
        )

    # Its pages: links that pointed into this repository point into the package; the old package id is the new one.
    readme = os.path.join(destination, "README.md")
    text = read_text(readme)
    text = replace_once(
        text,
        "**The Unity manual**, [`docs/UNITY.md`](../../../../docs/UNITY.md), explains",
        "**The Unity manual**, [`Documentation~/UNITY.md`](../../Documentation~/UNITY.md), explains",
        "the showcase's README",
    )
    text = replace_once(
        text,
        "](../../../../docs/img/showcase.png)",
        "](../../Documentation~/img/showcase.png)",
        "the showcase's README",
    )
    text = replace_once(
        text,
        "[section 5 of the Unity manual](../../../../docs/UNITY.md#cueing-patterns)",
        "[section 5 of the Unity manual](../../Documentation~/UNITY.md#cueing-patterns)",
        "the showcase's README",
    )
    text = replace_once(
        text,
        "## Open it and play\n\n",
        "## Open it and play\n\n"
        "> **In the package.** This folder is the sample \"" + SAMPLE_NAME + "\" of the package `" + PACKAGE_ID + "`.\n"
        "> Import it from the Package Manager (SFX Forge > Samples > Import), open `Showcase.unity` in the imported\n"
        "> folder and press Play. The page below was written in the repository the package is generated from: where\n"
        "> it says `unity/Demo`, read \"your project\"; where it names `docs/`, `tests/`, `videos/`, `out/` or `.tools/`, those are\n"
        "> that repository's and are not in the package (the videos and figures are made there). The older demo it\n"
        "> mentions (`SfxDemo`) is not in the package either. The links to the manual work in the package's own\n"
        "> folder or repository, not from the imported copy under `Assets`. The tests in `Tests/` are compiled\n"
        "> only when the Test Framework package (`com.unity.test-framework`) is in the project, and a new project\n"
        "> has to get the scene into its build settings once (**SFX Forge > Rebuild Showcase Scene** does it).\n\n",
        "the showcase's README",
    )
    text = replace_once(
        text,
        "`docs/MANUAL.md` in the repository\nhas the full reference",
        "the device's manual (`Documentation~/MANUAL.md` in the package)\nhas the full reference",
        "the showcase's README",
    )
    text = replace_once(
        text,
        "- Add the package to `Packages/manifest.json`:\n  `\"com.catsplat.sfxforge\": \"file:<path to>/com.catsplat.sfxforge\"`, and reference",
        "- Add the package (the package's README, \"Install\"), and reference",
        "the showcase's README",
    )
    write_text(readme, text.replace(OLD_PACKAGE_ID, PACKAGE_ID))
    architecture = os.path.join(destination, "Docs", "ARCHITECTURE.md")
    write_text(architecture, read_text(architecture).replace(OLD_PACKAGE_ID, PACKAGE_ID))
    return count


# ---------------------------------------------------------------- documentation


# The licence paragraph of each manual: as it stands in the repository, and as the package has it (the package's
# names for the licence file and for the front page's section).
UNITY_LICENCE_NOTE = (
    "**Licence.** The package is under the GNU General Public License, version 3 or any later version, with an\n"
    "additional permission for the Unity engine: [LICENSE](../LICENSE),\n"
    "[ADDITIONAL-PERMISSIONS.md](../ADDITIONAL-PERMISSIONS.md). A game that is not offered under the GPL needs another\n"
    "licence from the copyright holder ([README, Licence](../README.md#licence)). This is a summary; the licence text\n"
    "decides.\n",
    "**Licence.** The package is under the GNU General Public License, version 3 or any later version, with an\n"
    "additional permission for the Unity engine: [LICENSE.md](../LICENSE.md),\n"
    "[ADDITIONAL-PERMISSIONS.md](../ADDITIONAL-PERMISSIONS.md). A game that is not offered under the GPL needs another\n"
    "licence from the copyright holder ([README, License](../README.md#license)). This is a summary; the licence text\n"
    "decides.\n",
)
MANUAL_LICENCE_NOTE = (
    "**Licence.** SFX FORGE is free software under the GNU General Public License, version 3 or any later version:\n"
    "[LICENSE](../LICENSE), with the additional permissions of [ADDITIONAL-PERMISSIONS.md](../ADDITIONAL-PERMISSIONS.md)\n"
    "(one of them is for the hosts that load VIBE FM firmwares). See [README, Licence](../README.md#licence).\n",
    "**Licence.** SFX FORGE is free software under the GNU General Public License, version 3 or any later version:\n"
    "`LICENSE.md` at the top of the package, with the additional permissions of `ADDITIONAL-PERMISSIONS.md` beside it\n"
    "(one of them is for the hosts that load VIBE FM firmwares). See the package's README, \"License\".\n",
)


def make_manuals(target):
    # The long strings below are each ONE line of a manual, exactly as it stands there: replace_once must find it.
    # They are left as long as the line is, so that a line of the manual can be searched for here.
    docs = os.path.join(target, "Documentation~")

    # --- the Unity manual
    where = "docs/UNITY.md"
    text = read_text(need(src("docs", "UNITY.md"), "the Unity manual"))
    text = replace_once(
        text,
        "This is the sibling of [the device's manual](MANUAL.md). That one explains the instrument. This one explains the\n"
        "package `unity/com.catsplat.sfxforge` and the example game built with it.\n",
        "This is the sibling of [the device's manual](MANUAL.md). That one explains the instrument. This one explains the\n"
        "package `" + PACKAGE_ID + "` and the example game built with it.\n\n"
        "> **Where things are.** This manual is written in the repository the package is generated from, and a few of\n"
        "> its lines name things there. In the package:\n"
        ">\n"
        "> - the showcase game is the sample \"" + SAMPLE_NAME + "\" (`Samples~/Showcase`; imported, it is a folder\n"
        ">   under `Assets/Samples`). Paths such as `Scripts/Audio` are inside that folder;\n"
        "> - the firmware is `Firmware~/sfx-forge.zip`, the videos are in [`video/`](video), the MIDI protocol is in\n"
        ">   [LINK-PROTOCOL.md](LINK-PROTOCOL.md);\n"
        "> - **not in the package:** the older demo scene the manual mentions (a hovercraft, `SfxDemo`, with its tests\n"
        ">   `SfxDemoTests`), and the scripts under `docs/`, `tests/`, `core/` and `out/`: the engine's C source, the\n"
        ">   emulator script of the link tests, the test project of the editor UI (`tests/unity-inspectors`, with\n"
        ">   the pictures it writes to `out/inspector-shots`), and the scripts that draw the figures and cut the videos.\n",
        where,
    )
    text = replace_once(
        text,
        "**Two videos** show the Unity side running. They are built by `docs/video/make_unity_videos.py` and kept in\n"
        "[`videos/`](../videos/README.md), the repository's folder of finished videos:\n\n"
        "| File | What it shows |\n|---|---|\n"
        "| [`videos/sfx-forge-unity-showcase.mp4`](../videos/sfx-forge-unity-showcase.mp4) | the showcase game played from start to goal, with captions naming what you hear |\n"
        "| [`videos/sfx-forge-unity-how-it-works.mp4`](../videos/sfx-forge-unity-how-it-works.mp4) | the three routes and the adaptive music, one at a time. Its pages of code are silent; the game plays between them |\n",
        "**Three videos** show the Unity side running. They are in the package, in `Documentation~/video`:\n\n"
        "| File | What it shows |\n|---|---|\n"
        "| [`sfx-forge-unity-showcase.mp4`](video/sfx-forge-unity-showcase.mp4) | the showcase game played from start to goal, with captions naming what you hear |\n"
        "| [`sfx-forge-unity-how-it-works.mp4`](video/sfx-forge-unity-how-it-works.mp4) | the three routes and the adaptive music, one at a time. Its pages of code are silent; the game plays between them |\n"
        "| [`sfx-forge-unity-link.mp4`](video/sfx-forge-unity-link.mp4) | the device (in the emulator) and the game, linked: the song and a patch arrive in the running game ([section 6](#6-from-the-device-to-the-game)) |\n",
        where,
    )
    text = replace_once(text, UNITY_LICENCE_NOTE[0], UNITY_LICENCE_NOTE[1], where)
    text = replace_once(
        text,
        "**The pictures** in this manual are built by `docs/unity_figures.py`.",
        "**The pictures** in this manual are built by a script in the source repository (`docs/unity_figures.py`).",
        where,
    )
    text = replace_once(
        text,
        "You need Windows x64 and Unity. The package is developed and tested with Unity 6.3 (6000.3.8f1) only. Its manifest\n"
        "in this repository (`unity/com.catsplat.sfxforge/package.json`) says Unity 2022.3 or newer, which nobody has tried;\n"
        "the manifest of the generated package says 6000.3.",
        "You need Windows x64 and Unity "
        + UNITY
        + ". The package is developed and tested with Unity 6.3 ("
        + UNITY_TESTED
        + ") only; its\n"
        "manifest asks for " + UNITY + " for that reason, not because an older version is known to fail.",
        where,
    )
    text = replace_once(
        text,
        "and pick `unity/com.catsplat.sfxforge/package.json` | \"SFX Forge Bridge\" appears in the list |",
        "and pick the `package.json` in this package's folder | \"" + DISPLAY_NAME + "\" appears in the list |",
        where,
    )
    text = replace_once(
        text,
        "    \"com.catsplat.sfxforge\": \"file:<path to>/com.catsplat.sfxforge\"",
        "    \"" + PACKAGE_ID + "\": \"file:<path to the package's folder>\"",
        where,
    )
    text = replace_once(
        text,
        "`<path to>` is where the folder `unity/com.catsplat.sfxforge` of this repository is on your disk. Write it as an\n"
        "absolute path, or relative to your project's `Packages` folder. The demo project, two folders below `unity/`, has\n"
        "`file:../../com.catsplat.sfxforge`.",
        "The path is where this package's folder (the one with `package.json`) is on your disk. Write it as an absolute\n"
        "path, or relative to your project's `Packages` folder. Once the package's repository is published, its git URL\n"
        "can stand there instead (the package's README, \"Install\").",
        where,
    )
    text = replace_once(
        text,
        "You need a song asset. The repository has one ready: `unity/Demo/Assets/SfxDemo/DemoSong.asset`, the firmware's own\ndemo song.",
        "You need a song asset. The package has one ready, in its sample: `Song/DemoSong.asset` of the showcase, the firmware's own\ndemo song.",
        where,
    )
    text = replace_once(
        text,
        "| **1** | Copy `DemoSong.asset` and `DemoSong.asset.meta` from `unity/Demo/Assets/SfxDemo/` into your "
        "project's `Assets` |",
        "| **1** | Import the sample \""
        + SAMPLE_NAME
        + "\" (Package Manager > SFX Forge > Samples) and find `Song/DemoSong.asset` in the imported folder |",
        where,
    )
    text = replace_once(
        text,
        "Copying the asset this way was not tried in a second project; it is the asset the demo project itself plays.",
        "These five steps were not tried as written, in a project of their own; the asset is the one the showcase "
        "itself plays.",
        where,
    )
    text = replace_once(
        text,
        "The demo song's marks, read from the asset (`Assets/SfxDemo/DemoSong.asset`):",
        "The demo song's marks, read from the asset (the showcase's `Song/DemoSong.asset`):",
        where,
    )
    text = replace_once(
        text,
        "*Brio: First Harmonic* is an original one-level 2D momentum platformer in `unity/Demo/Assets/Showcase`.",
        "*Brio: First Harmonic* is an original one-level 2D momentum platformer, the package's sample "
        "(`Samples~/Showcase`).",
        where,
    )
    text = replace_once(
        text,
        "Open `unity/Demo` with Unity 6000.3, open `Assets/Showcase/Showcase.unity`, press Play. The keys, the checks and the\n"
        "whole folder map are in [the showcase's README](../unity/Demo/Assets/Showcase/README.md).",
        "Import the sample from the Package Manager (SFX Forge > Samples), open `Showcase.unity` in the imported folder, press\n"
        "Play. The keys, the checks and the whole folder map are in [the showcase's "
        "README](../Samples~/Showcase/README.md).",
        where,
    )
    text = replace_once(
        text,
        "ten files of `Scripts/Core`, and the showcase's song `Assets/Showcase/Song/DemoSong.asset` | below |",
        "ten files of `Scripts/Core`, and the song `Song/DemoSong.asset` | below |",
        where,
    )
    text = replace_once(
        text,
        "Copy the showcase's own `unity/Demo/Assets/Showcase/Song/DemoSong.asset` with its `.meta` and pass it\n"
        "to `Install`. (`Assets/SfxDemo/DemoSong.asset`, the older demo's, holds the same song.)",
        "Copy the showcase's `Song/DemoSong.asset` with its `.meta` and pass it\nto `Install`.",
        where,
    )
    text = replace_once(
        text,
        "| **0** | Add the package to `Packages/manifest.json`. Reference",
        "| **0** | Add the package to your project. Reference",
        where,
    )
    text = replace_once(
        text,
        "[the showcase's README](../unity/Demo/Assets/Showcase/README.md#check-it-test-it-rebuild-it)",
        "[the showcase's README](../Samples~/Showcase/README.md#check-it-test-it-rebuild-it)",
        where,
    )
    text = replace_once(
        text,
        "| **SFX Forge > Rebuild Demo Scene** · **Rebuild Showcase Scene** | in the demo project only. They rewrite "
        "generated files |",
        "| **SFX Forge > Rebuild Showcase Scene** · **SFX Forge > Showcase > Build for Windows** | only with the "
        "showcase sample imported. The first rewrites the sample's generated files |",
        where,
    )
    text = replace_once(
        text,
        "The demo's song was not fetched over MIDI. `tests/make-demo-song.mjs` writes what the firmware sends on SEND into\n"
        "`DemoSong.bytes` – 16 patches of 28 bytes, then the song, 2487 bytes of format 6 – and the demo's builder turns that\n"
        "file into the asset. This is the path that is proven to work:",
        "The showcase's song was not fetched over MIDI. A script in the source repository (`tests/make-demo-song.mjs`) wrote\n"
        "what the firmware sends on SEND into a file – 16 patches of 28 bytes, then the song, 2487 bytes of format 6. That file is in the\n"
        "sample, `Song/DemoSong.bytes`, and the sample's builder (`Editor/ShowcaseAudio.cs`) turns it into the asset\n"
        "`Song/DemoSong.asset`. This is the path that is proven to work:",
        where,
    )
    for test in (
        "`SfxDemoTests.FiringPlaysTheLaserAndSpawnsABolt` |",
        "`SfxDemoTests.TheMusicPlaysAndFollowsTheSpeed` |",
    ):
        text = replace_once(text, test, test[:-2] + " (the older demo, in the source repository) |", where)
    if OLD_PACKAGE_ID in text or "](../unity/" in text or "2021.3" in text or "2022.3" in text:
        fail(where + ": a line still names the old package, an old Unity version or a path into the source repository")
    write_text(os.path.join(docs, "UNITY.md"), text)

    # --- the device's manual
    where = "docs/MANUAL.md"
    text = read_text(need(src("docs", "MANUAL.md"), "the device's manual"))
    text = replace_once(
        text,
        "Everything is synthesized. There are no samples and no recordings in it.\n",
        "Everything is synthesized. There are no samples and no recordings in it.\n\n"
        "> **In the Unity package.** This is the manual of the instrument, as it is in the repository the package is\n"
        "> generated from. The built instrument (firmware "
        + FIRMWARE_VERSION
        + ") is in the package's `Firmware~` folder:\n"
        "> `sfx-forge.zip`, with `sfx-forge.wasm` inside it. The game's side is [the Unity manual](UNITY.md). The\n"
        "> instrument's and the engine's C source (`sfx-forge/`, `core/`) and the scripts under `tests/` and `docs/` are\n"
        "> in that repository, not in the package; here the engine is the compiled `Runtime/Plugins/x86_64/sfxforge.dll`.\n",
        where,
    )
    text = replace_once(
        text,
        "The protocol, byte by byte, is in the [README](../README.md#the-link-device--game-engine). The Unity package is in\n"
        "`unity/com.catsplat.sfxforge`; the demo project in `unity/Demo` shows all of it in one scene.",
        "The protocol, byte by byte, is in [LINK-PROTOCOL.md](LINK-PROTOCOL.md). The Unity side is this package:\n"
        "[the Unity manual](UNITY.md) explains it, and its sample, the showcase game, shows all of it in one level.",
        where,
    )
    text = replace_once(text, MANUAL_LICENCE_NOTE[0], MANUAL_LICENCE_NOTE[1], where)
    if "](../" in text:
        fail(where + ": a link still leaves the documentation folder")
    write_text(os.path.join(docs, "MANUAL.md"), text)

    # --- the protocol, from this repository's README
    readme = read_text(need(src("README.md"), "the repository's README"))
    found = re.search(r"^## The link \(device ⇄ game engine\)\n(.*?)^## ", readme, re.S | re.M)
    if not found or "F0 7D 56 42" not in found.group(1):
        fail("README.md: the section \"The link (device ⇄ game engine)\" was not found")
    write_text(
        os.path.join(docs, "LINK-PROTOCOL.md"),
        "# SFX FORGE – the link, byte by byte\n\n"
        "What travels between the device and a game engine. The Unity package implements it in\n"
        "`Runtime/Core/SfxProtocol.cs` and `Runtime/Core/SfxLink.cs`; [section 6 of the Unity manual](UNITY.md#6-from-the-device-to-the-game)\n"
        "says how to use it and what of it is tested. The C functions named here (`sfx_song_pack`, `sfx_player_note_on`)\n"
        "are the engine's, and `tests/check-identity.mjs`, `check-wire.mjs`, `check-store.mjs` and `build.sh` are the firmware's\n"
        "checks and its build script:\n"
        "all of them are in the source repository, not in this package.\n\n" + found.group(1).strip() + "\n",
    )

    # --- the pictures the two manuals and the showcase's README use
    used = set()
    for name in ("UNITY.md", "MANUAL.md"):
        used.update(re.findall(r"\]\(img/([A-Za-z0-9._-]+)\)", read_text(os.path.join(docs, name))))
    used.add("showcase.png")
    used.update(README_FIGURES)  # the front page's pictures of the editor UI
    for name in sorted(used):
        copy_file(need(src("docs", "img", name), "a figure of the manuals"), os.path.join(docs, "img", name))
    return len(used)


# The videos' title and closing pages were written for the source repository. Said wherever the videos are offered.
RECORDED_ELSEWHERE = (
    "**The videos were recorded in the repository this package is generated from, and their pages name things there.**\n"
    "Where a video shows the package id `" + OLD_PACKAGE_ID + "`, read `" + PACKAGE_ID + "`, and install it as\n"
    "{install} says, not with the manifest line on the video's \"INSTALL\" page. Where it shows `unity/Demo` or\n"
    "`unity/Demo/Assets/Showcase`, read the imported sample ({sample}); where it shows `docs/UNITY.md` or \"the\n"
    "manual in the repository\", read {manual}."
)


def make_videos(target):
    if not shutil.which("ffmpeg") or not shutil.which("ffprobe"):
        fail("ffmpeg and ffprobe must be on the PATH (the previews are cut from the videos)")
    missing = [v["file"] for v in VIDEOS if not os.path.exists(src("videos", v["file"]))]
    if missing:
        fail(
            "these videos are missing in videos/: "
            + ", ".join(missing)
            + ". Make them first (docs/video/make_videos.py, make_unity_videos.py, make_link_video.py); "
            "nothing was skipped."
        )
    print(
        "ffmpeg: %s   (the previews and still frames are encoded by it: another version may give other bytes)"
        % run(["ffmpeg", "-version"], "ffmpeg").splitlines()[0].strip()
    )
    videos = os.path.join(target, "Documentation~", "video")
    previews = os.path.join(target, "Documentation~", "img", "readme")
    os.makedirs(previews, exist_ok=True)
    for v in VIDEOS:
        source = src("videos", v["file"])
        length = float(
            run(
                ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", source], "ffprobe"
            ).strip()
        )
        # the video is not as long as VIDEOS says (2 s either way), or its preview's clip runs past its end
        if abs(length - v["seconds"]) > 2 or v["clip"][0] + v["clip"][1] > length:
            fail(
                "%s is %.1f s long, not about %d s: it was made again. Look at it and choose its preview's place "
                "again (VIDEOS in this script)." % (v["file"], length, v["seconds"])
            )
        copy_file(source, os.path.join(videos, v["file"]))
        stem = os.path.splitext(v["file"])[0]
        start, seconds = v["clip"]
        # fmt: off
        run(
            [
                "ffmpeg", "-v", "error", "-y", "-ss", str(start), "-t", str(seconds), "-i", source, "-an",
                "-map_metadata", "-1",
                "-filter_complex",
                "fps=%d,scale=%d:-2:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];"
                "[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" % (PREVIEW_FPS, PREVIEW_WIDTH),
                "-threads", "1",
                os.path.join(previews, stem + ".gif"),
            ],
            "cutting the preview of " + v["file"],
        )
        run(
            [
                "ffmpeg", "-v", "error", "-y", "-ss", str(v["poster"]), "-i", source, "-an", "-map_metadata", "-1",
                "-frames:v", "1", "-vf", "scale=%d:-2:flags=lanczos" % POSTER_WIDTH, "-q:v", "3", "-threads", "1",
                "-bitexact", os.path.join(previews, stem + ".jpg"),
            ],
            "taking the poster of " + v["file"],
        )
        # fmt: on
        size = os.path.getsize(os.path.join(previews, stem + ".gif"))
        if size > 4 * 1024 * 1024:
            fail("the preview of %s is %.1f MB: too large for a front page" % (v["file"], size / 1048576.0))

    rows = []
    for v in VIDEOS:
        stem = os.path.splitext(v["file"])[0]
        minutes = "%d:%02d" % divmod(v["seconds"], 60)
        rows.append(
            "## %s\n\n[![%s](../img/readme/%s.jpg)](%s)\n\n[`%s`](%s) · %s · 1920×1080, 30 frames a second, with sound.\n\n%s\n"
            % (v["title"], v["title"], stem, v["file"], v["file"], v["file"], minutes, v["shows"])
        )
    write_text(
        os.path.join(videos, "README.md"),
        "# The videos\n\n"
        "Five videos, made by scripts from the firmware and the game themselves: nobody held a controller or a camera.\n"
        "The sound in them is what SFX FORGE produced, on the device or in Unity; there is no voice-over, the captions\n"
        "say what happens. Nobody has listened to them: their levels were measured, not judged.\n\n"
        "The two device videos and the two Unity videos end with the words \"Open source, GPL-3.0-or-later\":\n"
        "SFX FORGE is under the GNU General Public License, version 3 or any later version\n"
        "([LICENSE.md](../../LICENSE.md)).\n\n"
        "The device in the two device videos and in the link video is drawn by the VIBE FM SDK's emulator\n"
        "(see [Third Party Notices](../../Third%20Party%20Notices.md)).\n\n"
        + RECORDED_ELSEWHERE.format(
            manual="[`Documentation~/UNITY.md`](../UNITY.md)",
            install="[the package's README](../../README.md#install)",
            sample="[`Samples~/Showcase`](../../Samples~/Showcase/README.md)",
        )
        + "\n\n"
        + "\n".join(rows),
    )


def make_firmware(target):
    # the built firmware: the zip must hold the .wasm and its manifest
    source = need(src("build", "sfx-forge.zip"), "the built Vibe (run build.sh)")
    with zipfile.ZipFile(source) as z:
        names = set(z.namelist())
        if not {"sfx-forge.wasm", "sfx-forge.json"} <= names:
            fail("build/sfx-forge.zip does not hold sfx-forge.wasm and sfx-forge.json")
        version = json.loads(z.read("sfx-forge.json").decode("utf-8")).get("version")
        wasm = z.read("sfx-forge.wasm")

    # it must be fit to give away (no debug sections, no path of this machine) and of the version the documents name
    debug = strip_wasm_debug.debug_sections(wasm)
    if debug or strip_wasm_debug.paths_in(wasm):
        fail(
            "build/sfx-forge.zip: the .wasm holds debug sections or a machine path (%s). Run "
            "python tools/strip_wasm_debug.py build/sfx-forge.wasm build/sfx-forge.zip (build.sh does it)."
            % ", ".join([name for name, _ in debug] + strip_wasm_debug.paths_in(wasm)[:3])
        )
    if version != FIRMWARE_VERSION:
        fail(
            "build/sfx-forge.zip is firmware %s, this script expects %s (FIRMWARE_VERSION): build it, or update the "
            "script and the documents" % (version, FIRMWARE_VERSION)
        )

    # into the package: the zip, and a README beside it (its text is checked against the documents: change both)
    copy_file(source, os.path.join(target, "Firmware~", "sfx-forge.zip"))
    write_text(
        os.path.join(target, "Firmware~", "README.md"),
        "# The device's side: SFX FORGE " + FIRMWARE_VERSION + " for VIBE FM\n\n"
        "`sfx-forge.zip` is the built instrument (a \"Vibe\"). Drop the zip onto the window of the VIBE FM plugin; or unzip it\n"
        "and drop `sfx-forge.wasm` onto the VIBE FM Playground, <https://play.thefirmwarehub.com/>, in Chrome or Edge.\n\n"
        "How to play it: [the device's manual](../Documentation~/MANUAL.md). How to join it to Unity:\n"
        "[section 6 of the Unity manual](../Documentation~/UNITY.md#6-from-the-device-to-the-game).\n\n"
        "This build ("
        + FIRMWARE_VERSION
        + ") was run without the emulator's page, in the VIBE FM SDK's command-line tools and, for\n"
        "SysEx and stored files, in a small host of the source repository's own: its checks hand the built `.wasm`\n"
        "events and SysEx and read its sound, its MIDI and what it stores, and all 24 of them pass, as do the SDK's\n"
        "own `vibe-check` and a short run of the new stress test (25 Nov 2026). A few of them also\n"
        "compare the display, to tell that it changed or that the LINK light is on: what a page shows and what a toast\n"
        "says is not read.\n\n"
        "This build was not run against Unity. The package's checks without Unity were run with it and pass\n"
        "(25 Nov 2026). What was run against Unity is firmware 1.7.0: the six link tests of\n"
        "the Unity manual passed in the SDK's local emulator on 24 Nov 2026, in one run,\n"
        "with songs of sixteenths only and the beat a quarter. That was the 1.7.0 build before one text of the\n"
        "display was corrected, the LINK page's footer (`CC 20-23` became `CC 20-25`), and the link tests were\n"
        "not run again after that, nor with 1.7.1 or 1.7.2. The session of the link video\n"
        "was recorded with the 1.7.0 build of 23 Nov 2026, before the beat was added; with four quarters the display\n"
        "is the same, and it was not recorded again.\n"
        "No build was run in the plugin or on hardware. Nobody has played the PERFORM page by hand, nobody has\n"
        "set a time signature by hand, and nobody has listened to what came with 1.7.0. Nobody has tried the mixer\n"
        "of 1.7.1 with a mixer controller, in the VIBE FM plugin, in a DAW or in the Playground. The source is not in\n"
        "this package.\n\n"
        "New in 1.7.2: hardening, and no new feature. A host that hands over a value that is no number or without\n"
        "end no longer breaks the device: a tempo or a song position like that is not followed (as an infinity it\n"
        "crashed the audio while the song played), a host parameter like that reads as its start value (as NaN it\n"
        "made the sound NaN until a restart), a velocity is kept within 0 to 1, and an event's value, an encoder's\n"
        "turn and a SysEx event's length are kept within bounds. The output is now always within -1..+1: it could be\n"
        "one float step over full scale on single samples of a loud mix, with a correct host too.\n"
        "1.7.2 changes nothing in this package's code, in the messages or in the formats, and only the identity's\n"
        "version says 1.7.2. It was found and tested by stress tests in the source repository that run the built\n"
        "`.wasm` in a host of their own and the firmware's source built natively in two threads. Nobody has run it\n"
        "in the plugin, a DAW, the Playground or on hardware. Known and not fixed: with SHIFT held, a DATA turn of\n"
        "more than about 16 million detents never returns, a loop in the VIBE FM SDK's own runtime that no real\n"
        "control reaches; and the mixing of two songs' pieces after a broken transfer that the package's changelog\n"
        "names.\n\n"
        "New in 1.7.1: the device reports its mixer to the host as parameters, as the VIBE FM SDK's mixer convention\n"
        "asks of an instrument with tracks. After the six parameters of before (`volume`, `mod_pitch`, `mod_cutoff`,\n"
        "`mod_fm`, `mod_level`, `intensity`, unchanged and in the same order) there are 65 more, 71 in all: for each\n"
        "of the song's 16 sounds `mix_<n>_level` (0 to 1), `mix_<n>_pan` (-1 to +1), `mix_<n>_mute` and\n"
        "`mix_<n>_solo` (0 or 1), with n from 1 to 16, and then `mix_select` (1 to 16, the selected sound). There is\n"
        "no `mix_<n>_arm`: REC is one key for the whole song, not one for each sound. Nothing new is stored. The\n"
        "values are the song's own, as the MIX page shows them: a level is the song's volume of 0 to 127 divided by\n"
        "127, and a pan's middle is the song's 64. The host is told whatever changes them: a fader turned on MIX, a\n"
        "pad that mutes, ALL ON, UNDO, NEW, a song from a game. A value the host sets goes into the song or selects\n"
        "the sound, is not said back to the host and makes no undo step (the MIX page's own faders and mutes make none\n"
        "either). Not taken from the host: a value that is no number, and `mix_select` while the device's tour\n"
        "runs; the host is then told what the value really is. The tour's own selections are not told to the host,\n"
        "and when it is over the host is told the sound that is selected then. A bug older than 1.7.1 is fixed with\n"
        "it: the tour now changes only the sound it selected itself and puts that one back, also when another sound\n"
        "was selected while it ran.\n"
        "1.7.1 changes nothing in this package's code, in the messages or in the formats: the song is in\n"
        "format 6 and a patch in format 2 as before, no SysEx message changed, and only the identity's version says\n"
        "1.7.1. It was tested by a script that works the SDK's own host functions for parameters in the source\n"
        "repository's small host: the list, both directions, that a mute and a fader set from the host are\n"
        "heard in the measured output, what is not taken, and that the tour leaves every sound as it was. No real\n"
        "host or controller was tried.\n\n"
        "New in 1.7.0: PERFORM, a fourth page of the K5 key (K5 walks SEQ, SONG, AUTO, PERFORM). It shows the whole\n"
        "pattern as a grid, one row for each of the 16 sounds, and F1 to F4 choose what the pads are: the steps of\n"
        "the selected sound, the kit, mutes, or the patterns and the intensity\n"
        "([the device's manual](../Documentation~/MANUAL.md#13-perform-the-whole-pattern-and-the-pads-four-ways)). It\n"
        "changes nothing in this package's code, in the messages or in the formats: a device with 1.6.0 and one with\n"
        "1.7.0 speak the same song format 6, and the package works with either. The page was tested by a script that\n"
        "presses its keys and does not look at the display.\n\n"
        "Also new in 1.7.0: a beat and a time signature. The bar's length was the song's already (encoder B on\n"
        "SONG and PERFORM, 1 to 16 sixteenths). New is the beat: SHIFT + B on SEQ, SONG and PERFORM chooses an\n"
        "eighth, three sixteenths, a quarter (as the device comes) or a dotted quarter. The click, the shades of\n"
        "the grids and FILL follow it, and the display names the bar by it: 12 sixteenths are `BAR 3/4` with a\n"
        "quarter and `BAR 6/8` with a dotted quarter ([the device's\n"
        "manual](../Documentation~/MANUAL.md#the-bar-and-the-beat-34-68-78)). The beat is the device's, not the\n"
        "song's: it is not in the packed song and in no message, a game never gets it, and it changes no note.\n"
        "The device keeps it in a file of its own, `meter.bin`. Nothing in the package's code or formats changes:\n"
        "the package has nothing to read, and a bar of 12 sixteenths plays the same in Unity whether the device\n"
        "calls it 3/4 or 6/8. With four quarters, as the device comes, nothing looks, sounds or is stored\n"
        "differently than before. Not there: a bar longer than 16 sixteenths (no 5/4, 6/4, 9/8 or 12/8),\n"
        "triplets, a length or a beat for each pattern, groupings like 2+2+3. The beat was tested by a script\n"
        "that reads what FILL writes, where the click falls, the song's bytes and the store; it does not read the\n"
        "display's words, and nobody has listened to the click.\n\n"
        "New in 1.6.0: each pattern has its own grid of 1/16, 1/32 or 1/64, which is 16, 32 or 64 steps a bar (SHIFT+F5\n"
        "and SHIFT+F6 on the SEQ and AUTO pages: coarser and finer; SHIFT+LEFT and SHIFT+RIGHT: the 16 steps before and\n"
        "after). With it the song is in format 6, which has no fixed length (the factory song is 2487 bytes), the song\n"
        "message has a new shape (pieces of up to 240 bytes, 27 messages for the factory song with its 16 sounds), the\n"
        "song is stored in `song6.bin`, and the identity says song format 6 and 64 steps. Songs of the formats 1 to 5\n"
        "and the song message of before are still taken.\n\n"
        "**This firmware and this package belong together.** A firmware up to 1.5.0 drops the new song message without a\n"
        "word, so the package does not send a format-6 song to one and says why (`SfxLink.Refusal`,\n"
        "[the Unity manual](../Documentation~/UNITY.md#when-a-song-is-not-sent)). A package from before firmware 1.6.0 takes no\n"
        "song from this firmware and does not read a format-6 song asset. An older firmware's `song.bin` is still read,\n"
        "and never written: put the older firmware back and its song is still there.\n\n"
        "New in 1.5.0, on the sequencer pages: LOOP and SONG (F1: a pattern repeats, or the song plays in order), a\n"
        "pattern strip that shows which pattern plays, which is shown and which waits, REC that plays the pads as the\n"
        "kit and loops the pattern, ERASE while recording (F4), and UNDO (F2, one step, again for redo).\n\n"
        "Licence: the firmware is under the GNU General Public License, version 3 or any later version\n"
        "(" + LICENCE_ID + "), as its manifest says: [LICENSE.md](../LICENSE.md). An additional permission allows\n"
        "combining it with the hosts of VIBE FM firmwares, which are not free software:\n"
        "[ADDITIONAL-PERMISSIONS.md](../ADDITIONAL-PERMISSIONS.md).\n\n"
        "The `.wasm` is SFX FORGE's code compiled together with the VIBE FM SDK's runtime and its display fonts\n"
        "(Apache-2.0 and SIL OFL 1.1): see [Third Party Notices](../Third%20Party%20Notices.md) and the licence texts in\n"
        "[`Documentation~/licenses`](../Documentation~/licenses). It carries no debug information.\n",
    )


# The licence and notice texts of what is compiled into the two binaries, read where the build took the code from.
def licence_sources():
    sdk = src(".tools", "vibe-fm-sdk")
    zigs = sorted(
        name for name in (os.listdir(src(".tools")) if os.path.isdir(src(".tools")) else []) if name.startswith("zig-")
    )
    if not zigs:
        fail("Zig is not in .tools (zig-*): its licence and the mingw-w64 runtime's are read from there")
    zig = src(".tools", zigs[-1])
    return [
        ("VIBE-FM-SDK-NOTICE.txt", os.path.join(sdk, "sdk", "NOTICE"), "VIBE FM SDK"),
        ("VIBE-FM-SDK-LICENSE-Apache-2.0.txt", os.path.join(sdk, "sdk", "LICENSE"), "Apache License"),
        ("VIBE-FM-SDK-LICENSE-FONTS-OFL-1.1.txt", os.path.join(sdk, "sdk", "LICENSE-FONTS"), "SIL OPEN FONT LICENSE"),
        ("mingw-w64-COPYING.txt", os.path.join(zig, "lib", "libc", "mingw", "COPYING"), "mingw-w64"),
        ("Zig-LICENSE.txt", os.path.join(zig, "LICENSE"), "Zig contributors"),
    ]


def make_licences(target):
    for name, source, must_say in licence_sources():
        with open(need(source, "a licence text (" + name + ")"), "rb") as f:
            raw = f.read().replace(b"\r\n", b"\n")
        if must_say.encode("utf-8") not in raw:
            fail("%s does not look like the text expected for %s" % (os.path.relpath(source, ROOT), name))
        destination = os.path.join(target, "Documentation~", "licenses", name)
        os.makedirs(os.path.dirname(destination), exist_ok=True)
        with open(destination, "wb") as f:
            f.write(raw if raw.endswith(b"\n") else raw + b"\n")


# ---------------------------------------------------------------- the front page and its companions


def toc_of(path):
    """The manual's own chapter list: its level-two headings, with GitHub's anchors."""
    return [(h, slug(h)) for h in re.findall(r"^## (.+)$", strip_code(read_text(path)), re.M)]


def strip_code(text):
    return re.sub(r"^```.*?^```", "", text, flags=re.S | re.M)


def slug(heading):
    text = heading.strip().lower()
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = "".join(c for c in text if c.isalnum() or c in " -_")
    return text.replace(" ", "-")


def make_readme(target):
    def video(v):
        stem = os.path.splitext(v["file"])[0]
        return "[![%s](Documentation~/img/readme/%s.gif)](Documentation~/video/%s)" % (v["title"], stem, v["file"])

    def length(v):
        return "%d:%02d" % divmod(v["seconds"], 60)

    # the five videos, each by its file's name (so that the order of VIDEOS may change)
    by_file = {v["file"]: v for v in VIDEOS}
    link = by_file["sfx-forge-unity-link.mp4"]
    showcase = by_file["sfx-forge-unity-showcase.mp4"]
    how = by_file["sfx-forge-unity-how-it-works.mp4"]
    tour = by_file["sfx-forge-tour.mp4"]
    song = by_file["sfx-forge-build-a-song.mp4"]
    unity_toc = "\n".join(
        "- [%s](Documentation~/UNITY.md#%s)" % t for t in toc_of(os.path.join(target, "Documentation~", "UNITY.md"))
    )
    device_toc = " · ".join(
        "[%s](Documentation~/MANUAL.md#%s)" % t for t in toc_of(os.path.join(target, "Documentation~", "MANUAL.md"))
    )

    text = """# SFX Forge for Unity

SFX FORGE is a synthesizer for game audio that runs on VIBE FM: sound effects, instruments and a sequenced song with
layers, made on the device (or its emulator) with pads, keys and knobs. This package puts the same engine into a Unity
game. A sound is a patch of 28 bytes and a song is a few kilobytes; you make them on the device, send them over MIDI,
they arrive in Unity as assets, and the game synthesizes them while it runs. There are no audio files anywhere in it.

The documentation and the videos are the main thing here. Start with the videos, then
[the Unity manual](Documentation~/UNITY.md).

**The licence, before you build on it.** SFX Forge is free software under the GNU General Public License, version 3
or any later version ({licence_id}): [LICENSE.md](LICENSE.md). In short: you may use it at no cost in a project that
is itself offered under the GPL, with its source. A game that is not offered under the GPL, a closed-source game for
one, needs another licence from the copyright holder: ask Cat Splat Studios, through
[the issue tracker](https://github.com/Cat-Splat-Studios/sfx-forge/issues). An additional permission covers the Unity engine itself, which is not free
software ([ADDITIONAL-PERMISSIONS.md](ADDITIONAL-PERMISSIONS.md)); it does not cover your own code. This is a
summary; the licence text decides. More under [License](#license).

## Watch

Each picture is a silent excerpt of ten to fourteen seconds, at a third of the video's size; the text under it says
what the whole video shows. Click it for the video (an mp4 in [`Documentation~/video`](Documentation~/video/README.md),
1920×1080, with sound), where the small print of the panels is readable. If your browser shows a file page instead
of a player, download the file.

{link_gif}

**[{link_title}](Documentation~/video/{link_file})** ({link_len}). {link_shows}

{showcase_gif}

**[{showcase_title}](Documentation~/video/{showcase_file})** ({showcase_len}). {showcase_shows}

{how_gif}

**[{how_title}](Documentation~/video/{how_file})** ({how_len}). {how_shows}

{tour_gif}

**[{tour_title}](Documentation~/video/{tour_file})** ({tour_len}). {tour_shows}

{song_gif}

**[{song_title}](Documentation~/video/{song_file})** ({song_len}). {song_shows}

The videos were made by scripts from the firmware and the game themselves. Their sound is SFX FORGE's own, on the
device and in Unity; nobody has listened to it (see [What is tested](#what-is-tested-and-what-is-not)).

{recorded_elsewhere}

## In the editor: the device's own look

The Inspector of a sound, the Inspector of a song and the window **Window > SFX Forge** are drawn as the device
draws its display: the firmware's colours, its pages of up to four values, a knob for each value, every value in the
device's units, the sound's waveform, and rows of soft keys.

![The Inspector of a sound, page OSC](Documentation~/img/unity-inspector-sound-osc.png)

**A sound** (`SfxPreset`). Seven pages, OSC TONE FILTER WAVE ENV LFO FX, hold the engine and all 25 values. Drag a
value up or down to turn it, or turn the wheel over its ring; double-click the value to type one, double-click the
ring for the plain patch's value. PLAY, MUTATE, UNDO, RANDOM and BAKE WAV work on the asset; SEND and PLAY ON DEVICE need the
link. The picture is the sound as the device draws it: one voice, without its effects. The line under it is the
sound's peak and RMS as PLAY and BAKE WAV render it.

![The Inspector of a song, page STEPS](Documentation~/img/unity-inspector-song.png)

**A song** (`SfxSong`). Tempo, steps, swing and chain; the patterns A – H; the arrangement; and four pages: STEPS
(the steps of the pattern looked at, with INTENSITY and a mark for the LAYER of every sound that plays in the song under it), MIX, MASTER and AUTO. It is a viewer: here
you can change TEMPO, SWING and a sound's LAYER, VOLUME and PAN. Everything else only shows, and is made on the
device. From firmware 1.6.0 on a pattern has its own grid of 1/16, 1/32 or 1/64: the title says it (`PATTERN A  1/16`),
the rows draw 16, 32 or 64 steps, and a finer pattern's tile carries `32` or `64` in its corner
([the manual, "A finer grid"](Documentation~/UNITY.md#a-finer-grid)).

![The SFX Forge window, shown with a filled-in model](Documentation~/img/unity-inspector-window.png)

**The window.** The link's state and the two MIDI ports, what the device's knobs send, its 16 pads as tiles that
play or fetch, a text for the bottom line of the device's LINK page, the patches that arrived as tiles with their
waveform, the last messages, and CONNECT, REFRESH PORTS, FETCH SONG.

**Tools for designing sounds**, without the device ([section 11 of the manual](Documentation~/UNITY.md#11-tools-for-designing-sounds)
has each of them with its keys, its API, its limits and what was tested):

![The VARY tool with eight variants](Documentation~/img/unity-inspector-tools-vary.png)

- **NEW**, a tab of a sound's Inspector: a new sound of a kind from nothing. Ten kinds: CLICK, CHIME, LEAP, RISE,
  ZAP, THUD, BLAST, DOOR, ALARM, HUM. In code: `SfxGenerator.Make(SfxCategory.Zap, seed)`.
- **VARY**: how far MUTATE goes (AMOUNT) and which values it leaves alone (the LOCK keys), and **MORE LIKE THIS**:
  eight variants of the sound to play and to keep. In code: `SfxMutate.Variant(base, seed, n, percent, locked)`.
- **Variation sets** (`SfxVariationSet`, played by `SfxVariationPlayer`): one sound as several, in a shuffled
  order that does not repeat, each play a little higher or lower and a little quieter. BAKE writes the variants as
  clips, which play on every platform; a set that is not baked plays only where the native library is.
- **Window > SFX Forge Bake**: the selected presets, variation sets and folders to `.wav` files at once, with a
  sample rate, mono, a pitch or a note, a velocity, a trimmed end and a normalised level.
- **LEVEL**: MATCH RMS and MATCH PEAK bring several selected sounds to the same reading by writing their LEVEL.
- **SHARE**: a sound as 62 characters of text to paste anywhere (`SFXF1:` and 56 hex digits), and as a `.syx` file.

```csharp
SfxPatch zap = SfxGenerator.Make(SfxCategory.Zap, 0xCF56CEED);       // this kind and seed: always the same patch
string text = SfxPatchText.ToText(zap);                              // 62 characters; SfxPatchText.Parse reads them back
```

**These pictures are not photographs of the Unity editor.** They are the same element tree and stylesheet
the Inspector hosts, rendered in a panel to a texture by the editor UI's tests (171 EditMode tests, in the source
repository). Nobody has opened the real Inspector or either of the two windows yet, and nobody has listened to a
sound the tools made. The window's picture shows a model the
test filled in: its ports, patches and messages are the test's, and no device was connected. Also not tried: the
menus (ENGINE, TABLE, the pad, the MIDI ports), typing into a value, any key (the arrow keys on a tile, ENTER in
the window's text field), and anything sent to a device from these panels. [The manual has more pictures, and what each control does](Documentation~/UNITY.md#the-inspectors-look-like-the-device).

## From the device to the game in five steps

![What flows between the device and Unity](Documentation~/img/unity-link.png)

**1. Make a sound on the device.** The instrument is in [`Firmware~/sfx-forge.zip`](Firmware~/README.md): drop the zip
onto the VIBE FM plugin, or the `sfx-forge.wasm` inside it onto the [Playground](https://play.thefirmwarehub.com/) in
the browser. Hit a pad, turn the knobs, press **F2 MUTATE** for variations; write a song on the sequencer pages.
[The device's manual](Documentation~/MANUAL.md) teaches it, with two tutorials:
[a sound effect](Documentation~/MANUAL.md#5-tutorial-a-sound-effect) and [a beat](Documentation~/MANUAL.md#9-tutorial-a-beat).

**2. Connect it over MIDI.** Unity talks to the device over two MIDI ports, one each way. Without the hardware, the
device is the emulator or the plugin, and the two ports are a virtual MIDI cable (for example loopMIDI). In Unity:
**Window > SFX Forge**, click the **FROM DEVICE** and **TO DEVICE** tiles to choose the ports, press **CONNECT**.

**3. EXPORT a sound, SEND the song.** On a sound page, **F5 EXPORT** sends the selected sound: one patch. On the SONG
page, **F5 SEND** sends everything: the 16 patches, then the song with its arrangement, layers, mix and automation
in pieces of up to 240 bytes (the factory song: 11 pieces, 27 messages in all). Unity can also ask: **FETCH SONG** in
the window, or from code:

```csharp
SfxLink.Shared.PatchReceived += (slot, patch) => Debug.Log($"pad {{slot + 1}}: {{patch.engine}}");
SfxLink.Shared.SongReceived += (packedSong, sounds) => {{ /* sounds is null if not all 16 came with it */ }};
SfxLink.Shared.RequestPatch(0);        // ask for pad 1
SfxLink.Shared.RequestSong();          // ask for the 16 sounds and the song
```

Both requests return false while the ports are not open, and the events fire from `SfxLink.Shared.Poll()`. In the
editor the SFX Forge window opens the ports and the editor polls for you once it is connected. In a running scene or a
built game, put an `SfxForgeLink` component in the scene: it opens the ports (in a built game, name them in its
`inputPort` and `outputPort`) and polls every frame.

**4. It arrives in Unity as an asset.** The window saves a patch as an `SfxPreset` (into `Assets/SfxForge`, named like
`Pad03_SciFiFm`; with SAVE AS THEY ARRIVE on, as it is at first, otherwise SAVE on its tile) and a song as an `SfxSong` named `Song`. A preset's Inspector has the device's pages with a knob for
every value, and **PLAY**, **MUTATE** and **BAKE WAV**. A sound can also be written in code, as an engine and up to 25
numbers:

```csharp
SfxPatch coin = SfxPatch.Create(SfxEngine.UiBlip, 86, 64, 0, 26, 60, 0, 127, 0, 62, 0);
```

**5. Play it in the game.** No device and no MIDI are needed from here on: the game carries the engine
(`sfxforge.dll`) and synthesizes the sound when it is asked for.

```csharp
using CatSplat.SfxForge;
using UnityEngine;

public class FromTheDevice : MonoBehaviour
{{
    public SfxPreset sound;          // the preset that EXPORT made
    public SfxMusicPlayer music;     // an SfxMusicPlayer on another object; the SfxSong that SEND made is in its Song field
    SfxPlayer player;
    AudioClip baked;

    void Start()
    {{
        player = gameObject.AddComponent<SfxPlayer>();              // adds the AudioSource it needs
        player.preset = sound;
        baked = sound.Bake(AudioSettings.outputSampleRate);         // or render it once into an ordinary clip
    }}

    void Update()
    {{
        if (Input.GetKeyDown(KeyCode.Space)) player.Play(1f, 7f);   // live: full velocity, 7 semitones up
        if (Input.GetKeyDown(KeyCode.Return) && baked != null) GetComponent<AudioSource>().PlayOneShot(baked, 0.7f);
        music.intensity = Mathf.PingPong(Time.time * 0.1f, 1f);     // the song's layers come in and go out
        if (Input.GetKeyDown(KeyCode.Tab)) music.CuePattern(2);     // pattern C from the next bar line; see below
    }}
}}
```

A cue does not hold by itself in a song that has an arrangement, and the song SEND delivers has one: the cued
pattern plays, then the song walks on in its own order after a bar or two. To stay on a pattern, switch
`music.loopPattern` on: the pattern that plays then repeats until the next cue
([the manual, "Holding a pattern"](Documentation~/UNITY.md#holding-a-pattern-looppattern)). The switch is tested in
the engine, through the native library, and in Unity by one Play mode test of the showcase sample, run headless
and muted. Nobody has listened to it.

That is the whole path. The manual's sections [4](Documentation~/UNITY.md#4-playing-sounds-three-routes),
[5](Documentation~/UNITY.md#5-music) and [6](Documentation~/UNITY.md#6-from-the-device-to-the-game) are the long
version. The showcase sample is a whole game that plays its sounds and a device-made song with these calls. It was
not itself made over the link: its 19 sounds are patches written in code, and its song asset was built from a file
of the bytes the device sends on SEND ([the manual, "A song without MIDI"](Documentation~/UNITY.md#a-song-without-midi)).
The MIDI path was tested separately, against the emulator. While the two are linked the game can also drive the
device (`SfxTelemetrySender`, `SfxLink.Shared.SendIntensity`), so you tune a sound on the device's knobs while the
game plays it.

## Install

The package needs Unity {unity} on Windows x64.

- **From disk** (works now): clone or download this repository, then **Window > Package Manager > + > Add package
  from disk...** and pick its `package.json`. Or add `"{package_id}": "file:<path to this folder>"` to the
  `dependencies` in your project's `Packages/manifest.json`.
- **From a git URL**: **Window > Package Manager > + > Add package from git URL...** with this repository's URL. That
  URL exists once the repository is published; at the time of writing it is not, so no URL is printed here.

Then **Window > SFX Forge** exists, and `using CatSplat.SfxForge;` compiles in scripts under `Assets`. Code with its
own assembly definition references `CatSplat.SfxForge` (and `CatSplat.SfxForge.Editor` for editor helpers).
[Section 1 of the manual](Documentation~/UNITY.md#1-five-minutes) is the first sound in five minutes.

## The showcase sample

![Brio: First Harmonic - four moments of the level](Documentation~/img/showcase.png)

*Brio: First Harmonic* is a one-level 2D platformer in which every sound is SFX FORGE at runtime: 19 sound effects
written as patches in code, and the device's demo song with its layers following the player's speed. Its art is
generated meshes; the folder holds no audio file, texture or model. It is written to be read and copied:
`Scripts/Audio` is the example, and [its README](Samples~/Showcase/README.md) is the guide.

- **Import**: Package Manager > SFX Forge > Samples > **{sample}** > Import. Unity copies it under
  `Assets/Samples/SFX Forge/{version}/{sample}`. It needs the built-in render pipeline, and **Active Input Handling**
  set to the legacy Input Manager or Both (Project Settings > Player). An empty project made by Unity {unity_tested}
  without a template has both already. A project made from a template with another render pipeline or with the
  Input System alone does not, and the sample was not tried there. Its tests are compiled only if the Test
  Framework package (`com.unity.test-framework`) is in the project; an empty project does not have it (Package
  Manager > Unity Registry > Test Framework).
- **Play**: open `Showcase.unity` in the imported folder, press Play. Arrows or A/D run, Space jumps, Down tucks
  into a roll. A gamepad is read through the Input Manager's default axes and buttons (left stick, a face button);
  that was not tried with a real pad.
- **Build**: **SFX Forge > Showcase > Build for Windows** writes `Builds/Brio/Brio.exe` beside `Assets`. The build
  settings are left as they are.
- **The build checks itself**: started with `-sfxforge-probe <path>\\probe.json` the game plays by itself for 14
  seconds, measures what comes out of its AudioListener, writes a report and quits with exit code 0 or 1. `"ok": true`
  means the native library was found, the title card was left, the song ran through at least two bar lines, every baked sound has its clip, a
  jump happened and the output was not silent. It does not say how it sounds.
- **Check and rebuild**: **SFX Forge > Rebuild Showcase Scene** writes the scene and the generated assets again,
  adds the scene to the build settings and runs the sample's 32 editor checks. Straight after the import one of
  the 32 fails: the scene is not in the project's build settings yet. Playing and the build command above do not
  need it there; the rebuild, or adding the scene by hand, settles it. The headless commands are in the sample's
  README.

## The manual

**[SFX FORGE in Unity](Documentation~/UNITY.md)**, the game's side:

{unity_toc}

**[SFX FORGE, the device](Documentation~/MANUAL.md)**, the instrument's side: {device_toc}. Everything on one
picture: [the cheat sheet](Documentation~/img/cheatsheet.png).

Also: [the link's protocol, byte by byte](Documentation~/LINK-PROTOCOL.md) · [the videos](Documentation~/video/README.md) ·
[the showcase's README](Samples~/Showcase/README.md) and its [design](Samples~/Showcase/Docs/DESIGN.md),
[architecture](Samples~/Showcase/Docs/ARCHITECTURE.md) and [audio notes](Samples~/Showcase/Docs/AUDIO-NOTES.md) ·
[the firmware](Firmware~/README.md) · [the changelog](CHANGELOG.md).

## What is tested, and what is not

Tested, on one machine (Windows 11, Unity {unity_tested}), in the repository this package is generated from:

- The showcase: its 32 editor checks pass, and of the demo project's 41 Play mode tests 26 passed and 15 were
  skipped, which are the capture and link takes that only run when asked for by name. That run was headless, on a
  copy of the project. A standalone Windows build passed its own probe, and the same was done with the showcase's
  folder copied to another place in an otherwise empty project; those two were last done before the tools for
  designing sounds, `loopPattern` and the firmwares 1.5, 1.6 and 1.7 were added, and were not repeated with them.
- `SfxMusicPlayer.loopPattern` in Play mode: one of those 26 tests
  (`LoopPatternKeepsTheSongOnOnePatternAndLetsItGoAgain` in the sample's `Tests/ShowcaseAudioTests.cs`) starts a
  second `SfxMusicPlayer` with the demo song. With `loopPattern` on it stays on its pattern for more than three
  bars; with it off the arrangement goes on. The run was headless and muted: nobody heard it.
- The link, end to end against the VIBE FM SDK's emulator over a virtual MIDI cable (loopMIDI): patches and the song
  from the device to Unity, a song and its sounds from Unity to the device, the game's messages leaving for the
  device, and the device saying what it is. That was done with firmware 1.7.0 on 24 Nov 2026: six tests, all
  passed, in one run, with songs of sixteenths only (on 23 Nov 2026 with the 1.7.0 build of that day, which did
  not have the beat yet, and with 1.6.0 the day before that). After that run one text of the display was
  corrected in 1.7.0, the LINK page's footer (`CC 20-23` became `CC 20-25`), and the six tests were not run again.
  **The firmware in this package is 1.7.2, and it was not run against Unity**: not in the link tests, not in the
  recorded session, not in the sample's runs below. Neither was 1.7.1. 1.7.1 adds the mixer as parameters for the
  host; 1.7.2 is hardening against a host that sends values that are no numbers or out of range. Neither changes a
  message or a format.
  SEND and EXPORT pressed on the device arriving in a running game were seen in the recorded session of the link
  video, run once; in its pictures the device's INTENSITY follows the game's. The session was recorded with the
  1.7.0 build of 23 Nov 2026, before the beat was added; with four quarters the display is the same. It was not
  run again with a later build.
- The firmware in this package (1.7.2): all 24 of its own checks pass, and the SDK's `vibe-check` and a short run
  of a stress test with them (25 Nov 2026). The stress test is new in 1.7.2: a seeded fuzzer that plays the host and
  hands the built `.wasm` events, parameters, a transport, SysEx and stored files, right and wrong, with values that
  are no numbers too; it looks for a crash, a hang and a sample that is no number or over full scale, not at whether
  it sounds right. The 24 checks run the built `.wasm` in the SDK's
  command-line tools and in a small host of the source repository's own (for SysEx and stored files), without the
  emulator's page and without Unity, and read its sound, its MIDI, what it stores
  and the song it sends: 27 messages, the 16 sounds and then 2487 bytes of format 6 in 11 pieces. They also hand it
  songs in both shapes of the song message, out of order, doubled and one straight after another. One of the 24
  presses the keys of PERFORM, the page that is new in 1.7.0; it does not look at the display. Another is about
  the beat, which is new in 1.7.0 too: what FILL writes with each beat, where the click falls, that the song's
  bytes stay as they were, and what the device stores. It does not read the display's words. A third is about
  the mixer that 1.7.1 reports to the host: the list of 71 parameters, a level, a pan, a mute, a solo and the
  selected sound set from the host and changed on the device, and that every sound muted or every fader down from
  the host is silent in the measured output; that a value that is no number is not taken; and that the device's
  tour, with the selected sound set from the host and DATA turned while it runs, leaves every sound as it was. It
  works the SDK's own host functions for parameters in that small host, not a real host.
- The finer grid and song format 6 on the Unity side, without Unity and without a device (the checks of the
  engine-independent code, below): a format-5 song renders bit for bit as it did before; the songs of formats 1 to
  5, converted to format 6, play the same samples; a pattern made finer and coarser again; where the player is in a
  64-step pattern; both shapes of the song message through the package's receiver; `SfxPackedSong.IsPlayable`
  against the native player on 28 000 damaged songs; and what `SfxLink.Refusal` says to a device that said 1.6 or
  1.7, one that said 1.5 and one that said nothing. In the editor UI's project, 8 of the tests below are about a
  song with patterns of 32 and 64 steps.
- The code in the manual was compiled against the package, except what firmware 1.6.0 added to it: the two
  snippets of "A finer grid" and the changed lines of "A song without MIDI" were not compiled.
- The editor UI: 171 EditMode tests in a project of their own. Two of them are of `SfxMusicPlayer.Play()` taking a
  song that a script assigned; the others build the Inspectors and the two windows as elements, call what their
  knobs and keys call, send them mouse and wheel events, check the asset and Undo afterwards, and
  compare the value texts with 46 values read off the device's own pictures and with a table of the firmware's
  arithmetic in 32-bit floats. They also render the pictures of the editor UI on this page and
  in the manual; those are not photographs of the editor.
- The engine-independent code, with plain .NET and without Unity: 205 checks, all passing with the native library
  that is in this package (last run on 25 Nov 2026, with the identity of firmware 1.7.2). Among them the loop switch of the music player's engine (`loopPattern`): on, the
  pattern stays; a cue changes it on the bar line and that one stays; off, the arrangement goes on.
- The tools for designing sounds. Without Unity: every kind of generated sound over 300 seeds is a valid patch
  inside its ranges, and 100 of each bake audible and unclipped; variants, locks and the shuffle; levels, the LEVEL
  law and matching; the text and `.syx` forms, both ways; bake options and the resampler. In the editor UI's
  project, 45 of the 171 tests: what each key of NEW, VARY, LEVEL and SHARE does to the asset, with Undo; a
  variation set's BAKE on real asset files (written next to the asset, out of date after an edit, nothing written
  when a file that is there cannot be written over); a batch bake's files, names and bytes; the Bake window over a model.
- The identity and the format numbers, on the Unity side and without a device: the bytes of the two messages,
  what is an identity and what is not, an identity arriving in the middle of a song, and when a patch or a song is
  not sent, a patch and a song of a higher format arriving and being said to be newer (the checks of the
  engine-independent code, run with plain .NET); asset files without a format line and with a higher one,
  imported by Unity, and the banner the Inspectors show for them (11 of the 171 tests above). A song asset counts
  its own format since firmware 1.6.0: 2 when it holds a format-6 song, 1 with an older one.
- The firmware's side of the identity, on the built `.wasm` without the emulator: a check of the source
  repository hands it the request and reads the answer, and a pad's patch asked for right after the request
  still arrives.

Tested on this package as generated, on the same machine, with the editor run headless. Most of it was done
before the tools for designing sounds, `loopPattern` and firmware 1.5.0 were added. One run was repeated with
them in the package: the sample's editor checks and Play mode tests, at the end of "The sample" below. The rest
was not. **With firmware 1.7.0 and song format 6 in the package, one more run was made**, last on 24 Nov 2026: the
sample's editor checks and Play mode tests, at the very end of "The sample" below. Everything else in this list was
run on a generated package from before firmware 1.6.0, whose sample held the demo song in format 5:

- **The package alone.** An empty project made by Unity (`-createProject`: Input Manager, built-in render pipeline,
  no Test Framework) with the package added by a `file:` line in `Packages/manifest.json`: it compiles, and the
  first script of the manual ([section 1](Documentation~/UNITY.md#1-five-minutes)) and the code on this page
  compile in `Assets`. They were compiled, not run.
- **The sample.** In a second such project the sample was imported with the Package Manager's own import call
  (`UnityEditor.PackageManager.UI.Sample.Import`, what the Import button calls) and landed in
  `Assets/Samples/SFX Forge/{version}/{sample}`. Without the Test Framework it compiles and the test assembly is left
  out. As imported, 31 of the 32 editor checks pass and the one about the build settings fails, as said above. With
  the scene in the build settings all 32 pass, on the scene as shipped and on a scene rebuilt in that project.
  With the Test Framework (1.6.0) added: 20 Play mode tests passed, none failed, and 14 were skipped, which are the
  capture and link takes that only run when asked for by name. The Windows build made from there passed its probe:
  native library found, 7 bars of the song, 14 of 14 baked clips, not silent; the output peak was 0.55 of full scale
  in one build and 0.62 in a second one, made after the plugin's import settings were written down. With the new
  editor UI in the package the sample's files were copied over the imported ones and these were run again: it
  compiles, all 32 checks pass, 20 Play mode tests pass and 14 are skipped, the Windows build passes its probe
  (peak 0.58) and holds no `CatSplat.SfxForge.Editor.dll`. The import call, the project without the Test
  Framework, the state as imported and the rebuilt scene were not repeated.
  With the tools for designing sounds, `loopPattern` and firmware 1.5.0 in the package, in a fresh project with
  the sample imported: the 32 editor checks pass, and of 36 Play mode tests 21 passed and 15 were skipped, the
  capture and link takes again. One of the 21 is the test of `loopPattern` named above. The Windows build and its
  probe were not repeated with them.
  With firmware 1.7.0 and song format 6 in the package, in that project with the sample's files copied over the
  imported ones (the import call was not repeated): the 32 editor checks pass, and of 36 Play mode tests 21 passed
  and 15 were skipped. The Windows build and its probe were not repeated.
- **The editor UI.** The source repository's test project of the editor UI was copied and pointed at this package
  by a `file:` line under the id `{package_id}`: 88 of 88 EditMode tests passed, the stylesheet was found (it is
  looked up by its GUID, not by a path), and the 51 pictures the tests rendered are byte for byte the ones rendered
  from the source. That is still a test panel: the Inspector and the window themselves were not opened. This was
  done before the identity, the format numbers, the tools for designing sounds and the finer grid were added (the
  project has 171 tests and renders 90 pictures since) and was not repeated with them: the 171 were run against the
  package's folder in the source repository only, where all 171 pass.

Not tested:

- **Nobody has listened to any of it.** The levels and every "it sounds" in these pages come from measurements: peak
  and RMS of recorded takes, the build's probe, tests that read the output buffer. They say that sound comes out and
  does not clip. They do not say that it sounds good or is balanced.
- A device that speaks another format. Refusing to send to one was only tried with identities made up by the
  tests, also for the one case that exists now: a firmware up to 1.5.0, which speaks song format 5.
- **Firmware 1.7.0 against Unity, beyond the six link tests and the recorded session.** They passed with 1.7.0 in
  the emulator (the recorded session with the build of 23 Nov 2026 only), but every pattern of the songs that
  crossed has 16 steps and every bar 16 sixteenths: a song with a pattern at 1/32 or 1/64 has
  not travelled through a MIDI port, only through the firmware's checks and the package's checks, which each read
  the other's bytes. Not in the plugin, not on hardware.
- **Firmware 1.7.2, the one in this package, against Unity.** The link tests, the sample's runs, the editor UI's
  tests and the videos were made with 1.7.0 and not again, not with 1.7.1 either. 1.7.1 and 1.7.2 change no message
  and no format, and that is read from the code, from the firmware's own checks and from the checks with plain .NET,
  not from a run with Unity.
- **Firmware 1.7.2 in a real host.** Its hardening was found and tested by stress tests in the source repository,
  in hosts of their own. A tool that sends heavy and hostile MIDI to the real plugin was written and built, and
  nothing was ever sent with it. Nobody has run 1.7.2 in the VIBE FM plugin, a DAW, the Playground or on hardware.
- **The mixer of firmware 1.7.1 in a host.** The device reports the levels, pans, mutes and solos of its 16 sounds
  and the selected sound to the host as parameters. A check in the source repository works the SDK's own host
  functions for parameters in a host of its own. Nobody has tried it with a mixer controller, in the VIBE FM
  plugin, in a DAW or in the Playground. The package has no part in it: the parameters are for the device's host,
  not for the link.
- **The PERFORM page of firmware 1.7.0, by hand.** A check in the source repository presses its keys in a host of
  its own and reads what the firmware then holds, plays and sends; it does not look at the display. Nobody has
  played the page or listened to it, and it was not open in any run of the link.
- **The beat and the time signature of firmware 1.7.0, by hand or by ear.** A check in the source repository
  chooses each beat and reads what FILL writes, where the click falls, the song's bytes and the store. It does not
  read what the display says (`BAR 6/8`), and it does not try the click with swing or a bar that is not a whole
  number of beats. Nobody has listened to the click. In Unity there is nothing of it: the beat is not in the song,
  and the song's Inspector shades its cells in fours whatever the device's beat is. A song with a bar of 12 or 14
  sixteenths was not played in a game.
- **An older firmware or an older package with this one.** That a firmware up to 1.5.0 drops the new song message
  without a word, and what a package from before firmware 1.6.0 does with a format-6 song asset (the banner of a newer
  asset, then NO SONG YET and silence), is read from their code. Neither was run.
- **A finer pattern in Unity, by ear or in a game.** The demo song and the sample use sixteenths only.
  `SfxMusicPlayer.GridStep` was not read in a Play mode test, and nobody has listened to a pattern of 32 or 64
  steps played by the package.
- **The standalone build with what is new.** The Windows build and its probe were last run before the tools for
  designing sounds, `loopPattern` and the firmwares 1.5, 1.6 and 1.7 were added.
- **`SfxMusicPlayer.loopPattern` by ear, or in a built game.** It was run by one Play mode test, headless and
  muted. Nobody has listened to a looped pattern, and nobody has listened to the videos that were made again.
- **The tools in use.** The NEW, VARY, LEVEL and SHARE tabs, a variation set's Inspector and the Bake window were
  built and driven by tests in a test panel; nobody has opened them. The file dialogs of EXPORT .SYX and IMPORT
  .SYX and the bake's progress bar were never shown. A variation set was not played in Play mode, in a built game
  or on a platform without the native library. A `.syx` file was not sent to a device.
- Hardware, and the VIBE FM plugin. The link was only run against the emulator. Pads and encoders coming from the
  device (`onPad`, `onEncoder`, `SfxEncoderBinding`), `SendPatch`, and the keys of the SFX Forge window and of the
  two Inspectors (SEND, PLAY ON DEVICE, the pads, FETCH SONG) were not tried against a device.
- **The Inspectors and the window in the editor itself.** Nobody has opened them: their tests host the same
  elements in a test panel. Their menus were never opened, and nobody typed into a value. No keys: the arrow keys on a tile, ENTER in the window's text field and the double-click that opens a value's field were never sent; what they call was called directly.
- Any Unity version other than {unity_tested}, any platform other than Windows x64, any render pipeline other than
  the built-in one. A gamepad.
- The Package Manager's window itself: nobody clicked **Add package from disk**, **Import** or the menu items; the
  calls behind them were run from the command line. Installing from a git URL (there is no published repository
  yet). A project made from a template that has another render pipeline or the Input System alone.
- A project without Unity's built-in modules. Every project the package ran in lists all of them in its own
  manifest, so the list of modules in this package's `package.json` was never what brought one in: nothing shows
  that it is enough.
- The 15 capture and link takes of the sample outside the source repository. They need that repository's scripts
  and the emulator.

## Requirements and limits

- **Windows x64 only.** The engine is a native library (`Runtime/Plugins/x86_64/sfxforge.dll`), and its import
  settings name the editor and Windows x64 only. Elsewhere the code is written to compile and stay silent with a
  warning (`SfxNative.Available` is false); that was not tried.
- **MIDI on Windows only** (winmm), and only needed for the link. Playing sounds and songs needs no device.
- **Unity {unity}**: developed and tested with {unity_tested}, nothing older.
- **Firmware 1.6.0 or 1.7.0 to 1.7.2 for songs.** A song of this version is in format 6. A firmware up to 1.5.0 does not
  take it; the package says so before it sends. A package from before firmware 1.6.0 does not read it.
- Live players are not spatialized (for 3D, bake a clip); one patch per `SfxPlayer`; a song is made on the device and
  only played and shaped in Unity: a pattern's grid too is chosen on the device. The device's beat (firmware 1.7.0) is not in the song: the package does not read or show it. A variation set must be baked to play where the native library is not, and
  nothing checks that at build time. [The manual's list of limits](Documentation~/UNITY.md#limits).

## License

Copyright (c) 2026 Cat Splat Studios. The package (its code, the native library, the firmware, the showcase, the
manuals and their figures) is under the GNU General Public License, version 3 or (at your option) any later version
({licence_id}): [LICENSE.md](LICENSE.md) is its text. It comes without warranty (sections 15 and 16).

- **What that means for a Unity project**, as a summary; the licence text decides. The package costs nothing in a
  project that is itself offered under the GPL: whoever gets the game can get its source under the same licence. A
  game that is not offered under the GPL needs another licence from the copyright holder.
- **The Unity engine.** The engine is not free software, and a game links with it. An additional permission under
  section 7 of the licence allows conveying SFX Forge combined with the Unity engine and its runtime and editor
  libraries. It is for the engine only, not for your own code or for other libraries:
  [ADDITIONAL-PERMISSIONS.md](ADDITIONAL-PERMISSIONS.md). A second permission there does the same for the hosts that
  load VIBE FM firmwares.
- **Other terms.** The copyright holder can offer SFX Forge under other terms. Ask Cat Splat Studios, through
  [the issue tracker](https://github.com/Cat-Splat-Studios/sfx-forge/issues).

Two binaries in the package hold other people's code in compiled form: the
firmware holds the VIBE FM SDK's runtime and display fonts (Apache-2.0, SIL OFL 1.1), and `sfxforge.dll` holds
start-up code of the mingw-w64 runtime. The device shown in the videos is drawn by the SDK's emulator. All of it is
in [Third Party Notices](Third%20Party%20Notices.md), with the licence texts in
[`Documentation~/licenses`](Documentation~/licenses). "VIBE FM" is its owner's trademark; this package is not
affiliated with it.
""".format(
        link_gif=video(link),
        link_title=link["title"],
        link_file=link["file"],
        link_len=length(link),
        link_shows=link["shows"],
        showcase_gif=video(showcase),
        showcase_title=showcase["title"],
        showcase_file=showcase["file"],
        showcase_len=length(showcase),
        showcase_shows=showcase["shows"],
        how_gif=video(how),
        how_title=how["title"],
        how_file=how["file"],
        how_len=length(how),
        how_shows=how["shows"],
        tour_gif=video(tour),
        tour_title=tour["title"],
        tour_file=tour["file"],
        tour_len=length(tour),
        tour_shows=tour["shows"],
        song_gif=video(song),
        song_title=song["title"],
        song_file=song["file"],
        song_len=length(song),
        song_shows=song["shows"],
        recorded_elsewhere=RECORDED_ELSEWHERE.format(
            manual="[`Documentation~/UNITY.md`](Documentation~/UNITY.md)",
            install="[Install](#install)",
            sample="[The showcase sample](#the-showcase-sample)",
        ),
        unity=UNITY,
        version=VERSION,
        unity_tested=UNITY_TESTED,
        package_id=PACKAGE_ID,
        licence_id=LICENCE_ID,
        sample=SAMPLE_NAME,
        unity_toc=unity_toc,
        device_toc=device_toc,
    )
    write_text(os.path.join(target, "README.md"), text)


def make_companions(target, counts):
    # (counts is not used here: the numbers in these texts are written out, see the texts)
    write_text(
        os.path.join(target, "CHANGELOG.md"),
        """# Changelog

## [{version}] - {released}

The first version of the standalone package. It is generated from the SFX FORGE repository (the firmware, the engine
and the Unity code live there) by `tools/make_unity_package.py`; nothing in it is edited by hand.

Licence: the GNU General Public License, version 3 or any later version ({licence_id}), with two additional
permissions under its section 7 (the Unity engine; the hosts of VIBE FM firmwares): `LICENSE.md`,
`ADDITIONAL-PERMISSIONS.md`. No version of this package was published under another licence.

### In it

- **Runtime** (`CatSplat.SfxForge`): `SfxPatch`, `SfxPreset`, `SfxSong`, `SfxPlayer`, `SfxMusicPlayer`, `SfxNative`
  with the native library for Windows x64, `SfxWav`, and the link: `SfxLink`, `SfxProtocol`, `SfxForgeLink`,
  `SfxTelemetrySender`, `SfxEncoderBinding`.
  - `SfxMusicPlayer.loopPattern`: the pattern that plays repeats until `CuePattern` chooses another. The native
    library has the engine's loop switch for it (`sfxn_player_set_loop`).
  - The finer grid of firmware 1.6.0 (song format 6): each pattern has a grid of 16, 32 or 64 steps a bar.
    `SfxSong.Grid`, `StepsPerSixteenth`, `GridSteps`, `HasFinerGrid`, `PackedFormat` and `GridStepOn`,
    `GridStepNote`, `GridStepVelocity`, `GridStepLength`, `GridLanePoint` read every step; the older `StepOn`,
    `StepNote`, `StepVelocity`, `StepLength` and `LanePoint` count sixteenths and read the first step of each.
    `SfxMusicPlayer.Step` stays the sixteenth; `GridStep` and `StepsPerSixteenth` are new.
  - `SfxPackedSong` reads a packed song of any format from bare bytes (`Format`, `IsValid`, `IsPlayable`, `Div`,
    `Grid`, `StepOn`, `StepNote`, `StepVelocity`, `StepLength`, `LanePoint`). `SfxSongTransfer` collects a song that
    arrives in pieces, in either shape of the song message.
  - `SfxProtocol`: `SongVersion` 6, `GridSteps` 64, `SongHead`, `SongBytesMin` (211), `SongBytesMax` (38355),
    `SongBytesV5` (7699), `SongPieceBytes` (240), `SongPiecesMax` (160), `BuildSongPieces`. A song has no fixed
    size any more: `SongBytes`, `SongChunkBytes`, `SongChunks` and `TryTakeSongChunk` are obsolete and kept.
  - `SfxNative` (the library's ABI is 5): `SongConvert` (an older song as format 6), `SongRegrid` and
    `SongRegridLoss` (a pattern in another grid, and what a coarser one would drop), `PlayerGrid`.
  - For designing sounds: `SfxGenerator` (a new sound of one of ten kinds, from a seed), `SfxMutate` and
    `SfxRandom` (variants that can be made again, with an amount and locks), `SfxVariationSet` and
    `SfxVariationPlayer` (one sound as several, shuffled), `SfxBake` and `SfxBakeOptions` (a patch as samples at a
    rate, mono, at a note, trimmed, normalised), `SfxLevels` (peak, RMS, matching by LEVEL), `SfxPatchText` (a
    sound as 62 characters of text and as a `.syx` file).
- **Reserved for later versions**, so that a project made with this one does not break when the Unity side can do
  more than a device, or a firmware for other hardware appears:
  - The identity (firmware 1.4.0): the link asks the device what it is (`SfxLink.RequestIdentity`, `Device`,
    `IdentityReceived`; `SfxDeviceInfo`). A firmware before 1.4.0 does not answer and is taken for one that speaks
    song format 5. A device that cannot take what is sent is sent nothing, and `SfxLink.LastRefusal` says why.
    `SfxLink.Refusal(device, packedSong)` asks beforehand: a firmware up to 1.5.0 takes a format-5 song only, and
    a song with a value out of range goes to no device.
  - `SfxProfile`: a device's limits as numbers. `SfxProfile.VibeFm1` is firmware 1.6.0 on (song format 6, 64
    steps a pattern) and every sound and song of this version fits it; `SfxProfile.VibeFm15` is what the firmwares
    1.4 and 1.5 said of themselves (song format 5, 16 steps).
  - `SfxPreset` and `SfxSong` keep the asset format that wrote them (`Format`) and the one that saved them
    last. A sound's is 1. A song's is 2 when it holds a format-6 song and 1 with an older one
    (`SfxSong.CurrentFormat` is 2). An asset from a newer SFX Forge says so in its Inspector and once in the Console.
  - A patch or a song with a higher format number is reported as newer, not as broken (`SfxPatch.IsNewer`,
    `SfxSong.IsNewer`, `SfxLink.NewerReceived` for one that arrives from the device).
  - There is no target switch and no feature the device lacks: this version only reserves the room.
- **Editor** (`CatSplat.SfxForge.Editor`): the Inspectors of presets and songs and the window **Window > SFX
  Forge**, built with UI Toolkit to look like the device's display, and `SfxForgeEditorLink`.
  - The sound inspector (`SfxPresetPanel`): the pages OSC, TONE, FILTER, WAVE, ENV, LFO and FX with the engine and
    all 25 values as knobs, each value in the device's units; the sound's waveform as the device draws it; HOLD for
    held sounds; the keys PLAY, MUTATE, UNDO, RANDOM, BAKE WAV, SEND, HEAR CHANGES, PLAY ON DEVICE, SEND CHANGES.
    Under the picture, the sound's peak and RMS. Under the keys, four tools: NEW (ten kinds of sound), VARY
    (MUTATE's amount and locks, MORE LIKE THIS with eight variants, MAKE SET), LEVEL (MATCH RMS, MATCH PEAK) and
    SHARE (the sound as text; EXPORT .SYX, IMPORT .SYX).
  - The Inspector of a variation set (`SfxVariationSetPanel`) with its BAKE (`SfxVariationBaker`), and the window
    **Window > SFX Forge Bake** (`SfxBakeView` over an `SfxBakeModel`; `SfxBatchBake` does the work).
  - The song inspector (`SfxSongPanel`): tempo, steps, swing, chain, the patterns A – H, the arrangement, and the
    pages STEPS, MIX, MASTER and AUTO. It is a viewer: TEMPO, SWING and a sound's LAYER, VOLUME and PAN can be
    changed in it, nothing else. It shows each pattern in its own grid: the title says `PATTERN B  1/32`, the rows
    draw 16, 32 or 64 steps, a finer pattern's tile has `32` or `64` in its corner. The line under its keys says
    LINKED, NOT CONNECTED, ANOTHER FORMAT or A DAMAGED SONG, and why SEND is off.
  - The window (`SfxForgeView` over an `SfxForgeModel`): the link's state, what the device says it is, the
    device's knobs, 16 pads that play or fetch, a text for the device, the patches that arrived with their
    waveform, the last messages.
  - The kit they are built from, in `Editor/UI` (`Knob`, `ValueTile`, `WaveformView`, `PadGrid`, `SoftKeyRow`,
    `SfxUnits`, `SfxForge.uss`), with a README of its own.
  - `SfxSong` has read-only accessors for what the song inspector shows: each pattern's grid, steps, notes,
    velocities, lengths, the mix, the master effects, the automation lanes.
- **Sample** "{sample}": a one-level game whose every sound is SFX FORGE, with its own song, editor checks, Play
  mode tests, a Windows build command and a self-check in the built game.
- **Documentation~**: the Unity manual, the device's manual with the cheat sheet, the link's protocol, five videos.
- **Firmware~**: the built instrument, firmware {firmware}, without debug information. Firmware 1.7.0 adds the
  PERFORM page on the device: the whole pattern as a grid, and the pads as steps, the kit, mutes or patterns.
  It also adds a beat and a time signature: SHIFT + B on the device chooses an eighth, three sixteenths, a quarter
  or a dotted quarter, and the click, the grids' shades, FILL and the bar's name on the display (`BAR 3/4`,
  `BAR 6/8`) follow it. The beat is the device's and is not in the song: a game never gets it.
  Nothing in the package's code or formats changes with either: a device with 1.6.0 and one with 1.7.0 speak the
  same song format 6.
  Firmware 1.7.1 reports the device's mixer to its host as parameters: level, pan, mute and solo of each of the
  16 sounds and the selected sound, 65 parameters after the six of before. Nothing in the package's code or
  formats changes with it either, and no message of the link changed.
  Firmware 1.7.2 is hardening: a host's value that is no number or out of range (a parameter, a tempo, a song
  position, a velocity, a turn) no longer crashes the device or spoils its sound, and the output is always within
  -1..+1. Nothing in the package's code or formats changes with it, and no message of the link changed.
- The native library's import settings name the editor and Windows x64 only.

The code is the package that was `{old_id}` 0.1.0 inside the source repository. The id, the display name and the
version are new; namespaces, assembly names and asset GUIDs are unchanged.

### Known limits

- Windows x64 only; MIDI on Windows only. Developed and tested with Unity {unity_tested} only.
- The link was tested against the VIBE FM SDK's emulator over a virtual MIDI cable, not against hardware or the
  plugin.
- Nobody has listened to the audio. Levels and "sound comes out" are measured (peaks, RMS, the build's probe).
- If a song transfer breaks off and a different song of as many pieces follows within two seconds, pieces of the
  two can be mixed.
- Songs are in format 6 (firmware 1.6.0). A firmware up to 1.5.0 drops the new song message without a word, so the
  package refuses in words before it sends one a format-6 song; nothing turns a format-6 song into format 5. A
  package from before firmware 1.6.0 does not read a format-6 song: it shows the banner of a newer asset, then NO SONG YET,
  and plays nothing. Neither an older firmware nor an older package was run against this one.
- The package reads and shows a pattern's grid and does not set it: it is chosen on the device. The demo song and
  the sample use sixteenths only, and nobody has listened to a finer pattern in Unity.
- The identity was not asked of a device by the package's own tests, only built and read. There is one device and
  one profile id; nothing can be targeted at another, and nothing converts a newer patch, song or asset.
- `loopPattern` is tested in the engine through the native library, and inside Unity by one Play mode test of the
  sample, headless and muted; nobody has listened to it. Firmware {firmware} passes its own checks. It was not run
  against Unity: the six link
  tests were last run in the emulator against firmware 1.7.0, the build before one text of its LINK page was
  corrected (songs of sixteenths only), and the session of the link video
  was recorded with the 1.7.0 build of 23 Nov 2026, before the beat was added. The PERFORM page and the beat
  were tested by scripts only: nobody has played them by hand or listened to the click, and no build was run on
  hardware or in the plugin.
- The mixer that firmware 1.7.1 reports to its host was tested by a script that works the SDK's own host functions
  for parameters. Nobody has tried it with a mixer controller, in the VIBE FM plugin, in a DAW or in the
  Playground.
- The device's beat and time signature (firmware 1.7.0) are not in the song. The package does not read or
  show them, and the song's Inspector shades its cells in fours whatever the device's beat is.
- The tools for designing sounds are measured, not heard: nobody has listened to a generated sound, a variant or
  a baked file. A variation set that is not baked plays only where the native library is, and nothing checks at
  build time that a set is baked or up to date.
- The Inspectors and the two windows were tested as elements in a test panel (171 EditMode tests in the source
  repository), and the pictures of them in these pages were rendered there. Nobody has opened them in the editor
  itself; their menus, typing into a value, and their keys against a device were not tried. No keys: the arrow
  keys on a tile, ENTER in the window's text field and the double-click that opens a value's field were never
  sent; what they call was called directly.
- Tried on the generated package, headless: added to an empty project from disk, the sample imported with the
  Package Manager's import call, with and without the Test Framework, its checks, tests, Windows build and the
  build's probe (the README, "What is tested", has the numbers, and which of it was repeated with the tools,
  `loopPattern` and firmware 1.5.0, and again with firmware 1.7.0 and song format 6: the sample's checks and
  tests, not the import call and not the build). Not tried: the Package Manager's buttons clicked
  by hand, a git URL, a project with another render pipeline or the Input System alone.
- Straight after the import, one of the sample's 32 editor checks fails (the scene is not in the build settings)
  until **SFX Forge > Rebuild Showcase Scene** is run or the scene is added by hand.
""".format(
            version=VERSION,
            released=RELEASED,
            sample=SAMPLE_NAME,
            firmware=FIRMWARE_VERSION,
            old_id=OLD_PACKAGE_ID,
            unity_tested=UNITY_TESTED,
            licence_id=LICENCE_ID,
        ),
    )

    licence = read_text(need(src("LICENSE"), "the repository's LICENSE"))
    if "GNU GENERAL PUBLIC LICENSE" not in licence or "Version 3, 29 June 2007" not in licence:
        fail("LICENSE is not the text of the GNU General Public License, version 3, any more")
    if "GNU AFFERO GENERAL PUBLIC LICENSE\n" in licence or "GNU LESSER GENERAL PUBLIC LICENSE\n" in licence:
        fail("LICENSE is the Affero or the Lesser licence, not the GNU General Public License")
    write_text(os.path.join(target, "LICENSE.md"), licence)

    # the two additional permissions (GPL version 3, section 7), the same text: only its link to the licence is the
    # package's name for that file
    permissions = read_text(need(src("ADDITIONAL-PERMISSIONS.md"), "the repository's ADDITIONAL-PERMISSIONS.md"))
    for must_say in (
        "Cat Splat Studios",
        "section 7",
        "version 3 or (at your option) any later version",
        "the Unity engine",
    ):
        if must_say not in permissions:
            fail("ADDITIONAL-PERMISSIONS.md does not say %r any more" % must_say)
    permissions = replace_once(
        permissions, "[LICENSE](LICENSE)", "[LICENSE.md](LICENSE.md)", "ADDITIONAL-PERMISSIONS.md"
    )
    write_text(os.path.join(target, "ADDITIONAL-PERMISSIONS.md"), permissions)

    write_text(
        os.path.join(target, "Third Party Notices.md"),
        """# Third Party Notices

The package's code, the showcase, the manuals and their figures are Cat Splat Studios' own work under the GNU
General Public License, version 3 or any later version ({licence_id}: [LICENSE.md](LICENSE.md), with the additional
permissions of [ADDITIONAL-PERMISSIONS.md](ADDITIONAL-PERMISSIONS.md)). The parts below are other people's and stay
under their own licences. Everything you hear is synthesized by that code: there are no samples or
recordings. Three things come from, or depend on, someone else's work: the VIBE FM SDK (in the firmware and in the
videos), the fonts of the device's display (in the firmware and in the videos), and the C runtime that the compiler
links into the native library. Their licence and notice texts are in
[`Documentation~/licenses`](Documentation~/licenses), copied unchanged.

## VIBE FM SDK

- Project: <{sdk}>
- Licence: Apache License 2.0, <https://www.apache.org/licenses/LICENSE-2.0>
- Author: Sven Trogus (zvenson). "VIBE FM" is its owner's trademark.

What of it is here:

- **In the videos** (`Documentation~/video`, and the previews and still frames cut from them in
  `Documentation~/img/readme`): the picture of the device in `sfx-forge-tour.mp4` and `sfx-forge-build-a-song.mp4` is
  the device as the SDK's emulator draws it, and `sfx-forge-unity-link.mp4` shows the SDK's emulator page itself,
  recorded while SFX FORGE ran in it.
- **In `Firmware~/sfx-forge.zip`**: the SDK's build compiles two of its own source files into every firmware, and
  so into `sfx-forge.wasm`: its runtime (`sdk/src/vibe_sdk.c`) and its display fonts (`sdk/src/vibe_font.c`),
  together with what its headers contribute. They are there in compiled form, unchanged, under the Apache License
  2.0. The zip also holds SFX FORGE's own compiled code, its description and its icon.
- Licence text: [`VIBE-FM-SDK-LICENSE-Apache-2.0.txt`](Documentation~/licenses/VIBE-FM-SDK-LICENSE-Apache-2.0.txt).
  The SDK's NOTICE file, as Apache-2.0 section 4(d) asks:
  [`VIBE-FM-SDK-NOTICE.txt`](Documentation~/licenses/VIBE-FM-SDK-NOTICE.txt).

No source file of the SDK is in this package, and the Unity code does not use it.

## The device's display fonts

- The SDK's `vibe_font.c` is glyph bitmaps rendered from fonts under the SIL Open Font License 1.1. Its own first
  lines name them: "Fonts: Roboto Condensed (the UI), Archivo (the logo) – SIL OFL 1.1". The NOTICE beside it
  (`sdk/NOTICE`, the one copied here) says the same; the NOTICE at the root of the SDK's repository names JetBrains
  Mono and Archivo instead. Which of the two is right for the UI font was not determined here.
- Copyright lines and licence text, as the SDK ships them:
  [`VIBE-FM-SDK-LICENSE-FONTS-OFL-1.1.txt`](Documentation~/licenses/VIBE-FM-SDK-LICENSE-FONTS-OFL-1.1.txt).
- Where they are: as bitmaps inside `Firmware~/sfx-forge.zip` (`sfx-forge.wasm`), and as the text on the device's
  display wherever the videos, previews and still frames show the device.

## The native library's C runtime

`Runtime/Plugins/x86_64/sfxforge.dll` is SFX FORGE's engine compiled with Zig 0.13.0 (`zig cc`, target
`x86_64-windows-gnu`). A game built with this package ships that file, so this applies to the game as well.

- **mingw-w64 runtime.** The linker puts the mingw-w64 C runtime's start-up code into the library (it is what
  prints "Mingw-w64 runtime failure"). Licence: [`mingw-w64-COPYING.txt`](Documentation~/licenses/mingw-w64-COPYING.txt),
  the file Zig ships with that runtime (Zope Public License 2.1, with parts in the public domain or under other
  permissive terms).
- **Zig's compiler runtime.** The compiler may also have linked helper routines of its own `compiler_rt` into the
  library and into the firmware. Whether any are in there was not determined. Licence, in case:
  [`Zig-LICENSE.txt`](Documentation~/licenses/Zig-LICENSE.txt) (MIT).
- The library imports the Universal C Runtime that is part of Windows (`api-ms-win-crt-*.dll`) and `KERNEL32.dll`;
  nothing of those is in the package.

## Not included, but named

- loopMIDI (Tobias Erichsen) is named as an example of a virtual MIDI cable. It is not part of the package.
- The showcase is an original game. Its character, level, art and song are Cat Splat Studios' own.
""".format(
            sdk=SDK_URL,
            licence_id=LICENCE_ID,
        ),
    )

    for name in ("README.md", "CHANGELOG.md", "LICENSE.md", "ADDITIONAL-PERMISSIONS.md", "Third Party Notices.md"):
        write_meta(target, name, "TextScriptImporter")

    write_text(
        os.path.join(target, ".gitignore"),
        "# A Unity package repository: Unity writes nothing here. Keep every .meta.\n"
        ".DS_Store\nThumbs.db\ndesktop.ini\n*.tmp\n*.swp\n*~.bak\n.vs/\n.vscode/\n.idea/\n",
    )
    write_text(
        os.path.join(target, ".gitattributes"),
        "# Everything is stored byte for byte: the package is generated, and Unity's YAML and .meta files must not be\n"
        "# rewritten by line-ending conversion.\n"
        "* -text\n"
        "*.mp4 binary\n*.gif binary\n*.png binary\n*.jpg binary\n*.dll binary\n*.zip binary\n*.bytes binary\n"
        "*.cs diff=csharp\n*.md diff=markdown\n",
    )


# ---------------------------------------------------------------- checks


def all_files(target):
    for folder, folders, files in os.walk(target):
        folders[:] = sorted(f for f in folders if not (folder == target and f == ".git"))
        for name in sorted(files):
            yield os.path.join(folder, name)


def check_metas(target):
    """Everything Unity sees has a .meta; nothing under a folder ending in ~ needs one at the root level."""
    problems = []
    for name in sorted(os.listdir(target)):
        if name == ".git" or name.startswith(".") or name.endswith("~") or name.endswith(".meta"):
            continue
        if not os.path.exists(os.path.join(target, name + ".meta")):
            problems.append(name + " has no .meta")
    for name in sorted(os.listdir(target)):
        if name.endswith("~") and os.path.exists(os.path.join(target, name + ".meta")):
            problems.append(name + " must not have a .meta")
    seen = {}
    for path in all_files(target):
        relative = os.path.relpath(path, target).replace("\\", "/")
        if path.endswith(".meta"):  # all of them, the sample's too
            guid = re.search(r"^guid: ([0-9a-f]{32})$", read_text(path), re.M)
            if not guid:
                problems.append(relative + " has no guid")
            elif guid.group(1) in seen:
                problems.append("%s and %s share a guid" % (relative, seen[guid.group(1)]))
            else:
                seen[guid.group(1)] = relative
    return problems


def check_privacy(target, final):
    """Looks through every file of the package for what must not be published. Returns the problems, as lines."""
    problems = []

    # ---- what is looked for: the studio's mail domain, this machine's folders in three spellings, the user's name
    allowed_name = b"github.com/hisham-css"
    machine = [os.path.expanduser("~"), ROOT, os.path.dirname(ROOT), target, final, os.path.dirname(final)]
    needles = [b"@catsplatstudios", b"cat splat repos", b"\\users\\", b"/users/"]
    for value in machine:
        for form in (value, value.replace("\\", "/"), value.replace("\\", "\\\\")):
            needles.append(form.lower().encode("utf-8"))
    user = os.path.basename(os.path.expanduser("~")).lower().encode("utf-8")

    # ---- where: every file, and every member of every zip
    blobs = []
    for path in all_files(target):
        relative = os.path.relpath(path, target).replace("\\", "/")
        with open(path, "rb") as f:
            raw = f.read()
        blobs.append((relative, path, raw))
        if path.lower().endswith(".zip"):
            with zipfile.ZipFile(path) as z:
                for info in z.infolist():
                    blobs.append((relative + " > " + info.filename, info.filename, z.read(info.filename)))
    for relative, path, raw in blobs:
        # ---- a .wasm: no debug section, no path
        if path.lower().endswith(".wasm"):
            for name, size in strip_wasm_debug.debug_sections(raw):
                problems.append("%s has the debug section %s (%d bytes)" % (relative, name, size))
            for found in strip_wasm_debug.paths_in(raw):
                problems.append("%s holds what looks like a machine path: %r" % (relative, found))
        # ---- the bytes as they are, and read as UTF-16 (an odd last byte is left off): the needles, and the name
        data = raw.lower()
        wide = (
            raw.decode("utf-16-le", "ignore").lower().encode("utf-8", "ignore")
            if len(raw) % 2 == 0
            else raw[:-1].decode("utf-16-le", "ignore").lower().encode("utf-8", "ignore")
        )
        for hay, kind in ((data, ""), (wide, " (UTF-16)")):
            for needle in needles:
                if needle in hay:
                    problems.append("%s contains %r%s" % (relative, needle.decode("utf-8"), kind))
            for word in {b"hisham", user} if len(user) >= 4 else {b"hisham"}:
                for found in re.finditer(re.escape(word), hay):
                    # the name may stand inside the GitHub address only: 11 is len("github.com/"), and 10 bytes
                    # from the name's start reach the end of "hisham-css"
                    start = found.start()
                    context = hay[max(0, start - 11) : start + 10]
                    if allowed_name not in context:
                        problems.append(
                            "%s contains %r outside the GitHub address%s" % (relative, word.decode("utf-8"), kind)
                        )
                        break
        # ---- text files only: a drive path (C:\...), an e-mail address
        if os.path.splitext(path)[1].lower() in TEXT_EXTENSIONS:
            text = raw.decode("utf-8", "replace")
            for allowed in ALLOWED_DRIVE_PATHS:
                text = text.replace(allowed, "")
            for found in re.finditer(r"(?<![A-Za-z])[A-Za-z]:[\\/][A-Za-z_]", text):
                line = text.count("\n", 0, found.start()) + 1
                problems.append(
                    "%s line %d: a drive path, %r" % (relative, line, text[found.start() : found.start() + 40])
                )
            for found in re.finditer(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+\.[A-Za-z]{2,}", text):
                problems.append("%s: an e-mail address, %r" % (relative, found.group(0)))
    return sorted(set(problems))


def anchors_of(path):
    text = strip_code(read_text(path))
    found = set()
    counts = {}
    for heading in re.findall(r"^#{1,6} +(.+?)\s*$", text, re.M):
        s = slug(heading)
        n = counts.get(s, 0)
        counts[s] = n + 1
        found.add(s if n == 0 else "%s-%d" % (s, n))
    return found


def check_links(target):
    """Every relative link and picture in every Markdown file of the package resolves, anchors included."""
    problems = []
    checked = 0
    pages = [p for p in all_files(target) if p.lower().endswith(".md")]
    for page in pages:
        relative = os.path.relpath(page, target).replace("\\", "/")
        text = strip_code(read_text(page))
        links = re.findall(r"\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)", text)
        links += re.findall(r"(?:src|href)=\"([^\"]+)\"", text)
        for link in links:
            if re.match(r"^[a-z][a-z0-9+.-]*:", link):
                continue
            checked += 1
            where, _, anchor = link.partition("#")
            where = urllib.parse.unquote(where)
            destination = page if not where else os.path.normpath(os.path.join(os.path.dirname(page), where))
            if not inside(destination, target):
                problems.append("%s: %s leaves the package" % (relative, link))
                continue
            if not os.path.exists(destination):
                problems.append("%s: %s does not exist" % (relative, link))
                continue
            if anchor and destination.lower().endswith(".md") and os.path.isfile(destination):
                if urllib.parse.unquote(anchor).lower() not in anchors_of(destination):
                    problems.append(
                        "%s: no heading for #%s in %s"
                        % (relative, anchor, os.path.relpath(destination, target).replace("\\", "/"))
                    )
    return problems, checked, len(pages)


def report_sizes(target):
    total = 0
    parts = {}
    digest = hashlib.sha256()
    count = 0
    for path in all_files(target):
        relative = os.path.relpath(path, target).replace("\\", "/")
        size = os.path.getsize(path)
        total += size
        count += 1
        top = relative.split("/")
        key = "/".join(top[:2]) if len(top) > 2 else (top[0] if len(top) == 2 else "(files at the root)")
        parts[key] = parts.get(key, 0) + size
        digest.update(relative.encode("utf-8") + b"\0")
        with open(path, "rb") as f:
            digest.update(hashlib.sha256(f.read()).digest())
    print("sizes:")
    for key in sorted(parts):
        print("  %-34s %8.2f MB" % (key, parts[key] / 1048576.0))
    print("total: %d files, %.2f MB" % (count, total / 1048576.0))
    print("content sha256: %s   (the same on every run from the same source)" % digest.hexdigest())


def warn_about_git_identity(target):
    """The files hold no e-mail address, but commits do: say so before the repository is published."""
    if not os.path.isdir(os.path.join(target, ".git")) or not shutil.which("git"):
        return

    def git(*arguments):
        result = subprocess.run(["git", "-C", target] + list(arguments), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return result.stdout.decode("utf-8", "replace").split() if result.returncode == 0 else []

    private = lambda address: bool(address) and not address.lower().endswith("@users.noreply.github.com")
    configured = git("config", "user.email")
    history = sorted(set(a for a in git("log", "--format=%ae %ce") if private(a)))
    if history or any(private(a) for a in configured):
        print(
            "NOTE: git in the target: %d address(es) in its commits and the configured user.email are not no-reply addresses.\n"
            "      No file of the package holds an e-mail address, but the commits will carry one when the repository is\n"
            "      published. To avoid that, commit with a no-reply identity (and re-create the existing commits) "
            "first." % len(history)
        )


def build(target, final):
    make_package_json(target)
    counts = make_code(target)
    counts["Samples~/Showcase"] = make_sample(target)
    counts["figures"] = make_manuals(target)
    make_videos(target)
    make_firmware(target)
    make_licences(target)
    make_readme(target)
    make_companions(target, counts)
    print(
        "copied: Runtime %(Runtime)d files, Editor %(Editor)d files, Samples~/Showcase %(Samples~/Showcase)d files, "
        "%(figures)d figures, 5 videos" % counts
    )

    failed = False
    for title, problems in (
        ("meta files", check_metas(target)),
        (
            "privacy (no e-mail, no machine path, no owner's name outside the GitHub address; zip members too)",
            check_privacy(target, final),
        ),
    ):
        print("check: %s: %s" % (title, "ok" if not problems else "FAILED"))
        for p in problems[:40]:
            print("   " + p)
        failed = failed or bool(problems)
    problems, checked, pages = check_links(target)
    print(
        "check: links: %s (%d relative links in %d Markdown files)"
        % ("ok" if not problems else "FAILED", checked, pages)
    )
    for p in problems[:60]:
        print("   " + p)
    failed = failed or bool(problems)
    report_sizes(target)
    if failed:
        fail("the package did not pass its checks: the target was not touched")
    install(target, final)


def main():
    arguments = sys.argv[1:]
    if len(arguments) > 1 or (arguments and arguments[0].startswith("-")):
        sys.exit(__doc__)
    final = prepare_target(arguments[0] if arguments else os.path.join(ROOT, "..", "unity-sfx-forge"))
    print("make_unity_package: %s %s -> %s" % (PACKAGE_ID, VERSION, os.path.basename(final)))
    os.makedirs(os.path.dirname(final), exist_ok=True)
    # Built and checked beside the target (the same disk, so the last step is a move); the target is touched last.
    staging = tempfile.mkdtemp(prefix=".make-unity-package-", dir=os.path.dirname(final))
    try:
        build(staging, final)
    finally:
        # the folder this run made: left over only when something failed
        if os.path.isdir(staging):
            shutil.rmtree(staging)
    warn_about_git_identity(final)
    print("make_unity_package: done")


if __name__ == "__main__":
    main()
