# The inspectors' test project

A small Unity project that tests the package's editor UI (the sound inspector, the song inspector, the SFX Forge
window) and renders pictures of it. It uses the package in `unity/com.catsplat.sfxforge` directly.

```sh
UNITY="C:/Program Files/Unity/Hub/Editor/6000.3.8f1/Editor/Unity.exe"   # where your Unity 6000.3 is
"$UNITY" -batchmode -projectPath "$PWD/tests/unity-inspectors" -runTests -testPlatform EditMode \
  -testResults "$PWD/out/inspectors.xml" -logFile out/inspectors.log        # from the repository's root; not -nographics
```

The run writes the pictures to `out/inspector-shots/`. A picture is the same element tree and stylesheet the
Inspector hosts, drawn in a panel that renders to a texture; it is not a photograph of the Inspector window, which
cannot be taken without a person at the editor. `units_table.py` makes `Assets/Data/units-float32.txt`, the
device's value texts worked out step by step in 32-bit floats as the firmware does.

`Assets/Data` holds copies of the demo song and ten of the showcase's presets, so this project must stay apart from
`unity/Demo` (the copies have the same GUIDs).
