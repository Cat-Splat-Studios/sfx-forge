# Unity Recipes

Thirteen small scripts that each solve one job with the package. This page is the index and the six recipes about
single sounds. The music recipes are on [Unity Recipes Music](Unity-Recipes-Music), the two that use telemetry and
the device on [Unity Recipes Link](Unity-Recipes-Link).

The numbers are those of [the Unity manual, section 8](../docs/UNITY.md#8-recipes), which has every script in full.
The scripts here are the manual's. They were compiled against the package; none was listened to
([What Is Tested](What-Is-Tested)). Every script uses the namespace `CatSplat.SfxForge` and nothing but the package.

## Find your recipe

| I want to ... | Recipe | Route | Page |
|---|---|---|---|
| play footsteps that never sound the same twice | [1](#1-footsteps-that-are-never-the-same) | baked variation set | this page |
| raise a pickup's pitch with the combo | [2](#2-a-pickup-that-rises-with-the-combo) | live voice | this page |
| make an engine hum follow speed | [3](#3-an-engine-hum-that-follows-speed) | held and bent | this page |
| do the same without my own update code | [4](Unity-Recipes-Link#4-an-engine-hum-wired-with-sfxtelemetrysender) | held, telemetry | Link |
| have UI clicks ready at load | [5](#5-ui-clicks-baked-at-load) | baked | this page |
| let the music follow danger | [6](Unity-Recipes-Music#6-music-that-follows-danger) | music | Music |
| play a stinger exactly on the bar line | [7](Unity-Recipes-Music#7-a-stinger-on-the-bar-line) | music | Music |
| keep one pattern per area | [8](Unity-Recipes-Music#8-a-pattern-per-area-held) | music | Music |
| duck the music under a hit or a line of dialogue | [9](Unity-Recipes-Music#9-ducking-the-music) | music | Music |
| switch to a boss theme and back | [10](Unity-Recipes-Music#10-a-boss-theme) | music | Music |
| play a one-shot at a point in 3D | [11](#11-a-3d-one-shot) | baked | this page |
| export presets as WAV files by my own rule | [12](#12-exporting-a-wav-in-the-editor) | baked, editor | this page |
| tune a game value with a knob of the device | [13](Unity-Recipes-Link#13-a-knob-on-the-device-tunes-a-game-value) | link | Link |

The route is the way a sound reaches the speakers: baked once into a clip, synthesized live at each play, held and
bent while it sounds, or music. [Unity Choosing A Route](Unity-Choosing-A-Route) has the chart that picks one.

## 1. Footsteps that are never the same

**Goal:** six variants of one footstep, shuffled, each play a little higher or lower and a little quieter. It
needs no code of your own. A variation set is an asset that holds the variants of one preset
([Unity Variations](Unity-Variations)).

1. Make the footstep as a preset: **Assets > Create > SFX Forge > Preset**. In its Inspector, under
   **TOOLS > NEW**, click **THUD** or **CLICK** until one is close ([Unity Sound Design Tools](Unity-Sound-Design-Tools#new-a-sound-of-a-kind)).
2. In the preset's Inspector press **TOOLS > VARY > MAKE SET**. A variation set appears next to the preset.
3. In the set's Inspector press **PLAY NEXT** a few times, then **BAKE**.
4. On the character, add the component **SFX Forge > SFX Variation Player**. It adds an `AudioSource`. Drag the
   set onto its **Set** field.
5. Call `PlayNext()` from an animation event, or `Play()` from code. With no animation yet,
   [a timer script](Unity-Variations#make-one-in-five-steps) does it.

With the loudness following the speed:

```csharp
using CatSplat.SfxForge;
using UnityEngine;

[RequireComponent(typeof(SfxVariationPlayer))]
public class Footsteps : MonoBehaviour
{
    SfxVariationPlayer steps;                    // its Set field holds the variation set

    void Awake() => steps = GetComponent<SfxVariationPlayer>();

    /// <summary>Call it from your animation or movement code. speed01: 0 walking … 1 flat out.</summary>
    public void Step(float speed01) => steps.Play(Mathf.Lerp(0.2f, 0.35f, speed01));
}
```

**Listen for:** no variant repeats within the last two plays (the set's `noRepeat` is 2 by default), and each play
is up to 50 cents off and up to 2 dB quieter (`pitchCents`, `volumeDb`). To move the pitch with the speed as well,
see the second script of [the manual's recipe](../docs/UNITY.md#1-footsteps-that-are-never-the-same).

## 2. A pickup that rises with the combo

**Goal:** each pickup of a combo is one step higher up a minor pentatonic scale. It is played live, because every
play has its own pitch.

```csharp
using CatSplat.SfxForge;
using UnityEngine;

[RequireComponent(typeof(SfxPlayer))]
public class ComboPickupSound : MonoBehaviour
{
    public float comboWindow = 1.2f;         // seconds between two pickups of one combo
    public int rootNote = 76;                // E5
    static readonly int[] Scale = { 0, 3, 5, 7, 10 };
    readonly SfxPatch coin = SfxPatch.Create(SfxEngine.UiBlip, 86, 64, 0, 37, 70, 0, 127, 0, 45);
    SfxPlayer player;
    int combo;
    float lastTime = float.NegativeInfinity;

    void Awake() => player = GetComponent<SfxPlayer>();

    public void Collected()
    {
        combo = Time.time - lastTime < comboWindow ? Mathf.Min(combo + 1, 10) : 0;
        lastTime = Time.time;
        int semitones = combo / Scale.Length * 12 + Scale[combo % Scale.Length];
        player.Play(coin, 1f, semitones, rootNote);
    }
}
```

**Listen for:** call `Collected()` quickly several times: E5, G5, A5, B5, D6, E6 ... and it stays on the tenth step
above the first (E7). Wait longer than 1.2 seconds and it starts at E5 again.

## 3. An engine hum that follows speed

**Goal:** one held sound whose pitch, brightness and loudness follow how fast the object moves. A held sound is a
patch with `sustain` above 0: it sounds until you call `Release()` ([Unity Held Sounds](Unity-Held-Sounds)).

```csharp
using CatSplat.SfxForge;
using UnityEngine;

[RequireComponent(typeof(SfxPlayer))]
public class EngineHum : MonoBehaviour
{
    public float topSpeed = 20f;
    readonly SfxPatch hum = SfxPatch.Create(SfxEngine.SciFiFm, 22, 64, 0, 21, 55, 8, 78, 45, 60, 0, 60, 127, 60);
    SfxPlayer player;
    Vector3 lastPosition;

    void Start()
    {
        player = GetComponent<SfxPlayer>();
        lastPosition = transform.position;
        player.Play(hum, 1f, 0f, 40);            // held (sustain 127), as the note E2
    }

    void Update()
    {
        if (Time.deltaTime <= 0f) return;
        float speed = (transform.position - lastPosition).magnitude / Time.deltaTime;
        lastPosition = transform.position;
        float speed01 = Mathf.Clamp01(speed / topSpeed);
        player.pitchSemitones = speed01 * 12f;
        player.cutoffOctaves = Mathf.Lerp(-1f, 1.5f, speed01);
        player.fm = speed01 * 0.5f;
        player.level = Mathf.Lerp(0.35f, 1f, speed01);
    }

    void OnDisable()
    {
        if (player != null) player.Release();
    }
}
```

**Listen for:** standing still, a low quiet hum on E2. At `topSpeed` it is an octave higher, brighter, with more FM
and at full level. `Play` is in `Start`, not `Awake`: a player ignores `Play` until its own `OnEnable` has run.

## 5. UI clicks, baked at load

**Goal:** four UI sounds rendered once in `Awake` and played as ordinary clips. No audio file is involved.

```csharp
using System.Collections.Generic;
using CatSplat.SfxForge;
using UnityEngine;

[RequireComponent(typeof(AudioSource))]
public class UiSounds : MonoBehaviour
{
    public enum Ui { Move, Confirm, Back, Error }

    readonly Dictionary<Ui, AudioClip> clips = new Dictionary<Ui, AudioClip>();
    AudioSource source;

    void Awake()
    {
        source = GetComponent<AudioSource>();
        SfxPatch blip = SfxPatch.Create(SfxEngine.UiBlip, 60, 64, 40, 0, 0, 0, 120, 0, 35);
        SfxPatch error = SfxPatch.Create(SfxEngine.UiBlip, 60, 84, 55, 0, 0, 0, 85, 25, 50, 60);
        Bake(Ui.Move, blip, 71);             // B4
        Bake(Ui.Confirm, blip, 83);          // B5
        Bake(Ui.Back, blip, 64);             // E4
        Bake(Ui.Error, error, 50);           // D3
    }

    void Bake(Ui sound, SfxPatch patch, int midiNote)
    {
        int rate = AudioSettings.outputSampleRate;
        float[] samples = SfxNative.BakeInterleaved(patch, rate, 1f, midiNote);
        if (samples == null || samples.Length == 0) return;
        var clip = AudioClip.Create(sound.ToString(), samples.Length / 2, 2, rate, false);
        clip.SetData(samples, 0);
        clips[sound] = clip;
    }

    public void Play(Ui sound)
    {
        if (clips.TryGetValue(sound, out AudioClip clip)) source.PlayOneShot(clip, 0.7f);
    }
}
```

**Listen for:** `Play(Ui.Move)`, `Confirm` and `Back` are the same blip on three notes (B4, B5, E4); `Error` is
another patch, with resonance and CRUSH, on D3. Where the native library is missing, `BakeInterleaved` returns
null and `Play` does nothing.

## 11. A 3D one-shot

**Goal:** an impact heard where it happens. `SfxPlayer` is not spatialized, but a baked clip is an ordinary clip.

```csharp
using CatSplat.SfxForge;
using UnityEngine;

public class ImpactSound : MonoBehaviour
{
    public SfxPreset preset;
    AudioClip clip;

    void Awake() => clip = preset != null ? preset.Bake(AudioSettings.outputSampleRate) : null;

    void OnCollisionEnter(Collision collision)
    {
        float hit = collision.relativeVelocity.magnitude;
        if (clip == null || hit < 1.5f) return;
        AudioSource.PlayClipAtPoint(clip, collision.GetContact(0).point, Mathf.Clamp01(hit / 18f));
    }
}
```

**Listen for:** nothing below 1.5 m/s; louder with a harder hit, full volume from 18 m/s; panned and attenuated by
Unity like any 3D clip.

## 12. Exporting a WAV in the editor

**Goal:** a menu item of your own that writes a `.wav` beside each selected preset. Put the script in an `Editor`
folder. (For the ready-made way, press **BAKE WAV** in a preset's Inspector or open **Window > SFX Forge Bake**:
[Unity Baking And WAV](Unity-Baking-And-WAV).)

```csharp
using CatSplat.SfxForge;
using UnityEditor;
using UnityEngine;

public static class ExportPresetWavs
{
    const int SampleRate = 48000;
    const int MidiNote = 0;                  // 0: as designed. 69: as the note A 440 Hz

    [MenuItem("Assets/SFX Forge/Export Selected Presets as WAV")]
    static void Export()
    {
        foreach (Object selected in Selection.objects)
        {
            var preset = selected as SfxPreset;
            if (preset == null) continue;
            float[] samples = SfxNative.BakeInterleaved(preset.patch, SampleRate, 1f, MidiNote, 0f, preset.holdSeconds);
            if (samples == null || samples.Length == 0) { Debug.LogWarning("SFX Forge: nothing to bake for " + preset.name); continue; }
            string path = System.IO.Path.ChangeExtension(AssetDatabase.GetAssetPath(preset), ".wav");
            SfxWav.Write(path, samples, SampleRate, 2);
            AssetDatabase.ImportAsset(path);
        }
    }
}
```

**Look for:** select one or more presets, choose **Assets > SFX Forge > Export Selected Presets as WAV**: a 16-bit
stereo WAV of the same name appears next to each preset.

## Next

- [Unity Recipes Music](Unity-Recipes-Music): recipes 6 to 10
- [Unity Recipes Link](Unity-Recipes-Link): recipes 4 and 13
- [Unity Showcase Walkthrough](Unity-Showcase-Walkthrough): all three routes at work in one game
- [Unity Troubleshooting](Unity-Troubleshooting): when a recipe stays silent

## Reference

- [The Unity manual, section 8: Recipes](../docs/UNITY.md#8-recipes)
- [The Unity manual, section 4: Playing sounds: three routes](../docs/UNITY.md#4-playing-sounds-three-routes)
- [The Unity manual: Variation sets](../docs/UNITY.md#variation-sets)
- [SfxPlayer.cs](../unity/com.catsplat.sfxforge/Runtime/SfxPlayer.cs),
  [SfxVariationPlayer.cs](../unity/com.catsplat.sfxforge/Runtime/SfxVariationPlayer.cs),
  [SfxPreset.cs](../unity/com.catsplat.sfxforge/Runtime/SfxPreset.cs),
  [SfxNative.cs](../unity/com.catsplat.sfxforge/Runtime/Core/SfxNative.cs),
  [SfxWav.cs](../unity/com.catsplat.sfxforge/Runtime/Core/SfxWav.cs)
