# The videos

The five finished videos of SFX FORGE, the current ones. They are committed, so they are here after a clone; a video
that is made again replaces its file in this folder. All are 1920×1080, 30 frames a second, with sound.

Nobody held a controller or a camera: scripts make them from the firmware and the game themselves. The sound is what
SFX FORGE produced, on the device or in Unity. There is no voice-over; the captions say what happens.

| Video | Length | What it shows | Made by | From |
|---|---|---|---|---|
| [`sfx-forge-tour.mp4`](sfx-forge-tour.mp4) | 2:23 | The device, a tour: sound effects, MUTATE, the five engines, the instruments, the sequencer and its finer grid, the song, layers that follow INTENSITY, automation, the mixer, the master effects, the link page | `docs/video/make_videos.py tour` | a script of key presses in that file, played to the built firmware (`build/sfx-forge.wasm`) |
| [`sfx-forge-build-a-song.mp4`](sfx-forge-build-a-song.mp4) | 3:44 | The device, building a song: played in from nothing with pads and keys over a looping pattern, with ERASE and UNDO, a second pattern with a hi-hat roll on a grid of 1/32, then arranged, layered, automated, performed on the PERFORM page (a pattern cued, sounds muted and brought back), mixed and sent to the game | `docs/video/make_videos.py song` | a script of key presses in that file, played to the built firmware |
| [`sfx-forge-unity-showcase.mp4`](sfx-forge-unity-showcase.mp4) | 1:55 | The showcase game, *Brio: First Harmonic*, played from start to goal, with a panel naming what SFX FORGE is doing at each moment | `docs/video/make_unity_videos.py showcase` | the takes the game records of itself in Unity, in `out/unity-video/` |
| [`sfx-forge-unity-how-it-works.mp4`](sfx-forge-unity-how-it-works.mp4) | 4:06 | How it works in Unity: the three ways to play a sound and the adaptive music, one at a time. A page of the showcase's code, then the game doing it | `docs/video/make_unity_videos.py how` | the same takes, and excerpts of the showcase's code |
| [`sfx-forge-unity-link.mp4`](sfx-forge-unity-link.mp4) | 0:31 | The link: SFX FORGE in the emulator beside the game in Unity. SEND is pressed on the device and the song arrives in the running game; a sound is mutated, EXPORT is pressed, and the game plays the patch | `docs/video/make_link_video.py` | one recorded session: the emulator's pictures in `out/link/emulator/` and the game's in `out/unity-video/link-session/` |

The device in the two device videos and in the link video is drawn by the VIBE FM SDK's emulator
(github.com/Vibe-FM-SDK/vibe-fm-sdk, Apache-2.0).

## Making them again

Run from the repository's root. Each script writes its videos into this folder; what it makes on the way (the
firmware's pictures and sound, a sound track) goes to `out/`, which git ignores.

```sh
sh build.sh                                         # the two device videos play the built firmware
python docs/video/make_videos.py                    # both device videos; or one: make_videos.py tour, make_videos.py song
python docs/video/make_unity_videos.py              # both Unity videos; or one: make_unity_videos.py showcase, make_unity_videos.py how
python docs/video/make_link_video.py                # the link video
```

They need ffmpeg and Pillow; the device videos also Node, the Unity and link videos also numpy.

- **The device videos** need nothing but the built firmware. Make them again whenever the firmware's pages or its
  sound changed. Each takes a few minutes.
- **The Unity videos** are laid out from takes that have to be recorded first, in Unity: "Recording it" in
  [the showcase's README](../unity/Demo/Assets/Showcase/README.md#recording-it) has the commands.
- **The link video** needs one recorded session with the emulator and Unity joined by two MIDI ports: "Recording the
  link" in the same README.

`tools/make_unity_package.py` reads the videos from here: it copies them into the Unity package's
`Documentation~/video` and cuts the previews of its front page from them. It checks each video's length against its
list `VIDEOS`; when a video's length changes, choose its preview's place there again.
